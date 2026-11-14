// Comp Suite — DSP sanity tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/engine.hpp"

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
                  float seconds) {
  engine.setControls(c);
  const int n = static_cast<int>(seconds * kSr);
  Render r;
  r.left.resize(n);
  r.right.resize(n);
  const float amp = dbToGain(dbfs);
  for (int i = 0; i < n; ++i) {
    const float s = amp * std::sin(2.0f * kPi * freq * i / kSr);
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

  // Disabled plugin passes the input through untouched.
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

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
