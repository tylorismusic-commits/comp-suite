// Comp Suite — typed parameter values
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <hdsp/hdsp.hpp>

#include "params.hpp"

namespace comp {

inline std::string lowerText(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

inline std::string trimText(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) {
    return {};
  }
  const size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

inline std::string formatParamValue(const Shared& shared, uint32_t id,
                                   double value) {
  const ParamSpec& s = kParamSpecs[id];
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  if (s.labels.count > 0) {
    const int i = std::clamp(
        static_cast<int>(std::lround(value - s.min)), 0, s.labels.count - 1);
    return s.labels.names[i];
  }
  char buf[64];
  switch (id) {
    case P_ENABLED:
    case P_LIMITER_ON:
    case P_AUTO_MAKEUP:
    case P_TRUE_PEAK:
    case P_VCA_AUTO:
      return value > 0.5 ? "On" : "Off";
    case P_SC_SOURCE:
      return value > 0.5 ? "External" : "Internal";
    case P_INPUT:
      if (model <= 1 || model == 4) {
        const double db =
            -80.0 + 103.0 * std::sqrt(std::clamp(value, 0.0, 1.0));
        if (db <= -79.0) {
          return "Off";
        }
        std::snprintf(buf, sizeof(buf), "%+.1f dB", db);
      } else if (model == 6) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -30.0 + 60.0 * value);
      } else if (model == 7) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -12.0 + 24.0 * value);
      } else if (model == 8 || model == 9) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -18.0 + 36.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      }
      return buf;
    case P_THRESHOLD:
      if (model == 2 || model == 3) {
        std::snprintf(buf, sizeof(buf), "%.0f", value * 100.0);
      } else if (model == 5) {
        std::snprintf(buf, sizeof(buf), "%.1f dB", -30.0 + 40.0 * value);
      } else if (model == 6 || model == 7 || model == 8) {
        std::snprintf(buf, sizeof(buf), "%.1f dB", -40.0 + 40.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      }
      return buf;
    case P_RATIO: {
      const int step = hdsp::ratioStepFrom(static_cast<float>(value));
      if (model == 8) {
        static constexpr float kRed[6] = {1.5f, 2.0f, 3.0f,
                                          5.0f, 7.0f, 10.0f};
        std::snprintf(buf, sizeof(buf), "%.1f:1",
                      kRed[std::min(std::max(step, 0), 5)]);
      } else if (model == 7 && hdsp::ratioFromStep(step) > 10.9) {
        return "Limit";
      } else if (model == 2 || model == 3) {
        return step >= 4 ? "Limit" : "Compress";
      } else if (model <= 1 && step >= 8) {
        return "ALL";
      } else {
        std::snprintf(buf, sizeof(buf), "%.1f:1", hdsp::ratioFromStep(step));
      }
      return buf;
    }
    case P_ATTACK: {
      double ms;
      if (model <= 1) {
        ms = 0.04 * std::pow(40.0, std::round(value * 6.0) / 6.0);
      } else if (model == 4) {
        ms = 0.1 * std::pow(500.0, value);
      } else if (model == 5) {
        ms = 0.1 * std::pow(300.0, value);
      } else if (model == 6) {
        ms = 0.1 * std::pow(2000.0, value);
      } else if (model == 7) {
        ms = 1.0 + 99.0 * value;
      } else if (model == 8) {
        ms = 0.3 * std::pow(300.0, value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
        return buf;
      }
      if (ms < 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0f us", ms * 1000.0);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
      }
      return buf;
    }
    case P_RELEASE: {
      double ms;
      if (model <= 1) {
        ms = 50.0 * std::pow(22.0, std::round(value * 6.0) / 6.0);
      } else if (model == 4) {
        ms = 50.0 * std::pow(60.0, value);
      } else if (model == 5) {
        ms = 100.0 * std::pow(12.0, value);
      } else if (model == 6) {
        ms = 5.0 * std::pow(1000.0, value);
      } else if (model == 7) {
        ms = 50.0 + 950.0 * value;
      } else if (model == 8) {
        ms = 100.0 * std::pow(40.0, value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
        return buf;
      }
      std::snprintf(buf, sizeof(buf), ms < 1000.0 ? "%.0f ms" : "%.2f s",
                    ms < 1000.0 ? ms : ms / 1000.0);
      return buf;
    }
    case P_MAKEUP:
      if (model == 7) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -12.0 + 24.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", (value - 0.5) * 20.0);
      }
      return buf;
    case P_OUTPUT:
      if (model == 2 || model == 3) {
        return value > 0.5 ? "+6" : "0";
      }
      if (model == 6) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -30.0 + 60.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -12.0 + 24.0 * value);
      }
      return buf;
    case P_MIX:
      std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      return buf;
    case P_SC_HPF:
      if (value < 0.01 && model == 9) {
        return "Auto";
      }
      if (value < 0.01) {
        return "Off";
      }
      if (model == 7) {
        std::snprintf(buf, sizeof(buf), "%.0f Hz", 40.0 + 220.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f Hz",
                      20.0 * std::pow(20.0, value));
      }
      return buf;
    case P_KNEE:
      if (model == 7) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      } else if (model == 8) {
        std::snprintf(buf, sizeof(buf), "%.1f dB", 12.0 * value);
      } else {
        std::snprintf(buf, sizeof(buf), "%.1f dB", 18.0 * value);
      }
      return buf;
    case P_SAT_DRIVE:
      if (model == 9) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      } else {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", -12.0 + 24.0 * value);
      }
      return buf;
    case P_BEEF:
    case P_AIR:
    case P_VCA_LINK:
      std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0);
      return buf;
    case P_LIMITER_THRESH:
      std::snprintf(buf, sizeof(buf), "%.1f dB", -24.0 + 24.0 * value);
      return buf;
    default:
      std::snprintf(buf, sizeof(buf), "%.2f", value);
      return buf;
  }
}

// Text shown in the editor's value bubble.  The 76 attack and release knobs
// are numbered 1..7 on the faceplate, so the bubble shows that position rather
// than a raw time; typing the same number selects the same position.
inline std::string formatEditValue(const Shared& shared, uint32_t id,
                                   double value) {
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  if ((id == P_ATTACK || id == P_RELEASE) && model <= 1) {
    const int position = std::clamp(
        static_cast<int>(std::lround(value * 6.0)), 0, 6) + 1;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", position);
    return buf;
  }
  return formatParamValue(shared, id, value);
}

inline bool parseParamValue(const Shared& shared, uint32_t id,
                            const std::string& raw, double* out) {
  const ParamSpec& s = kParamSpecs[id];
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  const std::string text = lowerText(trimText(raw));
  if (text.empty()) {
    return false;
  }
  for (int i = 0; i < s.labels.count; ++i) {
    if (text == lowerText(s.labels.names[i])) {
      *out = s.min + i;
      return true;
    }
  }
  if (text == "on" || text == "true" || text == "yes") {
    *out = 1.0;
    return true;
  }
  if (text == "off" || text == "false" || text == "no") {
    *out = 0.0;
    return true;
  }
  if (id == P_SC_HPF && text == "auto") {
    *out = 0.0;
    return true;
  }
  if (id == P_RATIO) {
    if (text == "all") {
      *out = 8.0;
      return true;
    }
    if (text == "limit") {
      *out = 8.0;
      return true;
    }
    if (text == "compress") {
      *out = 0.0;
      return true;
    }
  }

  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) {
    return false;
  }
  const std::string suffix = trimText(std::string(end));
  auto ms = [&]() {
    if (suffix == "us" || suffix == "\u00b5s") {
      return value / 1000.0;
    }
    return suffix == "s" || suffix == "sec" ? value * 1000.0 : value;
  };
  auto hz = [&]() {
    return suffix == "khz" || suffix == "k" ? value * 1000.0 : value;
  };
  auto percent = [&]() {
    return suffix == "%" || value > 1.0 ? value / 100.0 : value;
  };
  auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };

  switch (id) {
    case P_RATIO: {
      static constexpr double kRed[6] = {1.5, 2.0, 3.0, 5.0, 7.0, 10.0};
      int best = 0;
      double delta = 1.0e9;
      const int count = model == 8 ? 6 : 9;
      for (int i = 0; i < count; ++i) {
        const double candidate =
            model == 8 ? kRed[i] : hdsp::ratioFromStep(i);
        const double d = std::fabs(candidate - value);
        if (d < delta) {
          delta = d;
          best = i;
        }
      }
      *out = best;
      return true;
    }
    case P_INPUT:
      if (model <= 1 || model == 4) {
        *out = value <= -79.0
                   ? 0.0
                   : clamp01(std::pow((value + 80.0) / 103.0, 2.0));
      } else if (model == 6) {
        *out = (value + 30.0) / 60.0;
      } else if (model == 7) {
        *out = (value + 12.0) / 24.0;
      } else if (model == 8 || model == 9) {
        *out = (value + 18.0) / 36.0;
      } else {
        *out = percent();
      }
      return true;
    case P_THRESHOLD:
      if (model == 2 || model == 3) {
        *out = percent();
      } else if (model == 5) {
        *out = (value + 30.0) / 40.0;
      } else if (model == 6 || model == 7 || model == 8) {
        *out = (value + 40.0) / 40.0;
      } else {
        *out = percent();
      }
      return true;
    case P_ATTACK:
      if (model <= 1) {
        // A bare 1..7 is the knob's numbered position, matching the legends
        // and the editor bubble.  A unit suffix still means a time.
        if (suffix.empty() && value >= 1.0 && value <= 7.0 &&
            std::fabs(value - std::round(value)) < 1.0e-6) {
          *out = (std::round(value) - 1.0) / 6.0;
          return true;
        }
        const double step =
            std::round(6.0 * std::log(std::max(ms(), 0.001) / 0.04) /
                       std::log(40.0)) /
            6.0;
        *out = clamp01(step);
      } else if (model == 4) {
        *out = std::log(std::max(ms(), 0.01) / 0.1) / std::log(500.0);
      } else if (model == 5) {
        *out = std::log(std::max(ms(), 0.01) / 0.1) / std::log(300.0);
      } else if (model == 6) {
        *out = std::log(std::max(ms(), 0.01) / 0.1) / std::log(2000.0);
      } else if (model == 7) {
        *out = (ms() - 1.0) / 99.0;
      } else if (model == 8) {
        *out = std::log(std::max(ms(), 0.01) / 0.3) / std::log(300.0);
      } else {
        *out = percent();
      }
      return true;
    case P_RELEASE:
      if (model <= 1) {
        // Bare 1..7 selects the numbered knob position, as above.
        if (suffix.empty() && value >= 1.0 && value <= 7.0 &&
            std::fabs(value - std::round(value)) < 1.0e-6) {
          *out = (std::round(value) - 1.0) / 6.0;
          return true;
        }
        const double step =
            std::round(6.0 * std::log(std::max(ms(), 0.001) / 50.0) /
                       std::log(22.0)) /
            6.0;
        *out = clamp01(step);
      } else if (model == 4) {
        *out = std::log(std::max(ms(), 0.01) / 50.0) / std::log(60.0);
      } else if (model == 5) {
        *out = std::log(std::max(ms(), 0.01) / 100.0) / std::log(12.0);
      } else if (model == 6) {
        *out = std::log(std::max(ms(), 0.01) / 5.0) / std::log(1000.0);
      } else if (model == 7) {
        *out = (ms() - 50.0) / 950.0;
      } else if (model == 8) {
        *out = std::log(std::max(ms(), 0.01) / 100.0) / std::log(40.0);
      } else {
        *out = percent();
      }
      return true;
    case P_MAKEUP:
      *out = model == 7 ? (value + 12.0) / 24.0 : value / 20.0 + 0.5;
      return true;
    case P_OUTPUT:
      if (model == 2 || model == 3) {
        *out = text.find('6') != std::string::npos ? 1.0 : 0.0;
      } else if (model == 6) {
        *out = (value + 30.0) / 60.0;
      } else {
        *out = (value + 12.0) / 24.0;
      }
      return true;
    case P_MIX:
    case P_BEEF:
    case P_AIR:
    case P_VCA_LINK:
      *out = percent();
      return true;
    case P_SC_HPF:
      if (text == "auto" || text == "off") {
        *out = 0.0;
      } else if (model == 7) {
        *out = (hz() - 40.0) / 220.0;
      } else {
        *out = std::log(std::max(hz(), 20.0) / 20.0) / std::log(20.0);
      }
      return true;
    case P_KNEE:
      *out = model == 7 ? percent() : value / (model == 8 ? 12.0 : 18.0);
      return true;
    case P_SAT_DRIVE:
      *out = model == 9 ? percent() : (value + 12.0) / 24.0;
      return true;
    case P_LIMITER_THRESH:
      *out = (value + 24.0) / 24.0;
      return true;
    case P_ATTACK_MODE:
    case P_STEREO_MODE:
    case P_EASY_MODE:
    case P_EASY_CHAR:
    case P_SAT_TYPE:
    case P_OVERSAMPLE:
    case P_DRUM_TYPE:
    case P_DISTORTION:
    case P_METER:
    case P_MODEL:
    case P_SC_SOURCE:
      *out = value;
      return true;
    default:
      *out = value;
      return true;
  }
}

} // namespace comp
