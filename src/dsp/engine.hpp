// Comp Suite — engine.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "clean.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "distressor.hpp"
#include "fet.hpp"
#include "optical.hpp"
#include "truepeak.hpp"
#include "vca.hpp"

namespace comp {

// Hosts all six compressor models.  Only the selected model runs, except
// during a short crossfade when the selection changes, so switching between
// models does not click.  Mix and bypass are applied here, sample by sample.
class Engine {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate;
    blue_.prepare(sr_, FetLimiter::Variant::Blue);
    black_.prepare(sr_, FetLimiter::Variant::Black);
    la2a_.prepare(sr_, OptoLeveler::Variant::LA2A);
    la3a_.prepare(sr_, OptoLeveler::Variant::LA3A);
    xpress_.prepare(sr_);
    vca_.prepare(sr_);
    clean_.prepare(sr_);

    fadeStep_ = 1.0f / std::max(1.0f, 0.02f * sr_);
    mixSmooth_.setTime(10.0f, sr_);
    enableSmooth_.setTime(10.0f, sr_);
    mixSmooth_.reset(1.0f);
    enableSmooth_.reset(1.0f);
    vuCoeff_ = timeCoeff(300.0f, sr_);
    vuPower_ = 0.0f;
    autoMakeupCoeff_ = timeCoeff(2000.0f, sr_);
    autoMakeupDb_ = 0.0f;
    truePeakL_.reset();
    truePeakR_.reset();
    limiterGain_ = 1.0f;
    limiterEnv_ = 0.0f;
    limiterAttack_ = timeCoeff(0.5f, sr_);
    limiterRelease_ = timeCoeff(80.0f, sr_);
    fade_ = 1.0f;
    // -1 means "no model chosen yet": the first setControls adopts its model
    // directly instead of crossfading from the default.
    active_ = -1;
    previous_ = -1;
    primed_ = false;
  }

  void setControls(const Controls& c) noexcept {
    const int next = std::clamp(c.model, 0, static_cast<int>(Model::Count) - 1);
    if (active_ < 0) {
      active_ = next;
      previous_ = next;
      fade_ = 1.0f;
      resetModel(active_);
    } else if (next != active_) {
      previous_ = active_;
      active_ = next;
      resetModel(active_);
      fade_ = 0.0f;
    }
    controls_ = c;
    controls_.model = next;
    applyControls(active_, controls_);
    mixSmooth_.setTarget(clampf(c.mix, 0.0f, 1.0f));
    enableSmooth_.setTarget(c.enabled ? 1.0f : 0.0f);
    // The first controls after prepare take effect immediately: no fade-in,
    // and a disabled engine starts fully dry.
    if (!primed_) {
      primed_ = true;
      mixSmooth_.snap();
      enableSmooth_.snap();
    }
  }

  // Processes `frames` stereo frames in place.  `scLeft`/`scRight` are the
  // external side-chain inputs, or null when the host has not supplied them.
  // Meters receive the peak and averaged output levels for the frame block.
  void process(float* left, float* right, int frames, Meters& meters,
               const float* scLeft = nullptr,
               const float* scRight = nullptr) noexcept {
    const bool scReady = controls_.externalSidechain && scLeft && scRight;
    float peak = 0.0f;
    float grMax = 0.0f;
    float inPeak = 0.0f;
    float outPeakL = 0.0f;
    float outPeakR = 0.0f;
    float inPeakL = 0.0f;
    float inPeakR = 0.0f;
    for (int i = 0; i < frames; ++i) {
      const float dryL = left[i];
      const float dryR = right[i];
      float wetL = dryL;
      float wetR = dryR;
      SidechainFrame sc;
      if (scReady) {
        sc.left = scLeft[i];
        sc.right = scRight[i];
        sc.active = true;
      }
      float gr = processModel(active_, wetL, wetR, sc);

      if (fade_ < 1.0f) {
        float oldL = dryL;
        float oldR = dryR;
        const float oldGr = processModel(previous_, oldL, oldR, sc);
        const float f = fade_;
        wetL = oldL * (1.0f - f) + wetL * f;
        wetR = oldR * (1.0f - f) + wetR * f;
        gr = std::max(gr * f, oldGr * (1.0f - f));
        fade_ = std::min(1.0f, fade_ + fadeStep_);
      }

      // Auto-makeup tracks the average reduction and restores the level.  It
      // is applied to the wet signal only, so a parallel blend keeps its dry
      // share untouched.
      if (controls_.autoMakeup) {
        autoMakeupDb_ += (gr - autoMakeupDb_) * autoMakeupCoeff_;
        const float makeup = dbToGain(std::min(autoMakeupDb_, 24.0f));
        wetL *= makeup;
        wetR *= makeup;
      }

      const float mix = mixSmooth_.next();
      const float on = enableSmooth_.next();
      // Enable is applied before mix, so a bypassed engine is exactly the
      // dry signal whatever the mix setting is.
      const float wetMix = on * mix;
      float outL = dryL + wetMix * (wetL - dryL);
      float outR = dryR + wetMix * (wetR - dryR);

      if (controls_.limiterOn) {
        // Output limiter: stereo-linked peak limiter with a hard safety
        // ceiling, matching the limiter section in the reference layout.
        const float samplePeak = std::max(std::fabs(outL), std::fabs(outR));
        float peakNow = samplePeak;
        if (controls_.truePeak) {
          const float tp =
              std::max(truePeakL_.process(outL), truePeakR_.process(outR));
          peakNow = std::max(peakNow, tp);
        }
        const float thresh = dbToGain(-24.0f + 24.0f * clampf(controls_.limitThreshold, 0.0f, 1.0f));
        float target = 1.0f;
        if (peakNow > thresh) {
          target = thresh / peakNow;
        }
        if (target < limiterGain_) {
          limiterGain_ = limiterAttack_ * limiterGain_ + (1.0f - limiterAttack_) * target;
        } else {
          limiterGain_ = limiterRelease_ * limiterGain_ + (1.0f - limiterRelease_) * target;
        }
        outL = std::tanh(outL * limiterGain_);
        outR = std::tanh(outR * limiterGain_);
      }

      left[i] = outL;
      right[i] = outR;
      grMax = std::max(grMax, gr);
      peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
      inPeak = std::max(inPeak, std::max(std::fabs(dryL), std::fabs(dryR)));
      outPeakL = std::max(outPeakL, std::fabs(outL));
      outPeakR = std::max(outPeakR, std::fabs(outR));
      inPeakL = std::max(inPeakL, std::fabs(dryL));
      inPeakR = std::max(inPeakR, std::fabs(dryR));
      const float power = 0.5f * (outL * outL + outR * outR);
      vuPower_ = vuCoeff_ * vuPower_ + (1.0f - vuCoeff_) * power;
    }
    meters.grDb = grMax;
    meters.outPeakDb = gainToDb(peak);
    meters.inPeakDb = gainToDb(inPeak);
    meters.outPeakLDb = gainToDb(outPeakL);
    meters.outPeakRDb = gainToDb(outPeakR);
    meters.inPeakLDb = gainToDb(inPeakL);
    meters.inPeakRDb = gainToDb(inPeakR);
    meters.outVuDb = gainToDb(std::sqrt(vuPower_)) + 18.0f;
  }

  void reset() noexcept {
    blue_.reset();
    black_.reset();
    la2a_.reset();
    la3a_.reset();
    xpress_.reset();
    vca_.reset();
    clean_.reset();
    vuPower_ = 0.0f;
  }

private:
  float processModel(int index, float& l, float& r,
                     const SidechainFrame& sc) noexcept {
    switch (static_cast<Model>(index)) {
      case Model::Blue76: return blue_.process(l, r, sc);
      case Model::Black76: return black_.process(l, r, sc);
      case Model::Opto2A: return la2a_.process(l, r, sc);
      case Model::Opto3A: return la3a_.process(l, r, sc);
      case Model::Xpress: return xpress_.process(l, r, sc);
      case Model::Vca: return vca_.process(l, r, sc);
      case Model::Clean: return clean_.process(l, r, sc);
      default: return 0.0f;
    }
  }

  void applyControls(int index, const Controls& c) noexcept {
    switch (static_cast<Model>(index)) {
      case Model::Blue76: blue_.setControls(c); break;
      case Model::Black76: black_.setControls(c); break;
      case Model::Opto2A: la2a_.setControls(c); break;
      case Model::Opto3A: la3a_.setControls(c); break;
      case Model::Xpress: xpress_.setControls(c); break;
      case Model::Vca: vca_.setControls(c); break;
      case Model::Clean: clean_.setControls(c); break;
      default: break;
    }
  }

  void resetModel(int index) noexcept {
    switch (static_cast<Model>(index)) {
      case Model::Blue76: blue_.reset(); break;
      case Model::Black76: black_.reset(); break;
      case Model::Opto2A: la2a_.reset(); break;
      case Model::Opto3A: la3a_.reset(); break;
      case Model::Xpress: xpress_.reset(); break;
      case Model::Vca: vca_.reset(); break;
      case Model::Clean: clean_.reset(); break;
      default: break;
    }
  }

  float sr_ = 48000.0f;
  Controls controls_;
  bool primed_ = false;
  int active_ = -1;
  int previous_ = 0;
  float fade_ = 1.0f;
  float fadeStep_ = 0.001f;
  float vuCoeff_ = 0.0f;
  float vuPower_ = 0.0f;
  float autoMakeupCoeff_ = 0.0f;
  float autoMakeupDb_ = 0.0f;
  TruePeakDetector truePeakL_;
  TruePeakDetector truePeakR_;
  float limiterGain_ = 1.0f;
  float limiterEnv_ = 0.0f;
  float limiterAttack_ = 0.0f;
  float limiterRelease_ = 0.0f;
  Smoothed mixSmooth_;
  Smoothed enableSmooth_;
  FetLimiter blue_;
  FetLimiter black_;
  OptoLeveler la2a_;
  OptoLeveler la3a_;
  Distressor xpress_;
  BusVca vca_;
  CleanCompressor clean_;
};

} // namespace comp
