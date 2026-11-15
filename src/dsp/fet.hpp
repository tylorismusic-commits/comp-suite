// Comp Suite — fet.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "common.hpp"
#include "controls.hpp"

namespace comp {

// FET peak limiter modelled on the 1176 family.  The input transformer and
// FET attenuator add asymmetric even and odd harmonics that grow with gain
// reduction.  The "all buttons" setting runs the ratio at 20:1 with the extra
// colour that mode adds on the hardware.
//
// Both revisions have the "all buttons" mode.  On Blue it is extreme: the
// ratio pushes to 20:1, transients push it further, the attack tightens and
// the FET stage is driven harder.  On Black the same mode engages but stays
// more controlled, matching the Rev E's more linear response.
//
// Blue (Rev A "bluestripe") — more distortion and even-order colour from the
// input and FET stages.
// Black (Rev E "blackface") — more linear compression response (softer knee),
// slower release once reduction is deep, and a cleaner, slightly darker
// output.
//
// Distortion targets were measured against a stillwell-derived reference
// implementation (Tukan's NC76, BSD-3): at 100 Hz and -12 dBFS it produces
// roughly -24 dB H2 and -31 dB THD.  That is far dirtier than a textbook
// compressor, and matching it is a large part of the 1176 character.
class FetLimiter {
public:
  enum class Variant { Blue, Black };

  void prepare(float sampleRate, Variant variant) noexcept {
    sr_ = sampleRate;
    variant_ = variant;
    const bool blue = variant == Variant::Blue;
    inputBias_ = blue ? 0.26f : 0.16f;
    fetBias_ = 0.0f;
    baseDrive_ = blue ? 1.30f : 1.10f;
    inputDriveScale_ = blue ? 1.60f : 1.00f;
    const float outLpHz = blue ? 16500.0f : 14000.0f;
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].set(outLpHz, sr_);
      inShaper_[ch].reset();
      fetShaper_[ch].reset();
      lpOut_[ch].reset();
    }
    // Smooth the applied gain a little: without it the gain can jump from
    // sample to sample on fast transients, which modulates the waveform and
    // sounds like a slap on kicks.
    gainSmooth_.setTime(0.7f, sr_);
    gainSmooth_.reset(1.0f);
    inputSmooth_.setTime(25.0f, sr_);
    inputSmooth_.reset(dbToGain(-80.0f));
    driveSmooth_.setTime(25.0f, sr_);
    driveSmooth_.reset(1.3f);
    for (int ch = 0; ch < 2; ++ch) {
      dcLp_[ch].set(18.0f, sr_);
      dcLp_[ch].reset();
    }
    env_ = 0.0f;
    grPrev_ = 0.0f;
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    const float input = clampf(c.input, 0.0f, 1.0f);
    // Input 0 is silence, as on the hardware: the knob is an input
    // attenuator, so you bring the signal in with it.
    const float inputDb = -80.0f + 103.0f * std::sqrt(input);
    inputSmooth_.setTarget(dbToGain(inputDb));
    driveSmooth_.setTarget(baseDrive_ + inputDriveScale_ * input);

    const int ratioStep = ratioStepFrom(c.ratio);
    ratio_ = ratioFromStep(ratioStep);
    ratioStep_ = ratioStep;
    allButtons_ = ratioStep >= 8;

    // 1176 attack runs 1..7 (about 800 us down to 20 us); release runs 1..7
    // (about 50 ms up to 1.1 s).  Both are log-spaced between their ends.
    const float attackStep =
        1.0f + std::round(clampf(c.attack, 0.0f, 1.0f) * 6.0f);
    const float releaseStep =
        1.0f + std::round(clampf(c.release, 0.0f, 1.0f) * 6.0f);
    attackMs_ = 0.8f * std::pow(0.025f, (attackStep - 1.0f) / 6.0f);
    releaseMs_ = 50.0f * std::pow(22.0f, (releaseStep - 1.0f) / 6.0f);
    if (allButtons_) {
      attackMs_ *= variant_ == Variant::Blue ? 0.5f : 0.7f;
    }
    attackCoeff_ = timeCoeff(attackMs_, sr_);
    releaseCoeff_ = timeCoeff(releaseMs_, sr_);
    releaseDeepCoeff_ = timeCoeff(releaseMs_ * 1.35f, sr_);

    outputGain_ = dbToGain(-12.0f + 24.0f * clampf(c.output, 0.0f, 1.0f));
  }

  // Processes one stereo frame in place.  Returns gain reduction in dB (>= 0).
  float process(float& left, float& right) noexcept {
    const float inputGain = inputSmooth_.next();
    const float drive = driveSmooth_.next();
    const float inL = left * inputGain;
    const float inR = right * inputGain;

    // The 1176 is a feedback compressor: the side-chain taps the output of
    // the gain cell, not the input.  The detector therefore sees the already
    // attenuated signal, which is where the unit's soft knee and its recovery
    // shape come from.  The one-sample delay is the implicit loop.
    const float detL =
        topology_ == Topology::Feedback ? kFeedbackLoopGain * lastOutL_ : inL;
    const float detR =
        topology_ == Topology::Feedback ? kFeedbackLoopGain * lastOutR_ : inR;
    const float peak = std::max(std::fabs(detL), std::fabs(detR));
    const bool blackDeep = variant_ == Variant::Black && grPrev_ > 6.0f;
    if (peak > env_) {
      env_ = attackCoeff_ * env_ + (1.0f - attackCoeff_) * peak;
    } else {
      const float rc = blackDeep ? releaseDeepCoeff_ : releaseCoeff_;
      env_ = rc * env_ + (1.0f - rc) * peak;
    }

    constexpr float kThresholdDb = -18.0f;
    const float kneeDb = variant_ == Variant::Black ? 2.5f : 1.0f;
    // Program dependence: harder transients push the effective ratio up.
    const float overDb = gainToDb(env_) - kThresholdDb;
    const float push = clampf((overDb - 6.0f) / 12.0f, 0.0f, 1.0f);
    // Program dependence belongs to the all-buttons mode.  Applying it to
    // every ratio smeared 4:1, 8:1, 12:1 and 20:1 into each other, so the
    // ratio buttons barely changed the sound.
    const float allPush = variant_ == Variant::Blue ? 8.0f : 5.0f;
    const float effRatio = allButtons_ ? ratio_ + allPush * push : ratio_;
    const float grDb =
        gainReductionDb(gainToDb(env_), kThresholdDb, effRatio, kneeDb);
    grPrev_ = grDb;
    const float targetGain = dbToGain(-grDb);
    const float gain = gainSmooth_.next(targetGain);

    // The FET attenuator distorts more as it works harder.
    const float grNorm = clampf(grDb / 20.0f, 0.0f, 1.5f);
    // Higher ratios drive the FET stage harder, so the buttons change
    // character as well as amount, as on the hardware.
    const float allDrive = variant_ == Variant::Blue ? 1.5f : 0.7f;
    const float ratioDrive = 0.22f * static_cast<float>(ratioStep_);
    const float fetDrive =
        2.2f + 3.2f * grNorm + ratioDrive + (allButtons_ ? allDrive : 0.0f);

    lastOutL_ = processChannel(inL * gain, 0, fetDrive, drive);
    lastOutR_ = processChannel(inR * gain, 1, fetDrive, drive);
    left = lastOutL_ * outputGain_;
    right = lastOutR_ * outputGain_;
    return std::max(grDb, 0.0f);
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      lpOut_[ch].reset();
      dcLp_[ch].reset();
      inShaper_[ch].reset();
      fetShaper_[ch].reset();
    }
    env_ = 0.0f;
    grPrev_ = 0.0f;
    lastOutL_ = 0.0f;
    lastOutR_ = 0.0f;
    gainSmooth_.reset(1.0f);
  }

private:
  // Input transformer, then FET attenuator, then output transformer.
  float processChannel(float x, int ch, float fetDrive, float inputDrive) noexcept {
    const float y1 = inShaper_[ch].process(x, inputDrive, inputBias_);
    const float y2 = fetShaper_[ch].process(y1, fetDrive, fetBias_);
    // Remove the DC the asymmetric stages create; a kick would otherwise
    // offset the whole stage and thump.
    return lpOut_[ch].process(y2 - dcLp_[ch].process(y2));
  }

  float sr_ = 48000.0f;
  Variant variant_ = Variant::Blue;
  Controls controls_;

  float inputBias_ = 0.0f;
  float fetBias_ = 0.0f;
  float baseDrive_ = 2.0f;
  float inputDriveScale_ = 3.0f;
  float ratio_ = 4.0f;
  bool allButtons_ = false;
  int ratioStep_ = 0;
  float attackMs_ = 0.1f;
  float releaseMs_ = 100.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float releaseDeepCoeff_ = 0.0f;
  float outputGain_ = 1.0f;
  float env_ = 0.0f;
  float grPrev_ = 0.0f;
  float lastOutL_ = 0.0f;
  float lastOutR_ = 0.0f;
  Topology topology_ = Topology::Feedback;
  // A feedback detector sees the attenuated output, so at the same knob
  // position it would reduce far less than a feed-forward one.  This restores
  // the operating point without changing what the ratio buttons mean.
  static constexpr float kFeedbackLoopGain = 4.0f;
  OnePoleLP lpOut_[2];
  OnePoleLP dcLp_[2];
  Smoothed inputSmooth_;
  Smoothed driveSmooth_;
  Smoothed gainSmooth_;
  AsymShaper inShaper_[2];
  AsymShaper fetShaper_[2];
};

} // namespace comp
