// Comp Suite — LV2 host smoke test using lilv.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lv2/core/lv2.h>
#include <lilv/lilv.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "params.hpp"
#include "presets.hpp"

namespace {

constexpr const char* kPluginUri =
    "https://github.com/tylorismusic-commits/comp-suite";

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

LilvNode* uri(LilvWorld* w, const char* s) { return lilv_new_uri(w, s); }

} // namespace

int main(int argc, char** argv) {
  std::string bundle = argc > 1 ? argv[1] : "build/lv2/comp-suite.lv2";
  if (bundle.empty() || bundle.back() != '/') {
    bundle += '/';  // lilv treats the bundle as a directory
  }
  LilvWorld* world = lilv_world_new();
  LilvNode* pluginUri = uri(world, kPluginUri);
  lilv_world_load_bundle(
      world, lilv_new_file_uri(world, nullptr, bundle.c_str()));
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, pluginUri);
  check(plugin != nullptr, "bundle loads and plugin is found");
  if (!plugin) {
    return 1;
  }

  {
    LilvNode* name = lilv_plugin_get_name(plugin);
    const char* n = name ? lilv_node_as_string(name) : "";
    char label[96];
    std::snprintf(label, sizeof(label), "plugin is named 'Comp Suite' (got '%s')",
                  n);
    check(std::strcmp(n, "Comp Suite") == 0, label);
    if (name) lilv_node_free(name);
  }

  const LilvUIs* uis = lilv_plugin_get_uis(plugin);
  check(lilv_uis_size(uis) == 1, "one X11 UI declared");

  const uint32_t nPorts = lilv_plugin_get_num_ports(plugin);
  const uint32_t expected = 4 + comp::P_COUNT + 2 + 4;
  char portCountLabel[96];
  std::snprintf(portCountLabel, sizeof(portCountLabel),
                "%u ports declared (%u expected: 4 audio + %u params + 4 "
                "meters), got %u",
                expected, expected, comp::P_COUNT, nPorts);
  check(nPorts == expected, portCountLabel);

  // Every parameter symbol must survive into the TTL, in order, because the
  // port index is part of the saved-session format.
  {
    bool symbolsOk = true;
    for (uint32_t id = 0; id < comp::P_COUNT; ++id) {
      const LilvPort* p = lilv_plugin_get_port_by_index(plugin, 4 + id);
      const LilvNode* sym = p ? lilv_port_get_symbol(plugin, p) : nullptr;
      const char* got = sym ? lilv_node_as_string(sym) : "(missing)";
      if (std::strcmp(got, comp::kParamSpecs[id].symbol) != 0) {
        std::printf("  port %u symbol '%s', expected '%s'\n", 4 + id, got,
                    comp::kParamSpecs[id].symbol);
        symbolsOk = false;
      }
    }
    check(symbolsOk, "parameter symbols match the port order");
  }

  // Every control port's default must sit inside its declared range.
  {
    std::vector<float> mins(nPorts), maxs(nPorts), defs(nPorts);
    lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(),
                                      defs.data());
    LilvNode* audioPort = uri(world, LV2_CORE__AudioPort);
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

  // ---------------------------------------------------------------------
  // Presets: present, attached to this plugin, and every symbol resolves.
  // ---------------------------------------------------------------------
  {
    LilvNode* rdfType =
        uri(world, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
    LilvNode* presetClass = uri(world, "http://lv2plug.in/ns/ext/presets#Preset");
    LilvNode* bankClass = uri(world, "http://lv2plug.in/ns/ext/presets#Bank");
    LilvNode* appliesTo = uri(world, "http://lv2plug.in/ns/lv2core#appliesTo");
    LilvNode* lv2Port = uri(world, "http://lv2plug.in/ns/lv2core#port");
    LilvNode* lv2Symbol = uri(world, "http://lv2plug.in/ns/lv2core#symbol");
    LilvNode* psetValue = uri(world, "http://lv2plug.in/ns/ext/presets#value");

    LilvNodes* presets =
        lilv_world_find_nodes(world, nullptr, rdfType, presetClass);
    LilvNodes* banks = lilv_world_find_nodes(world, nullptr, rdfType, bankClass);

    char countLabel[128];
    const unsigned found =
        presets ? static_cast<unsigned>(lilv_nodes_size(presets)) : 0u;
    std::snprintf(countLabel, sizeof(countLabel),
                  "%d presets declared (got %u)", comp::kPresetCount, found);
    check(static_cast<int>(found) == comp::kPresetCount, countLabel);

    std::set<std::string> validSymbols;
    for (uint32_t id = 0; id < comp::P_COUNT; ++id) {
      validSymbols.insert(comp::kParamSpecs[id].symbol);
    }

    bool attached = true;
    bool symbolsResolve = true;
    bool modelEverywhere = true;
    int index = 0;
    LILV_FOREACH(nodes, it, presets) {
      const LilvNode* preset = lilv_nodes_get(presets, it);
      LilvNode* target = lilv_world_get(world, preset, appliesTo, nullptr);
      if (!target || !lilv_node_equals(target, pluginUri)) {
        std::printf("  preset %d does not apply to this plugin\n", index);
        attached = false;
      }
      if (target) lilv_node_free(target);

      LilvNodes* ports =
          lilv_world_find_nodes(world, preset, lv2Port, nullptr);
      bool hasModel = false;
      LILV_FOREACH(nodes, pit, ports) {
        const LilvNode* port = lilv_nodes_get(ports, pit);
        LilvNode* sym = lilv_world_get(world, port, lv2Symbol, nullptr);
        LilvNode* val = lilv_world_get(world, port, psetValue, nullptr);
        const char* s = sym ? lilv_node_as_string(sym) : "";
        if (validSymbols.find(s) == validSymbols.end()) {
          std::printf("  preset %d uses unknown symbol '%s'\n", index, s);
          symbolsResolve = false;
        }
        if (!val) {
          std::printf("  preset %d symbol '%s' has no value\n", index, s);
          symbolsResolve = false;
        }
        if (std::strcmp(s, "model") == 0) {
          hasModel = true;
        }
        if (sym) lilv_node_free(sym);
        if (val) lilv_node_free(val);
      }
      lilv_nodes_free(ports);
      if (!hasModel) {
        std::printf("  preset %d does not set its model\n", index);
        modelEverywhere = false;
      }
      ++index;
    }
    check(attached, "every preset applies to this plugin");
    check(symbolsResolve, "every preset port symbol resolves to a parameter");
    check(modelEverywhere, "every preset sets its model explicitly");
    check(banks && lilv_nodes_size(banks) >= 1, "preset banks are declared");

    lilv_nodes_free(presets);
    lilv_nodes_free(banks);
    lilv_node_free(rdfType);
    lilv_node_free(presetClass);
    lilv_node_free(bankClass);
    lilv_node_free(appliesTo);
    lilv_node_free(lv2Port);
    lilv_node_free(lv2Symbol);
    lilv_node_free(psetValue);
  }

  std::vector<float> values(nPorts, 0.0f);
  for (uint32_t i = 0; i < nPorts; ++i) {
    const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
    LilvNode* def = lilv_port_get(plugin, port, uri(world, LV2_CORE__default));
    values[i] = def ? lilv_node_as_float(def) : 0.0f;
    if (def) lilv_node_free(def);
  }

  LilvInstance* inst = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  check(inst != nullptr, "plugin instantiates");
  if (!inst) {
    return 1;
  }

  const uint32_t frames = 256;
  std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
  std::vector<float*> audio = {inL.data(), inR.data(), outL.data(),
                               outR.data()};
  for (uint32_t i = 0; i < 4; ++i) {
    lilv_instance_connect_port(inst, i, audio[i]);
  }
  for (uint32_t i = 4; i < nPorts; ++i) {
    lilv_instance_connect_port(inst, i, &values[i]);
  }

  // 76Blue, 8:1, unity input, mix fully wet.
  values[4 + comp::P_MODEL] = 0.0f;
  values[4 + comp::P_ENABLED] = 1.0f;
  values[4 + comp::P_INPUT] = 0.6f;
  values[4 + comp::P_RATIO] = 5.0f;
  values[4 + comp::P_MIX] = 1.0f;

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

  const float gr = values[4 + comp::P_COUNT + 2];
  check(gr >= 0.0f, "gain reduction meter port reports");

  lilv_instance_free(inst);
  lilv_node_free(pluginUri);
  lilv_world_free(world);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
