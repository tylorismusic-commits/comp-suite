// Comp Suite — easy.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "biquad.hpp"
#include "common.hpp"
#include "controls.hpp"

namespace comp {

// Drum-focused character compressor.
//
// Two characters rather than two models: Squish is fast and aggressive with a
// high ratio and a hard-ish knee; Lift is slower, gentler and glues.  A Drum
// Type selector re-voices the detector and time constants per source, Beef
// lifts the low end into the compression, Air lifts the top after it, and
// Saturation adds Soft / Tape / Hard colour.
class EasyCompressor {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    inputSmooth_.setTime(15.0f, sr_);
    outputSmooth_.setTime(15.0f, sr_);
    reset();
    setControls(controls_);
    inputSmooth_.snap();
    outputSmooth_.snap();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;

    squish_ = c.easyMode < 0.5f;
    extreme_ = c.easyChar > 0.5f;
    ratio_ = squish_ ? 8.0f : 2.5f;
    kneeDb_ = squish_ ? 3.0f : 9.0f;
    thresholdDb_ = -40.0f + 40.0f * clampf(c.threshold, 0.0f, 1.0f);

    // Drum Type re-voices the time constants and the detector high-pass.
    static const float kAttackMs[6] = {10.0f, 5.0f, 2.0f, 20.0f, 15.0f, 1.0f};
    static const float kReleaseMs[6] = {120.0f, 180.0f, 80.0f,
                                        400.0f, 250.0f, 60.0f};
    static const float kScHz[6] = {40.0f, 30.0f, 120.0f, 60.0f, 80.0f, 150.0f};
    const int type = std::clamp(static_cast<int>(c.drumType + 0.5f), 0, 5);
    float attackMs = kAttackMs[type];
    float releaseMs = kReleaseMs[type];
    if (squish_) {
      attackMs *= 0.5f;
      releaseMs *= 0.7f;
    } else {
      attackMs *= 1.5f;
      releaseMs *= 1.6f;
    }
    if (extreme_) {
      attackMs *= 0.6f;
      releaseMs *= 1.25f;
    }
    attackCoeff_ = timeCoeff(attackMs, sr_);
    releaseCoeff_ = timeCoeff(releaseMs, sr_);
    for (auto& f : scHpf_) {
      f.set(kScHz[type], sr_);
    }

    inputSmooth_.setTarget(
        dbToGain(-18.0f + 36.0f * clampf(c.input, 0.0f, 1.0f)));
    outputSmooth_.setTarget(
        dbToGain(-12.0f + 24.0f * clampf(c.output, 0.0f, 1.0f)));
    mix_ = clampf(c.mix, 0.0f, 1.0f);
    link_ = clampf(c.link, 0.0f, 1.0f);

    const float beefDb = 12.0f * clampf(c.beef, 0.0f, 1.0f);
    const float airDb = 12.0f * clampf(c.air, 0.0f, 1.0f);
    for (int ch = 0; ch < 2; ++ch) {
      beef_[ch].lowShelf(120.0f, beefDb, 0.7f, sr_);
      air_[ch].highShelf(8000.0f, airDb, 0.7f, sr_);
    }

    satType_ = std::clamp(static_cast<int>(c.satType + 0.5f), 0, 2);
    satDrive_ = clampf(c.satDrive, 0.0f, 1.0f) * (extreme_ ? 1.4f : 1.0f);
  }

  // Returns gain reduction in dB (>= 0).
  float process(float& left, float& right,
                const SidechainFrame& sc = {}) noexcept {
    const float inputGain = inputSmooth_.next();
    const float outputGain = outputSmooth_.next();

    const float dryL = left;
    const float dryR = right;
    float inL = dryL * inputGain;
    float inR = dryR * inputGain;

    // Beef shapes the signal entering both the detector and the compressor.
    inL = beef_[0].process(inL);
    inR = beef_[1].process(inR);

    float detL = sc.active ? sc.left : inL;
    float detR = sc.active ? sc.right : inR;
    detL = scHpf_[0].process(detL);
    detR = scHpf_[1].process(detR);

    const float levelL = std::fabs(detL);
    const float levelR = std::fabs(detR);
    const float loudest = std::max(levelL, levelR);
    const float targetL = link_ * loudest + (1.0f - link_) * levelL;
    const float targetR = link_ * loudest + (1.0f - link_) * levelR;

    updateEnv(0, targetL);
    updateEnv(1, targetR);
    const float grL =
        gainReductionDb(gainToDb(env_[0]), thresholdDb_, ratio_, kneeDb_);
    const float grR =
        gainReductionDb(gainToDb(env_[1]), thresholdDb_, ratio_, kneeDb_);

    float wetL = inL * dbToGain(-grL);
    float wetR = inR * dbToGain(-grR);

    if (satDrive_ > 0.01f) {
      const float k = 1.6f * satDrive_;
      const float bias = satType_ == 1 ? 0.10f : 0.0f;   // Tape: even orders
      const float drive = satType_ == 2 ? k * 1.5f : k;  // Hard: steeper
      wetL = sat_[0].process(wetL, drive, bias);
      wetR = sat_[1].process(wetR, drive, bias);
    }

    // Air lifts the top after compression.
    wetL = air_[0].process(wetL);
    wetR = air_[1].process(wetR);

    const float wet = mix_;
    const float dry = 1.0f - mix_;
    left = (dryL * dry + wetL * wet) * outputGain;
    right = (dryR * dry + wetR * wet) * outputGain;
    return std::max(grL, grR);
  }

  void reset() noexcept {
    env_[0] = env_[1] = 0.0f;
    hold_[0] = hold_[1] = 0;
    for (auto& f : beef_) f.reset();
    for (auto& f : air_) f.reset();
    for (auto& f : scHpf_) f.reset();
    for (auto& s : sat_) s.reset();
  }

private:
  void updateEnv(int ch, float level) noexcept {
    if (level > env_[ch]) {
      env_[ch] += (level - env_[ch]) * (1.0f - attackCoeff_);
      hold_[ch] = holdSamples_;
    } else if (hold_[ch] > 0) {
      --hold_[ch];
    } else {
      env_[ch] += (level - env_[ch]) * (1.0f - releaseCoeff_);
    }
  }

  float sr_ = 48000.0f;
  Controls controls_;
  float thresholdDb_ = -20.0f;
  float ratio_ = 8.0f;
  float kneeDb_ = 3.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float mix_ = 1.0f;
  float link_ = 1.0f;
  float satDrive_ = 0.0f;
  int satType_ = 0;
  int holdSamples_ = 48;  // 1 ms
  bool squish_ = true;
  bool extreme_ = false;
  float env_[2] = {0.0f, 0.0f};
  int hold_[2] = {0, 0};
  Smoothed inputSmooth_;
  Smoothed outputSmooth_;
  Biquad beef_[2];
  Biquad air_[2];
  HighPass2 scHpf_[2];
  AsymShaper sat_[2];
};

} // namespace comp
