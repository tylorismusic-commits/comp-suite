// Comp Suite — insert peer glue.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Comp Suite participates in the Suite peer bus as an insert plug-in: it
// publishes its parameter metadata once, mirrors parameters both ways and
// adopts the LINK assignment Suite Control writes.  The peer header is a copy
// of suite-control/src/peer.hpp; protocol v3 must match byte for byte or the
// segment is rejected on attach.
#pragma once

#include <algorithm>
#include <cstdio>

#include "params.hpp"
#include "peer.hpp"

namespace comp {

inline void peerSetText(char* dst, size_t size, const char* text) {
  std::snprintf(dst, size, "%s", text ? text : "");
}

// Fills one metadata record per parameter from the same table that drives the
// DSP, the CLAP parameters and the LV2 TTL.
inline void fillParamMeta(suite::peer::ParamMeta* out, uint32_t count) {
  for (uint32_t i = 0; i < count && i < P_COUNT; ++i) {
    const ParamSpec& spec = kParamSpecs[i];
    suite::peer::ParamMeta& meta = out[i];
    meta = suite::peer::ParamMeta{};
    peerSetText(meta.symbol, sizeof(meta.symbol), spec.symbol);
    peerSetText(meta.name, sizeof(meta.name), spec.name);
    peerSetText(meta.group, sizeof(meta.group), spec.module);
    peerSetText(meta.unit, sizeof(meta.unit), spec.unit);
    meta.min = static_cast<float>(spec.min);
    meta.max = static_cast<float>(spec.max);
    meta.def = static_cast<float>(spec.def);
    if (spec.stepped) meta.flags |= suite::peer::kMetaStepped;
    if (spec.toggled) meta.flags |= suite::peer::kMetaToggle;
    if (spec.hidden) meta.flags |= suite::peer::kMetaHidden;
    if (spec.labels.names && spec.labels.count > 0) {
      const uint32_t labels = std::min<uint32_t>(
          static_cast<uint32_t>(spec.labels.count), suite::peer::kMaxLabels);
      meta.labelCount = labels;
      for (uint32_t l = 0; l < labels; ++l) {
        peerSetText(meta.label[l], sizeof(meta.label[l]),
                    spec.labels.names[l]);
        meta.labelValue[l] = static_cast<float>(l);
      }
    }
  }
}

// The LINK assignment Suite Control made, saved with the plug-in so it
// survives a reload even when Suite Control has not refreshed yet.
struct InsertLink {
  uint32_t bus = 0;
  uint32_t channel = 0;
  uint32_t slot = 0;
};

} // namespace comp
