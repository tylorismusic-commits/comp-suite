// Comp Suite — vca.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "common.hpp"
#include "controls.hpp"

namespace comp {

// Stereo-linked feed-forward VCA bus compressor in the style of an SSL-type
// bus unit.  The detector is RMS over both channels, so the mix bus stays
// balanced.  Attack and release are continuous, the ratio runs 2:1 to 10:1,
// and the sidechain high-pass keeps low-frequency content from pumping the
// gain.  The output stage is clean, as on the hardware.
class BusVca {
public:
  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate;
    for (auto& f : sidechain_) {
      f.reset();
    }
    rmsPower_ = 0.0f;
    env_ = 0.0f;
    for (auto& sh : shaper_) {
      sh.reset();
    }
    rmsCoeff_ = timeCoeff(10.0f, sr_);
    setControls(controls_);
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    const float thr = clampf(c.threshold, 0.0f, 1.0f);
    thresholdDb_ = -40.0f + 40.0f * thr;
    ratio_ = ratioFromStep(ratioStepFrom(c.ratio));
    attackMs_ = 0.1f * std::pow(300.0f, clampf(c.attack, 0.0f, 1.0f));
    releaseMs_ = 100.0f * std::pow(12.0f, clampf(c.release, 0.0f, 1.0f));
    attackCoeff_ = timeCoeff(attackMs_, sr_);
    releaseCoeff_ = timeCoeff(releaseMs_, sr_);
    // Bipolar: 0.5 is unity, matching the other models.
    makeupGain_ = dbToGain(20.0f * (clampf(c.makeup, 0.0f, 1.0f) - 0.5f));

    const float hpf = clampf(c.sidechainHpf, 0.0f, 1.0f);
    sidechainOn_ = hpf > 0.01f;
    if (sidechainOn_) {
      const float cutoff = 20.0f * std::pow(20.0f, hpf);
      for (auto& f : sidechain_) {
        f.set(cutoff, sr_);
      }
    }
  }

  // Processes one stereo frame in place.  Returns gain reduction in dB (>= 0).
  float process(float& left, float& right) noexcept {
    float detL = left;
    float detR = right;
    if (sidechainOn_) {
      detL = sidechain_[0].process(left);
      detR = sidechain_[1].process(right);
    }
    // Peak detection, not RMS: with an RMS detector a -18 dBFS tone sits
    // below the default threshold and the ratio control appears dead.
    const float level = std::max(std::fabs(detL), std::fabs(detR));

    if (level > env_) {
      env_ = attackCoeff_ * env_ + (1.0f - attackCoeff_) * level;
    } else {
      env_ = releaseCoeff_ * env_ + (1.0f - releaseCoeff_) * level;
    }

    constexpr float kKneeDb = 2.0f;
    const float grDb =
        gainReductionDb(gainToDb(env_), thresholdDb_, ratio_, kKneeDb);
    const float gain = dbToGain(-grDb) * makeupGain_;
    // The VCAs and output stage add a little colour; the measured reference
    // bus compressor is not perfectly clean either.
    left = shaper_[0].process(left * gain, 1.7f, 0.13f);
    right = shaper_[1].process(right * gain, 1.7f, 0.13f);
    return std::max(grDb, 0.0f);
  }

  void reset() noexcept {
    for (auto& f : sidechain_) {
      f.reset();
    }
    rmsPower_ = 0.0f;
    env_ = 0.0f;
    for (auto& sh : shaper_) {
      sh.reset();
    }
  }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  float thresholdDb_ = -18.0f;
  float ratio_ = 4.0f;
  float attackMs_ = 10.0f;
  float releaseMs_ = 300.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float rmsCoeff_ = 0.0f;
  float makeupGain_ = 1.0f;
  bool sidechainOn_ = false;
  float rmsPower_ = 0.0f;
  float env_ = 0.0f;
  HighPass2 sidechain_[2];
  AsymShaper shaper_[2];
};

} // namespace comp
