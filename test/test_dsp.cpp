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
  // leaves the release knob doing nothing.
  {
    bool ok = true;
    for (int i = 0; i < kPresetCount; ++i) {
      const Preset& preset = kPresets[i];
      if (preset.model != static_cast<int>(Model::Vca)) {
        continue;
      }
      bool resets = false;
      for (const PresetValue& v : preset.values) {
        if (v.param == static_cast<int>(P_VCA_AUTO) && v.value < 0.5f) {
          resets = true;
        }
      }
      if (!resets) {
        std::printf("  VCA preset '%s' does not reset auto release\n",
                    preset.name);
        ok = false;
      }
    }
    check(ok, "VCA presets reset auto release");
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
  }

  // Every model's mix at 0% gives the dry signal back.
  for (Model m : {Model::Blue76, Model::Black76, Model::Opto2A, Model::Opto3A,
                  Model::Xpress, Model::Vca}) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(m);
    c.mix = 0.0f;
    const Render r = renderSine(engine, c, 100.0f, -6.0f, 1.0f);
    const float expected = dbToGain(-6.0f) / std::sqrt(2.0f);
    char label[64];
    std::snprintf(label, sizeof(label), "model %d mix 0%% is dry",
                  static_cast<int>(m));
    check(std::fabs(rmsTail(r.left, 0.1f) - expected) < 0.01f, label);
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
      Controls c = baseControls(static_cast<Model>(round % 6));
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

  // With neutral settings and a quiet signal, every model passes audio at
  // unity gain, so no model adds level or colour on its own.
  for (int m = 0; m < static_cast<int>(Model::Count); ++m) {
    Engine engine;
    engine.prepare(kSr);
    Controls c = baseControls(static_cast<Model>(m));
    c.input = 0.6f;  // unity: input 0 is silence
    c.threshold = m == static_cast<int>(Model::Vca) ? 1.0f
                  : m == static_cast<int>(Model::Xpress) ? 0.0f
                  : 0.0f;
    c.makeup = 0.5f;  // unity: the make-up controls are bipolar
    c.output = 0.5f;
    c.ratio = 0.5f;
    const Render r = renderSine(engine, c, 100.0f, -30.0f, 0.5f);
    const float inRms = dbToGain(-30.0f) / std::sqrt(2.0f);
    const float outDb = gainToDb(rmsTail(r.left, 0.1f)) - gainToDb(inRms);
    char label[96];
    std::snprintf(label, sizeof(label), "model %d neutral is unity (%.2f dB)", m, outDb);
    check(std::fabs(outDb) < 0.5f, label);
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
    Meters meters;
    std::vector<float> l(256), r(256);
    std::vector<float> out;
    for (int block = 0; block < 40; ++block) {
      for (int i = 0; i < 256; ++i) {
        const int n = block * 256 + i;
        const float env = std::exp(-static_cast<float>(n) / (0.12f * kSr));
        l[i] = r[i] = 0.8f * env * std::sin(2.0f * kPi * 55.0f * n / kSr);
      }
      engine.process(l.data(), r.data(), 256, meters);
      out.insert(out.end(), l.begin(), l.end());
    }
    double sum = 0.0;
    const size_t tail = out.size() / 4;
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
    sh.set(P_MODEL, -7.0);
    check(sh.get(P_MODEL) == 0.0, "negative parameter clamps");
  }

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
