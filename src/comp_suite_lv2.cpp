// Comp Suite — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ten compressor models in one plugin.  The control ports mirror the CLAP
// parameter ids: port = kPortFirstParam + ParamId.

#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "dsp/engine.hpp"
#include "dsp/inline_display.hpp"
#include "insert_peer.hpp"
#include "value_text.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri = "https://github.com/tylorismusic-commits/comp-suite";
constexpr const char* kStateUri =
    "https://github.com/tylorismusic-commits/comp-suite#insert";

// Audio ports first, then controls, then meter outputs.
enum Port : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_FIRST_PARAM = 4,
  // External side-chain inputs.  They sit after the parameters so existing
  // parameter port indices never move.
  PORT_SC_IN_L = PORT_FIRST_PARAM + comp::P_COUNT,
  PORT_SC_IN_R,
  PORT_GR_METER,
  PORT_OUT_PEAK_L,
  PORT_OUT_PEAK_R,
  PORT_IN_PEAK_L,
  PORT_IN_PEAK_R,
  PORT_OUT_VU,
  PORT_SC_PEAK,
  PORT_SC_ACTIVE,
  PORT_LATENCY,
  PORT_COUNT
};

struct Plugin {
  float sampleRate = 48000.0f;
  float lastPort[comp::P_COUNT] = {};
  bool portsSeen = false;
  comp::Shared shared;
  comp::Engine engine;
  comp::Meters meters;

  std::unique_ptr<float*[]> ports;

  float* audioInL = nullptr;
  float* audioInR = nullptr;
  float* audioOutL = nullptr;
  float* audioOutR = nullptr;
  float* scInL = nullptr;
  float* scInR = nullptr;
  float* gr = nullptr;
  float* outPeakL = nullptr;
  float* outPeakR = nullptr;
  float* inPeakL = nullptr;
  float* inPeakR = nullptr;
  float* outVu = nullptr;
  float* scPeak = nullptr;
  float* scActive = nullptr;
  float* latency = nullptr;

  comp::InlineDisplay display;
  const comp::InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;

  // Insert peer (protocol v3).
  suite::peer::Client peer;
  bool peerRegistered = false;
  std::thread peerThread;
  std::atomic<bool> peerRun{false};
  std::atomic<uint32_t> linkBus{0};
  std::atomic<uint32_t> linkChannel{0};
  std::atomic<uint32_t> linkSlot{0};
  const LV2_URID_Map* map = nullptr;
  uint32_t chunkType = 0;
  uint32_t stateKey = 0;
  bool restored = false;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const* features) {
  auto* p = new Plugin();
  p->sampleRate = static_cast<float>(rate);
  p->engine.prepare(p->sampleRate);
  // Start from the defaults already in Shared.
  p->engine.setControls(comp::controlsFrom(p->shared));

  // Ardour's mixer-strip gain-reduction display.
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, comp::kInlineDisplayQueueDrawURI) &&
        (*f)->data) {
      p->queueDraw = static_cast<const comp::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    } else if (!std::strcmp((*f)->URI, LV2_URID__map) && (*f)->data) {
      p->map = static_cast<const LV2_URID_Map*>(f[0]->data);
      p->chunkType = p->map->map(p->map->handle, LV2_ATOM__Chunk);
      p->stateKey = p->map->map(p->map->handle, kStateUri);
    }
  }
  return p;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data) {
  auto* p = static_cast<Plugin*>(handle);
  switch (port) {
    case PORT_AUDIO_IN_L: p->audioInL = static_cast<float*>(data); break;
    case PORT_AUDIO_IN_R: p->audioInR = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_L: p->audioOutL = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_R: p->audioOutR = static_cast<float*>(data); break;
    case PORT_SC_IN_L: p->scInL = static_cast<float*>(data); break;
    case PORT_SC_IN_R: p->scInR = static_cast<float*>(data); break;
    case PORT_GR_METER: p->gr = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_L: p->outPeakL = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_R: p->outPeakR = static_cast<float*>(data); break;
    case PORT_IN_PEAK_L: p->inPeakL = static_cast<float*>(data); break;
    case PORT_IN_PEAK_R: p->inPeakR = static_cast<float*>(data); break;
    case PORT_OUT_VU: p->outVu = static_cast<float*>(data); break;
    case PORT_SC_PEAK: p->scPeak = static_cast<float*>(data); break;
    case PORT_SC_ACTIVE: p->scActive = static_cast<float*>(data); break;
    case PORT_LATENCY: p->latency = static_cast<float*>(data); break;
    default:
      // Control input: stash the pointer so run() can read it.
      if (!p->ports) {
        p->ports = std::make_unique<float*[]>(PORT_COUNT);
        std::memset(p->ports.get(), 0, sizeof(float*) * PORT_COUNT);
      }
      if (port < PORT_COUNT) {
        p->ports[port] = static_cast<float*>(data);
      }
      break;
  }
}

void activate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  p->engine.reset();
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }
  if (!p->peerRegistered &&
      p->peer.registerSlot(0, "Comp Suite", comp::P_COUNT,
                           suite::peer::kKindInsert, comp::P_ENABLED) >= 0) {
    p->peerRegistered = true;
    suite::peer::ParamMeta meta[comp::P_COUNT];
    comp::fillParamMeta(meta, comp::P_COUNT);
    p->peer.publishMeta(meta, comp::P_COUNT);
    p->peer.setShortName("COMP");
    p->peer.setHasGr(true);
    p->peer.publishLink(p->linkBus.load(std::memory_order_relaxed),
                        p->linkChannel.load(std::memory_order_relaxed),
                        p->linkSlot.load(std::memory_order_relaxed));
    p->peerRun.store(true, std::memory_order_relaxed);
    p->peerThread = std::thread([p] {
      while (p->peerRun.load(std::memory_order_relaxed)) {
        if (p->peerRegistered) {
          for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
            double v = 0.0;
            if (p->peer.takeRemote(static_cast<int>(i), &v) &&
                std::fabs(v - p->shared.get(i)) > 1e-12) {
              p->shared.set(i, v);
            }
          }
          suite::peer::LinkInfo link;
          if (p->peer.takeRemoteLink(&link)) {
            p->linkBus.store(link.bus, std::memory_order_relaxed);
            p->linkChannel.store(link.channel, std::memory_order_relaxed);
            p->linkSlot.store(link.slot, std::memory_order_relaxed);
          }
          for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
            p->peer.publish(static_cast<int>(i), p->shared.get(i));
            const std::string text =
                comp::formatParamValue(p->shared, i, p->shared.get(i));
            p->peer.publishText(static_cast<int>(i), text.c_str());
          }
          p->peer.publishMeters(p->meters.inPeakLDb,
                              p->meters.inPeakRDb,
                              p->meters.outPeakLDb,
                              p->meters.outPeakRDb,
                              p->meters.grDb);
          p->peer.beat();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
      }
    });
  }
}

void deactivate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  if (p->peerRun.exchange(false) && p->peerThread.joinable()) {
    p->peerThread.join();
  }
  if (p->peerRegistered) {
    p->peer.unregisterSlot();
    p->peerRegistered = false;
  }
}

void run(LV2_Handle handle, uint32_t frames) {
  auto* p = static_cast<Plugin*>(handle);
  if (!p->audioInL || !p->audioOutL) {
    return;
  }
  // Mono or partially connected hosts: mirror the left channel.
  if (!p->audioInR) {
    p->audioInR = p->audioInL;
  }
  if (!p->audioOutR) {
    p->audioOutR = p->audioOutL;
  }

  // Pull host parameter values in, then configure the engine.  Only values
  // that actually changed are applied, so a port the host has not refreshed
  // yet cannot undo a change made in the UI.
  if (p->ports) {
    if (p->restored) {
      // State restore wins over the session's stale control port values.
      for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
        float* v = p->ports[PORT_FIRST_PARAM + i];
        if (v) p->lastPort[i] = *v;
      }
      p->restored = false;
    } else {
      for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
        float* v = p->ports[PORT_FIRST_PARAM + i];
        if (!v) {
          continue;
        }
        if (!p->portsSeen || *v != p->lastPort[i]) {
          p->shared.setFromHost(i, static_cast<double>(*v));
          p->lastPort[i] = *v;
        }
      }
    }
    p->portsSeen = true;
  }
  p->engine.setControls(comp::controlsFrom(p->shared));

  // In-place when the host provides the same buffer, otherwise copy.
  if (p->audioOutL != p->audioInL) {
    std::memcpy(p->audioOutL, p->audioInL, frames * sizeof(float));
  }
  if (p->audioOutR != p->audioInR) {
    std::memcpy(p->audioOutR, p->audioInR, frames * sizeof(float));
  }

  p->engine.process(p->audioOutL, p->audioOutR, static_cast<int>(frames),
                    p->meters, p->scInL, p->scInR ? p->scInR : p->scInL);

  if (p->gr) {
    *p->gr = p->meters.grDb;
  }
  if (p->outPeakL) {
    *p->outPeakL = p->meters.outPeakLDb;
  }
  if (p->outPeakR) {
    *p->outPeakR = p->meters.outPeakRDb;
  }
  if (p->inPeakL) {
    *p->inPeakL = p->meters.inPeakLDb;
  }
  if (p->inPeakR) {
    *p->inPeakR = p->meters.inPeakRDb;
  }
  if (p->outVu) {
    *p->outVu = p->meters.outVuDb;
  }
  if (p->scPeak) {
    *p->scPeak = p->meters.scPeakDb;
  }
  if (p->scActive) {
    *p->scActive = p->meters.scActive ? 1.0f : 0.0f;
  }
  if (p->latency) {
    *p->latency = static_cast<float>(p->engine.latencySamples());
  }

  // Feed the Ardour mixer-strip display.  The render itself happens on the
  // host's GUI thread; the audio thread only publishes a number and asks for
  // a redraw every 32 blocks.
  p->display.setGainReductionDb(p->meters.grDb);
  if (p->queueDraw && p->queueDraw->queue_draw &&
      ((p->drawCounter++ & 31u) == 0u)) {
    p->queueDraw->queue_draw(p->queueDraw->handle);
  }
}

void cleanup(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  if (p->peerRun.exchange(false) && p->peerThread.joinable()) {
    p->peerThread.join();
  }
  delete p;
}

// --------------------------------------------------------------- LV2 state

constexpr uint32_t kStateMagic = 0x49504d43;  // CMPI
constexpr uint32_t kStateVersion = 1;

LV2_State_Status stateSave(LV2_Handle handle, LV2_State_Store_Function store,
                           LV2_State_Handle storeHandle, uint32_t,
                           const LV2_Feature* const*) {
  auto* p = static_cast<Plugin*>(handle);
  if (!p->chunkType || !p->stateKey) return LV2_STATE_ERR_UNKNOWN;

  const uint32_t count = comp::P_COUNT;
  std::vector<uint8_t> blob(12 + count * sizeof(double) + 3 * sizeof(uint32_t));
  std::memcpy(blob.data(), &kStateMagic, 4);
  std::memcpy(blob.data() + 4, &kStateVersion, 4);
  std::memcpy(blob.data() + 8, &count, 4);
  size_t offset = 12;
  for (uint32_t i = 0; i < count; ++i) {
    const double v = p->shared.get(i);
    std::memcpy(blob.data() + offset, &v, sizeof(double));
    offset += sizeof(double);
  }
  const uint32_t link[3] = {p->linkBus.load(std::memory_order_relaxed),
                            p->linkChannel.load(std::memory_order_relaxed),
                            p->linkSlot.load(std::memory_order_relaxed)};
  std::memcpy(blob.data() + offset, link, sizeof(link));
  return store(storeHandle, p->stateKey, blob.data(), blob.size(),
               p->chunkType, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

LV2_State_Status stateRestore(LV2_Handle handle,
                              LV2_State_Retrieve_Function retrieve,
                              LV2_State_Handle stateHandle, uint32_t,
                              const LV2_Feature* const*) {
  auto* p = static_cast<Plugin*>(handle);
  size_t size = 0;
  uint32_t type = 0;
  uint32_t flags = 0;
  const void* data = retrieve(stateHandle, p->stateKey, &size, &type, &flags);
  if (!data) return LV2_STATE_ERR_NO_PROPERTY;
  if (!p->chunkType || type != p->chunkType) return LV2_STATE_ERR_BAD_TYPE;

  const uint32_t count = comp::P_COUNT;
  const size_t expected = 12 + count * sizeof(double) + 3 * sizeof(uint32_t);
  if (size != expected) return LV2_STATE_ERR_BAD_TYPE;
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t magic = 0, version = 0, stored = 0;
  std::memcpy(&magic, bytes, 4);
  std::memcpy(&version, bytes + 4, 4);
  std::memcpy(&stored, bytes + 8, 4);
  if (magic != kStateMagic || version != kStateVersion || stored != count) {
    return LV2_STATE_ERR_UNKNOWN;
  }
  size_t offset = 12;
  for (uint32_t i = 0; i < count; ++i) {
    double v = 0.0;
    std::memcpy(&v, bytes + offset, sizeof(double));
    offset += sizeof(double);
    p->shared.set(i, v);
  }
  uint32_t link[3] = {};
  std::memcpy(link, bytes + offset, sizeof(link));
  p->linkBus.store(link[0], std::memory_order_relaxed);
  p->linkChannel.store(link[1], std::memory_order_relaxed);
  p->linkSlot.store(link[2], std::memory_order_relaxed);
  p->restored = true;
  return LV2_STATE_SUCCESS;
}

const LV2_State_Interface kStateInterface = {stateSave, stateRestore};

comp::InlineDisplaySurface* renderInlineDisplay(void* instance, uint32_t width,
                                                uint32_t height) {
  auto* p = static_cast<Plugin*>(instance);
  return p ? p->display.render(width, height) : nullptr;
}

const comp::InlineDisplayInterface kInlineDisplay = {&renderInlineDisplay};

const void* extensionData(const char* uri) {
  if (uri && !std::strcmp(uri, comp::kInlineDisplayInterfaceURI)) {
    return &kInlineDisplay;
  }
  if (uri && !std::strcmp(uri, LV2_STATE__interface)) {
    return &kStateInterface;
  }
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate,
    cleanup, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
