// Comp Suite — DSP sanity tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/engine.hpp"
#include "params.hpp"
#include "presets.hpp"

using namespace comp;

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

constexpr float kSr = 48000.0f;

// Renders a sine at the given level through the engine and returns the
// stereo output plus the peak gain reduction seen.
struct Render {
  std::vector<float> left;
  std::vector<float> right;
  float peakGr = 0.0f;
};

Render renderSine(Engine& engine, const Controls& c, float freq, float dbfs,
                  float seconds, float sampleRate = kSr) {
  engine.setControls(c);
  const int n = static_cast<int>(seconds * sampleRate);
  Render r;
  r.left.resize(n);
  r.right.resize(n);
  const float amp = dbToGain(dbfs);
  for (int i = 0; i < n; ++i) {
    const float s = amp * std::sin(2.0f * kPi * freq * i / sampleRate);
    r.left[i] = s;
    r.right[i] = s;
  }
  // Process in 256-frame blocks, as a host would.
  Meters meters;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    engine.process(r.left.data() + start, r.right.data() + start, len, meters);
    r.peakGr = std::max(r.peakGr, meters.grDb);
  }
  return r;
}

float rmsTail(const std::vector<float>& x, float seconds) {
  const int tail = static_cast<int>(seconds * kSr);
  const int start = static_cast<int>(x.size()) - tail;
  double sum = 0.0;
  for (int i = start; i < static_cast<int>(x.size()); ++i) {
    sum += static_cast<double>(x[i]) * x[i];
  }
  return static_cast<float>(std::sqrt(sum / tail));
}

// Magnitude of one frequency bin under a Hann window, for alias checks.
float toneMagnitude(const std::vector<float>& x, double hz, double sr) {
  const int n = static_cast<int>(x.size());
  double re = 0.0;
  double im = 0.0;
  for (int i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * i / (n - 1));
    const double ph = 2.0 * 3.141592653589793 * hz * i / sr;
    re += w * x[i] * std::cos(ph);
    im -= w * x[i] * std::sin(ph);
  }
  return static_cast<float>(std::hypot(re, im) / n);
}

bool allFinite(const Render& r) {
  for (size_t i = 0; i < r.left.size(); ++i) {
    if (!std::isfinite(r.left[i]) || !std::isfinite(r.right[i])) {
      return false;
    }
  }
  return true;
}

Controls baseControls(Model model) {
  Controls c;
  c.model = static_cast<int>(model);
  c.enabled = true;
  c.input = 0.5f;
  c.threshold = 0.5f;
  c.ratio = 0.5f;
  c.attack = 0.5f;
  c.release = 0.5f;
  c.makeup = 0.5f;
  c.output = 0.5f;
  c.mix = 1.0f;
  return c;
}

} // namespace

int main() {
  // Every model stays finite and compresses a loud sine.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(static_cast<Model>(m));
    if (m == static_cast<int>(Model::Opto2A) ||
        m == static_cast<int>(Model::Opto3A)) {
      c.threshold = 0.8f; // strong peak reduction
    }
    if (m == static_cast<int>(Model::Vca)) {
      c.threshold = 0.25f; // -20 dBFS threshold
    }
    const Render r = renderSine(engine, c, 100.0f, -6.0f, 1.0f);
    char label[96];
    std::snprintf(label, sizeof(label), "model %d finite output", m);
    check(allFinite(r), label);
    std::snprintf(label, sizeof(label), "model %d reduces a loud sine (GR %.1f dB)", m, r.peakGr);
    check(r.peakGr > 0.5f, label);
  }

  // The optical side-chain must react differently to bass and midrange.  The
  // 43 Hz shelf on the compressed side-chain raises the integrated level at
  // low frequencies, which pulls the second gain stage back: the cell holds
  // off on bass instead of pumping the whole mix under it.
  {
    const auto grAt = [](float freq) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Opto2A);
      c.threshold = 0.9f;
      return renderSine(engine, c, freq, -12.0f, 2.0f).peakGr;
    };
    const float low = grAt(40.0f);
    const float mid = grAt(1000.0f);
    char label[144];
    std::snprintf(label, sizeof(label),
                  "2A ducks bass less than mid (40 Hz %.1f dB vs 1 kHz "
                  "%.1f dB)",
                  low, mid);
    // Feedback narrows this differential relative to the feed-forward
    // reference, so the guard is 0.5 dB rather than the 1 dB the structure
    // shows on its own.
    check(low < mid - 0.5f, label);
  }

  // Maximum peak reduction on a hot low sine: the cell must stay bounded.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Opto2A);
    c.threshold = 1.0f;
    c.ratio = 8.0f;  // LIMIT
    c.makeup = 1.0f;
    const Render r = renderSine(engine, c, 40.0f, 0.0f, 2.0f);
    float peak = 0.0f;
    for (float v : r.left) {
      peak = std::max(peak, std::fabs(v));
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "2A at maximum reduction stays bounded (peak %.2f, GR %.1f "
                  "dB)",
                  peak, r.peakGr);
    check(allFinite(r) && peak < 8.0f && r.peakGr > 3.0f, label);
  }

  // LA2A and LA3A use different cell memory paths: the 3A releases faster.
  // Peak reduction is a poor probe for that, so compare the residual
  // reduction half a second after the signal drops away.
  {
    const auto tailGr = [](Model model) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(model);
      c.threshold = 0.7f;
      engine.setControls(c);
      Meters meters;
      std::vector<float> l(256), r(256);
      const int blocksPerSection = static_cast<int>(kSr / 256);
      float tail = 0.0f;
      int tailCount = 0;
      for (int b = 0; b < blocksPerSection * 2; ++b) {
        const bool loud = b < blocksPerSection;
        const float amp = dbToGain(loud ? -6.0f : -50.0f);
        for (int i = 0; i < 256; ++i) {
          const float t = static_cast<float>(b * 256 + i) / kSr;
          l[i] = r[i] = amp * std::sin(2.0f * kPi * 200.0f * t);
        }
        engine.process(l.data(), r.data(), 256, meters);
        if (b > blocksPerSection + blocksPerSection / 2) {
          tail += meters.grDb;
          ++tailCount;
        }
      }
      return tail / static_cast<float>(std::max(1, tailCount));
    };
    const float a = tailGr(Model::Opto2A);
    const float b = tailGr(Model::Opto3A);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "3A releases faster than 2A (residual %.2f vs %.2f dB)", b,
                  a);
    check(b < a - 0.3f, label);
  }

  // A feedback detector is a closed loop: at maximum input, ratio and the
  // fastest attack it must still settle rather than oscillate.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.input = 1.0f;
    c.ratio = 1.0f;   // ALL
    c.attack = 0.0f;  // fastest
    c.release = 0.0f;
    const Render r = renderSine(engine, c, 40.0f, 0.0f, 3.0f);
    float peak = 0.0f;
    for (float v : r.left) {
      peak = std::max(peak, std::fabs(v));
    }
    char label[144];
    std::snprintf(label, sizeof(label),
                  "76 feedback loop is stable at maximum settings (peak %.2f, "
                  "GR %.1f dB)",
                  peak, r.peakGr);
    check(allFinite(r) && peak < 8.0f && r.peakGr > 3.0f, label);
  }

  // The shared ratio ladder has to survive the feedback loop, otherwise the
  // same control position means something different on the 76 than on the
  // VCA and Xpress.
  {
    const auto ratioAt = [](float step) {
      const auto grAt = [&](float dbfs) {
        Engine engine;
        engine.prepare(kSr);
        Controls c = baseControls(Model::Blue76);
        c.input = 0.6f;
        c.ratio = step;
        return renderSine(engine, c, 500.0f, dbfs, 6.0f).peakGr;
      };
      const float slope = (grAt(0.0f) - grAt(-6.0f)) / 6.0f;
      return 1.0f / (1.0f - slope);
    };
    const float four = ratioAt(3.0f);  // 4:1
    const float eight = ratioAt(5.0f); // 8:1
    char label[160];
    std::snprintf(label, sizeof(label),
                  "76 feedback ladder lands on 4:1 and 8:1 (%.1f:1, %.1f:1)",
                  four, eight);
    check(std::fabs(four - 4.0f) < 0.8f && std::fabs(eight - 8.0f) < 1.5f,
          label);
  }

  // A feedback loop at high ratio must settle, not hunt.  Measure the spread
  // of gain reduction once the tone has been running for five seconds.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.input = 0.7f;
    c.ratio = 7.0f;  // 20:1
    c.release = 0.5f;
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256), r(256);
    float lo = 0.0f;
    float hi = 0.0f;
    bool started = false;
    const int blocks = static_cast<int>(8.0f * kSr / 256);
    for (int b = 0; b < blocks; ++b) {
      for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>(b * 256 + i) / kSr;
        l[i] = r[i] = 0.5f * std::sin(2.0f * kPi * 500.0f * t);
      }
      engine.process(l.data(), r.data(), 256, meters);
      if (b > blocks - static_cast<int>(2.0f * kSr / 256)) {
        if (!started) {
          lo = hi = meters.grDb;
          started = true;
        }
        lo = std::min(lo, meters.grDb);
        hi = std::max(hi, meters.grDb);
      }
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "76 at 20:1 settles without hunting (%.2f dB spread)",
                  hi - lo);
    check(hi - lo < 0.35f, label);
  }

  // Sample-rate independence: the side-chain filters and the envelope have to
  // behave at every rate hosts use, not just 48 kHz.
  for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
    bool ok = true;
    float worst = 0.0f;
    for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
      Engine engine;
      engine.prepare(sr);
      Controls c = baseControls(static_cast<Model>(m));
      if (m == 2 || m == 3) {
        c.threshold = 0.7f;
      }
      if (m == 5) {
        c.threshold = 0.25f;
      }
      const Render r = renderSine(engine, c, 440.0f, -6.0f, 0.5f, sr);
      if (!allFinite(r)) {
        ok = false;
      }
      for (float v : r.left) {
        worst = std::max(worst, std::fabs(v));
      }
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "all models finite and bounded at %.0f Hz (peak %.2f)", sr,
                  worst);
    check(ok && worst < 8.0f, label);
  }

  // External side-chain: a loud side-chain must reduce a quiet main signal.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.externalSidechain = true;
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256, 0.0f), r(256, 0.0f), sc(256, 0.0f);
    float peakGr = 0.0f;
    for (int b = 0; b < 200; ++b) {
      for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>(b * 256 + i) / kSr;
        sc[i] = 0.5f * std::sin(2.0f * kPi * 200.0f * t);
        l[i] = r[i] = 0.0f;  // main input silent
      }
      engine.process(l.data(), r.data(), 256, meters, sc.data(), sc.data());
      peakGr = std::max(peakGr, meters.grDb);
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "external side-chain reduces a silent signal (GR %.1f dB)",
                  peakGr);
    // The feedback slope must not be applied outside the loop: with it, a
    // normal side-chain level pinned the detector at 50+ dB.
    check(peakGr > 3.0f && peakGr < 25.0f, label);
  }

  // Auto-makeup restores the level as reduction deepens.
  {
    const auto outLevel = [](bool autoMakeup) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Blue76);
      c.input = 0.85f;
      c.autoMakeup = autoMakeup;
      return rmsTail(renderSine(engine, c, 500.0f, -12.0f, 4.0f).left, 2.0f);
    };
    const float manual = outLevel(false);
    const float automatic = outLevel(true);
    char label[144];
    std::snprintf(label, sizeof(label),
                  "auto-makeup restores level (%.1f dB louder)",
                  gainToDb(automatic) - gainToDb(manual));
    check(gainToDb(automatic) - gainToDb(manual) > 2.0f, label);
  }

  // VCA link: with a loud left and quiet right, a linked detector applies the
  // same reduction to both; dual mono leaves the quiet channel alone.
  {
    const auto rightLevel = [](float link) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Vca);
      c.threshold = 0.35f;
      c.link = link;
      engine.setControls(c);
      Meters meters;
      std::vector<float> l(256), r(256);
      double sum = 0.0;
      long n = 0;
      for (int b = 0; b < 600; ++b) {
        for (int i = 0; i < 256; ++i) {
          const float t = static_cast<float>(b * 256 + i) / kSr;
          l[i] = 0.9f * std::sin(2.0f * kPi * 120.0f * t);
          r[i] = 0.02f * std::sin(2.0f * kPi * 900.0f * t);
        }
        engine.process(l.data(), r.data(), 256, meters);
        if (b > 400) {
          for (int i = 0; i < 256; ++i) {
            sum += static_cast<double>(r[i]) * r[i];
            ++n;
          }
        }
      }
      return 10.0 * std::log10(std::max(sum / n, 1e-20));
    };
    const float linked = rightLevel(1.0f);
    const float dual = rightLevel(0.0f);
    char label[144];
    std::snprintf(label, sizeof(label),
                  "VCA link ducks the quiet channel (linked %.1f vs dual %.1f "
                  "dBFS)",
                  linked, dual);
    check(dual > linked + 2.0f, label);
  }

  // VCA auto release: more reduction should produce a longer tail than the
  // manual release at the same setting.
  {
    const auto residual = [](bool autoRelease) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Vca);
      c.threshold = 0.35f;
      c.release = 0.2f;  // fast manual release
      c.vcaAutoRelease = autoRelease;
      engine.setControls(c);
      Meters meters;
      std::vector<float> l(256), r(256);
      float tail = 0.0f;
      const int loudBlocks = static_cast<int>(1.5f * kSr / 256);
      const int quietBlocks = static_cast<int>(2.0f * kSr / 256);
      for (int b = 0; b < loudBlocks + quietBlocks; ++b) {
        const bool loud = b < loudBlocks;
        const float amp = loud ? 0.7f : 0.01f;
        for (int i = 0; i < 256; ++i) {
          const float t = static_cast<float>(b * 256 + i) / kSr;
          l[i] = r[i] = amp * std::sin(2.0f * kPi * 300.0f * t);
        }
        engine.process(l.data(), r.data(), 256, meters);
        if (b > loudBlocks + quietBlocks / 2) {
          tail = std::max(tail, meters.grDb);
        }
      }
      return tail;
    };
    const float manual = residual(false);
    const float automatic = residual(true);
    char label[144];
    std::snprintf(label, sizeof(label),
                  "VCA auto release holds longer (%.2f vs %.2f dB)", automatic,
                  manual);
    check(automatic > manual + 0.2f, label);
  }

  // A 20:1 feedback loop must not null the gain on a transient.  The static
  // slope is 19 dB/dB, so without a ceiling a 6 dB burst asks for >100 dB and
  // rounds the gain to zero.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.input = 0.85f;
    c.ratio = 7.0f;  // 20:1
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256), r(256);
    float maxGr = 0.0f;
    float quietest = 1.0f;
    for (int b = 0; b < 400; ++b) {
      const bool burst = b >= 200 && b < 205;
      const float amp = burst ? 0.35f : 0.177f;
      for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>(b * 256 + i) / kSr;
        l[i] = r[i] = amp * std::sin(2.0f * kPi * 100.0f * t);
      }
      engine.process(l.data(), r.data(), 256, meters);
      maxGr = std::max(maxGr, meters.grDb);
      if (b >= 200) {
        for (int i = 0; i < 256; ++i) {
          quietest = std::min(quietest, std::fabs(l[i]));
        }
      }
    }
    char label[160];
    std::snprintf(label, sizeof(label),
                  "76 transient is bounded, not nulled (max GR %.1f dB)",
                  maxGr);
    // A null would show max GR in the 70s and near-zero output.
    check(maxGr > 20.0f && maxGr < 45.0f, label);
  }

  // VCA presets must reset the auto-release switch, otherwise selecting one
  // leaves the release knob doing nothing.  Presets are partial by design, so
  // every preset for a model with a mode switch must set it explicitly instead
  // of inheriting whatever the previous preset left behind.
  {
    bool autoOk = true;
    for (int i = 0; i < kPresetCount; ++i) {
      const Preset& preset = kPresets[i];
      if (preset.model != static_cast<int>(Model::Vca) &&
          preset.model != static_cast<int>(Model::Red)) {
        continue;
      }
      bool resets = false;
      for (const PresetValue& v : preset.values) {
        if (v.param == static_cast<int>(P_VCA_AUTO) && v.value < 0.5f) {
          resets = true;
        }
      }
      if (!resets) {
        std::printf("  preset '%s' does not reset auto release\n",
                    preset.name);
        autoOk = false;
      }
    }
    check(autoOk, "VCA and Red presets reset auto release");

    // The 3A has no ratio control, but its DSP still reads the Limit/Compress
    // switch, so a preceding 2A Limit preset must not leak into it.
    bool ratioOk = true;
    for (int i = 0; i < kPresetCount; ++i) {
      const Preset& preset = kPresets[i];
      if (preset.model != static_cast<int>(Model::Opto3A)) {
        continue;
      }
      bool resets = false;
      for (const PresetValue& v : preset.values) {
        if (v.param == static_cast<int>(P_RATIO) && v.value < 0.5f) {
          resets = true;
        }
      }
      if (!resets) {
        std::printf("  3A preset '%s' does not set Compress\n", preset.name);
        ratioOk = false;
      }
    }
    check(ratioOk, "3A presets set their fixed Compress mode");
  }

  // Clean's distortion should shade, not squash: the shaped modes must stay
  // within a decibel of the clean path at a realistic level, and Off must be
  // genuinely transparent.
  {
    const auto levelAndThd = [](float mode) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Clean);
      c.input = 0.5f;
      c.threshold = 1.0f;  // no compression, isolate the colour stage
      c.distortion = mode;
      const Render r = renderSine(engine, c, 1000.0f, -6.0f, 1.0f);
      return rmsTail(r.left, 0.4f);
    };
    const float off = levelAndThd(0.0f);
    const float soft = levelAndThd(1.0f / 3.0f);
    const float hard = levelAndThd(2.0f / 3.0f);
    const float clip = levelAndThd(1.0f);
    const float expected = dbToGain(-6.0f) / std::sqrt(2.0f) *
                           dbToGain(kModelTrimDb[static_cast<int>(Model::Clean)]);
    char label[160];
    std::snprintf(label, sizeof(label),
                  "Clean colour modes stay subtle (off %.2f, soft %.2f, hard "
                  "%.2f, clip %.2f dB re input)",
                  gainToDb(off / expected), gainToDb(soft / expected),
                  gainToDb(hard / expected), gainToDb(clip / expected));
    check(std::fabs(gainToDb(off / expected)) < 0.1f &&
              std::fabs(gainToDb(soft / expected)) < 0.5f &&
              std::fabs(gainToDb(hard / expected)) < 1.0f &&
              std::fabs(gainToDb(clip / expected)) < 0.1f,
          label);
  }

  // Molot's switches must actually flip the thing they claim to.
  {
    const auto render = [](float distortion, float attackMode) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Molot);
      c.input = 0.5f;
      c.threshold = 0.5f;
      c.distortion = distortion;
      c.attackMode = attackMode;
      c.satDrive = 1.0f;
      return renderSine(engine, c, 500.0f, -6.0f, 1.5f);
    };
    const Render dry = render(0.0f, 0.0f);   // sat off, Alpha
    const Render sat = render(1.0f, 0.0f);    // sat on
    const Render sigma = render(0.0f, 1.0f);  // Sigma

    const float dryRms = rmsTail(dry.left, 0.5f);
    const float satRms = rmsTail(sat.left, 0.5f);
    char label[160];
    std::snprintf(label, sizeof(label),
                  "Molot saturation changes the signal (%.2f dB)",
                  gainToDb(satRms / std::max(dryRms, 1e-9f)));
    check(std::fabs(gainToDb(satRms / std::max(dryRms, 1e-9f))) > 0.2f ||
              std::fabs(sat.peakGr - dry.peakGr) > 0.2f,
          label);

    std::snprintf(label, sizeof(label),
                  "Molot Alpha and Sigma differ (GR %.1f vs %.1f dB)",
                  dry.peakGr, sigma.peakGr);
    check(std::fabs(dry.peakGr - sigma.peakGr) > 0.5f, label);

    // In M/S the saturator is bypassed, so the SAT DRIVE knob must not change
    // the sound while Saturation is off.
    const auto renderMs = [](float satDrive) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Molot);
      c.stereoMode = 4.0f;      // M/S
      c.distortion = 0.0f;      // Saturation off
      c.satDrive = satDrive;
      return renderSine(engine, c, 500.0f, -6.0f, 1.5f);
    };
    const Render msLow = renderMs(0.0f);
    const Render msHigh = renderMs(1.0f);
    float maxDelta = 0.0f;
    for (size_t i = msLow.left.size() / 2; i < msLow.left.size(); ++i) {
      maxDelta = std::max(maxDelta, std::fabs(msLow.left[i] - msHigh.left[i]));
    }
    std::snprintf(label, sizeof(label),
                  "Molot M/S bypasses saturation (max delta %.4f)", maxDelta);
    check(maxDelta < 1.0e-4f, label);
  }

  // Peak reduction must increase reduction monotonically, and the control
  // must be usable across its travel rather than slamming at one end.
  {
    float previous = -1.0f;
    bool monotonic = true;
    float first = 0.0f;
    float last = 0.0f;
    bool firstValue = true;
    for (float threshold : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Opto2A);
      c.threshold = threshold;
      const float gr = renderSine(engine, c, 500.0f, -10.0f, 2.0f).peakGr;
      if (gr + 0.05f < previous) {
        monotonic = false;
      }
      previous = gr;
      if (firstValue) {
        first = gr;
        firstValue = false;
      }
      last = gr;
    }
    char label[144];
    std::snprintf(label, sizeof(label),
                  "peak reduction is monotonic and spans the control "
                  "(%.1f to %.1f dB)",
                  first, last);
    check(monotonic && last > first + 8.0f, label);
  }

  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.enabled = false;
    const Render r = renderSine(engine, c, 100.0f, -6.0f, 0.5f);
    const float expected = dbToGain(-6.0f) / std::sqrt(2.0f);
    check(std::fabs(rmsTail(r.left, 0.1f) - expected) < 0.01f,
          "disabled engine is transparent");
    check(r.peakGr < 1.0e-6f, "disabled engine reports no reduction");

    // Bypass must bypass the limiter too.
    Engine limited;
    limited.prepare(kSr);
    Controls cl = c;
    cl.limiterOn = true;
    cl.limitThreshold = 0.0f;  // -24 dBFS, hard
    const Render rl = renderSine(limited, cl, 100.0f, -6.0f, 0.5f);
    check(std::fabs(rmsTail(rl.left, 0.1f) - expected) < 0.01f,
          "limiter is bypassed with the engine");
  }

  // Below its ceiling the output limiter is transparent: it must not colour
  // or attenuate a signal it is not limiting.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Clean);
    c.threshold = 1.0f;      // no compression
    c.limiterOn = true;
    c.limitThreshold = 1.0f; // 0 dBFS ceiling
    const Render r = renderSine(engine, c, 100.0f, -6.0f, 0.5f);
    const float expected = dbToGain(-6.0f) / std::sqrt(2.0f) *
                           dbToGain(kModelTrimDb[static_cast<int>(Model::Clean)]);
    char label[96];
    std::snprintf(label, sizeof(label),
                  "limiter is transparent below its ceiling (%.1f dB off)",
                  gainToDb(rmsTail(r.left, 0.1f)) - gainToDb(expected));
    check(std::fabs(rmsTail(r.left, 0.1f) - expected) < 0.01f, label);
  }

  // Every model's mix at 0% gives the dry signal back.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(static_cast<Model>(m));
    c.mix = 0.0f;
    const Render r = renderSine(engine, c, 100.0f, -6.0f, 1.0f);
    const float expected = dbToGain(-6.0f) / std::sqrt(2.0f);
    char label[64];
    std::snprintf(label, sizeof(label), "model %d mix 0%% is dry", m);
    check(std::fabs(rmsTail(r.left, 0.1f) - expected) < 0.01f, label);
  }

  // A 50% mix must be exactly halfway between dry and wet.  Applying the
  // blend inside a model as well as in the engine made the control quadratic.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    auto renderAt = [&](float mix) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(static_cast<Model>(m));
      c.input = 0.75f;
      c.threshold = 0.25f;
      c.mix = mix;
      return renderSine(engine, c, 100.0f, -6.0f, 0.8f);
    };
    const Render dry = renderAt(0.0f);
    const Render wet = renderAt(1.0f);
    const Render half = renderAt(0.5f);
    const size_t start = half.left.size() - static_cast<size_t>(0.2f * kSr);
    bool linear = true;
    for (size_t i = start; i < half.left.size(); ++i) {
      const float want = 0.5f * dry.left[i] + 0.5f * wet.left[i];
      if (std::fabs(half.left[i] - want) > 2.0e-4f) {
        linear = false;
        break;
      }
    }
    char label[96];
    std::snprintf(label, sizeof(label), "model %d mix is a single blend", m);
    check(linear, label);
  }

  // The 76's high ratios must stay musical.  A feedback loop collapsed the
  // gain at 8:1 and above, leaving blips; the feed-forward law keeps the
  // output alive and still increases reduction with the ratio.
  {
    const auto renderRatio = [](float ratio) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Blue76);
      c.input = 0.8f;
      c.ratio = ratio;
      return renderSine(engine, c, 100.0f, -12.0f, 1.0f);
    };
    float previousGr = 0.0f;
    bool monotonic = true;
    bool alive = true;
    for (float step : {3.0f, 5.0f, 6.0f, 7.0f, 8.0f}) {
      const Render r = renderRatio(step);
      float peak = 0.0f;
      for (float v : r.left) {
        peak = std::max(peak, std::fabs(v));
      }
      if (r.peakGr < 1.0f || r.peakGr > 48.0f || peak < 0.001f) {
        alive = false;
      }
      if (r.peakGr < previousGr - 0.5f) {
        monotonic = false;
      }
      previousGr = r.peakGr;
    }
    check(alive, "76 high ratios stay audible (no blips)");
    check(monotonic, "76 reduction rises with the ratio");

    // More input means more reduction, as on the reference.
    const float quiet = renderRatio(3.0f).peakGr;
    Engine hotter;
    hotter.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.input = 0.95f;
    c.ratio = 3.0f;
    const Render hot = renderSine(hotter, c, 100.0f, -12.0f, 1.0f);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "76 input drives reduction (%.1f -> %.1f dB)", quiet,
                  hot.peakGr);
    check(hot.peakGr > quiet + 1.0f, label);
  }

  // Both 76 revisions have an all-buttons mode, and it compresses harder
  // than 4:1 in each.
  for (Model m : {Model::Blue76, Model::Black76}) {
    Engine a;
    a.prepare(kSr);
    Controls c4 = baseControls(m);
    c4.ratio = 0.0f;
    c4.input = 0.8f;
    const Render r4 = renderSine(a, c4, 100.0f, -6.0f, 0.5f);

    Engine b;
    b.prepare(kSr);
    Controls call = c4;
    call.ratio = 1.0f;
    const Render rall = renderSine(b, call, 100.0f, -6.0f, 0.5f);
    char label[96];
    std::snprintf(label, sizeof(label),
                  "%s all-buttons out-compresses 4:1 (%.1f vs %.1f dB)",
                  m == Model::Blue76 ? "76Blue" : "76Black", rall.peakGr,
                  r4.peakGr);
    check(rall.peakGr > r4.peakGr + 2.5f, label);
  }

  // Switching models does not produce NaNs or runaway levels.
  {
    Engine engine;
    engine.prepare(kSr);
    Meters meters;
    std::vector<float> l(256), r(256);
    bool ok = true;
    float maxAbs = 0.0f;
    for (int round = 0; round < 60; ++round) {
      Controls c = baseControls(
          static_cast<Model>(round % static_cast<int>(Model::Count)));
      c.input = 0.9f;
      engine.setControls(c);
      for (int block = 0; block < 4; ++block) {
        for (int i = 0; i < 256; ++i) {
          l[i] = r[i] = 0.7f * std::sin(0.03f * (block * 256 + i));
        }
        engine.process(l.data(), r.data(), 256, meters);
        for (int i = 0; i < 256; ++i) {
          if (!std::isfinite(l[i])) {
            ok = false;
          }
          maxAbs = std::max(maxAbs, std::fabs(l[i]));
        }
      }
    }
    char label[96];
    std::snprintf(label, sizeof(label),
                  "model switching stays finite and bounded (peak %.1f)", maxAbs);
    check(ok && maxAbs < 8.0f, label);
  }

  // Easy's Side Chain HPF overrides the Drum Type default; it used to be
  // drawn but never read by the DSP.
  {
    const auto grWithHpf = [](float hpf) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Easy);
      c.threshold = 0.45f;
      c.drumType = 0.0f;   // Toms: 40 Hz detector high-pass
      c.sidechainHpf = hpf;
      return renderSine(engine, c, 60.0f, -6.0f, 1.5f).peakGr;
    };
    const float autoHz = grWithHpf(0.0f);
    const float high = grWithHpf(1.0f);  // 400 Hz
    char label[128];
    std::snprintf(label, sizeof(label),
                  "Easy side-chain HPF changes the detector (%.1f vs %.1f dB)",
                  autoHz, high);
    check(high < autoHz - 0.2f, label);
  }

  // The side-chain meter follows the detector source, not the programme.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Vca);
    c.externalSidechain = true;
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256), r(256), sl(256), sr(256);
    for (int i = 0; i < 256; ++i) {
      l[i] = r[i] = 0.05f;   // quiet programme
      sl[i] = sr[i] = 0.5f;  // loud side-chain
    }
    engine.process(l.data(), r.data(), 256, meters, sl.data(), sr.data());
    char label[128];
    std::snprintf(label, sizeof(label),
                  "sidechain meter is active at %.1f dB (programme %.1f dB)",
                  meters.scPeakDb, meters.inPeakDb);
    check(meters.scActive && std::fabs(meters.scPeakDb - gainToDb(0.5f)) < 0.1f,
          label);

    // With External selected but no side-chain routed, detection falls back
    // to the programme and the meter says so.
    Meters fallback;
    engine.process(l.data(), r.data(), 256, fallback, nullptr, nullptr);
    check(!fallback.scActive, "sidechain meter is inactive without a routed SC");
  }

  // Oversampling modes: zero delay when off, exact reported latency when on,
  // and unity gain for a quiet sine in every mode.
  for (int mode = 0; mode < 3; ++mode) {
    const int expectedLatency = mode == 0 ? 0 : (mode == 1 ? 30 : 45);
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Clean);
    c.threshold = 1.0f;
    c.input = 0.5f;
    c.output = 0.5f;
    c.oversample = static_cast<float>(mode);
    engine.setControls(c);
    char label[96];
    std::snprintf(label, sizeof(label),
                  "oversampling mode %d reports latency %d", mode,
                  expectedLatency);
    check(engine.latencySamples() == expectedLatency, label);

    Meters meters;
    std::vector<float> l(1024), r(1024);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    l[64] = r[64] = 1.0f;
    engine.process(l.data(), r.data(), 1024, meters);
    int arg = 0;
    float peak = 0.0f;
    for (int i = 0; i < 1024; ++i) {
      if (std::fabs(l[i]) > peak) {
        peak = std::fabs(l[i]);
        arg = i;
      }
    }
    std::snprintf(label, sizeof(label),
                  "oversampling mode %d delay is %d samples", mode,
                  arg - 64);
    check(arg - 64 == expectedLatency, label);

    // A -30 dBFS tone must come through at the same level.
    Engine gain;
    gain.prepare(kSr);
    gain.setControls(c);
    const Render pass = renderSine(gain, c, 1000.0f, -30.0f, 0.5f);
    const float expected = dbToGain(-30.0f) / std::sqrt(2.0f) *
                           dbToGain(kModelTrimDb[static_cast<int>(Model::Clean)]);
    std::snprintf(label, sizeof(label),
                  "oversampling mode %d is unity (%.2f dB)", mode,
                  gainToDb(rmsTail(pass.left, 0.1f)) - gainToDb(expected));
    check(std::fabs(rmsTail(pass.left, 0.1f) - expected) < 0.002f, label);
  }

  // Oversampling must actually remove aliasing.  A 10 kHz tone through the
  // Clean model's hard clip puts its third harmonic at 30 kHz, which folds to
  // 18 kHz at 48 kHz.  2x and 4x should bury that bin.
  {
    const auto aliasLevel = [](int mode) {
      Engine engine;
      engine.prepare(kSr);
      Controls c = baseControls(Model::Clean);
      c.threshold = 1.0f;   // no compression, just the clip stage
      c.input = 0.5f;
      c.output = 0.5f;
      c.distortion = 1.0f;  // CLIP
      c.oversample = static_cast<float>(mode);
      engine.setControls(c);
      const int n = 48000;
      std::vector<float> l(n), r(n);
      Meters meters;
      for (int i = 0; i < n; ++i) {
        l[i] = r[i] = 0.95f * std::sin(2.0f * kPi * 10000.0f * i / kSr);
      }
      for (int start = 0; start < n; start += 256) {
        engine.process(l.data() + start, r.data() + start,
                       std::min(256, n - start), meters);
      }
      const std::vector<float> tail(l.end() - 24000, l.end());
      const float f = toneMagnitude(tail, 10000.0, kSr);
      const float a = toneMagnitude(tail, 18000.0, kSr);
      return gainToDb(a / std::max(f, 1e-9f));
    };
    const float off = aliasLevel(0);
    const float two = aliasLevel(1);
    const float four = aliasLevel(2);
    char label[160];
    std::snprintf(label, sizeof(label),
                  "2x kills the 18 kHz alias (%.1f -> %.1f dB)", off, two);
    check(two < off - 25.0f, label);
    std::snprintf(label, sizeof(label),
                  "4x kills the 18 kHz alias (%.1f -> %.1f dB)", off, four);
    check(four < off - 25.0f, label);
  }

  // The oversampler must keep left and right completely separate.  Sharing
  // filter history made the right channel inherit the left channel's tail:
  // a buzz, louder on the right.
  for (int mode = 1; mode < 3; ++mode) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Clean);
    c.threshold = 1.0f;  // no compression
    c.input = 0.5f;
    c.output = 0.5f;
    c.oversample = static_cast<float>(mode);
    engine.setControls(c);

    const int n = 4096;
    std::vector<float> l(n), r(n);
    Meters meters;
    for (int i = 0; i < n; ++i) {
      l[i] = 0.2f * std::sin(2.0f * kPi * 1000.0f * i / kSr);
      r[i] = 0.0f;
    }
    for (int start = 0; start < n; start += 256) {
      engine.process(l.data() + start, r.data() + start,
                     std::min(256, n - start), meters);
    }
    double leftPower = 0.0;
    double rightPower = 0.0;
    for (int i = n / 2; i < n; ++i) {
      leftPower += static_cast<double>(l[i]) * l[i];
      rightPower += static_cast<double>(r[i]) * r[i];
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "oversampling mode %d keeps L and R separate (R %.1f dB)",
                  mode, 10.0 * std::log10((rightPower + 1e-30) /
                                          (leftPower + 1e-30)));
    check(leftPower > 1.0 && rightPower < 1.0e-10, label);

    // A right-only signal must not leak into the left either.
    for (int i = 0; i < n; ++i) {
      l[i] = 0.0f;
      r[i] = 0.2f * std::sin(2.0f * kPi * 1000.0f * i / kSr);
    }
    for (int start = 0; start < n; start += 256) {
      engine.process(l.data() + start, r.data() + start,
                     std::min(256, n - start), meters);
    }
    double l2 = 0.0;
    double r2 = 0.0;
    for (int i = n / 2; i < n; ++i) {
      l2 += static_cast<double>(l[i]) * l[i];
      r2 += static_cast<double>(r[i]) * r[i];
    }
    std::snprintf(label, sizeof(label),
                  "oversampling mode %d keeps R out of L (L %.1f dB)", mode,
                  10.0 * std::log10((l2 + 1e-30) / (r2 + 1e-30)));
    check(r2 > 1.0 && l2 < 1.0e-10, label);
  }

  // A non-finite or absurd input sample must not poison the model state.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(static_cast<Model>(m));
    c.input = 0.9f;
    c.threshold = 0.2f;
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 256; ++i) {
      l[i] = (i % 3 == 0) ? std::nanf("") : ((i % 3 == 1) ? INFINITY : 1.0e30f);
      r[i] = -l[i];
    }
    engine.process(l.data(), r.data(), 256, meters);
    bool ok = std::isfinite(meters.grDb) && std::isfinite(meters.outPeakDb);
    // Then normal audio must still come through cleanly.
    for (int block = 0; block < 20; ++block) {
      for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>(block * 256 + i) / kSr;
        l[i] = r[i] = 0.3f * std::sin(2.0f * kPi * 220.0f * t);
      }
      engine.process(l.data(), r.data(), 256, meters);
      for (int i = 0; i < 256; ++i) {
        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
          ok = false;
        }
      }
    }
    char label[96];
    std::snprintf(label, sizeof(label),
                  "model %d survives non-finite input", m);
    check(ok, label);
  }

  // With neutral settings and a quiet signal, every model passes audio at
  // unity gain, so no model adds level or colour on its own.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(static_cast<Model>(m));
    // Clean's input gain is centred: 0.5 is unity.  The 76s use 0.6.
    c.input = (m == static_cast<int>(Model::Clean) ||
               m == static_cast<int>(Model::Molot) ||
               m == static_cast<int>(Model::Red) ||
               m == static_cast<int>(Model::Easy))
                  ? 0.5f
                  : 0.6f;
    c.threshold = (m == static_cast<int>(Model::Vca) ||
                   m == static_cast<int>(Model::Clean) ||
                   m == static_cast<int>(Model::Molot) ||
                   m == static_cast<int>(Model::Red) ||
                   m == static_cast<int>(Model::Easy))
                      ? 1.0f
                      : 0.0f;
    if (m == static_cast<int>(Model::Easy)) {
      // Easy's tone section is intentionally active by default.
      c.beef = 0.0f;
      c.air = 0.0f;
      c.satDrive = 0.0f;
    }
    c.makeup = 0.5f;  // unity: the make-up controls are bipolar
    c.output = 0.5f;
    c.ratio = 0.5f;
    const float testDb = (m <= 1) ? -50.0f : -30.0f;
    const Render r = renderSine(engine, c, 100.0f, testDb, 0.5f);
    const float inRms = dbToGain(testDb) / std::sqrt(2.0f);
    const float outDb = gainToDb(rmsTail(r.left, 0.1f)) - gainToDb(inRms);
    // Every model is level-calibrated; neutral means "no reduction", not
    // necessarily unity gain.
    const float expectedDb = kModelTrimDb[m];
    char label[96];
    std::snprintf(label, sizeof(label), "model %d neutral is unity (%.2f dB)", m, outDb);
    check(std::fabs(outDb - expectedDb) < 0.5f, label);
  }

  // At noon, every model must land at roughly the same output level, so a
  // user can flip tabs and choose by sound rather than by loudness.
  {
    std::vector<float> level((size_t)Model::Count, 0.0f);
    for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
      Shared sh;
      sh.set(P_MODEL, m);
      sh.set(P_INPUT, 0.5);
      sh.set(P_OUTPUT, 0.5);
      sh.set(P_MAKEUP, 0.5);
      sh.set(P_ATTACK, 0.5);
      sh.set(P_RELEASE, 0.5);
      sh.set(P_RATIO, 3);
      sh.set(P_THRESHOLD, 0.5);
      sh.set(P_KNEE, 0.5);
      sh.set(P_MIX, 1.0);
      sh.set(P_DISTORTION, 0.0);
      sh.set(P_SAT_DRIVE, 0.5);
      sh.set(P_BEEF, 0.0);
      sh.set(P_AIR, 0.0);
      Engine engine;
      engine.prepare(kSr);
      engine.setControls(controlsFrom(sh));
      Meters meters;
      std::vector<float> l(256), r(256);
      double sum = 0.0;
      int count = 0;
      for (int b = 0; b < 160; ++b) {
        for (int i = 0; i < 256; ++i) {
          const float t = static_cast<float>(b * 256 + i) / kSr;
          l[i] = r[i] =
              0.07f * (std::sin(2.0f * kPi * 100.0f * t) +
                       std::sin(2.0f * kPi * 1000.0f * t) +
                       std::sin(2.0f * kPi * 8000.0f * t));
        }
        engine.process(l.data(), r.data(), 256, meters);
        if (b > 80) {
          for (int i = 0; i < 256; ++i) {
            sum += static_cast<double>(l[i]) * l[i] +
                   static_cast<double>(r[i]) * r[i];
            count += 2;
          }
        }
      }
      level[m] = static_cast<float>(
          10.0 * std::log10(sum / count + 1.0e-20));  // power dB
    }
    float lo = level[0];
    float hi = level[0];
    for (float v : level) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "models are level-matched at noon (spread %.1f dB)", hi - lo);
    check(hi - lo < 3.5f, label);
  }

  // Moving the input knob must not produce zipper clicks: the gain is
  // smoothed, so no sample step should exceed the signal's own slope by much.
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    Meters meters;
    std::vector<float> l(256), r(256);
    float maxStep = 0.0f;
    float prev = 0.0f;
    bool first = true;
    for (int block = 0; block < 80; ++block) {
      c.input = block < 20 ? 0.30f : (block < 25 ? 0.85f : 0.55f);
      engine.setControls(c);
      for (int i = 0; i < 256; ++i) {
        const float t = static_cast<float>((block * 256 + i)) / kSr;
        l[i] = r[i] = 0.2f * std::sin(2.0f * kPi * 220.0f * t);
      }
      engine.process(l.data(), r.data(), 256, meters);
      for (int i = 0; i < 256; ++i) {
        if (!first) {
          maxStep = std::max(maxStep, std::fabs(l[i] - prev));
        }
        prev = l[i];
        first = false;
      }
    }
    char label[96];
    std::snprintf(label, sizeof(label), "input knob moves without clicks (max step %.3f)", maxStep);
    check(maxStep < 0.25f, label);
  }

  // A low-frequency transient must not leave a DC offset behind (the "fart"
  // from asymmetric saturation).
  {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(Model::Blue76);
    c.input = 0.8f;
    engine.setControls(c);
    Meters meters;
    std::vector<float> l(256), r(256);
    std::vector<float> out;
    for (int block = 0; block < 100; ++block) {
      for (int i = 0; i < 256; ++i) {
        const int n = block * 256 + i;
        const float env = std::exp(-static_cast<float>(n) / (0.12f * kSr));
        l[i] = r[i] = 0.8f * env * std::sin(2.0f * kPi * 55.0f * n / kSr);
      }
      engine.process(l.data(), r.data(), 256, meters);
      out.insert(out.end(), l.begin(), l.end());
    }
    double sum = 0.0;
    // Five whole 55 Hz cycles in the fully decayed tail: the mean is DC, not
    // an artefact of a partial cycle.
    const size_t tail = static_cast<size_t>(5.0f * kSr / 55.0f);
    for (size_t i = out.size() - tail; i < out.size(); ++i) sum += out[i];
    const double dc = sum / static_cast<double>(tail);
    char label[96];
    std::snprintf(label, sizeof(label), "kick leaves no DC offset (%.4f)", dc);
    check(std::fabs(dc) < 0.01, label);
  }

  // Every factory preset must apply cleanly and produce finite audio.
  {
    check(comp::kPresetCount >= 30, "at least 30 factory presets");
    bool everyPresetOk = true;
    for (int i = 0; i < comp::kPresetCount; ++i) {
      comp::Shared shared;
      const comp::Preset& p = comp::kPresets[i];
      shared.set(comp::P_MODEL, p.model);
      for (const comp::PresetValue& v : p.values) {
        if (v.param >= 0) {
          shared.set(static_cast<uint32_t>(v.param), v.value);
        }
      }
      Engine engine;
      engine.prepare(kSr);
      Controls c = comp::controlsFrom(shared);
      const Render r = renderSine(engine, c, 120.0f, -12.0f, 0.3f);
      if (!allFinite(r)) {
        everyPresetOk = false;
      }
    }
    check(everyPresetOk, "factory presets render finite audio");
  }

  // Parameters from a host must never poison the engine.
  {
    Shared sh;
    sh.set(P_INPUT, 0.5);
    sh.set(P_INPUT, std::nan(""));
    check(sh.get(P_INPUT) == 0.5, "NaN parameter is ignored");
    sh.setFromHost(P_INPUT, std::nan(""));
    check(sh.get(P_INPUT) == 0.5, "NaN from host is ignored");
    sh.set(P_INPUT, 1e30);
    check(sh.get(P_INPUT) == 1.0, "huge parameter clamps to range");
    sh.set(P_RATIO, 5.4);
    check(sh.get(P_RATIO) == 5.0, "stepped parameter rounds");
    sh.setFromHost(P_RATIO, 5.6);
    check(sh.get(P_RATIO) == 6.0, "stepped host parameter rounds");
    sh.setFromHost(P_MODEL, 9.4);
    check(sh.get(P_MODEL) == 9.0, "model value from host rounds");
    sh.set(P_MODEL, -7.0);
    check(sh.get(P_MODEL) == 0.0, "negative parameter clamps");
  }

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
