// Comp Suite — renders every faceplate to a PNG for visual checks.
// Usage: comp_ui_preview <font-dir> <output-dir> [scale]
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cairo.h>

#include <cstdlib>
#include <cstdio>
#include <string>

#include "params.hpp"
#include "ui/geometry.hpp"
#include "ui/panel.hpp"

using namespace comp;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <font-dir> <output-dir> [scale]\n", argv[0]);
    return 2;
  }
  const std::string fontDir = argv[1];
  const std::string outDir = argv[2];
  const double scale = argc > 3 ? std::atof(argv[3]) : 1.0;
  const UiSize size = sizeForScale(scale);
  const double drawScale = static_cast<double>(size.width) / kPanelWidth;

  static const char* kNames[] = {"76blue", "76black", "2a",    "3a",
                                "xpress", "vca",     "clean", "molot",
                                "red"};
  Panel panel;
  panel.loadFont(fontDir);
  for (int model = 0; model < static_cast<int>(comp::Model::Count); ++model) {
    Shared shared;
    shared.set(P_MODEL, model);
    shared.set(P_INPUT, 0.6);
    shared.set(P_THRESHOLD, model == 2 || model == 3 ? 0.55 : 0.5);
    shared.set(P_RATIO, 5);  // 8:1 on the shared ladder
    shared.set(P_ATTACK, 0.5);
    shared.set(P_RELEASE, 0.5);
    shared.set(P_MAKEUP, 0.5);
    shared.set(P_OUTPUT, 0.5);
    shared.set(P_MIX, 0.8);
    shared.set(P_SC_HPF, 0.3);
    shared.set(P_DISTORTION, 2);
    shared.set(P_METER, 0);
    shared.grDb.store(7.5f);
    shared.outVuDb.store(-6.0f);
    shared.inPeakDb.store(-8.0f);
    shared.outPeakDb.store(-10.0f);

    PanelState state;
    state.rightPage = (model == 4 || model == 5) ? 1 : 1;
    for (int i = 0; i < 40; ++i) {
      panel.tick(shared, state);
    }

    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size.width, size.height);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, drawScale, drawScale);
    panel.draw(cr, shared, state);
    cairo_destroy(cr);

    const std::string path = outDir + "/" + kNames[model] + ".png";
    const cairo_status_t st = cairo_surface_write_to_png(surface, path.c_str());

    // Clean with the transfer-curve display.
    if (model == 6) {
      shared.set(P_METER, 5);
      cairo_surface_t* sc = cairo_image_surface_create(
          CAIRO_FORMAT_ARGB32, size.width, size.height);
      cairo_t* crc = cairo_create(sc);
      cairo_scale(crc, drawScale, drawScale);
      panel.draw(crc, shared, state);
      cairo_destroy(crc);
      cairo_surface_write_to_png(sc, (outDir + "/clean_curve.png").c_str());
      cairo_surface_destroy(sc);
      shared.set(P_METER, 0);
    }

    // Second pass with the Side Chain page selected.
    if (model == 4 || model == 5) {
      state.rightPage = 0;
      cairo_surface_t* s2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                       size.width, size.height);
      cairo_t* cr2 = cairo_create(s2);
      cairo_scale(cr2, drawScale, drawScale);
      panel.draw(cr2, shared, state);
      cairo_destroy(cr2);
      state.presetOpen = 1;
      state.presetIndex = 25;
      for (int i = 0; i < 3; ++i) panel.tick(shared, state);
      cairo_surface_t* s3 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                       size.width, size.height);
      cairo_t* cr3 = cairo_create(s3);
      cairo_scale(cr3, drawScale, drawScale);
      panel.draw(cr3, shared, state);
      cairo_destroy(cr3);
      cairo_surface_write_to_png(s3, (outDir + "/" + kNames[model] + "_presets.png").c_str());
      cairo_surface_destroy(s3);
      state.presetOpen = 0;

      const std::string p2 = outDir + "/" + kNames[model] + "_sidechain.png";
      cairo_surface_write_to_png(s2, p2.c_str());
      cairo_surface_destroy(s2);
      state.rightPage = 1;
    }
    if (st != CAIRO_STATUS_SUCCESS) {
      std::printf("write failed: %s\n", cairo_status_to_string(st));
    }
    cairo_surface_destroy(surface);
    std::printf("wrote %s\n", path.c_str());
  }
  return 0;
}
