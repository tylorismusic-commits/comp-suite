// Comp Suite — factory presets
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "params.hpp"

namespace comp {

// A preset sets the parameters it cares about; -1 means "leave as is".
struct PresetValue {
  // Defaults make unused slots inert.  Without this, zero-filled entries read
  // as {param 0, value 0} - and parameter 0 is Model, so applying a preset
  // reset the compressor back to 76Blue.
  int16_t param = -1;
  float value = 0.0f;
};

struct Preset {
  const char* name;
  const char* category;
  int model;
  PresetValue values[10];
};

// Roughly thirty starting points, each using its model the way it turns up in
// a real session: vocal chains, drum parallel, bass, guitar, mix bus.
inline constexpr Preset kPresets[] = {
    // --- 76Blue -------------------------------------------------------------
    {"76 Vocal Push", "Vocals", 0,
     {{P_INPUT, 0.619f}, {P_RATIO, 3.f}, {P_ATTACK, 0.35f}, {P_RELEASE, 0.45f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Vocal Slam", "Vocals", 0,
     {{P_INPUT, 0.702f}, {P_RATIO, 8.f}, {P_ATTACK, 0.5f}, {P_RELEASE, 0.4f},
      {P_OUTPUT, 0.45f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Vocal Gentle", "Vocals", 0,
     {{P_INPUT, 0.55f}, {P_RATIO, 2.f}, {P_ATTACK, 0.45f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.52f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Kick Punch", "Drums", 0,
     {{P_INPUT, 0.549f}, {P_RATIO, 5.f}, {P_ATTACK, 0.85f}, {P_RELEASE, 0.25f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Snare Crack", "Drums", 0,
     {{P_INPUT, 0.586f}, {P_RATIO, 6.f}, {P_ATTACK, 0.70f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.48f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Drum Smash (par)", "Parallel", 0,
     {{P_INPUT, 0.647f}, {P_RATIO, 8.f}, {P_ATTACK, 0.6f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.42f}, {P_MIX, 0.35f}, {-1, 0}}},
    {"76 Room Crush", "Parallel", 0,
     {{P_INPUT, 0.789f}, {P_RATIO, 8.f}, {P_ATTACK, 0.5f}, {P_RELEASE, 0.25f},
      {P_OUTPUT, 0.40f}, {P_MIX, 0.50f}, {-1, 0}}},
    {"76 Bass DI", "Bass", 0,
     {{P_INPUT, 0.599f}, {P_RATIO, 3.f}, {P_ATTACK, 0.55f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76 Guitar Bite", "Guitars", 0,
     {{P_INPUT, 0.619f}, {P_RATIO, 4.f}, {P_ATTACK, 0.45f}, {P_RELEASE, 0.50f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},

    // --- 76Black ------------------------------------------------------------
    {"76B Vocal Level", "Vocals", 1,
     {{P_INPUT, 0.599f}, {P_RATIO, 3.f}, {P_ATTACK, 0.40f}, {P_RELEASE, 0.50f},
      {P_OUTPUT, 0.51f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76B Bass Control", "Bass", 1,
     {{P_INPUT, 0.579f}, {P_RATIO, 4.f}, {P_ATTACK, 0.60f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"76B Drum Bus", "Drums", 1,
     {{P_INPUT, 0.548f}, {P_RATIO, 3.f}, {P_ATTACK, 0.75f}, {P_RELEASE, 0.35f},
      {P_OUTPUT, 0.52f}, {P_MIX, 0.85f}, {-1, 0}}},
    {"76B Room Glue", "Parallel", 1,
     {{P_INPUT, 0.779f}, {P_RATIO, 6.f}, {P_ATTACK, 0.55f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.40f}, {P_MIX, 0.45f}, {-1, 0}}},

    // --- 2A -----------------------------------------------------------------
    {"2A Vocal Glue", "Vocals", 2,
     {{P_THRESHOLD, 0.365f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.60f}, {P_MIX, 1.f},
      {-1, 0}}},
    {"2A Vocal Limit", "Vocals", 2,
     {{P_THRESHOLD, 0.737f}, {P_RATIO, 8.f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f},
      {-1, 0}}},
    {"2A Bass Smooth", "Bass", 2,
     {{P_THRESHOLD, 0.475f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.65f}, {P_MIX, 1.f},
      {-1, 0}}},
    {"2A Guitar Strum", "Guitars", 2,
     {{P_THRESHOLD, 0.315f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f},
      {-1, 0}}},
    {"2A Room Level", "Parallel", 2,
     {{P_THRESHOLD, 0.517f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.58f}, {P_MIX, 0.45f},
      {-1, 0}}},

    // --- 3A -----------------------------------------------------------------
    {"3A Vocal Presence", "Vocals", 3,
     {{P_THRESHOLD, 0.308f}, {P_MAKEUP, 0.60f}, {P_MIX, 1.f}, {-1, 0}}},
    {"3A Drum Room", "Drums", 3,
     {{P_THRESHOLD, 0.419f}, {P_MAKEUP, 0.55f}, {P_MIX, 0.8f}, {-1, 0}}},
    {"3A Bass Fingers", "Bass", 3,
     {{P_THRESHOLD, 0.375f}, {P_MAKEUP, 0.63f}, {P_MIX, 1.f}, {-1, 0}}},
    {"3A Guitar Bus", "Guitars", 3,
     {{P_THRESHOLD, 0.342f}, {P_MAKEUP, 0.56f}, {P_MIX, 0.9f}, {-1, 0}}},

    // --- Xpress -------------------------------------------------------------
    {"Xpress Vocal Dense", "Vocals", 4,
     {{P_INPUT, 0.55f}, {P_RATIO, 4.f}, {P_ATTACK, 0.20f}, {P_RELEASE, 0.45f},
      {P_DISTORTION, 0.f}, {P_OUTPUT, 0.52f}, {P_MIX, 1.f}, {-1, 0}}},
    {"Xpress Drum Bus Crush", "Parallel", 4,
     {{P_INPUT, 0.72f}, {P_RATIO, 7.f}, {P_ATTACK, 0.06f}, {P_RELEASE, 0.30f},
      {P_DISTORTION, 1.f}, {P_OUTPUT, 0.45f}, {P_MIX, 0.20f}, {-1, 0}}},
    {"Xpress Kick Click", "Drums", 4,
     {{P_INPUT, 0.60f}, {P_RATIO, 3.f}, {P_ATTACK, 0.03f}, {P_RELEASE, 0.25f},
      {P_DISTORTION, 3.f}, {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"Xpress Bass Grind", "Bass", 4,
     {{P_INPUT, 0.66f}, {P_RATIO, 5.f}, {P_ATTACK, 0.30f}, {P_RELEASE, 0.50f},
      {P_DISTORTION, 2.f}, {P_OUTPUT, 0.48f}, {P_MIX, 1.f}, {-1, 0}}},
    {"Xpress Snare Smash", "Drums", 4,
     {{P_INPUT, 0.76f}, {P_RATIO, 7.f}, {P_ATTACK, 0.10f}, {P_RELEASE, 0.20f},
      {P_DISTORTION, 1.f}, {P_OUTPUT, 0.44f}, {P_MIX, 0.60f}, {-1, 0}}},
    {"Xpress Grit Bus", "Parallel", 4,
     {{P_INPUT, 0.52f}, {P_RATIO, 3.f}, {P_ATTACK, 0.40f}, {P_RELEASE, 0.50f},
      {P_DISTORTION, 2.f}, {P_OUTPUT, 0.50f}, {P_MIX, 0.70f}, {-1, 0}}},

    // --- VCA ----------------------------------------------------------------
    {"VCA Mixbus Glue", "Mix Bus", 5,
     {{P_VCA_AUTO, 0.f}, {P_VCA_LINK, 1.f}, {P_THRESHOLD, 0.453f}, {P_RATIO, 3.f}, {P_ATTACK, 0.45f},
      {P_RELEASE, 0.55f}, {P_MAKEUP, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"VCA Mixbus Punch", "Mix Bus", 5,
     {{P_VCA_AUTO, 0.f}, {P_VCA_LINK, 1.f}, {P_THRESHOLD, 0.528f}, {P_RATIO, 5.f}, {P_ATTACK, 0.62f},
      {P_RELEASE, 0.40f}, {P_MAKEUP, 0.50f}, {P_MIX, 1.f}, {-1, 0}}},
    {"VCA Drum Bus", "Drums", 5,
     {{P_VCA_AUTO, 0.f}, {P_VCA_LINK, 1.f}, {P_THRESHOLD, 0.552f}, {P_RATIO, 3.f}, {P_ATTACK, 0.28f},
      {P_RELEASE, 0.35f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {-1, 0}}},
    {"VCA Parallel Crush", "Parallel", 5,
     {{P_VCA_AUTO, 0.f}, {P_VCA_LINK, 1.f}, {P_THRESHOLD, 0.701f}, {P_RATIO, 7.f}, {P_ATTACK, 0.06f},
      {P_RELEASE, 0.20f}, {P_MAKEUP, 0.48f}, {P_MIX, 0.40f}, {-1, 0}}},
    {"VCA Master Gentle", "Mix Bus", 5,
     {{P_VCA_AUTO, 0.f}, {P_VCA_LINK, 1.f}, {P_THRESHOLD, 0.352f}, {P_RATIO, 1.f}, {P_ATTACK, 0.40f},
      {P_RELEASE, 0.60f}, {P_MAKEUP, 0.50f}, {P_MIX, 1.f},
      {P_LIMITER_ON, 1.f}, {-1, 0}}},
};

inline constexpr int kPresetCount =
    static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace comp
