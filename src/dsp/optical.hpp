// Comp Suite — optical.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "common.hpp"
#include "controls.hpp"

namespace comp {

// Optical leveling amplifier modelled on the 2A and 3A families.  A photocell
// with program-dependent response: attack starts quick and slows as the cell
// is driven harder, and release is two-stage, fast for small reductions and
// slow for large ones.  This is the behaviour that gives these units their
// smooth, "politely" compressed sound.
//
// LA2A — Peak Reduction plus a Compress/Limit switch, 2A-style output stage.
// LA3A — Peak Reduction only, fixed ratio, slightly darker, more even-order
//        colour in the output stage.
class OptoLeveler {
public:
  enum class Variant { LA2A, LA3A };

  void prepare(float sampleRate, Variant variant) noexcept {
    sr_ = sampleRate;
    variant_ = variant;
    const bool la2a = variant == Variant::LA2A;
    releaseSlowMs_ = la2a ? 1200.0f : 2000.0f;
    outLpHz_ = la2a ? 15000.0f : 12500.0f;
    bias_ = la2a ? 0.24f : 0.28f;
    driveScale_ = la2a ? 0.55f : 0.65f;
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].set(outLpHz_, sr_);
      lpOut_[ch].reset();
      shaper_[ch].reset();
    }
    detectAttack_ = timeCoeff(2.0f, sr_);
    detectRelease_ = timeCoeff(40.0f, sr_);
    env_ = 0.0f;
    cell_ = 0.0f;
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    const float pr = clampf(c.threshold, 0.0f, 1.0f);
    const bool la2a = variant_ == Variant::LA2A;
    const float baseDb = la2a ? -2.0f - 34.0f * pr : -2.0f - 30.0f * pr;

    if (la2a) {
      const bool limit = ratioStepFrom(c.ratio) >= 4;
      // COMP is a gentle, wide-knee 3:1; LIMIT is a firm 12:1 that also bites
      // 6 dB earlier.  Without that offset the two settings measured ~1 dB
      // apart and the switch sounded like it did nothing.
      ratio_ = limit ? 12.0f : 3.0f;
      kneeDb_ = limit ? 3.0f : 10.0f;
      thresholdDb_ = limit ? baseDb - 6.0f : baseDb;
      outputDb_ = c.output > 0.5f ? 6.0f : 0.0f;
    } else {
      ratio_ = 6.0f;
      kneeDb_ = 9.0f;
      thresholdDb_ = baseDb;
      outputDb_ = 0.0f;
    }
    // Bipolar: 0.5 is unity, so switching models does not jump in level.
    makeupGain_ = dbToGain(20.0f * (clampf(c.makeup, 0.0f, 1.0f) - 0.5f));
    outputGain_ = dbToGain(outputDb_);
  }

  // Processes one stereo frame in place.  Returns gain reduction in dB (>= 0).
  float process(float& left, float& right) noexcept {
    const float peak = std::max(std::fabs(left), std::fabs(right));
    if (peak > env_) {
      env_ = detectAttack_ * env_ + (1.0f - detectAttack_) * peak;
    } else {
      env_ = detectRelease_ * env_ + (1.0f - detectRelease_) * peak;
    }

    const float target =
        gainReductionDb(gainToDb(env_), thresholdDb_, ratio_, kneeDb_);
    updateCell(target);

    const float gain = dbToGain(-cell_) * makeupGain_;
    const float drive = 1.9f + driveScale_ * std::min(cell_ / 10.0f, 1.5f);
    left = lpOut_[0].process(shaper_[0].process(left * gain, drive, bias_)) *
           outputGain_;
    right = lpOut_[1].process(shaper_[1].process(right * gain, drive, bias_)) *
            outputGain_;
    return cell_;
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].reset();
      shaper_[ch].reset();
    }
    env_ = 0.0f;
    cell_ = 0.0f;
  }

private:
  // Photocell response in dB of reduction, with program-dependent time
  // constants.
  void updateCell(float target) noexcept {
    const float heavy = std::min(cell_ / 12.0f, 1.0f);
    if (target > cell_) {
      const float tau = 10.0f + 50.0f * heavy;
      const float a = timeCoeff(tau, sr_);
      cell_ = target + a * (cell_ - target);
    } else {
      const float heavyRel = std::min(cell_ / 10.0f, 1.0f);
      const float tau = 60.0f + (releaseSlowMs_ - 60.0f) * heavyRel;
      const float a = timeCoeff(tau, sr_);
      cell_ = target + a * (cell_ - target);
    }
    if (cell_ < 1.0e-6f) {
      cell_ = 0.0f;
    }
  }

  float sr_ = 48000.0f;
  Variant variant_ = Variant::LA2A;
  Controls controls_;
  float thresholdDb_ = -20.0f;
  float ratio_ = 3.0f;
  float kneeDb_ = 8.0f;
  float outputDb_ = 0.0f;
  float makeupGain_ = 1.0f;
  float outputGain_ = 1.0f;
  float releaseSlowMs_ = 1200.0f;
  float outLpHz_ = 15000.0f;
  float bias_ = 0.04f;
  float driveScale_ = 0.1f;
  float detectAttack_ = 0.0f;
  float detectRelease_ = 0.0f;
  float env_ = 0.0f;
  float cell_ = 0.0f;
  OnePoleLP lpOut_[2];
  AsymShaper shaper_[2];
};

} // namespace comp
