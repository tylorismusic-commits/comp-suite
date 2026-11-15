// Comp Suite — engine.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "clean.hpp"
#include "common.hpp"
#include "molot.hpp"
#include "easy.hpp"
#include "red.hpp"
#include "controls.hpp"
#include "distressor.hpp"
#include "fet.hpp"
#include "optical.hpp"
#include "oversample.hpp"
#include "truepeak.hpp"
#include "vca.hpp"

namespace comp {

// Hosts every compressor model.  Only the selected model runs, except
// during a short crossfade when the selection changes, so switching between
// models does not click.  Mix and bypass are applied here, sample by sample.
//
// The whole signal path can run at 1x, 2x or 4x.  The models are prepared at
// the internal rate, so their time constants stay in real time; the engine's
// own smoothers use the internal rate too.  Off keeps zero latency.
class Engine {
public:
  static int factorForMode(float mode) noexcept {
    const int m = std::clamp(static_cast<int>(mode + 0.5f), 0, 2);
    return m == 0 ? 1 : (m == 1 ? 2 : 4);
  }

  // oversampleMode < 0 keeps the current mode; hosts that activate after a
  // state load pass the stored mode so the first block is already correct.
  void prepare(float sampleRate, float oversampleMode = -1.0f) noexcept {
    baseRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    if (oversampleMode >= 0.0f) {
      controls_.oversample = oversampleMode;
    }
    configureRate(factorForMode(controls_.oversample));
  }

  // CLAP requires the plugin to keep its old latency until the host has
  // reactivated it.  In that mode a new oversampling setting is stored but the
  // engine keeps running at the old rate; prepare() applies it during
  // activation.  LV2 has no activate/restart cycle, so it applies immediately.
  void setDeferOversample(bool defer) noexcept { deferOversample_ = defer; }

  void setControls(const Controls& c) noexcept {
    const int requested = factorForMode(c.oversample);
    if (!deferOversample_ && requested != factor_) {
      // A rate change rebuilds every model and clears the converters.  The
      // switch is a setup action, not an automatable performance gesture.
      configureRate(requested);
    }
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

  // Latency introduced by the current oversampling mode, in base-rate samples.
  int latencySamples() const noexcept { return os_.latencySamples(); }

  // Processes `frames` stereo frames in place.  `scLeft`/`scRight` are the
  // external side-chain inputs, or null when the host has not supplied them.
  // Meters receive the peak and averaged output levels for the frame block.
  void process(float* left, float* right, int frames, Meters& meters,
               const float* scLeft = nullptr,
               const float* scRight = nullptr) noexcept {
    const bool scReady = controls_.externalSidechain && scLeft && scRight;
    Stats st;
    if (factor_ == 1) {
      processBlock(left, right, frames, scReady ? scLeft : nullptr,
                   scReady ? scRight : nullptr, st);
    } else {
      for (int start = 0; start < frames;
           start += Oversampler::kMaxBaseFrames) {
        const int n = std::min(Oversampler::kMaxBaseFrames, frames - start);
        os_.up(left + start, right + start, n, osL_, osR_);
        if (scReady) {
          scOs_.up(scLeft + start, scRight + start, n, osScL_, osScR_);
        }
        processBlock(osL_, osR_, n * factor_,
                     scReady ? osScL_ : nullptr,
                     scReady ? osScR_ : nullptr, st);
        os_.down(osL_, osR_, n, left + start, right + start);
      }
    }
    // A bypassed compressor is not reducing anything, and hosts use this
    // figure for automatic gain compensation: reporting wet-path reduction
    // while the output is dry would make them compensate for nothing.
    meters.grDb = controls_.enabled ? st.grMax : 0.0f;
    meters.outPeakDb = gainToDb(st.peak);
    meters.inPeakDb = gainToDb(st.inPeak);
    meters.outPeakLDb = gainToDb(st.outPeakL);
    meters.outPeakRDb = gainToDb(st.outPeakR);
    meters.inPeakLDb = gainToDb(st.inPeakL);
    meters.inPeakRDb = gainToDb(st.inPeakR);
    meters.scPeakDb = gainToDb(st.scPeak);
    meters.scActive = scReady;
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
    molot_.reset();
    red_.reset();
    easy_.reset();
    os_.reset();
    scOs_.reset();
    vuPower_ = 0.0f;
    autoMakeupDb_ = 0.0f;
    limiterGain_ = 1.0f;
    truePeakL_.reset();
    truePeakR_.reset();
    fade_ = 1.0f;
    previous_ = active_;
    mixSmooth_.snap();
    enableSmooth_.snap();
  }

private:
  struct Stats {
    float grMax = 0.0f;
    float peak = 0.0f;
    float inPeak = 0.0f;
    float outPeakL = 0.0f;
    float outPeakR = 0.0f;
    float inPeakL = 0.0f;
    float inPeakR = 0.0f;
    float scPeak = 0.0f;
  };

  // (Re)builds every model and engine smoother for a given oversampling
  // factor.  Called from prepare() and from a mode change.
  void configureRate(int factor) noexcept {
    factor_ = factor;
    osRate_ = baseRate_ * static_cast<float>(factor_);
    blue_.prepare(osRate_, FetLimiter::Variant::Blue);
    black_.prepare(osRate_, FetLimiter::Variant::Black);
    la2a_.prepare(osRate_, OptoLeveler::Variant::LA2A);
    la3a_.prepare(osRate_, OptoLeveler::Variant::LA3A);
    xpress_.prepare(osRate_);
    vca_.prepare(osRate_);
    clean_.prepare(osRate_);
    molot_.prepare(osRate_);
    red_.prepare(osRate_);
    easy_.prepare(osRate_);

    fadeStep_ = 1.0f / std::max(1.0f, 0.02f * osRate_);
    mixSmooth_.setTime(10.0f, osRate_);
    enableSmooth_.setTime(10.0f, osRate_);
    mixSmooth_.reset(1.0f);
    enableSmooth_.reset(1.0f);
    vuCoeff_ = timeCoeff(300.0f, osRate_);
    vuPower_ = 0.0f;
    autoMakeupCoeff_ = timeCoeff(2000.0f, osRate_);
    autoMakeupDb_ = 0.0f;
    truePeakL_.reset();
    truePeakR_.reset();
    limiterGain_ = 1.0f;
    limiterAttack_ = timeCoeff(0.5f, osRate_);
    limiterRelease_ = timeCoeff(80.0f, osRate_);
    os_.prepare(factor_);
    scOs_.prepare(factor_);
    fade_ = 1.0f;
    // -1 means "no model chosen yet": the first setControls adopts its model
    // directly instead of crossfading from the default.
    active_ = -1;
    previous_ = -1;
    primed_ = false;
  }

  float processModel(int index, float& l, float& r,
                     const SidechainFrame& sc) noexcept {
    float gr = 0.0f;
    switch (static_cast<Model>(index)) {
      case Model::Blue76: gr = blue_.process(l, r, sc); break;
      case Model::Black76: gr = black_.process(l, r, sc); break;
      case Model::Opto2A: gr = la2a_.process(l, r, sc); break;
      case Model::Opto3A: gr = la3a_.process(l, r, sc); break;
      case Model::Xpress: gr = xpress_.process(l, r, sc); break;
      case Model::Vca: gr = vca_.process(l, r, sc); break;
      case Model::Clean: gr = clean_.process(l, r, sc); break;
      case Model::Molot: gr = molot_.process(l, r, sc); break;
      case Model::Red: gr = red_.process(l, r, sc); break;
      case Model::Easy: gr = easy_.process(l, r, sc); break;
      default: return 0.0f;
    }
    return gr;
  }

  // The per-sample path.  `frames` are at the internal (possibly oversampled)
  // rate; side-chain pointers are already at that rate.
  void processBlock(float* left, float* right, int frames,
                    const float* scLeft, const float* scRight,
                    Stats& st) noexcept {
    const bool scReady = scLeft && scRight;
    for (int i = 0; i < frames; ++i) {
      // A host may hand us NaN, inf, or absurd values from a broken bus.  A
      // single one would poison every recursive state in the model, so the
      // boundary is sanitised once, here.
      const float rawL = std::isfinite(left[i]) ? left[i] : 0.0f;
      const float rawR = std::isfinite(right[i]) ? right[i] : 0.0f;
      const float dryL = clampf(rawL, -32.0f, 32.0f);
      const float dryR = clampf(rawR, -32.0f, 32.0f);
      float wetL = dryL;
      float wetR = dryR;
      SidechainFrame sc;
      if (scReady) {
        sc.left = clampf(std::isfinite(scLeft[i]) ? scLeft[i] : 0.0f, -32.0f,
                         32.0f);
        sc.right = clampf(std::isfinite(scRight[i]) ? scRight[i] : 0.0f, -32.0f,
                          32.0f);
        sc.active = true;
        st.scPeak =
            std::max(st.scPeak, std::max(std::fabs(sc.left), std::fabs(sc.right)));
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

      // The limiter and Clean's colour work on the untrimmed model output, so
      // the calibration trim changes level only, never how they behave.
      if (controls_.limiterOn && on > 0.001f &&
          active_ != static_cast<int>(Model::Red)) {
        // Output limiter: stereo-linked peak limiter with a hard safety
        // ceiling, matching the limiter section in the reference layout.
        const float samplePeak = std::max(std::fabs(wetL), std::fabs(wetR));
        float peakNow = samplePeak;
        if (controls_.truePeak) {
          const float tp =
              std::max(truePeakL_.process(wetL), truePeakR_.process(wetR));
          peakNow = std::max(peakNow, tp);
        }
        const float thresh =
            dbToGain(-24.0f +
                     24.0f * clampf(controls_.limitThreshold, 0.0f, 1.0f));
        float target = 1.0f;
        if (peakNow > thresh) {
          target = thresh / peakNow;
        }
        if (target < limiterGain_) {
          limiterGain_ =
              limiterAttack_ * limiterGain_ + (1.0f - limiterAttack_) * target;
        } else {
          limiterGain_ = limiterRelease_ * limiterGain_ +
                         (1.0f - limiterRelease_) * target;
        }
        float yL = wetL * limiterGain_;
        float yR = wetR * limiterGain_;
        if (std::fabs(yL) > thresh) {
          yL = std::copysign(thresh * std::tanh(std::fabs(yL) / thresh), yL);
        }
        if (std::fabs(yR) > thresh) {
          yR = std::copysign(thresh * std::tanh(std::fabs(yR) / thresh), yR);
        }
        wetL = yL;
        wetR = yR;
      }

      // The Clean model's colour sits after the limiter, right before the
      // output, and only while the plugin is active.
      if (active_ == static_cast<int>(Model::Clean) && controls_.enabled) {
        clean_.postColour(wetL, wetR);
      }

      // Model level calibration: applied to the wet path only, after all
      // nonlinear stages, so every model lands at the same output level at
      // noon without changing its dynamics or colour.
      const float trim = dbToGain(kModelTrimDb[active_]);
      wetL *= trim;
      wetR *= trim;

      float outL = dryL + wetMix * (wetL - dryL);
      float outR = dryR + wetMix * (wetR - dryR);

      left[i] = std::isfinite(outL) ? outL : 0.0f;
      right[i] = std::isfinite(outR) ? outR : 0.0f;
      st.grMax = std::max(st.grMax, gr);
      st.peak = std::max(st.peak, std::max(std::fabs(outL), std::fabs(outR)));
      st.inPeak = std::max(st.inPeak, std::max(std::fabs(dryL), std::fabs(dryR)));
      st.outPeakL = std::max(st.outPeakL, std::fabs(outL));
      st.outPeakR = std::max(st.outPeakR, std::fabs(outR));
      st.inPeakL = std::max(st.inPeakL, std::fabs(dryL));
      st.inPeakR = std::max(st.inPeakR, std::fabs(dryR));
      const float power = 0.5f * (outL * outL + outR * outR);
      vuPower_ = vuCoeff_ * vuPower_ + (1.0f - vuCoeff_) * power;
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
      case Model::Molot: molot_.setControls(c); break;
      case Model::Red: red_.setControls(c); break;
      case Model::Easy: easy_.setControls(c); break;
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
      case Model::Molot: molot_.reset(); break;
      case Model::Red: red_.reset(); break;
      case Model::Easy: easy_.reset(); break;
      default: break;
    }
  }

  float baseRate_ = 48000.0f;
  float osRate_ = 48000.0f;
  int factor_ = 1;
  bool deferOversample_ = false;
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
  MolotCompressor molot_;
  RedCompressor red_;
  EasyCompressor easy_;
  Oversampler os_;
  Oversampler scOs_;
  float osL_[4 * Oversampler::kMaxBaseFrames] = {};
  float osR_[4 * Oversampler::kMaxBaseFrames] = {};
  float osScL_[4 * Oversampler::kMaxBaseFrames] = {};
  float osScR_[4 * Oversampler::kMaxBaseFrames] = {};
};

} // namespace comp
