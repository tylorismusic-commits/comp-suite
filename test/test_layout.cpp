// Comp Suite — panel layout test
// SPDX-License-Identifier: GPL-3.0-or-later
//
// For every model and both right-hand pages, checks that no two controls
// overlap and that every control sits on the faceplate.  The faceplate is
// drawn at (16, 122) with a size of 1008x482, so anything outside that box
// hangs off the "unit".

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <cairo.h>

#include "params.hpp"
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

constexpr double kPlateX = 16.0;
constexpr double kPlateY = 122.0;
constexpr double kPlateW = 1008.0;
constexpr double kPlateH = 482.0;

bool overlaps(const Panel::WidgetRect& a, const Panel::WidgetRect& b) {
  const double ax2 = a.x + a.w;
  const double ay2 = a.y + a.h;
  const double bx2 = b.x + b.w;
  const double by2 = b.y + b.h;
  // A shared edge is fine; require a real intersection.
  return a.x < bx2 - 0.5 && b.x < ax2 - 0.5 && a.y < by2 - 0.5 &&
         b.y < ay2 - 0.5;
}

} // namespace

int main() {
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_RGB24, kPanelWidth, kPanelHeight);
  cairo_t* cr = cairo_create(surface);

  Panel panel;
  Shared shared;
  panel.loadFont("assets/fonts");

  char label[192];
  for (int model = 0; model < static_cast<int>(Model::Count); ++model) {
    for (int page = 0; page < 2; ++page) {
      shared.set(P_MODEL, static_cast<double>(model));
      PanelState state;
      state.rightPage = page;
      panel.draw(cr, shared, state);
      const std::vector<Panel::WidgetRect> rects = panel.widgetRects();

      // On-plate check.
      bool onPlate = true;
      for (const auto& r : rects) {
        // The header (model tabs, power) is above the faceplate by design.
        if (r.w <= 0.0 || r.h <= 0.0 || r.y < kPlateY) {
          continue;
        }
        if (r.x < kPlateX - 0.5 || r.y < kPlateY - 0.5 ||
            r.x + r.w > kPlateX + kPlateW + 0.5 ||
            r.y + r.h > kPlateY + kPlateH + 0.5) {
          std::printf("  model %d page %d: '%s' at %.0f,%.0f %.0fx%.0f is off "
                      "the faceplate\n",
                      model, page, r.label, r.x, r.y, r.w, r.h);
          onPlate = false;
        }
      }
      std::snprintf(label, sizeof(label),
                    "model %d page %d controls sit on the faceplate", model,
                    page);
      check(onPlate, label);

      // Overlap check.
      bool clean = true;
      for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
          if (!overlaps(rects[i], rects[j])) {
            continue;
          }
          std::printf("  model %d page %d: '%s' overlaps '%s'\n", model, page,
                      rects[i].label, rects[j].label);
          clean = false;
        }
      }
      std::snprintf(label, sizeof(label),
                    "model %d page %d controls do not overlap", model, page);
      check(clean, label);
    }
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
