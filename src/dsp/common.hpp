// Comp Suite — common.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace comp {

// Where a model's detector taps the signal.
//
// Feedback models (1176, 2A/3A) put the gain cell inside the detector loop,
// so the detector sees the already-compressed output.  That is what gives them
// their slow knee and thick recovery; it is not a stylistic choice.  The VCA
// model is genuinely feed-forward, as the bus compressor it follows is.
enum class Topology { FeedForward, Feedback };

// Per-sample external side-chain input.  `active` is false when the host has
// not routed anything to the side-chain ports, in which case models detect
// from their own input.
struct SidechainFrame {
  float left = 0.0f;
  float right = 0.0f;
  bool active = false;
};

constexpr float kPi = 3.14159265358979323846f;

inline float clampf(float x, float lo, float hi) noexcept {
  return std::min(std::max(x, lo), hi);
}

inline float dbToGain(float db) noexcept {
  return std::pow(10.0f, db * 0.05f);
}

inline float gainToDb(float gain) noexcept {
  return 20.0f * std::log10(std::max(gain, 1.0e-12f));
}

// One-pole smoothing coefficient for a time constant in milliseconds.
inline float timeCoeff(float ms, float sampleRate) noexcept {
  const float samples = std::max(1.0f, ms * 0.001f * sampleRate);
  return std::exp(-1.0f / samples);
}

// Per-sample parameter smoother, used to avoid zipper noise.
class Smoothed {
public:
  void setTime(float ms, float sampleRate) noexcept {
    coeff_ = timeCoeff(ms, sampleRate);
  }
  void reset(float value) noexcept { current_ = target_ = value; }
  void snap() noexcept { current_ = target_; }
  void setTarget(float value) noexcept { target_ = value; }
  float current() const noexcept { return current_; }
  float next() noexcept {
    current_ = target_ + coeff_ * (current_ - target_);
    return current_;
  }

  // Advance toward an explicit target, without changing the stored one.
  float next(float target) noexcept {
    current_ = target + coeff_ * (current_ - target);
    return current_;
  }

private:
  float current_ = 0.0f;
  float target_ = 0.0f;
  float coeff_ = 0.0f;
};

// Topology-preserving one-pole low-pass (Zavalishin).
struct OnePoleLP {
  float g = 0.0f;
  float ic = 0.0f;

  void set(float cutoff, float sampleRate) noexcept {
    cutoff = clampf(cutoff, 1.0f, sampleRate * 0.49f);
    const float t = std::tan(kPi * cutoff / sampleRate);
    g = t / (1.0f + t);
  }
  void reset() noexcept { ic = 0.0f; }
  float process(float x) noexcept {
    const float v = (x - ic) * g;
    const float y = v + ic;
    ic = y + v;
    if (std::fabs(ic) < 1.0e-20f) {
      ic = 0.0f;
    }
    return y;
  }
};

// Two cascaded one-poles give a 12 dB/oct high-pass for side-chain filtering.
struct HighPass2 {
  OnePoleLP lp1;
  OnePoleLP lp2;

  void set(float cutoff, float sampleRate) noexcept {
    lp1.set(cutoff, sampleRate);
    lp2.set(cutoff, sampleRate);
  }
  void reset() noexcept {
    lp1.reset();
    lp2.reset();
  }
  float process(float x) noexcept {
    const float low = lp2.process(lp1.process(x));
    return x - low;
  }
};

// Antiderivative-based anti-aliasing for a memoryless waveshaper
// y = f(x), where F is an antiderivative of f.  First-order ADAA.
struct Adaa {
  float x1 = 0.0f;
  double f1 = 0.0;  // antiderivative at x1, kept in double on purpose

  void reset() noexcept {
    x1 = 0.0f;
    f1 = 0.0;
  }

  // y = (F(x) - F(x1)) / (x - x1).  The subtraction is done in double
  // precision: in float32, near a signal peak where dx is tiny, the
  // cancellation error is large enough to produce audible spikes.  When dx is
  // very small the midpoint value f((x+x1)/2) is used instead; the error is
  // O(dx^2), which is far below the noise floor.
  template <typename Shaper, typename Antider>
  float process(float x, Shaper f, Antider F) noexcept {
    const double dx = static_cast<double>(x) - static_cast<double>(x1);
    const double F0 = F(static_cast<double>(x));
    const double y = std::fabs(dx) < 1.0e-4 ? f(0.5 * (x + x1)) : (F0 - f1) / dx;
    f1 = F0;
    x1 = x;
    return static_cast<float>(y);
  }
};

// Numerically stable log(cosh(x)), antiderivative of tanh(x).
inline double logCosh(double x) noexcept {
  const double ax = std::fabs(x);
  return ax + std::log1p(std::exp(-2.0 * ax)) - 0.6931471805599453;
}

// Asymmetric soft clipper, normalised to unity slope at the origin, with ADAA.
// `k` sets drive, `bias` sets the asymmetry (even harmonics).
class AsymShaper {
public:
  void reset() noexcept {
    adaa_.reset();
    primed_ = false;
  }

  float process(float x, float k, float bias) noexcept {
    const double kd = k;
    const double bd = bias;
    const double tb = std::tanh(bd);
    const float norm = static_cast<float>(kd * (1.0 - tb * tb));
    auto f = [kd, bd, tb](double v) { return std::tanh(kd * v + bd) - tb; };
    auto F = [kd, bd, tb](double v) { return logCosh(kd * v + bd) / kd - tb * v; };

    // ADAA stores F(x1) from the previous sample, computed with the drive in
    // force then.  When the drive changes, that stored value is inconsistent
    // with the new F, and near a peak (tiny dx) the mismatch is amplified into
    // a burst.  Recompute the stored antiderivative whenever the drive moves.
    if (k != lastK_ || bias != lastBias_) {
      if (primed_) {
        adaa_.f1 = F(static_cast<double>(adaa_.x1));
      }
      lastK_ = k;
      lastBias_ = bias;
    }
    // Prime the antiderivative state on the first sample.  Starting from 0
    // instead would make the first output a huge spike, because F(0) is not 0
    // for a biased shaper.
    if (!primed_) {
      primed_ = true;
      adaa_.x1 = x;
      adaa_.f1 = F(static_cast<double>(x));
      return static_cast<float>(f(static_cast<double>(x)) / norm);
    }
    return adaa_.process(x, f, F) / norm;
  }

private:
  Adaa adaa_;
  bool primed_ = false;
  float lastK_ = -1.0f;
  float lastBias_ = 0.0f;
};

// One ratio ladder shared by every model with a selectable ratio, so the same
// control position means the same ratio when switching models.  Step 8 is the
// 76 "all buttons" position; other models treat it as 20:1.
inline int ratioStepFrom(float value) noexcept {
  return std::min(std::max(static_cast<int>(std::lround(value)), 0), 8);
}

inline float ratioFromStep(int step) noexcept {
  static const float kRatios[9] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f,
                                   8.0f, 12.0f, 20.0f, 20.0f};
  return kRatios[std::min(std::max(step, 0), 8)];
}

// Static gain computer with an explicit slope (dB of reduction per dB over
// threshold).  Feed-forward models pass 1 - 1/ratio; feedback models need
// ratio - 1, because the loop multiplies the static curve by 1/(1 + slope).
inline float gainReductionDbSlope(float levelDb, float thresholdDb,
                                  float slope, float kneeDb) noexcept {
  const float over = levelDb - thresholdDb;
  if (kneeDb <= 0.0f || over > 0.5f * kneeDb) {
    return std::max(slope * over, 0.0f);
  }
  if (over <= -0.5f * kneeDb) {
    return 0.0f;
  }
  const float t = over + 0.5f * kneeDb;
  return std::max(slope * t * t / (2.0f * kneeDb), 0.0f);
}

// Static gain computer.  Returns gain reduction in dB (>= 0) for a level in
// dB, with a quadratic soft knee centred on the threshold.
inline float gainReductionDb(float levelDb, float thresholdDb, float ratio,
                             float kneeDb) noexcept {
  const float slope = 1.0f - 1.0f / std::max(ratio, 1.0f);
  return gainReductionDbSlope(levelDb, thresholdDb, slope, kneeDb);
}

} // namespace comp
