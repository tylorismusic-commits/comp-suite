// Comp Suite — LV2 metadata generator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emits lv2/comp_suite.ttl, lv2/manifest.ttl and lv2/presets.ttl straight from
// src/params.hpp and src/presets.hpp.  Because the generator is compiled
// against the same headers as the plugin, the metadata cannot drift from the
// code.
//
//   gen_ttl                 write the files next to ./lv2
//   gen_ttl --check         exit 1 if the committed files are stale
//   gen_ttl --root <dir>    use <dir>/lv2 instead of ./lv2
//
// Run with --check from the build (see the comp_ttl test).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "params.hpp"
#include "presets.hpp"

namespace {

constexpr const char* kUri =
    "https://github.com/tylorismusic-commits/comp-suite";
constexpr const char* kName = "Comp Suite";
constexpr const char* kShortDesc = "Six compressor models in one plugin";
constexpr const char* kDescription =
    "76Blue, 76Black, 2A, 3A, Xpress and VCA, with a shared ratio ladder, "
    "parallel mix and an output limiter.";
constexpr const char* kLicense = "https://spdx.org/licenses/GPL-3.0-or-later.html";
constexpr const char* kMaintainer = "tylorismusic";
constexpr const char* kHomepage =
    "https://github.com/tylorismusic-commits/comp-suite";
constexpr const char* kPluginBinary = "comp_suite.so";
constexpr const char* kUiBinary = "comp_suite_ui.so";
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 4;
constexpr int kVersionMicro = 5;  // LV2 has major/minor/micro, no patch

// The meter outputs are not parameters: they exist only in the LV2 port list,
// after the control ports.
struct MeterPort {
  const char* symbol;
  const char* name;
};

constexpr MeterPort kMeters[] = {
    {"gr_meter", "Gain Reduction"},
    {"out_peak_l", "Output Peak Left"},
    {"out_peak_r", "Output Peak Right"},
    {"in_peak_l", "Input Peak Left"},
    {"in_peak_r", "Input Peak Right"},
    {"out_vu", "Output VU"},
};

constexpr int kAudioPorts = 4;

std::string lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// "Side Chain" -> "sidechain"; the same token is the pg:Group fragment.
std::string groupSlug(const char* module) {
  std::string out;
  for (const char* p = module; *p; ++p) {
    if (*p != ' ' && *p != '\t') {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
  }
  return out;
}

std::string groupUri(const char* module) {
  return std::string(kUri) + "#" + groupSlug(module);
}

// Shortest decimal that round-trips the common cases without trailing zeros.
std::string num(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string escaped(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

// Lowercase, alphanumerics only, single dashes between words.
std::string slug(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!out.empty() && out.back() != '-') {
      out += '-';
    }
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  return out.empty() ? "group" : out;
}

std::string portBlock(int index, const char* symbol, const char* name,
                      double minimum, double maximum, double def, bool integer,
                      bool toggled, const comp::Labels& labels,
                      const char* unit, const char* group, bool output) {
  std::ostringstream out;
  out << "    [\n";
  out << "        a lv2:" << (output ? "OutputPort" : "InputPort")
      << " , lv2:ControlPort ;\n";
  out << "        lv2:index " << index << " ;\n";
  out << "        lv2:symbol \"" << symbol << "\" ;\n";
  out << "        lv2:name \"" << name << "\" ;\n";
  out << "        lv2:default " << num(def) << " ;\n";
  out << "        lv2:minimum " << num(minimum) << " ;\n";
  out << "        lv2:maximum " << num(maximum) << " ;\n";
  if (integer) {
    out << "        lv2:integer true ;\n";
  }
  if (toggled) {
    out << "        lv2:toggled true ;\n";
  }
  if (labels.count > 0) {
    out << "        lv2:scalePoint ";
    for (int i = 0; i < labels.count; ++i) {
      if (i > 0) {
        out << " ,\n            ";
      }
      out << "[ rdf:value " << i << " ; rdfs:label \""
          << escaped(labels.names[i]) << "\" ]";
    }
    out << " ;\n";
  }
  if (unit) {
    out << "        units:unit units:" << unit << " ;\n";
  }
  if (output) {
    // Meters are not user controls; keep generic UIs from rendering sliders.
    out << "        lv2:portProperty pprops:notOnGUI ;\n";
  }
  out << "        pg:group <" << group << "> ;\n";
  out << "    ]";
  return out.str();
}

std::string buildPluginTtl() {
  std::ostringstream out;
  out << "# Generated by tools/gen_ttl.cpp - do not edit.\n";
  out << "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
  out << "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
  out << "@prefix doap: <http://usefulinc.com/ns/doap#> .\n";
  out << "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n";
  out << "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .\n";
  out << "@prefix pg:   <http://lv2plug.in/ns/ext/port-groups#> .\n";
  out << "@prefix foaf: <http://xmlns.com/foaf/0.1/> .\n";
  out << "@prefix idpy: <http://harrisonconsoles.com/lv2/inlinedisplay#> .\n";
  out << "@prefix pprops: <http://lv2plug.in/ns/ext/port-props#> .\n";
  out << "@prefix rdf:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n";
  out << "\n";
  out << "<" << kUri << ">\n";
  out << "    a lv2:Plugin , lv2:CompressorPlugin ;\n";
  out << "    doap:name \"" << kName << "\" ;\n";
  out << "    doap:shortdesc \"" << kShortDesc << "\" ;\n";
  out << "    doap:description \"" << kDescription << "\"@en ;\n";
  out << "    doap:license <" << kLicense << "> ;\n";
  out << "    doap:maintainer [\n";
  out << "        foaf:name \"" << kMaintainer << "\" ;\n";
  out << "        foaf:homepage <" << kHomepage << "> ;\n";
  out << "    ] ;\n";
  out << "    doap:homepage <" << kHomepage << "> ;\n";
  out << "    lv2:majorVersion " << kVersionMajor << " ;\n";
  out << "    lv2:minorVersion " << kVersionMinor << " ;\n";
  out << "    lv2:microVersion " << kVersionMicro << " ;\n";
  out << "    lv2:binary <" << kPluginBinary << "> ;\n";
  out << "    rdfs:seeAlso <manifest.ttl> ;\n";
  out << "    ui:ui <" << kUri << "#ui> ;\n";
  out << "    lv2:optionalFeature lv2:hardRTCapable , idpy:queue_draw ;\n";
  out << "    lv2:extensionData idpy:interface ;\n";
  out << "    lv2:port\n";

  std::vector<std::string> ports;
  const char* const audio[4][3] = {
      {"in_l", "Input Left", "in"},
      {"in_r", "Input Right", "in"},
      {"out_l", "Output Left", "out"},
      {"out_r", "Output Right", "out"},
  };
  for (int i = 0; i < kAudioPorts; ++i) {
    std::ostringstream p;
    p << "    [\n";
    p << "        a lv2:AudioPort , lv2:"
      << (std::strcmp(audio[i][2], "in") == 0 ? "InputPort" : "OutputPort")
      << " ;\n";
    p << "        lv2:index " << i << " ;\n";
    p << "        lv2:symbol \"" << audio[i][0] << "\" ;\n";
    p << "        lv2:name \"" << audio[i][1] << "\" ;\n";
    p << "    ]";
    ports.push_back(p.str());
  }

  for (uint32_t id = 0; id < comp::P_COUNT; ++id) {
    const comp::ParamSpec& s = comp::kParamSpecs[id];
    ports.push_back(portBlock(static_cast<int>(kAudioPorts + id), s.symbol,
                              s.name, s.min, s.max, s.def, s.stepped, s.toggled,
                              s.labels, s.unit, groupUri(s.module).c_str(),
                              false));
  }

  int index = kAudioPorts + static_cast<int>(comp::P_COUNT);
  {
    // External side-chain inputs, after the parameters so existing parameter
    // ports keep their indices.
    const char* const sc[2][2] = {{"sc_in_l", "Sidechain Left"},
                                  {"sc_in_r", "Sidechain Right"}};
    for (int i = 0; i < 2; ++i) {
      std::ostringstream p;
      p << "    [\n";
      p << "        a lv2:AudioPort , lv2:InputPort ;\n";
      p << "        lv2:index " << index++ << " ;\n";
      p << "        lv2:symbol \"" << sc[i][0] << "\" ;\n";
      p << "        lv2:name \"" << sc[i][1] << "\" ;\n";
      p << "        lv2:portProperty lv2:isSideChain ;\n";
      p << "    ]";
      ports.push_back(p.str());
    }
  }

  for (const MeterPort& m : kMeters) {
    ports.push_back(portBlock(index++, m.symbol, m.name, -120.0, 24.0, 0.0,
                              false, false, {}, "db", groupUri("Meter").c_str(),
                              true));
  }

  for (size_t i = 0; i < ports.size(); ++i) {
    out << ports[i];
    out << (i + 1 == ports.size() ? " .\n" : " ,\n");
  }

  // Port groups, in first-appearance order across the parameter table.
  std::vector<std::string> seen;
  std::ostringstream groups;
  auto addGroup = [&](const char* module) {
    const std::string s = groupSlug(module);
    if (std::find(seen.begin(), seen.end(), s) != seen.end()) {
      return;
    }
    seen.push_back(s);
    groups << "\n<" << kUri << "#" << s << "> a pg:Group ; lv2:name \""
           << module << "\" .\n";
  };
  for (uint32_t id = 0; id < comp::P_COUNT; ++id) {
    addGroup(comp::kParamSpecs[id].module);
  }
  addGroup("Meter");
  out << "\n" << groups.str();
  return out.str();
}

std::string buildManifestTtl() {
  std::ostringstream out;
  out << "# Generated by tools/gen_ttl.cpp - do not edit.\n";
  out << "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
  out << "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
  out << "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .\n";
  out << "\n";
  out << "<" << kUri << ">\n";
  out << "    a lv2:Plugin , lv2:CompressorPlugin ;\n";
  out << "    lv2:binary <" << kPluginBinary << "> ;\n";
  out << "    rdfs:seeAlso <comp_suite.ttl> , <presets.ttl> ;\n";
  out << "    ui:ui <" << kUri << "#ui> .\n";
  out << "\n";
  out << "<" << kUri << "#ui>\n";
  out << "    a ui:X11UI ;\n";
  out << "    ui:binary <" << kUiBinary << "> ;\n";
  out << "    rdfs:label \"" << kName << " UI\" ;\n";
  out << "    lv2:requiredFeature ui:parent ;\n";
  out << "    lv2:optionalFeature ui:scaleFactor , ui:idleInterface ;\n";
  out << "    lv2:extensionData ui:idleInterface ;\n";
  out << "    rdfs:seeAlso <comp_suite.ttl> .\n";
  return out.str();
}

// One preset -> one pset:Preset.  The model is always emitted explicitly:
// Preset::model lives outside values[] and forgetting it reset the model.
std::string buildPresetsTtl() {
  struct Bank {
    std::string label;
    std::string uri;
  };
  std::vector<Bank> banks;
  std::vector<std::string> presetBlocks;
  std::set<std::string> used;

  for (int i = 0; i < comp::kPresetCount; ++i) {
    const comp::Preset& p = comp::kPresets[i];
    const std::string bankSlug = slug(p.category == nullptr ? "" : p.category);
    auto it = std::find_if(banks.begin(), banks.end(), [&](const Bank& b) {
      return b.uri == std::string(kUri) + "#bank_" + bankSlug;
    });
    if (it == banks.end()) {
      banks.push_back(
          {p.category == nullptr ? "" : p.category,
           std::string(kUri) + "#bank_" + bankSlug});
    }

    std::string prefix = slug(p.name == nullptr ? "" : p.name);
    std::string presetSlug = prefix;
    int n = 2;
    while (!used.insert(presetSlug).second) {
      presetSlug = prefix + "-" + std::to_string(n++);
    }

    std::ostringstream out;
    out << "<" << kUri << "#preset_" << presetSlug << ">\n";
    out << "    a pset:Preset ;\n";
    out << "    lv2:appliesTo <" << kUri << "> ;\n";
    out << "    rdfs:label \"" << escaped(p.name == nullptr ? "" : p.name)
        << "\" ;\n";
    out << "    rdfs:comment \"" << escaped(p.category == nullptr ? "" : p.category)
        << "\" ;\n";
    out << "    pset:bank <" << kUri << "#bank_" << bankSlug << "> ;\n";

    std::vector<std::pair<const char*, double>> values;
    const int model = std::min(std::max(p.model, 0), 5);
    values.emplace_back(comp::kParamSpecs[comp::P_MODEL].symbol,
                        static_cast<double>(model));
    for (const comp::PresetValue& v : p.values) {
      if (v.param < 0) {
        continue;
      }
      if (v.param >= static_cast<int>(comp::P_COUNT)) {
        continue;
      }
      const char* symbol = comp::kParamSpecs[v.param].symbol;
      if (std::strcmp(symbol, comp::kParamSpecs[comp::P_MODEL].symbol) == 0) {
        continue;  // already emitted
      }
      values.emplace_back(symbol, static_cast<double>(v.value));
    }

    out << "    lv2:port\n";
    for (size_t v = 0; v < values.size(); ++v) {
      out << "    [ lv2:symbol \"" << values[v].first << "\" ; pset:value "
          << num(values[v].second) << " ]"
          << (v + 1 == values.size() ? " .\n" : " ,\n");
    }
    presetBlocks.push_back(out.str());
  }

  std::ostringstream out;
  out << "# Generated by tools/gen_ttl.cpp - do not edit.\n";
  out << "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
  out << "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n";
  out << "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
  out << "\n";
  for (const Bank& b : banks) {
    out << "<" << b.uri << ">\n";
    out << "    a pset:Bank ;\n";
    out << "    rdfs:label \"" << escaped(b.label) << "\" .\n";
    out << "\n";
  }
  for (const std::string& block : presetBlocks) {
    out << block << "\n";
  }
  return out.str();
}

bool checkOrWrite(const std::string& path, const std::string& content,
                  bool check) {
  if (check) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "gen_ttl: missing %s\n", path.c_str());
      return false;
    }
    std::ostringstream existing;
    existing << in.rdbuf();
    if (existing.str() != content) {
      std::fprintf(stderr,
                   "gen_ttl: %s is out of date - rebuild it with "
                   "`gen_ttl`\n",
                   path.c_str());
      return false;
    }
    return true;
  }
  std::ofstream outFile(path, std::ios::binary | std::ios::trunc);
  if (!outFile) {
    std::fprintf(stderr, "gen_ttl: cannot write %s\n", path.c_str());
    return false;
  }
  outFile << content;
  return true;
}

} // namespace

int main(int argc, char** argv) {
  bool check = false;
  std::string root = ".";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--check") == 0) {
      check = true;
    } else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
      root = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::printf(
          "usage: gen_ttl [--check] [--root <dir>]\n"
          "  writes <dir>/lv2/{comp_suite,manifest,presets}.ttl\n");
      return 0;
    } else {
      std::fprintf(stderr, "gen_ttl: unknown argument %s\n", argv[i]);
      return 2;
    }
  }

  const std::string dir = root + "/lv2/";
  bool ok = true;
  ok &= checkOrWrite(dir + "comp_suite.ttl", buildPluginTtl(), check);
  ok &= checkOrWrite(dir + "manifest.ttl", buildManifestTtl(), check);
  ok &= checkOrWrite(dir + "presets.ttl", buildPresetsTtl(), check);
  if (!ok) {
    return 1;
  }
  if (!check) {
    std::printf("gen_ttl: wrote %s\n", dir.c_str());
  }
  return 0;
}
