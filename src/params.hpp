// Comp Suite — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "dsp/controls.hpp"

namespace comp {

// Parameter ids are the CLAP parameter ids and the index into Shared::values.
// They are also the ordering of the LV2 control ports (port = 4 + id), so this
// enum must only ever be appended to.
enum ParamId : uint32_t {
  P_MODEL = 0,
  P_ENABLED,
  P_INPUT,
  P_THRESHOLD,
  P_RATIO,
  P_ATTACK,
  P_RELEASE,
  P_MAKEUP,
  P_OUTPUT,
  P_MIX,
  P_SC_HPF,
  P_DISTORTION,
  P_METER,
  P_LIMITER_ON,
  P_LIMITER_THRESH,
  P_AUTO_MAKEUP,
  P_SC_SOURCE,
  P_TRUE_PEAK,
  P_VCA_LINK,
  P_VCA_AUTO,
  P_COUNT
};

// Scale-point labels for a stepped parameter.  Values are always 0..count-1.
struct Labels {
  const char* const* names = nullptr;
  int count = 0;
};

template <size_t N>
constexpr Labels labels(const char* const (&arr)[N]) noexcept {
  return Labels{arr, static_cast<int>(N)};
}

// One table drives the DSP, the CLAP parameters, the editor and the LV2
// metadata (via tools/gen_ttl.cpp).  Adding a parameter means adding one row.
//
// `symbol` is the LV2 port symbol and is part of the saved-session format:
// changing one silently breaks every session that used it.  Never rename.
struct ParamSpec {
  const char* symbol;
  const char* name;
  const char* module;
  double min;
  double max;
  double def;
  bool stepped;
  bool hidden;
  bool toggled;
  const char* unit;
  Labels labels;
};

inline constexpr const char* kModelLabels[] = {"76Blue", "76Black", "2A",
                                               "3A",     "Xpress",  "VCA"};
inline constexpr const char* kDistortionLabels[] = {"Off", "Dist 2", "Dist 3",
                                                    "Clip"};
inline constexpr const char* kMeterLabels[] = {"GR", "+8", "+4", "Graph",
                                               "Off"};
inline constexpr const char* kScSourceLabels[] = {"Internal", "External"};

inline constexpr ParamSpec kParamSpecs[P_COUNT] = {
    {"model", "Model", "Compressor", 0.0, 5.0, 0.0, true, false, false, nullptr,
     labels(kModelLabels)},
    {"power", "Power", "Compressor", 0.0, 1.0, 1.0, true, false, true, nullptr,
     {}},
    {"input", "Input", "Compressor", 0.0, 1.0, 0.6, false, false, false,
     nullptr, {}},
    {"threshold", "Threshold", "Compressor", 0.0, 1.0, 0.5, false, false, false,
     nullptr, {}},
    {"ratio", "Ratio", "Compressor", 0.0, 8.0, 3.0, true, false, false, nullptr,
     {}},
    {"attack", "Attack", "Compressor", 0.0, 1.0, 0.5, false, false, false,
     nullptr, {}},
    {"release", "Release", "Compressor", 0.0, 1.0, 0.5, false, false, false,
     nullptr, {}},
    {"makeup", "Makeup", "Compressor", 0.0, 1.0, 0.5, false, false, false,
     nullptr, {}},
    {"output", "Output", "Output", 0.0, 1.0, 0.5, false, false, false, nullptr,
     {}},
    {"mix", "Mix", "Output", 0.0, 1.0, 1.0, false, false, false, nullptr, {}},
    {"sc_hpf", "Sidechain HPF", "Sidechain", 0.0, 1.0, 0.0, false, false, false,
     nullptr, {}},
    {"distortion", "Distortion", "Output", 0.0, 3.0, 0.0, true, false, false,
     nullptr, labels(kDistortionLabels)},
    {"meter", "Meter", "Meter", 0.0, 4.0, 0.0, true, true, false, nullptr,
     labels(kMeterLabels)},
    {"limiter_on", "Limiter", "Limiter", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}},
    {"limiter_threshold", "Limiter Threshold", "Limiter", 0.0, 1.0, 1.0, false,
     false, false, nullptr, {}},
    {"auto_makeup", "Auto Makeup", "Output", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}},
    {"sc_source", "Sidechain", "Sidechain", 0.0, 1.0, 0.0, true, false, true,
     nullptr, labels(kScSourceLabels)},
    {"true_peak", "True Peak", "Limiter", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}},
    {"vca_link", "Link", "Compressor", 0.0, 1.0, 1.0, false, false, false,
     nullptr, {}},
    {"vca_auto", "Auto Release", "Compressor", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}},
};

static_assert(sizeof(kParamSpecs) / sizeof(kParamSpecs[0]) == P_COUNT,
              "kParamSpecs must cover every parameter id");

// The control values, meters, and dirty flags shared between the audio
// thread, the host thread, and the editor.  Everything is atomic so the
// editor can run on the GUI thread without locks.
class Shared {
public:
  Shared() {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      values_[p].store(kParamSpecs[p].def, std::memory_order_relaxed);
      dirty_[p].store(false, std::memory_order_relaxed);
    }
    grDb.store(0.0f, std::memory_order_relaxed);
    inPeakDb.store(-120.0f, std::memory_order_relaxed);
    outPeakDb.store(-120.0f, std::memory_order_relaxed);
    outVuDb.store(-120.0f, std::memory_order_relaxed);
    inPeakLDb.store(-120.0f, std::memory_order_relaxed);
    inPeakRDb.store(-120.0f, std::memory_order_relaxed);
    outPeakLDb.store(-120.0f, std::memory_order_relaxed);
    outPeakRDb.store(-120.0f, std::memory_order_relaxed);
  }

  double get(uint32_t p) const noexcept {
    return values_[p].load(std::memory_order_relaxed);
  }

  // Clamps to the parameter range and marks the value for the host.
  void set(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) {
      return;  // a host (or a bad automation curve) sent NaN/inf
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) {
      v = static_cast<double>(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    }
    values_[p].store(v, std::memory_order_relaxed);
    dirty_[p].store(true, std::memory_order_release);
  }

  // Stores a value that came from the host, without marking it dirty.
  void setFromHost(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) {
      return;
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    values_[p].store(v, std::memory_order_relaxed);
  }

  bool takeDirty(uint32_t p) noexcept {
    return dirty_[p].exchange(false, std::memory_order_acquire);
  }

  bool isDirty(uint32_t p) const noexcept {
    return dirty_[p].load(std::memory_order_acquire);
  }

  bool anyDirty() const noexcept {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      if (dirty_[p].load(std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  std::atomic<float> grDb;
  std::atomic<float> inPeakDb;
  std::atomic<float> outPeakDb;
  std::atomic<float> outVuDb;
  std::atomic<float> inPeakLDb;
  std::atomic<float> inPeakRDb;
  std::atomic<float> outPeakLDb;
  std::atomic<float> outPeakRDb;

private:
  std::atomic<double> values_[P_COUNT];
  std::atomic<bool> dirty_[P_COUNT];
};

// Maps the host-facing parameter values to the DSP controls.
inline Controls controlsFrom(const Shared& s) {
  Controls c;
  c.model = static_cast<int>(s.get(P_MODEL) + 0.5);
  c.enabled = s.get(P_ENABLED) > 0.5;
  c.input = static_cast<float>(s.get(P_INPUT));
  c.threshold = static_cast<float>(s.get(P_THRESHOLD));
  c.ratio = static_cast<float>(s.get(P_RATIO));
  c.attack = static_cast<float>(s.get(P_ATTACK));
  c.release = static_cast<float>(s.get(P_RELEASE));
  c.makeup = static_cast<float>(s.get(P_MAKEUP));
  c.output = static_cast<float>(s.get(P_OUTPUT));
  c.mix = static_cast<float>(s.get(P_MIX));
  c.sidechainHpf = static_cast<float>(s.get(P_SC_HPF));
  c.distortion = static_cast<float>(s.get(P_DISTORTION) / 3.0);
  c.limiterOn = s.get(P_LIMITER_ON) > 0.5;
  c.limitThreshold = static_cast<float>(s.get(P_LIMITER_THRESH));
  c.autoMakeup = s.get(P_AUTO_MAKEUP) > 0.5;
  c.externalSidechain = s.get(P_SC_SOURCE) > 0.5;
  c.truePeak = s.get(P_TRUE_PEAK) > 0.5;
  c.link = static_cast<float>(s.get(P_VCA_LINK));
  c.vcaAutoRelease = s.get(P_VCA_AUTO) > 0.5;
  return c;
}

} // namespace comp
