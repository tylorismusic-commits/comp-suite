// Comp Suite — panel interaction tests (no X server needed).
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "params.hpp"
#include "presets.hpp"
#include "ui/panel.hpp"
#include "value_text.hpp"

using namespace comp;

namespace {
int gFailures = 0;
void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}
} // namespace

int main() {
  {
    // A preset must switch the compressor model, not just the controls.
    Shared shared;
    shared.set(P_MODEL, 5);  // VCA
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);

    // Click the "next preset" arrow: from Init it applies the first preset,
    // which is a 76Blue (model 0).
    panel.pointerDown(shared, state, 72 + 330 - 18, 32, false);
    panel.pointerUp();

    char label[128];
    std::snprintf(label, sizeof(label),
                  "preset switches model (expected 0, got %.0f)", shared.get(P_MODEL));
    check(shared.get(P_MODEL) == 0.0, label);
    check(state.presetIndex == 0, "preset index is set");

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  {
    // The header's oversampling control cycles Off -> 2x -> 4x -> Off.
    Shared shared;
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);
    check(shared.get(P_OVERSAMPLE) == 0.0, "oversampling starts off");
    panel.pointerDown(shared, state, 686, 32, false);
    panel.pointerUp();
    check(shared.get(P_OVERSAMPLE) == 1.0, "oversampling cycles to 2x");
    panel.pointerDown(shared, state, 686, 32, false);
    panel.pointerUp();
    check(shared.get(P_OVERSAMPLE) == 2.0, "oversampling cycles to 4x");
    panel.pointerDown(shared, state, 686, 32, false);
    panel.pointerUp();
    check(shared.get(P_OVERSAMPLE) == 0.0, "oversampling cycles back to off");
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  {
    // Opening the list applies the preset under the cursor, not just on arrows.
    Shared shared;
    shared.set(P_MODEL, 0);
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);

    panel.pointerDown(shared, state, 200, 32, false);  // open the list
    panel.pointerUp();
    panel.draw(cr, shared, state);

    int applied = -1;
    for (int y = 70; y < 420 && applied < 0; y += 4) {
      panel.pointerDown(shared, state, 200, y, false);
      panel.pointerUp();
      if (state.presetIndex >= 0) {
        applied = state.presetIndex;
      }
    }
    check(applied >= 0, "clicking the list applies a preset");
    check(state.presetOpen == 0, "the list closes after selection");
    if (applied >= 0) {
      std::printf("  applied=%d '%s' expected model=%d actual=%.0f\n", applied,
                  kPresets[applied].name, kPresets[applied].model,
                  shared.get(P_MODEL));
      check(shared.get(P_MODEL) == kPresets[applied].model,
            "list selection switches to the preset's model");
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  {
    // 76 attack and release accept the faceplate positions 1..7 as typed
    // values, and still accept a time with a unit suffix.
    Shared shared;
    shared.set(P_MODEL, 0);
    double v = -1.0;
    check(parseParamValue(shared, P_ATTACK, "1", &v) &&
              std::fabs(v - 0.0) < 1e-9,
          "76 attack typed 1 = position 1");
    check(parseParamValue(shared, P_ATTACK, "3", &v) &&
              std::fabs(v - 2.0 / 6.0) < 1e-9,
          "76 attack typed 3 = position 3");
    check(parseParamValue(shared, P_ATTACK, "7", &v) &&
              std::fabs(v - 1.0) < 1e-9,
          "76 attack typed 7 = position 7");
    check(parseParamValue(shared, P_RELEASE, "5", &v) &&
              std::fabs(v - 4.0 / 6.0) < 1e-9,
          "76 release typed 5 = position 5");
    check(parseParamValue(shared, P_ATTACK, "200 us", &v) &&
              std::fabs(v - 0.5) < 1e-6,
          "76 attack typed 200 us still means a time");
    check(formatEditValue(shared, P_ATTACK, 2.0 / 6.0) == "3",
          "76 attack bubble shows the position");
  }

  {
    // Double-click restores the current preset's value, not the factory
    // default the preset overrides.
    Shared shared;
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);

    panel.pointerDown(shared, state, 72 + 330 - 18, 32, false);  // first preset
    panel.pointerUp();
    const double presetAttack = shared.get(P_ATTACK);
    double expectedAttack = -1.0;
    for (const PresetValue& v : kPresets[0].values) {
      if (v.param == static_cast<int>(P_ATTACK)) {
        expectedAttack = v.value;
      }
    }
    check(expectedAttack >= 0.0 &&
              std::fabs(presetAttack - expectedAttack) < 1e-6,
          "preset sets the attack value");

    shared.set(P_ATTACK, 0.95);
    panel.pointerDown(shared, state, 320, 552, false);  // single click
    panel.pointerUp();
    check(std::fabs(shared.get(P_ATTACK) - 0.95) < 1e-6,
          "single click leaves the knob alone");

    panel.pointerDown(shared, state, 320, 552, false);  // double click
    panel.pointerUp();
    check(std::fabs(shared.get(P_ATTACK) - presetAttack) < 1e-6,
          "double click restores the preset value");

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  {
    // With no preset loaded, double-click falls back to the parameter default.
    Shared shared;
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);
    shared.set(P_RELEASE, 0.95);
    panel.pointerDown(shared, state, 480, 552, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 480, 552, false);
    panel.pointerUp();
    check(std::fabs(shared.get(P_RELEASE) - kParamSpecs[P_RELEASE].def) < 1e-6,
          "double click without a preset restores the default");
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }


  {
    // Full user preset workflow: save, recall, rename, duplicate, export,
    // delete and import.  Keep every file in a temporary XDG/HOME tree.
    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() / "comp-suite-preset-test";
    fs::remove_all(root);
    fs::create_directories(root / "config");
    fs::create_directories(root / "home");
    const std::string oldXdg = std::getenv("XDG_CONFIG_HOME")
                                   ? std::getenv("XDG_CONFIG_HOME")
                                   : "";
    const std::string oldHome =
        std::getenv("HOME") ? std::getenv("HOME") : "";
    setenv("XDG_CONFIG_HOME", (root / "config").string().c_str(), 1);
    setenv("HOME", (root / "home").string().c_str(), 1);

    Shared shared;
    Panel panel;
    PanelState state;
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
    cairo_t* cr = cairo_create(surf);
    panel.draw(cr, shared, state);

    shared.set(P_THRESHOLD, 0.73);
    const double savedThreshold = shared.get(P_THRESHOLD);

    // Open the browser and press SAVE.
    panel.pointerDown(shared, state, 72 + 165, 32, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 100, 450, false);
    panel.pointerUp();
    check(state.savePreset, "SAVE opens the name field");
    for (char c : std::string("Test Preset")) {
      panel.textKey(shared, state, 0, c);
    }
    panel.textKey(shared, state, Panel::kTextEnter, 0);
    check(state.userPresets.size() == 1, "user preset is written");
    check(state.presetIndex == kPresetCount, "saved preset becomes active");
    check(state.userPresets[0].name == "Test Preset",
          "saved preset keeps its name");

    shared.set(P_THRESHOLD, 0.12);
    panel.applyPreset(shared, state, state.presetIndex);
    check(std::fabs(shared.get(P_THRESHOLD) - savedThreshold) < 1e-9,
          "user preset recalls every parameter");

    // Rename.
    panel.pointerDown(shared, state, 72 + 165, 32, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 100, 480, false);
    panel.pointerUp();
    check(state.renamePreset, "RENAME opens the name field");
    for (char c : std::string("Renamed Preset")) {
      panel.textKey(shared, state, 0, c);
    }
    panel.textKey(shared, state, Panel::kTextEnter, 0);
    check(state.userPresets.size() == 1 &&
              state.userPresets[0].name == "Renamed Preset",
          "user preset renames");

    // Duplicate.
    panel.pointerDown(shared, state, 72 + 165, 32, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 180, 450, false);
    panel.pointerUp();
    check(state.userPresets.size() == 2, "DUPLICATE creates a second preset");

    // Export the active duplicate.
    panel.pointerDown(shared, state, 72 + 165, 32, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 250, 480, false);
    panel.pointerUp();
    const fs::path exported =
        userPresetExportDir() / (std::string("Renamed Preset copy") +
                                 kUserPresetExt);
    check(fs::exists(exported), "EXPORT writes a preset file");

    // Delete the active duplicate: first click confirms, second deletes.
    panel.pointerDown(shared, state, 72 + 165, 32, false);
    panel.pointerUp();
    panel.pointerDown(shared, state, 180, 480, false);
    panel.pointerUp();
    check(state.deletePresetConfirm, "DELETE asks for confirmation");
    panel.pointerDown(shared, state, 180, 480, false);
    panel.pointerUp();
    check(state.userPresets.size() == 1, "DELETE removes the user preset");

    // Import the exported file back.
    check(panel.importDroppedFile(shared, state, exported.string()),
          "IMPORT accepts a dropped preset file");
    check(state.userPresets.size() == 2, "import adds a preset");

    if (!oldXdg.empty()) setenv("XDG_CONFIG_HOME", oldXdg.c_str(), 1);
    else unsetenv("XDG_CONFIG_HOME");
    if (!oldHome.empty()) setenv("HOME", oldHome.c_str(), 1);
    else unsetenv("HOME");
    fs::remove_all(root);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
