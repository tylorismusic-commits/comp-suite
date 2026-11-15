// Comp Suite — LV2 host smoke test using lilv.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lv2/core/lv2.h>
#include <lilv/lilv.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

} // namespace

int main(int argc, char** argv) {
  std::string bundle = argc > 1 ? argv[1] : "build/lv2/comp-suite.lv2";
  if (bundle.empty() || bundle.back() != '/') {
    bundle += '/';  // lilv treats the bundle as a directory
  }
  LilvWorld* world = lilv_world_new();
  LilvNode* uri = lilv_new_uri(world, "https://github.com/tylorismusic-commits/comp-suite");
  lilv_world_load_bundle(world, lilv_new_file_uri(world, nullptr, bundle.c_str()));
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);
  check(plugin != nullptr, "bundle loads and plugin is found");
  if (!plugin) {
    return 1;
  }

  {
    LilvNode* name = lilv_plugin_get_name(plugin);
    const char* n = name ? lilv_node_as_string(name) : "";
    char label[96];
    std::snprintf(label, sizeof(label), "plugin is named 'Comp Suite' (got '%s')", n);
    check(std::strcmp(n, "Comp Suite") == 0, label);
    if (name) lilv_node_free(name);
  }

  const LilvUIs* uis = lilv_plugin_get_uis(plugin);
  check(lilv_uis_size(uis) == 1, "one X11 UI declared");

  const uint32_t nPorts = lilv_plugin_get_num_ports(plugin);
  check(nPorts == 23, "23 ports declared");

  // Every control port's default must sit inside its declared range.
  {
    std::vector<float> mins(nPorts), maxs(nPorts), defs(nPorts);
    lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(),
                                      defs.data());
    LilvNode* audioPort =
        lilv_new_uri(world, "http://lv2plug.in/ns/lv2core#AudioPort");
    bool rangesOk = true;
    for (uint32_t i = 0; i < nPorts; ++i) {
      const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
      if (lilv_port_is_a(plugin, port, audioPort)) {
        continue;  // audio ports carry no control range
      }
      if (!(mins[i] <= maxs[i]) || defs[i] < mins[i] || defs[i] > maxs[i]) {
        std::printf("  port %u range broken: min %g max %g default %g\n", i,
                    mins[i], maxs[i], defs[i]);
        rangesOk = false;
      }
    }
    lilv_node_free(audioPort);
    check(rangesOk, "port ranges and defaults are consistent");
  }

  std::vector<float> values(nPorts, 0.0f);
  lilv_plugin_get_port_ranges_float(plugin, nullptr,
                                    values.data() + 0, nullptr);
  // Fill defaults.
  for (uint32_t i = 0; i < nPorts; ++i) {
    const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
    LilvNode* def = lilv_port_get(plugin, port, lilv_new_uri(world, "http://lv2plug.in/ns/lv2core#default"));
    values[i] = def ? lilv_node_as_float(def) : 0.0f;
  }

  LilvInstance* inst = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  check(inst != nullptr, "plugin instantiates");
  if (!inst) {
    return 1;
  }

  const uint32_t frames = 256;
  std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
  std::vector<float*> audio = {inL.data(), inR.data(), outL.data(), outR.data()};
  for (uint32_t i = 0; i < 4; ++i) {
    lilv_instance_connect_port(inst, i, audio[i]);
  }
  for (uint32_t i = 4; i < nPorts; ++i) {
    lilv_instance_connect_port(inst, i, &values[i]);
  }

  // 76Blue, 8:1, unity input, mix fully wet.
  values[4] = 0.0f;   // model
  values[5] = 1.0f;   // power
  values[6] = 0.6f;   // input
  values[8] = 5.0f;   // ratio 8:1
  values[13] = 1.0f;  // mix

  lilv_instance_activate(inst);
  float peak = 0.0f;
  bool finite = true;
  for (int block = 0; block < 200; ++block) {
    for (uint32_t i = 0; i < frames; ++i) {
      const float t = static_cast<float>(block * frames + i) / 48000.0f;
      inL[i] = inR[i] = 0.4f * std::sin(2.0f * 3.14159265f * 220.0f * t);
    }
    lilv_instance_run(inst, frames);
    for (uint32_t i = 0; i < frames; ++i) {
      if (!std::isfinite(outL[i])) {
        finite = false;
      }
      peak = std::max(peak, std::fabs(outL[i]));
    }
  }
  lilv_instance_deactivate(inst);
  check(finite, "LV2 processing stays finite");
  check(peak > 0.01f && peak < 2.0f, "LV2 output has a sane level");

  const float gr = values[19];
  check(gr >= 0.0f, "gain reduction meter port reports");

  lilv_instance_free(inst);
  lilv_node_free(uri);
  lilv_world_free(world);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
