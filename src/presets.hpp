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
  PresetValue values[16];
};

// Roughly thirty starting points, each using its model the way it turns up in
// a real session: vocal chains, drum parallel, bass, guitar, mix bus.
inline constexpr Preset kPresets[] = {
    // --- 76Blue -------------------------------------------------------------
    {"76 Vocal Push", "Vocals", 0,
     {{P_INPUT, 0.471f}, {P_RATIO, 3.f}, {P_ATTACK, 0.35f}, {P_RELEASE, 0.45f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Vocal Slam", "Vocals", 0,
     {{P_INPUT, 0.436f}, {P_RATIO, 8.f}, {P_ATTACK, 0.5f}, {P_RELEASE, 0.4f},
      {P_OUTPUT, 0.45f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Vocal Gentle", "Vocals", 0,
     {{P_INPUT, 0.472f}, {P_RATIO, 3.f}, {P_ATTACK, 0.45f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.52f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Kick Punch", "Drums", 0,
     {{P_INPUT, 0.445f}, {P_RATIO, 5.f}, {P_ATTACK, 0.85f}, {P_RELEASE, 0.25f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Snare Crack", "Drums", 0,
     {{P_INPUT, 0.442f}, {P_RATIO, 6.f}, {P_ATTACK, 0.70f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.48f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Drum Smash (par)", "Parallel", 0,
     {{P_INPUT, 0.459f}, {P_RATIO, 8.f}, {P_ATTACK, 0.6f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.42f}, {P_MIX, 0.35f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Room Crush", "Parallel", 0,
     {{P_INPUT, 0.457f}, {P_RATIO, 8.f}, {P_ATTACK, 0.5f}, {P_RELEASE, 0.25f},
      {P_OUTPUT, 0.40f}, {P_MIX, 0.50f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Bass DI", "Bass", 0,
     {{P_INPUT, 0.454f}, {P_RATIO, 3.f}, {P_ATTACK, 0.55f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76 Guitar Bite", "Guitars", 0,
     {{P_INPUT, 0.429f}, {P_RATIO, 5.f}, {P_ATTACK, 0.45f}, {P_RELEASE, 0.50f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},

    // --- 76Black ------------------------------------------------------------
    {"76B Vocal Level", "Vocals", 1,
     {{P_INPUT, 0.471f}, {P_RATIO, 3.f}, {P_ATTACK, 0.40f}, {P_RELEASE, 0.50f},
      {P_OUTPUT, 0.51f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76B Bass Control", "Bass", 1,
     {{P_INPUT, 0.454f}, {P_RATIO, 3.f}, {P_ATTACK, 0.60f}, {P_RELEASE, 0.55f},
      {P_OUTPUT, 0.50f}, {P_MIX, 1.f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76B Drum Bus", "Drums", 1,
     {{P_INPUT, 0.456f}, {P_RATIO, 3.f}, {P_ATTACK, 0.75f}, {P_RELEASE, 0.35f},
      {P_OUTPUT, 0.52f}, {P_MIX, 0.85f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},
    {"76B Room Glue", "Parallel", 1,
     {{P_INPUT, 0.485f}, {P_RATIO, 6.f}, {P_ATTACK, 0.55f}, {P_RELEASE, 0.30f},
      {P_OUTPUT, 0.40f}, {P_MIX, 0.45f}, {P_MAKEUP, 0.5f},
      {-1, 0}}},

    // --- 2A -----------------------------------------------------------------
    {"2A Vocal Glue", "Vocals", 2,
     {{P_THRESHOLD, 0.365f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.60f}, {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"2A Vocal Limit", "Vocals", 2,
     {{P_THRESHOLD, 0.737f}, {P_RATIO, 8.f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"2A Bass Smooth", "Bass", 2,
     {{P_THRESHOLD, 0.475f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.65f}, {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"2A Guitar Strum", "Guitars", 2,
     {{P_THRESHOLD, 0.315f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"2A Room Level", "Parallel", 2,
     {{P_THRESHOLD, 0.517f}, {P_RATIO, 0.f}, {P_MAKEUP, 0.58f}, {P_MIX, 0.45f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},

    // --- 3A -----------------------------------------------------------------
    {"3A Vocal Presence", "Vocals", 3,
     {{P_RATIO, 0.f}, {P_THRESHOLD, 0.308f}, {P_MAKEUP, 0.60f},
      {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"3A Drum Room", "Drums", 3,
     {{P_RATIO, 0.f}, {P_THRESHOLD, 0.419f}, {P_MAKEUP, 0.55f},
      {P_MIX, 0.8f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"3A Bass Fingers", "Bass", 3,
     {{P_RATIO, 0.f}, {P_THRESHOLD, 0.375f}, {P_MAKEUP, 0.63f},
      {P_MIX, 1.f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},
    {"3A Guitar Bus", "Guitars", 3,
     {{P_RATIO, 0.f}, {P_THRESHOLD, 0.342f}, {P_MAKEUP, 0.56f},
      {P_MIX, 0.9f}, {P_OUTPUT, 0.0f},
      {-1, 0}}},

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

    // --- Clean --------------------------------------------------------------
    {"Clean Vocal Control", "Vocals", 6,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.461f}, {P_RATIO, 3.f}, {P_KNEE, 0.5f}, {P_ATTACK, 0.35f},
      {P_RELEASE, 0.50f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_OUTPUT, 0.5f},
      {-1, 0}}},
    {"Clean Drum Control", "Drums", 6,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.508f}, {P_RATIO, 4.f}, {P_KNEE, 0.35f}, {P_ATTACK, 0.20f},
      {P_RELEASE, 0.35f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_OUTPUT, 0.5f},
      {-1, 0}}},
    {"Clean Mix Glue", "Mix Bus", 6,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.516f}, {P_RATIO, 2.f}, {P_KNEE, 0.6f}, {P_ATTACK, 0.55f},
      {P_RELEASE, 0.60f}, {P_MAKEUP, 0.52f}, {P_MIX, 1.f},
      {P_LIMITER_ON, 1.f}, {P_DISTORTION, 0.0f}, {P_OUTPUT, 0.5f},
      {-1, 0}}},
    {"Clean Bass Control", "Bass", 6,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.494f}, {P_RATIO, 3.f}, {P_KNEE, 0.45f}, {P_ATTACK, 0.45f},
      {P_RELEASE, 0.55f}, {P_MAKEUP, 0.54f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_OUTPUT, 0.5f},
      {-1, 0}}},

    // --- Molot --------------------------------------------------------------
    {"Molot Drum Smash", "Drums", 7,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.581f}, {P_RATIO, 5.f}, {P_KNEE, 0.4f}, {P_ATTACK, 0.25f},
      {P_RELEASE, 0.35f}, {P_MAKEUP, 0.56f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_STEREO_MODE, 5.0f}, {P_ATTACK_MODE, 0.0f},
      {-1, 0}}},
    {"Molot Vocal Grit", "Vocals", 7,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.522f}, {P_RATIO, 3.f}, {P_KNEE, 0.6f}, {P_ATTACK, 0.4f},
      {P_RELEASE, 0.45f}, {P_MAKEUP, 0.54f}, {P_MIX, 1.f},
      {P_DISTORTION, 1.f}, {P_STEREO_MODE, 5.0f}, {P_ATTACK_MODE, 0.0f},
      {-1, 0}}},
    {"Molot Mix Bus", "Mix Bus", 7,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.579f}, {P_RATIO, 2.f}, {P_KNEE, 0.7f}, {P_ATTACK, 0.55f},
      {P_RELEASE, 0.6f}, {P_MAKEUP, 0.52f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_STEREO_MODE, 5.0f}, {P_ATTACK_MODE, 0.0f},
      {-1, 0}}},
    {"Molot Bass Grip", "Bass", 7,
     {{P_INPUT, 0.5f}, {P_THRESHOLD, 0.574f}, {P_RATIO, 4.f}, {P_KNEE, 0.5f}, {P_ATTACK, 0.35f},
      {P_RELEASE, 0.5f}, {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_DISTORTION, 0.0f}, {P_STEREO_MODE, 5.0f}, {P_ATTACK_MODE, 0.0f},
      {-1, 0}}},

    // --- Red ----------------------------------------------------------------
    {"Red Vocal Control", "Vocals", 8,
     {{P_VCA_AUTO, 0.f}, {P_THRESHOLD, 0.55f}, {P_RATIO, 2.f},
      {P_KNEE, 0.5f}, {P_ATTACK, 0.35f}, {P_RELEASE, 0.50f},
      {P_MAKEUP, 0.55f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_LIMITER_ON, 0.0f},
      {-1, 0}}},
    {"Red Drum Punch", "Drums", 8,
     {{P_VCA_AUTO, 0.f}, {P_THRESHOLD, 0.48f}, {P_RATIO, 3.f},
      {P_KNEE, 0.35f}, {P_ATTACK, 0.15f}, {P_RELEASE, 0.35f},
      {P_MAKEUP, 0.56f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_LIMITER_ON, 0.0f},
      {-1, 0}}},
    {"Red Mix Glue", "Mix Bus", 8,
     {{P_VCA_AUTO, 0.f}, {P_THRESHOLD, 0.60f}, {P_RATIO, 1.f},
      {P_KNEE, 0.6f}, {P_ATTACK, 0.50f}, {P_RELEASE, 0.60f},
      {P_MAKEUP, 0.53f}, {P_MIX, 1.f}, {P_LIMITER_ON, 1.f}, {P_OUTPUT, 0.5f},
      {-1, 0}}},
    {"Red Bass Control", "Bass", 8,
     {{P_VCA_AUTO, 0.f}, {P_THRESHOLD, 0.58f}, {P_RATIO, 1.f},
      {P_KNEE, 0.45f}, {P_ATTACK, 0.40f}, {P_RELEASE, 0.55f},
      {P_MAKEUP, 0.54f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_LIMITER_ON, 0.0f},
      {-1, 0}}},

    // --- Easy ---------------------------------------------------------------
    {"Easy Squish Drums", "Drums", 9,
     {{P_THRESHOLD, 0.50f}, {P_DRUM_TYPE, 2.f}, {P_BEEF, 0.45f}, {P_AIR, 0.30f},
      {P_SAT_DRIVE, 0.40f}, {P_SAT_TYPE, 2.f}, {P_EASY_MODE, 0.f},
      {P_EASY_CHAR, 1.f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_VCA_LINK, 1.0f},
      {-1, 0}}},
    {"Easy Lift Drums", "Drums", 9,
     {{P_THRESHOLD, 0.55f}, {P_DRUM_TYPE, 2.f}, {P_BEEF, 0.35f}, {P_AIR, 0.35f},
      {P_SAT_DRIVE, 0.20f}, {P_SAT_TYPE, 1.f}, {P_EASY_MODE, 1.f},
      {P_EASY_CHAR, 0.f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_VCA_LINK, 1.0f},
      {-1, 0}}},
    {"Easy Room Glue", "Mix Bus", 9,
     {{P_THRESHOLD, 0.60f}, {P_DRUM_TYPE, 3.f}, {P_BEEF, 0.30f}, {P_AIR, 0.40f},
      {P_SAT_DRIVE, 0.15f}, {P_SAT_TYPE, 1.f}, {P_EASY_MODE, 1.f},
      {P_EASY_CHAR, 0.f}, {P_MIX, 1.f}, {P_OUTPUT, 0.5f}, {P_VCA_LINK, 1.0f},
      {-1, 0}}},
    {"Easy Parallel Squish", "Parallel", 9,
     {{P_THRESHOLD, 0.45f}, {P_DRUM_TYPE, 5.f}, {P_BEEF, 0.50f},
      {P_AIR, 0.25f}, {P_SAT_DRIVE, 0.60f}, {P_SAT_TYPE, 2.f},
      {P_EASY_MODE, 0.f}, {P_EASY_CHAR, 1.f}, {P_MIX, 0.40f}, {P_OUTPUT, 0.5f}, {P_VCA_LINK, 1.0f},
      {-1, 0}}},
};

inline constexpr int kPresetCount =
    static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace comp
