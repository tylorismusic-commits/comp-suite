// Comp Suite — panel interaction tests (no X server needed).
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cairo.h>

#include <cstdio>

#include "params.hpp"
#include "presets.hpp"
#include "ui/panel.hpp"

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

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
