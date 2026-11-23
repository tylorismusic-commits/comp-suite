// Comp Suite — insert peer test (protocol v3).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads the real CLAP plug-in, activates it and checks what Suite Control
// sees: kind, bypass index, parameter metadata, remote edits and the LINK
// assignment saved in the plug-in's own state.
#include <clap/all.h>
#include <dlfcn.h>

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "params.hpp"
#include "peer.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
  return ok;
}

uint32_t inSize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t) {
  return nullptr;
}
bool outPush(const clap_output_events_t*, const clap_event_header_t*) {
  return true;
}
void requestCallback(const clap_host_t*) {}

const clap_host_t kHost = [] {
  clap_host_t h{};
  h.clap_version = CLAP_VERSION;
  h.name = "comp-insert-test";
  h.vendor = "tylorismusic";
  h.url = "https://example.invalid";
  h.version = "0";
  h.request_callback = requestCallback;
  return h;
}();

clap_process_status process(const clap_plugin_t* plugin) {
  std::vector<float> l(256, 0.0f), r(256, 0.0f);
  float* chans[2] = {l.data(), r.data()};
  clap_audio_buffer_t buf{};
  buf.data32 = chans;
  buf.channel_count = 2;
  clap_input_events_t in{nullptr, inSize, inGet};
  clap_output_events_t out{nullptr, outPush};
  clap_process_t proc{};
  proc.frames_count = 256;
  proc.audio_inputs = &buf;
  proc.audio_inputs_count = 1;
  proc.audio_outputs = &buf;
  proc.audio_outputs_count = 1;
  proc.in_events = &in;
  proc.out_events = &out;
  return plugin->process(plugin, &proc);
}

struct MemStream {
  std::vector<unsigned char> data;
  size_t readPos = 0;
};

int64_t streamWrite(const clap_ostream_t* s, const void* buf, uint64_t size) {
  auto* m = static_cast<MemStream*>(s->ctx);
  const auto* bytes = static_cast<const unsigned char*>(buf);
  m->data.insert(m->data.end(), bytes, bytes + size);
  return static_cast<int64_t>(size);
}
} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/clap/CompSuite.clap";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!check(lib != nullptr, dlerror())) return 1;
  auto* entry =
      static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!check(entry && entry->init(path), "bundle entry")) return 1;
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_descriptor_t* desc =
      factory->get_plugin_descriptor(factory, 0);
  const clap_plugin_t* plugin = factory->create_plugin(factory, &kHost, desc->id);
  if (!check(plugin && plugin->init(plugin), "plugin instance")) return 1;
  check(plugin->activate(plugin, 48000.0, 16, 256), "plugin activates");
  process(plugin);

  suite::peer::Client viewer;
  auto* seg = viewer.segment();
  if (!check(seg != nullptr, "peer segment maps")) return 1;
  const int32_t pid = static_cast<int32_t>(getpid());
  int slot = -1;
  for (int i = 0; i < suite::peer::kSlots; ++i) {
    if (seg->slot[i].used.load(std::memory_order_acquire) &&
        seg->slot[i].ownerPid.load(std::memory_order_relaxed) == pid &&
        std::strcmp(seg->slot[i].name, "Comp Suite") == 0) {
      slot = i;
    }
  }
  if (!check(slot >= 0, "insert registers with the peer bus")) return 1;
  suite::peer::Slot& s = seg->slot[slot];

  check(s.kind.load() == suite::peer::kKindInsert, "kind is insert");
  check(s.bypassParam.load() == comp::P_ENABLED, "bypass is P_ENABLED");
  check(s.metaReady.load(std::memory_order_acquire) ==
            static_cast<uint32_t>(comp::P_COUNT),
        "metadata covers every parameter");
  check(std::strcmp(s.meta[comp::P_THRESHOLD].symbol,
                    comp::kParamSpecs[comp::P_THRESHOLD].symbol) == 0,
        "metadata symbols come from the parameter table");
  check(std::strcmp(s.meta[comp::P_MODEL].name, "Model") == 0 &&
            s.meta[comp::P_MODEL].labelCount > 1,
        "enum metadata carries its scale points");

  // Suite Control assigns an insert slot; the plug-in adopts it.
  s.linkBus.store(3);
  s.linkChannel.store(2);
  s.linkSlot.store(1);
  s.linkStamp.fetch_add(1, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  if (check(state != nullptr, "state extension")) {
    MemStream saved;
    clap_ostream_t out{&saved, streamWrite};
    check(state->save(plugin, &out), "state saves");
    const auto* bytes = saved.data.data();
    const size_t size = saved.data.size();
    bool found = false;
    for (size_t i = 0; i + 20 <= size; ++i) {
      uint32_t header[5];
      std::memcpy(header, bytes + i, sizeof(header));
      if (header[0] == 0x4b504c43 && header[1] == 1 && header[2] == 3 &&
          header[3] == 2 && header[4] == 1) {
        found = true;
        break;
      }
    }
    check(found, "LINK assignment is saved in the plug-in state");
  }

  // A remote parameter edit reaches the plug-in and the host model.
  // 5:1 is a valid button on the default 76 bank; 4:1 would be snapped to
  // 3:1 by the plug-in and look like a failed edit.
  suite::peer::Client::writeRemote(s, comp::P_RATIO, 5.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  process(plugin);
  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  double value = 0.0;
  check(params && params->get_value(plugin, comp::P_RATIO, &value) &&
            std::fabs(value - 5.0) < 1e-9,
        "remote edit reaches the insert parameter");

  // The generic page shows the plug-in's formatted readout.  Ratio is a step
  // whose real value depends on the model; threshold is a percentage or dB.
  suite::peer::Hub hub;
  hub.refresh();
  int hubEntry = -1;
  for (int i = 0; i < hub.size(); ++i) {
    if (hub.ownerPid(i) == pid &&
        std::strcmp(hub.name(i), "Comp Suite") == 0) {
      hubEntry = i;
    }
  }
  if (check(hubEntry >= 0, "hub lists the insert for readouts")) {
    char ratioText[32] = {};
    hub.text(hubEntry, comp::P_RATIO, ratioText, sizeof(ratioText));
    check(std::strstr(ratioText, ":1") != nullptr,
          "ratio readout is formatted as x:1");
    char thresholdText[32] = {};
    hub.text(hubEntry, comp::P_THRESHOLD, thresholdText,
             sizeof(thresholdText));
    check(thresholdText[0] && (std::strchr(thresholdText, '%') ||
                               std::strstr(thresholdText, "dB")),
          "threshold readout carries its real unit");
  }

  plugin->deactivate(plugin);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  check(s.used.load(std::memory_order_acquire) == 0,
        "insert unregisters on deactivate");
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
