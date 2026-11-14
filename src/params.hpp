// Comp Suite — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "dsp/controls.hpp"

namespace comp {

// Parameter ids are the CLAP parameter ids and the index into Shared::values.
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
  P_COUNT
};

struct ParamSpec {
  const char* name;
  const char* module;
  double min;
  double max;
  double def;
  bool stepped;
  bool hidden;
};

inline constexpr ParamSpec kParamSpecs[P_COUNT] = {
    {"Model", "", 0.0, 5.0, 0.0, true, false},
    {"Power", "", 0.0, 1.0, 1.0, true, false},
    {"Input", "Compressor", 0.0, 1.0, 0.6, false, false},
    {"Threshold", "Compressor", 0.0, 1.0, 0.5, false, false},
    {"Ratio", "Compressor", 0.0, 8.0, 3.0, true, false},
    {"Attack", "Compressor", 0.0, 1.0, 0.5, false, false},
    {"Release", "Compressor", 0.0, 1.0, 0.5, false, false},
    {"Makeup", "Compressor", 0.0, 1.0, 0.5, false, false},
    {"Output", "Output", 0.0, 1.0, 0.5, false, false},
    {"Mix", "Output", 0.0, 1.0, 1.0, false, false},
    {"Sidechain HPF", "Side Chain", 0.0, 1.0, 0.0, false, false},
    {"Distortion", "Output", 0.0, 3.0, 0.0, true, false},
    {"Meter", "", 0.0, 4.0, 0.0, true, true},
    {"Limiter", "Limiter", 0.0, 1.0, 0.0, true, false},
    {"Limiter Threshold", "Limiter", 0.0, 1.0, 0.5, false, false},
};

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
  }

  double get(uint32_t p) const noexcept {
    return values_[p].load(std::memory_order_relaxed);
  }

  // Clamps to the parameter range and marks the value for the host.
  void set(uint32_t p, double v) noexcept {
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
  return c;
}

} // namespace comp
