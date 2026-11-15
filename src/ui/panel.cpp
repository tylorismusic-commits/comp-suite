// Comp Suite — panel.cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/panel.hpp"

#include "presets.hpp"

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace comp {

namespace {

struct Rgb {
  double r, g, b;
};

// Per-model faceplate colours and knob styling.
struct Skin {
  const char* caption;
  Rgb bodyTop, bodyBottom;
  Rgb legend, legendDim;
  Rgb headerBg;
  Rgb tabOff, tabOn, tabTextOff, tabTextOn;
  Rgb meterFrame, meterFace, meterNeedle, meterScale;
  Rgb meterBlock;           // blue-stripe block behind the meter, or frame colour
  bool hasBlueBlock;
  Rgb knobCap, knobCapEdge, knobSkirt, knobIndicator;
  bool knurled;
  Rgb btnOff, btnOn, btnText, btnTextOn;
  Rgb screw;
};

enum Model { kBlue76, kBlack76, kOpto2A, kOpto3A, kXpress, kVca, kClean,
             kMolot, kRed, kEasy };

Skin skinFor(int model) {
  switch (model) {
    case kBlue76:
      return {"76Blue  -  FET limiter",
              {0.85, 0.87, 0.89}, {0.70, 0.73, 0.76},
              {0.08, 0.08, 0.08}, {0.25, 0.25, 0.25},
              {0.17, 0.18, 0.20},
              {0.45, 0.48, 0.52}, {0.92, 0.93, 0.94}, {0.15, 0.16, 0.18}, {0.05, 0.05, 0.05},
              {0.10, 0.10, 0.10}, {0.90, 0.85, 0.74}, {0.07, 0.07, 0.07}, {0.12, 0.12, 0.12},
              {0.18, 0.42, 0.62}, true,
              {0.82, 0.84, 0.87}, {0.45, 0.48, 0.52}, {0.10, 0.10, 0.10}, {0.05, 0.05, 0.05},
              true,
              {0.08, 0.08, 0.08}, {0.94, 0.94, 0.94}, {0.92, 0.92, 0.92}, {0.05, 0.05, 0.05},
              {0.35, 0.37, 0.40}};
    case kBlack76:
      return {"76Black  -  FET limiter",
              {0.13, 0.13, 0.13}, {0.05, 0.05, 0.05},
              {0.91, 0.91, 0.91}, {0.55, 0.55, 0.55},
              {0.05, 0.05, 0.05},
              {0.17, 0.17, 0.17}, {0.30, 0.30, 0.30}, {0.55, 0.55, 0.55}, {1.0, 1.0, 1.0},
              {0.16, 0.16, 0.16}, {0.93, 0.90, 0.80}, {0.07, 0.07, 0.07}, {0.12, 0.12, 0.12},
              {0.05, 0.05, 0.05}, false,
              {0.83, 0.84, 0.86}, {0.40, 0.42, 0.45}, {0.09, 0.09, 0.09}, {0.02, 0.02, 0.02},
              true,
              {0.06, 0.06, 0.06}, {0.94, 0.94, 0.94}, {0.92, 0.92, 0.92}, {0.05, 0.05, 0.05},
              {0.45, 0.45, 0.45}};
    case kOpto2A:
      return {"2A  -  optical leveler",
              {0.87, 0.87, 0.85}, {0.76, 0.76, 0.74},
              {0.10, 0.10, 0.10}, {0.33, 0.33, 0.33},
              {0.20, 0.20, 0.20},
              {0.75, 0.75, 0.73}, {0.94, 0.94, 0.92}, {0.20, 0.20, 0.20}, {0.0, 0.0, 0.0},
              {0.35, 0.37, 0.39}, {0.91, 0.86, 0.75}, {0.07, 0.07, 0.07}, {0.12, 0.12, 0.12},
              {0.35, 0.37, 0.39}, false,
              {0.84, 0.84, 0.82}, {0.40, 0.40, 0.39}, {0.06, 0.06, 0.06}, {0.05, 0.05, 0.05},
              true,
              {0.22, 0.22, 0.22}, {0.90, 0.90, 0.88}, {0.10, 0.10, 0.10}, {0.0, 0.0, 0.0},
              {0.45, 0.45, 0.45}};
    case kOpto3A:
      return {"3A  -  optical leveler",
              {0.08, 0.08, 0.08}, {0.03, 0.03, 0.03},
              {0.93, 0.93, 0.93}, {0.60, 0.60, 0.60},
              {0.03, 0.03, 0.03},
              {0.15, 0.15, 0.15}, {0.30, 0.30, 0.30}, {0.56, 0.56, 0.56}, {1.0, 1.0, 1.0},
              {0.19, 0.19, 0.19}, {0.91, 0.87, 0.77}, {0.07, 0.07, 0.07}, {0.12, 0.12, 0.12},
              {0.19, 0.19, 0.19}, false,
              {0.81, 0.83, 0.86}, {0.45, 0.47, 0.50}, {0.09, 0.09, 0.09}, {0.04, 0.04, 0.04},
              true,
              {0.07, 0.07, 0.07}, {0.93, 0.93, 0.93}, {0.90, 0.90, 0.90}, {0.05, 0.05, 0.05},
              {0.40, 0.40, 0.40}};
    case kXpress:
      return {"Xpress  -  diode compressor",
              {0.12, 0.12, 0.12}, {0.06, 0.06, 0.06},
              {0.94, 0.94, 0.94}, {0.58, 0.58, 0.58},
              {0.06, 0.06, 0.06},
              {0.16, 0.16, 0.16}, {0.32, 0.32, 0.32}, {0.56, 0.56, 0.56}, {1.0, 1.0, 1.0},
              {0.17, 0.17, 0.17}, {0.83, 0.80, 0.72}, {0.07, 0.07, 0.07}, {0.12, 0.12, 0.12},
              {0.17, 0.17, 0.17}, false,
              {0.78, 0.80, 0.83}, {0.38, 0.40, 0.43}, {0.20, 0.20, 0.20}, {0.05, 0.05, 0.05},
              false,
              {0.08, 0.08, 0.08}, {0.95, 0.95, 0.95}, {0.92, 0.92, 0.92}, {0.05, 0.05, 0.05},
              {0.40, 0.40, 0.40}};
    case kEasy:
      return {"Easy  -  drum character compressor",
              {0.16, 0.42, 0.52}, {0.10, 0.30, 0.38},
              {0.92, 0.94, 0.93}, {0.62, 0.72, 0.74},
              {0.09, 0.20, 0.25},
              {0.22, 0.45, 0.53}, {0.12, 0.32, 0.40}, {0.85, 0.92, 0.94},
              {0.55, 0.95, 1.0},
              {0.08, 0.14, 0.17}, {0.88, 0.90, 0.89}, {0.10, 0.10, 0.10},
              {0.15, 0.18, 0.20},
              {0.08, 0.14, 0.17}, false,
              {0.78, 0.80, 0.82}, {0.45, 0.48, 0.50}, {0.10, 0.26, 0.33},
              {0.90, 0.95, 1.0},
              true,
              {0.14, 0.30, 0.36}, {0.85, 0.25, 0.18}, {0.90, 0.94, 0.95},
              {1.0, 0.96, 0.92},
              {0.55, 0.60, 0.62}};
    case kRed:
      return {"Red  -  class-A VCA compressor",
              {0.58, 0.12, 0.11}, {0.46, 0.08, 0.08},
              {0.94, 0.91, 0.82}, {0.70, 0.62, 0.55},
              {0.30, 0.06, 0.06},
              {0.48, 0.13, 0.12}, {0.66, 0.17, 0.14}, {0.82, 0.78, 0.70},
              {0.98, 0.95, 0.88},
              {0.18, 0.05, 0.05}, {0.88, 0.85, 0.72}, {0.55, 0.10, 0.08},
              {0.14, 0.12, 0.10},
              {0.18, 0.05, 0.05}, false,
              {0.80, 0.80, 0.78}, {0.48, 0.48, 0.46}, {0.24, 0.07, 0.07},
              {0.10, 0.08, 0.07},
              true,
              {0.36, 0.09, 0.09}, {0.72, 0.18, 0.13}, {0.86, 0.82, 0.74},
              {0.99, 0.96, 0.90},
              {0.55, 0.52, 0.48}};
    case kMolot:
      return {"Molot  -  character compressor",
              {0.31, 0.32, 0.20}, {0.24, 0.25, 0.15},
              {0.89, 0.87, 0.74}, {0.63, 0.61, 0.49},
              {0.16, 0.16, 0.12},
              {0.36, 0.37, 0.25}, {0.20, 0.21, 0.14}, {0.80, 0.79, 0.66},
              {0.95, 0.93, 0.80},
              {0.12, 0.09, 0.08}, {0.36, 0.09, 0.08}, {0.15, 0.10, 0.09},
              {0.85, 0.72, 0.62},
              {0.36, 0.09, 0.08}, false,
              {0.16, 0.16, 0.14}, {0.42, 0.42, 0.36}, {0.10, 0.10, 0.08},
              {0.86, 0.84, 0.70},
              false,
              {0.18, 0.18, 0.14}, {0.55, 0.16, 0.12}, {0.80, 0.78, 0.66},
              {0.95, 0.92, 0.82},
              {0.35, 0.35, 0.30}};
    case kClean:
      return {"Clean  -  digital compressor",
              {0.19, 0.25, 0.35}, {0.13, 0.17, 0.25},
              {0.90, 0.91, 0.89}, {0.62, 0.66, 0.70},
              {0.11, 0.14, 0.20},
              {0.26, 0.31, 0.40}, {0.16, 0.24, 0.36}, {0.72, 0.78, 0.86},
              {0.45, 0.75, 1.0},
              {0.10, 0.13, 0.18}, {0.73, 0.73, 0.70}, {0.12, 0.12, 0.12},
              {0.25, 0.25, 0.25},
              {0.10, 0.13, 0.18}, false,
              {0.76, 0.78, 0.81}, {0.45, 0.48, 0.53}, {0.14, 0.17, 0.22},
              {0.08, 0.09, 0.11},
              false,
              {0.22, 0.27, 0.34}, {0.32, 0.58, 0.88}, {0.86, 0.89, 0.93},
              {0.98, 0.99, 1.0},
              {0.55, 0.58, 0.62}};
    default:
      return {"VCA  -  stereo bus compressor",
              {0.18, 0.20, 0.24}, {0.11, 0.12, 0.15},
              {0.84, 0.87, 0.91}, {0.49, 0.53, 0.58},
              {0.09, 0.10, 0.12},
              {0.16, 0.18, 0.21}, {0.24, 0.28, 0.33}, {0.54, 0.58, 0.63}, {0.91, 0.95, 0.99},
              {0.07, 0.08, 0.09}, {0.06, 0.07, 0.09}, {0.35, 0.72, 1.0}, {0.62, 0.70, 0.78},
              {0.10, 0.12, 0.14}, false,
              {0.18, 0.56, 0.88}, {0.07, 0.29, 0.49}, {0.08, 0.09, 0.10}, {0.91, 0.96, 1.0},
              false,
              {0.13, 0.15, 0.18}, {0.35, 0.72, 1.0}, {0.04, 0.05, 0.06}, {0.05, 0.10, 0.14},
              {0.30, 0.34, 0.40}};
  }
}

// Scale legends.  Each list is spread evenly over the knob sweep.
template <size_t N>
std::vector<const char*> sc(const char* const (&labels)[N]) {
  return std::vector<const char*>(labels, labels + N);
}

const char* const kScaleStep7[] = {"1", "2", "3", "4", "5", "6", "7"};
const char* const kScaleThreshold[] = {"-40", "-30", "-20", "-10", "0"};
const char* const kScaleRatioNoAll[] = {"1.5", "2", "3", "4", "6", "8", "12", "20"};
// The Red only has six ratios; the shared ladder snaps to the nearest.
const char* const kScaleRatioRed[] = {"1.5", "2", "3", "5", "7", "10"};
const char* const kScaleDrumType[] = {"Toms", "Kick", "Snare",
                                      "Room", "OH",   "Par"};
const char* const kScaleSatType[] = {"Soft", "Tape", "Hard"};
const char* const kScaleStereo[] = {"Mono", "Dual", "M",
                                    "S",    "M/S",  "St"};
const char* const kScaleMakeupVca[] = {"-10", "-5", "0", "+5", "+10"};
const char* const kScaleAttackVca[] = {"0.1", "0.4", "1.7", "7", "30"};
const char* const kScaleReleaseVca[] = {"0.1", "0.2", "0.3", "0.6", "1.2"};
const char* const kScalePr[] = {"0", "20", "40", "60", "80", "100"};
const char* const kScaleGain[] = {"0", "20", "40", "60", "80", "100"};
const char* const kScaleInputXp[] = {"0", "10", "20", "30", "40"};
const char* const kScaleAttackXp[] = {"0.1", "1", "10", "50 ms"};
const char* const kScaleReleaseXp[] = {"50 ms", "0.3 s", "1 s", "3 s"};
const char* const kScaleInput76[] = {"0", "12", "24", "36", "48"};
const char* const kScaleOutput76[] = {"-12", "0", "+12"};
const char* const kScaleInput18[] = {"-18", "0", "+18"};
// Clean's input and output trims are wider than the other models'.
const char* const kScale30[] = {"-30", "0", "+30"};
// Clean's time constants span a wider range than the VCA's.
const char* const kScaleAttackClean[] = {"0.1", "0.7", "3", "18", "200"};
const char* const kScaleReleaseClean[] = {"5 ms", "30", "160", "900", "5 s"};
// Molot's attack is linear in milliseconds, release linear too.
const char* const kScaleAttackMolot[] = {"1", "26", "50", "75", "100"};
const char* const kScaleReleaseMolot[] = {"50 ms", "0.3 s", "0.5 s", "0.8 s",
                                          "1 s"};
// Red's attack and release run longer than the VCA's.
const char* const kScaleAttackRed[] = {"0.3", "1.2", "5", "22", "90"};
const char* const kScaleReleaseRed[] = {"100 ms", "0.25 s", "0.6 s", "1.6 s",
                                        "4 s"};
const char* const kScaleHpfEasy[] = {"AUTO", "40", "90", "190", "400"};
const char* const kScaleDistortion[] = {"OFF", "DIST 2", "DIST 3", "CLIP"};
const char* const kScaleHpf[] = {"OFF", "40", "90", "190", "400"};
const char* const kScaleMix[] = {"DRY", "", "", "", "COMP"};
const char* const kScaleLimit[] = {"-24", "-18", "-12", "-6", "0"};
const char* const kScaleOutputGeneric[] = {"-12", "0", "+12"};

const char* const kMeterLabelsGr[] = {"0", "2", "4", "6", "8", "10", "12", "14", "16", "18", "20"};
const char* const kMeterLabelsVu[] = {"-20", "-10", "-7", "-5", "-3", "-1", "0", "+1", "+2", "+3"};
const double kMeterFractionsVu[] = {0.0, 0.435, 0.565, 0.652, 0.739, 0.826, 0.870, 0.913, 0.957, 1.0};

constexpr double kPi = 3.14159265358979323846;

// Header preset browser geometry (logical units).
constexpr double kPresetX = 72.0;
constexpr double kPresetY = 14.0;
constexpr double kPresetW = 330.0;
constexpr double kPresetH = 36.0;
constexpr double kListY = 56.0;
constexpr double kListRow = 22.0;
constexpr double kListVisible = 17.0;

// Display order: grouped by category, then by name, so the list does not show
// the same category heading several times.
const std::vector<int>& presetOrder() {
  static const std::vector<int> order = [] {
    std::vector<int> v;
    for (int i = 0; i < kPresetCount; ++i) {
      v.push_back(i);
    }
    std::stable_sort(v.begin(), v.end(), [](int a, int b) {
      const int c = std::strcmp(kPresets[a].category, kPresets[b].category);
      if (c != 0) {
        return c < 0;
      }
      return std::strcmp(kPresets[a].name, kPresets[b].name) < 0;
    });
    return v;
  }();
  return order;
}

// Angle of a knob position (0..1), in cairo convention.  The sweep runs
// from 7:30 to 4:30 o'clock.
double knobAngle(double f) {
  return (135.0 + 270.0 * f) * kPi / 180.0;
}

double clamp01(double v) {
  return std::min(std::max(v, 0.0), 1.0);
}

void roundRect(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPi / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, kPi / 2);
  cairo_arc(cr, x + r, y + h - r, r, kPi / 2, kPi);
  cairo_arc(cr, x + r, y + r, r, kPi, 3 * kPi / 2);
  cairo_close_path(cr);
}

void setRgb(cairo_t* cr, Rgb c, double a = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

Rgb mix(Rgb a, Rgb b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Models that have a Side Chain page.
bool hasSideChain(int model) {
  return model == kXpress || model == kVca || model == kClean ||
         model == kMolot || model == kRed || model == kEasy;
}

bool isLimiter76(int model) {
  return model == kBlue76 || model == kBlack76;
}

// Meter modes are stored as 0 GR, 1 +8, 2 +4, 3 Graph, 4 Off.  The optical
// and diode units only offer GR, Graph, and Off, so other modes fall back to GR.
int meterModeFor(const Shared& shared, int model) {
  int mode = static_cast<int>(shared.get(P_METER) + 0.5);
  if (!isLimiter76(model)) {
    if (mode == 3 || mode == 4 || mode == 5) {
      return mode;
    }
    return 0;
  }
  return mode;
}

} // namespace

Panel::Panel() {
  // 64x64 tile of low-alpha noise; tiled over the faceplate so it reads as a
  // painted surface rather than flat plastic.
  grain_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
  unsigned char* data = cairo_image_surface_get_data(grain_);
  const int stride = cairo_image_surface_get_stride(grain_);
  unsigned int seed = 22222u;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      seed = seed * 1103515245u + 12345u;
      const unsigned v = (seed >> 16) & 0xff;
      const unsigned a = (v < 128) ? 16u : 0u;
      const unsigned c = (v < 128) ? 0u : 255u;
      unsigned char* px = data + y * stride + x * 4;
      px[0] = static_cast<unsigned char>(c & 0xff);
      px[1] = static_cast<unsigned char>(c & 0xff);
      px[2] = static_cast<unsigned char>(c & 0xff);
      px[3] = static_cast<unsigned char>(a);
    }
  }
  cairo_surface_mark_dirty(grain_);
  fallbackFont_ = cairo_toy_font_face_create("sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                             CAIRO_FONT_WEIGHT_BOLD);
  font_ = fallbackFont_;
}

Panel::~Panel() {
  if (grain_) {
    cairo_surface_destroy(grain_);
  }
  if (font_ && font_ != fallbackFont_) {
    cairo_font_face_destroy(font_);
  }
  if (fallbackFont_) {
    cairo_font_face_destroy(fallbackFont_);
  }
  if (ftFace_) {
    FT_Done_Face(static_cast<FT_Face>(ftFace_));
  }
  if (ft_) {
    FT_Done_FreeType(static_cast<FT_Library>(ft_));
  }
}

void Panel::loadFont(const std::string& dir) {
  if (ft_) {
    return;
  }
  std::string path = dir;
  if (!path.empty() && path.back() != '/') {
    path += '/';
  }
  path += "Jost-ExtraBold.ttf";
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) != 0) {
    return;
  }
  FT_Face face = nullptr;
  if (FT_New_Face(lib, path.c_str(), 0, &face) != 0) {
    FT_Done_FreeType(lib);
    return;
  }
  cairo_font_face_t* font = cairo_ft_font_face_create_for_ft_face(face, 0);
  if (cairo_font_face_status(font) != CAIRO_STATUS_SUCCESS) {
    cairo_font_face_destroy(font);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return;
  }
  ft_ = lib;
  ftFace_ = face;
  font_ = font;
}

void Panel::text(cairo_t* cr, const char* s, double x, double y, double size,
                 int align, double r, double g, double b) const {
  cairo_set_font_face(cr, font_);
  cairo_set_font_size(cr, size);
  cairo_set_source_rgb(cr, r, g, b);
  cairo_text_extents_t e;
  cairo_text_extents(cr, s, &e);
  double tx = x;
  if (align == 1) {
    tx = x - e.x_advance * 0.5;
  } else if (align == 2) {
    tx = x - e.x_advance;
  }
  cairo_move_to(cr, tx, y);
  cairo_show_text(cr, s);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void Panel::layout(const Shared& shared, const PanelState& state) {
  widgets_.clear();
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);

  // Power and the six model tabs across the top.
  {
    Widget p{};
    p.kind = Kind::Power;
    p.param = P_ENABLED;
    p.x = 34;
    p.y = 30;
    p.r = 14;
    widgets_.push_back(p);
  }
  // Labels are indexed by model value; display order is separate so the tab
  // strip can lead with Clean without renumbering the model parameter (which
  // would silently repoint every saved session and preset).
  static const char* kTabNames[10] = {"76Blue", "76Black", "2A",    "3A",
                                      "Xpress", "VCA",     "Clean", "Molot",
                                      "Red",    "Easy"};
  static const int kTabOrder[10] = {6, 9, 0, 1, 2, 3, 4, 5, 7, 8};
  static_assert(sizeof(kTabNames) / sizeof(kTabNames[0]) ==
                    static_cast<size_t>(comp::Model::Count),
                "tab names must cover every model");
  static_assert(sizeof(kTabOrder) / sizeof(kTabOrder[0]) ==
                    static_cast<size_t>(comp::Model::Count),
                "tab order must cover every model");
  const int tabCount = static_cast<int>(comp::Model::Count);
  const double tabW = (980.0 - (tabCount - 1) * 8.0) / tabCount;
  for (int i = 0; i < tabCount; ++i) {
    const int modelValue = kTabOrder[i];
    Widget t{};
    t.kind = Kind::ModelTab;
    t.param = P_MODEL;
    t.value = modelValue;
    t.x = 30 + i * (tabW + 8.0);
    t.y = 70;
    t.w = tabW;
    t.h = 40;
    t.label = kTabNames[modelValue];
    widgets_.push_back(t);
  }

  // Meter mode selector, inside the top-left of the meter window.
  {
    static const char* kSeg76[] = {"GR", "+8", "+4", "GRAPH", "CURVE", "OFF"};
    static const int kVal76[] = {0, 1, 2, 3, 5, 4};
    static const char* kSegOther[] = {"GR", "GRAPH", "CURVE", "OFF"};
    static const int kValOther[] = {0, 3, 5, 4};
    const bool full = isLimiter76(model);
    const int count = full ? 6 : 4;
    const double segW = full ? 56.0 : 90.0;
    for (int i = 0; i < count; ++i) {
      Widget s{};
      s.kind = Kind::MeterSegment;
      s.param = P_METER;
      s.value = full ? kVal76[i] : kValOther[i];
      s.x = 182 + i * segW;
      s.y = 142;
      s.w = segW;
      s.h = 20;
      s.label = full ? kSeg76[i] : kSegOther[i];
      widgets_.push_back(s);
    }
  }

  auto knob = [&](uint32_t param, double x, double y, double r, const char* label,
                  std::vector<const char*> scale, bool below = false,
                  int steps = 0, double scaleRadius = 1.44) {
    Widget k{};
    k.kind = Kind::Knob;
    k.param = param;
    k.x = x;
    k.y = y;
    k.r = r;
    k.steps = steps;
    k.scaleRadius = scaleRadius;
    k.label = label;
    k.scale = std::move(scale);
    k.labelBelow = below;
    widgets_.push_back(k);
  };

  auto button = [&](uint32_t param, double x, double y, double w, double h,
                    double value, const char* label) {
    Widget b{};
    b.kind = Kind::Button;
    b.param = param;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.value = value;
    b.label = label;
    widgets_.push_back(b);
  };

  auto toggle = [&](uint32_t param, double x, double y, double value,
                    const char* top, const char* bottom) {
    Widget t{};
    t.kind = Kind::Toggle;
    t.param = param;
    t.x = x;
    t.y = y;
    t.w = 30;
    t.h = 56;
    t.value = value;
    t.label = top;
    t.buttonText = {top, bottom};
    widgets_.push_back(t);
  };

  // Logic-style slots: threshold (240,410), ratio (400,410), makeup (560,410),
  // attack (320,510), release (480,510), input (96,520), output (930,500),
  // side-chain/distortion knob (890,260), mix (830,500).
  switch (model) {
    case kBlue76:
    case kBlack76: {
      knob(P_INPUT, 120, 520, 28, "INPUT", sc(kScaleInput76), true);
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleStep7));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleStep7));
      // Ratio bank: 20, 12, 8, 4 and ALL, top to bottom.
      button(P_RATIO, 378, 352, 44, 26, 7.0, "20");
      button(P_RATIO, 378, 382, 44, 26, 6.0, "12");
      button(P_RATIO, 378, 412, 44, 26, 5.0, "8");
      button(P_RATIO, 378, 442, 44, 26, 3.0, "4");
      button(P_RATIO, 378, 472, 44, 26, 8.0, "ALL");
      knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
      knob(P_OUTPUT, 912, 500, 32, "OUTPUT", sc(kScaleOutput76));
      break;
    }
    case kOpto2A: {
      knob(P_THRESHOLD, 240, 446, 40, "PEAK REDUCTION", sc(kScalePr));
      toggle(P_RATIO, 400, 430, 8.0, "LIMIT", "COMP");
      knob(P_MAKEUP, 560, 446, 34, "GAIN", sc(kScaleGain));
      knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
      toggle(P_OUTPUT, 930, 470, 0.0, "+6", "0");
      break;
    }
    case kOpto3A: {
      knob(P_THRESHOLD, 240, 446, 40, "PEAK REDUCTION", sc(kScalePr));
      knob(P_MAKEUP, 560, 446, 34, "GAIN", sc(kScaleGain));
      knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
      break;
    }
    case kXpress: {
      knob(P_INPUT, 112, 520, 28, "INPUT", sc(kScaleInputXp), true);
      static const char* kRatioText[] = {"1.5:1", "2:1", "3:1", "4:1",
                                         "6:1", "8:1", "12:1", "20:1", "NUKE"};
      for (int i = 0; i < 9; ++i) {
        const int col = i % 3;
        const int row = i / 3;
        button(P_RATIO, 336 + col * 54, 366 + row * 26, 50, 22,
               static_cast<double>(i), kRatioText[i]);
      }
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleAttackXp));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleReleaseXp));
      if (state.rightPage == 0) {
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpf));
      } else {
        // Distortion as four buttons: a stepped knob hid the current mode,
        // and the labels around a knob did not fit.
        button(P_DISTORTION, 826, 374, 60, 22, 0.0, "OFF");
        button(P_DISTORTION, 890, 374, 60, 22, 1.0, "DIST 2");
        button(P_DISTORTION, 826, 400, 60, 22, 2.0, "DIST 3");
        button(P_DISTORTION, 890, 400, 60, 22, 3.0, "CLIP");
        knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
        knob(P_OUTPUT, 912, 500, 27, "OUTPUT", sc(kScaleOutputGeneric));
      }
      break;
    }
    case kEasy: {
      knob(P_THRESHOLD, 240, 424, 34, "THRESHOLD", sc(kScaleThreshold));
      knob(P_BEEF, 400, 424, 30, "BEEF", {});
      knob(P_AIR, 560, 424, 30, "AIR", {});
      knob(P_SAT_DRIVE, 700, 424, 30, "SATURATION", {});
      knob(P_DRUM_TYPE, 800, 420, 24, "DRUM TYPE", sc(kScaleDrumType), false,
           0, 1.56);
      knob(P_SAT_TYPE, 620, 552, 26, "SAT TYPE", sc(kScaleSatType));
      knob(P_INPUT, 120, 520, 26, "INPUT", sc(kScaleInput18), true);
      knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
      knob(P_OUTPUT, 912, 500, 27, "OUTPUT", sc(kScaleOutputGeneric));
      toggle(P_EASY_MODE, 320, 515, 1.0, "LIFT", "SQUISH");
      toggle(P_EASY_CHAR, 480, 515, 1.0, "EXTREME", "NORMAL");
      if (state.rightPage == 0) {
        // Auto follows the Drum Type high-pass; moving it overrides it.
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpfEasy));
      }
      break;
    }
    case kRed: {
      knob(P_THRESHOLD, 240, 424, 34, "THRESHOLD", sc(kScaleThreshold));
      knob(P_RATIO, 400, 424, 34, "RATIO", sc(kScaleRatioRed), false, 6);
      knob(P_MAKEUP, 560, 424, 34, "MAKE UP", sc(kScaleMakeupVca));
      knob(P_KNEE, 700, 424, 26, "KNEE", {});
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleAttackRed));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleReleaseRed));
      knob(P_INPUT, 120, 520, 26, "INPUT GAIN", sc(kScaleInput18), true);
      knob(P_OUTPUT, 912, 500, 27, "OUTPUT", sc(kScaleOutputGeneric));
      toggle(P_VCA_AUTO, 700, 520, 1.0, "AUTO", "MAN");
      if (state.rightPage == 0) {
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpf));
      }
      break;
    }
    case kMolot: {
      knob(P_THRESHOLD, 240, 424, 34, "THRESHOLD", sc(kScaleThreshold));
      knob(P_RATIO, 400, 424, 34, "RATIO", sc(kScaleRatioNoAll));
      knob(P_MAKEUP, 560, 424, 34, "MAKE UP", sc(kScaleOutputGeneric));
      knob(P_KNEE, 700, 424, 26, "KNEE", {});
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleAttackMolot));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleReleaseMolot));
      knob(P_INPUT, 120, 520, 26, "INPUT", sc(kScaleOutputGeneric), true);
      knob(P_SAT_DRIVE, 700, 520, 24, "SAT DRIVE", {});
      knob(P_MIX, 796, 508, 25, "DRY / WET", sc(kScaleMix), true);
      if (state.rightPage == 0) {
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpf));
      } else {
        toggle(P_DISTORTION, 830, 380, 1.0, "SAT", "OFF");
        toggle(P_ATTACK_MODE, 888, 380, 1.0, "SIGMA", "ALPHA");
        knob(P_STEREO_MODE, 888, 530, 22, "STEREO", sc(kScaleStereo), false,
             0, 1.60);
      }
      break;
    }
    case kClean: {
      knob(P_THRESHOLD, 240, 424, 34, "THRESHOLD", sc(kScaleThreshold));
      knob(P_RATIO, 400, 424, 34, "RATIO", sc(kScaleRatioNoAll));
      knob(P_MAKEUP, 560, 424, 34, "MAKE UP", sc(kScaleMakeupVca));
      knob(P_KNEE, 700, 424, 26, "KNEE", {});
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleAttackClean));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleReleaseClean));
      knob(P_INPUT, 120, 520, 26, "INPUT", sc(kScale30), true);
      if (state.rightPage == 0) {
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpf));
      } else {
        button(P_DISTORTION, 826, 374, 60, 22, 0.0, "OFF");
        button(P_DISTORTION, 890, 374, 60, 22, 1.0, "SOFT");
        button(P_DISTORTION, 826, 400, 60, 22, 2.0, "HARD");
        button(P_DISTORTION, 890, 400, 60, 22, 3.0, "CLIP");
        knob(P_MIX, 796, 508, 25, "MIX", sc(kScaleMix), true);
        knob(P_OUTPUT, 912, 500, 27, "OUTPUT", sc(kScale30));
      }
      break;
    }
    default: {
      knob(P_THRESHOLD, 240, 424, 34, "THRESHOLD", sc(kScaleThreshold));
      knob(P_RATIO, 400, 424, 34, "RATIO", sc(kScaleRatioNoAll));
      knob(P_MAKEUP, 560, 424, 34, "MAKE UP", sc(kScaleMakeupVca));
      knob(P_ATTACK, 320, 552, 30, "ATTACK", sc(kScaleAttackVca));
      knob(P_RELEASE, 480, 552, 30, "RELEASE", sc(kScaleReleaseVca));
      knob(P_VCA_LINK, 700, 424, 28, "LINK", {});
      // High enough that both labels stay on the faceplate (it ends at 604).
      toggle(P_VCA_AUTO, 600, 520, 1.0, "AUTO", "MAN");
      if (state.rightPage == 0) {
        knob(P_SC_HPF, 888, 260, 34, "SIDE CHAIN HPF", sc(kScaleHpf));
      } else {
        knob(P_MIX, 796, 508, 25, "BLEND", sc(kScaleMix), true);
      }
      break;
    }
  }

  // Global output options live in the header's empty right-hand side.  They
  // were in the model rows before and collided with the output knobs, the
  // Xpress distortion buttons and the output meter.
  auto pill = [&](uint32_t param, double x, double y, double w, double h,
                  const char* label) {
    Widget widget{};
    widget.kind = Kind::Pill;
    widget.param = param;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    widget.value = 1.0;
    widget.label = label;
    widgets_.push_back(widget);
  };
  {
    Widget c{};
    c.kind = Kind::Cycle;
    c.param = P_OVERSAMPLE;
    c.x = 644;
    c.y = 20;
    c.w = 84;
    c.h = 24;
    widgets_.push_back(c);
  }
  pill(P_AUTO_MAKEUP, 736, 20, 84, 24, "AUTO MAKEUP");
  pill(P_TRUE_PEAK, 828, 20, 84, 24, "TRUE PEAK");
  pill(P_SC_SOURCE, 920, 20, 84, 24, "EXT SC");

  // Limiter section, in the right column for every model (the reference
  // layout has one regardless of compressor style).
  // The Red's limiter is part of the model, but it still hides on the
  // side-chain page so the side-chain HPF has the column to itself.
  const bool showLimiter = !(hasSideChain(model) && state.rightPage == 0);
  if (showLimiter) {
    // Latching: a plain button always writes 1, so it could never be
    // switched back off.
    pill(P_LIMITER_ON, 843, 172, 90, 22, "LIMITER");
    knob(P_LIMITER_THRESH, 888, 272, 24, "THRESH", sc(kScaleLimit));
  }

  if (hasSideChain(model)) {
    // Narrow and placed in the gap between the LED column (which ends at 770)
    // and the output meter's numbers (which start around 960).
    const double pageW = 68;
    for (int i = 0; i < 2; ++i) {
      Widget t{};
      t.kind = Kind::PageTab;
      t.value = i;
      t.x = 818 + i * (pageW + 4);
      t.y = 136;
      t.w = pageW;
      t.h = 24;
      t.label = i == 0 ? "Side Chain" : "Output";
      widgets_.push_back(t);
    }
  }
}

// ---------------------------------------------------------------------------
// Hit testing and interaction
// ---------------------------------------------------------------------------

bool Panel::hitWidget(const Widget& w, double x, double y) const {
  switch (w.kind) {
    case Kind::Knob: {
      const double dx = x - w.x;
      const double dy = y - w.y;
      return dx * dx + dy * dy <= w.r * w.r * 1.3;
    }
    case Kind::Power: {
      const double dx = x - w.x;
      const double dy = y - w.y;
      return dx * dx + dy * dy <= w.r * w.r * 1.3;
    }
    case Kind::Toggle:
      return x >= w.x - w.w * 0.5 && x <= w.x + w.w * 0.5 && y >= w.y &&
             y <= w.y + w.h;
    case Kind::Pill:
      return x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h;
    default:
      return x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h;
  }
}

double Panel::knobValue(const Shared& shared, const Widget& w) const {
  return shared.get(w.param);
}

bool Panel::isLit(const Shared& shared, const Widget& w) const {
  return std::fabs(shared.get(w.param) - w.value) < 1e-3;
}

bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  if (handlePresetClick(shared, state, x, y)) {
    return true;
  }
  for (size_t i = 0; i < widgets_.size(); ++i) {
    const Widget& w = widgets_[i];
    if (!hitWidget(w, x, y)) {
      continue;
    }
    switch (w.kind) {
      case Kind::Knob: {
        dragParam_ = static_cast<int>(w.param);
        dragSteps_ = w.steps;
        dragStartY_ = y;
        dragStartValue_ = shared.get(w.param);
        if (dragSteps_ > 1) {
          dragStartValue_ = std::min(dragStartValue_, dragSteps_ - 1.0);
        }
        (void)fine;
        return false;
      }
      case Kind::Power:
        shared.set(P_ENABLED, shared.get(P_ENABLED) > 0.5 ? 0.0 : 1.0);
        state.presetDirty = true;
        return true;
      case Kind::ModelTab:
      case Kind::MeterSegment:
        shared.set(w.param, w.value);
        state.presetDirty = true;
        return true;
      case Kind::Button:
        shared.set(w.param, w.value);
        state.presetDirty = true;
        return true;
      case Kind::Toggle: {
        // Upper half selects the first label, lower half the second.
        const double f = (y - w.y) / w.h;
        shared.set(w.param, f < 0.5 ? 1.0 : 0.0);
        state.presetDirty = true;
        return true;
      }
      case Kind::PageTab:
        state.rightPage = static_cast<int>(w.value);
        return true;
      case Kind::Pill:
        shared.set(w.param, shared.get(w.param) > 0.5 ? 0.0 : 1.0);
        state.presetDirty = true;
        return true;
      case Kind::Cycle: {
        const int v = static_cast<int>(shared.get(w.param) + 0.5);
        shared.set(w.param, static_cast<double>((v + 1) % 3));
        state.presetDirty = true;
        return true;
      }
    }
  }
  return false;
}

bool Panel::pointerMove(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  (void)state;
  (void)x;
  if (dragParam_ < 0) {
    return false;
  }
  const uint32_t param = static_cast<uint32_t>(dragParam_);
  const ParamSpec& spec = kParamSpecs[param];
  // Stepped controls move one step per ~18 px, so they stay controllable.
  const double perPixel = spec.stepped
                              ? 1.0 / 18.0
                              : (spec.max - spec.min) / (fine ? 800.0 : 200.0);
  double v = dragStartValue_ + (dragStartY_ - y) * perPixel;
  if (dragSteps_ > 1) {
    v = std::min(v, dragSteps_ - 1.0);
  }
  shared.set(param, v);
  state.presetDirty = true;
  return true;
}

void Panel::pointerUp() {
  dragParam_ = -1;
}

bool Panel::wheel(Shared& shared, PanelState& state, double x, double y, double dy) {
  if (state.presetOpen && x >= kPresetX && x <= kPresetX + kPresetW &&
      y >= kListY && y <= kListY + kListVisible * kListRow + 10) {
    int rows = kPresetCount;
    const char* last = nullptr;
    for (int i : presetOrder()) {
      if (!last || std::strcmp(last, kPresets[i].category) != 0) {
        last = kPresets[i].category;
        ++rows;
      }
    }
    const int maxScroll = std::max(0, rows - static_cast<int>(kListVisible));
    state.presetScroll = std::min(maxScroll,
                                  std::max(0, state.presetScroll - (int)dy));
    return true;
  }
  (void)state;
  for (const Widget& w : widgets_) {
    if (w.kind == Kind::Knob && hitWidget(w, x, y)) {
      const ParamSpec& spec = kParamSpecs[w.param];
      if (spec.stepped) {
        shared.set(w.param, shared.get(w.param) - (dy > 0 ? 1.0 : -1.0));
      } else {
        shared.set(w.param,
                   shared.get(w.param) - dy * (spec.max - spec.min) * 0.01);
      }
      state.presetDirty = true;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void Panel::tick(const Shared& shared, PanelState& state) const {
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  const int mode = meterModeFor(shared, model);
  const float gr = shared.grDb.load(std::memory_order_relaxed);
  const float vu = shared.outVuDb.load(std::memory_order_relaxed);

  double target = 0.0;
  if (mode == 0) {
    target = clamp01(gr / 20.0);
  } else if (mode == 1 || mode == 2) {
    const double offset = mode == 1 ? -4.0 : 0.0;
    target = clamp01((vu + offset + 20.0) / 23.0);
  }
  state.needle += static_cast<float>((target - state.needle) * 0.25);

  state.grHistory[state.grHead] = gr;
  state.grHead = (state.grHead + 1) % PanelState::kHistory;

  state.inLevel = shared.inPeakDb.load(std::memory_order_relaxed);
  state.outLevel = shared.outPeakDb.load(std::memory_order_relaxed);
  state.inLevelL = shared.inPeakLDb.load(std::memory_order_relaxed);
  state.inLevelR = shared.inPeakRDb.load(std::memory_order_relaxed);
  state.outLevelL = shared.outPeakLDb.load(std::memory_order_relaxed);
  state.outLevelR = shared.outPeakRDb.load(std::memory_order_relaxed);
  state.outPeakHold = std::max(state.outPeakHold - 0.2f, state.outLevel);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Panel::draw(cairo_t* cr, const Shared& shared, const PanelState& state) {
  layout(shared, state);
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);

  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.07, 0.07, 0.07);
  cairo_paint(cr);

  drawHeader(cr, shared, state);
  drawBody(cr, model);
  drawMeter(cr, shared, state, model);
  drawLeds(cr, shared);

  // Input and output level meters, as in the Logic reference.
  drawLevelMeter(cr, 20, 150, 16, 352, state.inLevelL, state.inLevelR, -60.0,
                 0.0, model, true);
  drawLevelMeter(cr, 986, 150, 16, 352, state.outLevelL, state.outLevelR,
                 -60.0, 3.0, model, false);

  for (const Widget& w : widgets_) {
    drawWidget(cr, w, shared, state, model);
  }

  if (state.presetOpen) {
    drawPresetList(cr, shared, state);
  }

  cairo_restore(cr);
}

void Panel::drawHeader(cairo_t* cr, const Shared& shared,
                       const PanelState& state) const {
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  const Skin skin = skinFor(model);
  cairo_pattern_t* bar = cairo_pattern_create_linear(0, 0, 0, 62);
  cairo_pattern_add_color_stop_rgb(bar, 0.0, skin.headerBg.r * 1.35,
                                   skin.headerBg.g * 1.35, skin.headerBg.b * 1.35);
  cairo_pattern_add_color_stop_rgb(bar, 1.0, skin.headerBg.r * 0.75,
                                   skin.headerBg.g * 0.75, skin.headerBg.b * 0.75);
  cairo_rectangle(cr, 0, 0, kPanelWidth, 62);
  cairo_set_source(cr, bar);
  cairo_fill(cr);
  cairo_pattern_destroy(bar);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 0.5);
  cairo_line_to(cr, kPanelWidth, 0.5);
  cairo_stroke(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
  cairo_move_to(cr, 0, 61.5);
  cairo_line_to(cr, kPanelWidth, 61.5);
  cairo_stroke(cr);
  text(cr, "COMP SUITE", 520, 38, 19, 1, 0.80, 0.82, 0.84);
  drawPresetBar(cr, shared, state);
}

void Panel::drawPresetBar(cairo_t* cr, const Shared& shared,
                          const PanelState& state) const {
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  const Skin skin = skinFor(model);

  roundRect(cr, kPresetX, kPresetY, kPresetW, kPresetH, 4);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  roundRect(cr, kPresetX + 0.5, kPresetY + 0.5, kPresetW - 1, kPresetH - 1, 4);
  cairo_stroke(cr);

  // Arrows.
  text(cr, "\u2039", kPresetX + 18, kPresetY + 25, 20, 1, 0.72, 0.75, 0.78);
  text(cr, "\u203a", kPresetX + kPresetW - 18, kPresetY + 25, 20, 1, 0.72, 0.75, 0.78);

  char label[96];
  if (state.presetIndex >= 0 && state.presetIndex < kPresetCount) {
    std::snprintf(label, sizeof(label), "%s%s", kPresets[state.presetIndex].name,
                  state.presetDirty ? " *" : "");
  } else {
    std::snprintf(label, sizeof(label), "Init%s", state.presetDirty ? " *" : "");
  }
  text(cr, label, kPresetX + kPresetW * 0.5, kPresetY + 24, 14, 1, 0.90, 0.92, 0.94);
  (void)skin;
}

void Panel::drawPresetList(cairo_t* cr, const Shared& shared,
                           const PanelState& state) const {
  const int model = static_cast<int>(shared.get(P_MODEL) + 0.5);
  const Skin skin = skinFor(model);
  const double h = kListVisible * kListRow + 10;
  roundRect(cr, kPresetX, kListY, kPresetW, h, 5);
  cairo_set_source_rgba(cr, 0.07, 0.08, 0.09, 0.97);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, skin.tabOn, 0.7);
  roundRect(cr, kPresetX + 0.5, kListY + 0.5, kPresetW - 1, h - 1, 5);
  cairo_stroke(cr);

  cairo_save(cr);
  roundRect(cr, kPresetX + 1, kListY + 1, kPresetW - 2, h - 2, 4);
  cairo_clip(cr);

  const char* lastCategory = nullptr;
  double y = kListY + 6 - state.presetScroll * kListRow;
  for (int i : presetOrder()) {
    const Preset& p = kPresets[i];
    if (!lastCategory || std::strcmp(lastCategory, p.category) != 0) {
      lastCategory = p.category;
      text(cr, p.category, kPresetX + 14, y + 15, 11, 0, 0.52, 0.66, 0.80);
      y += kListRow;
      if (y > kListY + h) break;
    }
    if (y + kListRow > kListY && y < kListY + h) {
      if (i == state.presetIndex) {
        roundRect(cr, kPresetX + 6, y + 1, kPresetW - 12, kListRow - 2, 3);
        setRgb(cr, skin.tabOn, 0.85);
        cairo_fill(cr);
      }
      text(cr, p.name, kPresetX + 26, y + 15, 12.5, 0,
           i == state.presetIndex ? 1.0 : 0.86,
           i == state.presetIndex ? 1.0 : 0.88,
           i == state.presetIndex ? 1.0 : 0.90);
      // Which unit this preset uses, so the list reads at a glance.
      static const char* kModelShort[10] = {"76B", "76K", "2A", "3A",  "XP",
                                            "VCA", "CLN", "MOL", "RED", "EZY"};
      text(cr, kModelShort[std::clamp(p.model, 0,
                                      static_cast<int>(comp::Model::Count) - 1)],
           kPresetX + kPresetW - 16,
           y + 15, 10.5, 2, 0.56, 0.60, 0.66);
    }
    y += kListRow;
  }
  cairo_restore(cr);
}

void Panel::applyPreset(Shared& shared, PanelState& state, int index) const {
  if (index < 0 || index >= kPresetCount) {
    return;
  }
  const Preset& p = kPresets[index];
  shared.set(P_MODEL, p.model);
  for (const PresetValue& v : p.values) {
    if (v.param >= 0 && v.param < static_cast<int>(P_COUNT)) {
      shared.set(static_cast<uint32_t>(v.param), v.value);
    }
  }
  state.presetIndex = index;
  state.presetDirty = false;
  state.presetOpen = 0;
}

bool Panel::handlePresetClick(Shared& shared, PanelState& state, double x,
                              double y) {
  // Arrows.
  if (y >= kPresetY && y <= kPresetY + kPresetH) {
    if (x >= kPresetX && x <= kPresetX + 36) {
      const int next = state.presetIndex <= 0 ? 0 : state.presetIndex - 1;
      applyPreset(shared, state, next);
      return true;
    }
    if (x >= kPresetX + kPresetW - 36 && x <= kPresetX + kPresetW) {
      const int next = std::min(kPresetCount - 1, state.presetIndex + 1);
      applyPreset(shared, state, next);
      return true;
    }
    if (x > kPresetX + 36 && x < kPresetX + kPresetW - 36) {
      state.presetOpen = state.presetOpen ? 0 : 1;
      if (state.presetOpen) {
        state.presetScroll = 0;  // always open at the top of the list
      }
      return true;
    }
  }

  if (!state.presetOpen) {
    return false;
  }
  const double h = kListVisible * kListRow + 10;
  if (x < kPresetX || x > kPresetX + kPresetW || y < kListY || y > kListY + h) {
    state.presetOpen = 0;
    return true;  // click outside closes
  }

  // Walk the same layout used for drawing.
  double rowY = kListY + 6 - state.presetScroll * kListRow;
  const char* lastCategory = nullptr;
  for (int i : presetOrder()) {
    if (!lastCategory || std::strcmp(lastCategory, kPresets[i].category) != 0) {
      lastCategory = kPresets[i].category;
      rowY += kListRow;
    }
    if (y >= rowY && y < rowY + kListRow) {
      applyPreset(shared, state, i);
      return true;
    }
    rowY += kListRow;
  }
  return true;
}

void Panel::drawBody(cairo_t* cr, int model) const {
  const Skin skin = skinFor(model);
  cairo_pattern_t* pat = cairo_pattern_create_linear(0, 122, 0, 604);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, skin.bodyTop.r, skin.bodyTop.g, skin.bodyTop.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, skin.bodyBottom.r, skin.bodyBottom.g, skin.bodyBottom.b);
  roundRect(cr, 16, 122, 1008, 482, 6);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);

  // Grain: tile the noise across the faceplate.
  if (grain_) {
    cairo_pattern_t* grain = cairo_pattern_create_for_surface(grain_);
    cairo_pattern_set_extend(grain, CAIRO_EXTEND_REPEAT);
    cairo_save(cr);
    cairo_rectangle(cr, 17, 123, 1006, 480);
    cairo_clip(cr);
    cairo_set_source(cr, grain);
    cairo_paint_with_alpha(cr, 0.018);
    cairo_restore(cr);
    cairo_pattern_destroy(grain);
  }

  // Inner shadow under the top edge, so the panel sits inside the frame.
  cairo_pattern_t* inner = cairo_pattern_create_linear(0, 123, 0, 165);
  cairo_pattern_add_color_stop_rgba(inner, 0.0, 0.0, 0.0, 0.0, 0.30);
  cairo_pattern_add_color_stop_rgba(inner, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_rectangle(cr, 17, 123, 1006, 42);
  cairo_set_source(cr, inner);
  cairo_fill(cr);
  cairo_pattern_destroy(inner);

  // Bevel: a light top edge and a dark bottom edge make the faceplate read
  // as a physical panel instead of a flat rectangle.
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, {1.0, 1.0, 1.0}, 0.16);
  cairo_move_to(cr, 18, 123.5);
  cairo_line_to(cr, 1022, 123.5);
  cairo_stroke(cr);
  setRgb(cr, {0.0, 0.0, 0.0}, 0.22);
  cairo_move_to(cr, 18, 602.5);
  cairo_line_to(cr, 1022, 602.5);
  cairo_stroke(cr);

  // Screws in the corners, as on the hardware front panels.
  for (double sx : {32.0, 1008.0}) {
    for (double sy : {138.0, 588.0}) {
      cairo_arc(cr, sx, sy, 4.5, 0, 2 * kPi);
      setRgb(cr, skin.screw, 0.9);
      cairo_fill(cr);
    }
  }

  // Centre meter window, inset with a shadow and a highlight so it sits
  // below the surface.
  roundRect(cr, 170, 136, 570, 204, 4);
  setRgb(cr, skin.hasBlueBlock ? skin.meterBlock : skin.meterFrame);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, {0.0, 0.0, 0.0}, 0.30);
  roundRect(cr, 170.5, 136.5, 569, 203, 4);
  cairo_stroke(cr);

  // Footer: a shallow band with the model name, like the reference's
  // "Compressor" strip.
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
  cairo_rectangle(cr, 0, 604, kPanelWidth, 36);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 604.5);
  cairo_line_to(cr, kPanelWidth, 604.5);
  cairo_stroke(cr);
  text(cr, skin.caption, kPanelWidth * 0.5, 629, 16, 1, 0.86, 0.88, 0.90);
}


// Static input/output transfer for the Curve display, using each model's own
// threshold, ratio and knee.  Illustrative rather than sample-exact.
double curveTransferDb(int model, double inDb, const Shared& shared) {
  double threshold = -40.0 + 40.0 * shared.get(P_THRESHOLD);
  double knee = 18.0 * shared.get(P_KNEE);
  static const double kRatios[9] = {1.5, 2.0, 3.0, 4.0, 6.0,
                                    8.0, 12.0, 20.0, 20.0};
  const int step = std::clamp(
      static_cast<int>(std::lround(shared.get(P_RATIO))), 0, 8);
  double ratio = kRatios[step];
  double slope = 1.0 - 1.0 / std::max(ratio, 1.0);
  switch (model) {
    case kBlue76:
    case kBlack76:
      // Feed-forward detector and (ratio-1)/ratio slope, as in the model.
      threshold = -18.0;
      knee = model == kBlue76 ? 1.0 : 2.5;
      slope = 1.0 - 1.0 / std::max(ratio, 1.0);
      if (step >= 8) {
        slope *= 1.35;  // All In's over-compression factor
      }
      break;
    case kOpto2A:
    case kOpto3A:
      threshold = -2.0 - 34.0 * shared.get(P_THRESHOLD);
      knee = 6.0;
      break;
    case kXpress:
      threshold = -20.0;
      knee = 2.0;
      break;
    case kEasy: {
      const bool squish = shared.get(P_EASY_MODE) < 0.5;
      ratio = squish ? 8.0 : 2.5;
      knee = squish ? 3.0 : 9.0;
      slope = 1.0 - 1.0 / ratio;
      break;
    }
    case kRed: {
      // Red's ratio control has six positions, and its knee tops out at 12 dB.
      static const double kRedRatios[6] = {1.5, 2.0, 3.0, 5.0, 7.0, 10.0};
      ratio = kRedRatios[std::clamp(step, 0, 5)];
      slope = 1.0 - 1.0 / ratio;
      knee = 12.0 * shared.get(P_KNEE);
      break;
    }
    default:
      break;
  }
  const double over = inDb - threshold;
  double gr = 0.0;
  if (knee <= 0.0 || over > 0.5 * knee) {
    gr = std::max(slope * over, 0.0);
  } else if (over > -0.5 * knee) {
    const double t = over + 0.5 * knee;
    gr = std::max(slope * t * t / (2.0 * knee), 0.0);
  }
  return inDb - gr;
}

void Panel::drawMeter(cairo_t* cr, const Shared& shared, const PanelState& state,
                      int model) const {
  const Skin skin = skinFor(model);
  const int mode = meterModeFor(shared, model);
  const double fx = 182, fy = 172, fw = 546, fh = 160;

  roundRect(cr, fx, fy, fw, fh, 3);
  if (mode == 5) {
    cairo_pattern_t* pat = cairo_pattern_create_linear(0, fy, 0, fy + fh);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, skin.meterFace.r,
                                     skin.meterFace.g, skin.meterFace.b);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, skin.meterFace.r * 0.8,
                                     skin.meterFace.g * 0.8,
                                     skin.meterFace.b * 0.8);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    constexpr double lo = -60.0;
    constexpr double hi = 0.0;
    auto px = [&](double db) { return fx + fw * (db - lo) / (hi - lo); };
    auto py = [&](double db) { return fy + fh * (hi - db) / (hi - lo); };

    cairo_set_line_width(cr, 1.0);
    setRgb(cr, skin.meterScale, 0.20);
    for (int d = -48; d <= 0; d += 12) {
      cairo_move_to(cr, px(d), fy);
      cairo_line_to(cr, px(d), fy + fh);
      cairo_stroke(cr);
      cairo_move_to(cr, fx, py(d));
      cairo_line_to(cr, fx + fw, py(d));
      cairo_stroke(cr);
    }

    // Unity reference.
    setRgb(cr, skin.meterScale, 0.45);
    cairo_move_to(cr, px(lo), py(lo));
    cairo_line_to(cr, px(hi), py(hi));
    cairo_stroke(cr);

    // Transfer curve.
    setRgb(cr, skin.meterNeedle, 0.95);
    cairo_set_line_width(cr, 1.8);
    bool first = true;
    for (double x = lo; x <= hi + 1e-6; x += 0.5) {
      const double y = curveTransferDb(model, x, shared);
      if (first) {
        cairo_move_to(cr, px(x), py(std::clamp(y, lo, hi)));
        first = false;
      } else {
        cairo_line_to(cr, px(x), py(std::clamp(y, lo, hi)));
      }
    }
    cairo_stroke(cr);

    // Live operating point.  When the detector is fed from the external
    // side-chain, the curve's input axis is that source, not the programme.
    const bool scDetector =
        shared.get(P_SC_SOURCE) > 0.5 &&
        shared.scActive.load(std::memory_order_relaxed);
    const double inDb =
        scDetector ? shared.scPeakDb.load(std::memory_order_relaxed)
                   : shared.inPeakDb.load(std::memory_order_relaxed);
    const double outDb = inDb - shared.grDb.load(std::memory_order_relaxed);
    if (inDb > lo && inDb <= hi) {
      cairo_arc(cr, px(inDb), py(std::clamp(outDb, lo, hi)), 3.5, 0, 2 * kPi);
      setRgb(cr, skin.meterNeedle);
      cairo_fill(cr);
    }

    // Axis numbers: dB along the bottom (in) and left (out).
    char tick[8];
    for (int d = -48; d <= 0; d += 12) {
      std::snprintf(tick, sizeof(tick), "%d", d);
      text(cr, tick, px(d), fy + fh - 4, 8, 1, skin.meterScale.r,
           skin.meterScale.g, skin.meterScale.b);
      text(cr, tick, fx + 10, py(d) + 3, 8, 0, skin.meterScale.r,
           skin.meterScale.g, skin.meterScale.b);
    }

    text(cr, "TRANSFER", fx + 30, fy + fh - 10, 12, 0, skin.meterScale.r,
         skin.meterScale.g, skin.meterScale.b);
    text(cr, scDetector ? "SC" : "IN", fx + fw - 12, fy + fh - 10, 10, 2,
         skin.meterScale.r, skin.meterScale.g, skin.meterScale.b);
    text(cr, "OUT", fx + 8, fy + 16, 10, 0, skin.meterScale.r, skin.meterScale.g,
         skin.meterScale.b);
    return;
  }

  if (mode == 3) {
    // Graph: gain reduction history, drawn from the top down.
    cairo_pattern_t* pat = cairo_pattern_create_linear(0, fy, 0, fy + fh);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, skin.meterFace.r, skin.meterFace.g, skin.meterFace.b);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, skin.meterFace.r * 0.8, skin.meterFace.g * 0.8,
                                     skin.meterFace.b * 0.8);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    const int n = PanelState::kHistory;
    cairo_move_to(cr, fx, fy);
    for (int i = 0; i < n; ++i) {
      const int idx = (state.grHead + i) % n;
      const double gr = clamp01(state.grHistory[idx] / 20.0);
      const double px = fx + fw * i / (n - 1);
      cairo_line_to(cr, px, fy + fh * gr);
    }
    cairo_line_to(cr, fx + fw, fy);
    cairo_close_path(cr);
    setRgb(cr, skin.meterNeedle, 0.85);
    cairo_fill(cr);
    text(cr, "GAIN REDUCTION", fx + fw - 12, fy + fh - 10, 12, 2, skin.meterScale.r,
         skin.meterScale.g, skin.meterScale.b);
    return;
  }

  cairo_pattern_t* face = cairo_pattern_create_linear(0, fy, 0, fy + fh);
  cairo_pattern_add_color_stop_rgb(face, 0.0, skin.meterFace.r, skin.meterFace.g, skin.meterFace.b);
  cairo_pattern_add_color_stop_rgb(face, 1.0, skin.meterFace.r * 0.88, skin.meterFace.g * 0.88,
                                   skin.meterFace.b * 0.88);
  cairo_set_source(cr, face);
  cairo_fill(cr);
  cairo_pattern_destroy(face);

  // Glass: a faint reflection across the upper part of the face.
  cairo_pattern_t* glass = cairo_pattern_create_linear(0, fy, 0, fy + fh * 0.55);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.10);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, fx, fy, fw, fh * 0.55);
  cairo_set_source(cr, glass);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);

  if (mode == 4) {
    text(cr, "OFF", fx + fw * 0.5, fy + fh * 0.5 + 8, 22, 1, skin.meterScale.r,
         skin.meterScale.g, skin.meterScale.b);
    return;
  }

  // Needle pivot sits below the window; the arc and scale sit in the face.
  cairo_save(cr);
  cairo_rectangle(cr, fx, fy, fw, fh);
  cairo_clip(cr);
  const double px = fx + fw * 0.5;
  const double py = fy + fh + 120;
  const double radius = 200;
  const double spread = 40.0 * kPi / 180.0;
  auto angleFor = [&](double f) { return -kPi / 2 + (-spread + 2 * spread * f); };

  // Arc and red zone on the output scale.
  // Minor ticks between the numbered marks, like the reference scale.
  cairo_set_source_rgba(cr, skin.meterScale.r, skin.meterScale.g,
                        skin.meterScale.b, 0.45);
  cairo_set_line_width(cr, 1.0);
  for (int i = 0; i <= 40; ++i) {
    const double f = i / 40.0;
    const double a = angleFor(f);
    const double ca = std::cos(a);
    const double sa = std::sin(a);
    const bool major = (i % 4) == 0;
    cairo_move_to(cr, px + ca * (radius - 1), py + sa * (radius - 1));
    cairo_line_to(cr, px + ca * (radius - (major ? 10 : 5)),
                  py + sa * (radius - (major ? 10 : 5)));
    cairo_stroke(cr);
  }

  cairo_set_line_width(cr, 1.4);
  setRgb(cr, skin.meterScale, 0.95);
  cairo_new_path(cr);
  cairo_arc(cr, px, py, radius, angleFor(0), angleFor(1));
  cairo_stroke(cr);

  const bool grScale = mode == 0;
  const int labelCount = grScale ? 11 : 10;
  for (int i = 0; i < labelCount; ++i) {
    const double f = grScale ? i / 10.0 : kMeterFractionsVu[i];
    const double a = angleFor(f);
    const double ca = std::cos(a);
    const double sa = std::sin(a);
    cairo_move_to(cr, px + ca * (radius - 6), py + sa * (radius - 6));
    cairo_line_to(cr, px + ca * (radius - 18), py + sa * (radius - 18));
    cairo_stroke(cr);
    const char* label = grScale ? kMeterLabelsGr[i] : kMeterLabelsVu[i];
    text(cr, label, px + ca * (radius - 34), py + sa * (radius - 34) + 4, 14, 1,
         skin.meterScale.r, skin.meterScale.g, skin.meterScale.b);
  }
  if (!grScale) {
    cairo_set_source_rgba(cr, 0.75, 0.10, 0.10, 0.9);
    cairo_new_path(cr);
    cairo_arc(cr, px, py, radius - 4, angleFor(0.870), angleFor(1.0));
    cairo_set_line_width(cr, 5);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1.2);
  }

  // Needle.
  const double na = angleFor(clamp01(state.needle));
  cairo_set_line_width(cr, 2.2);
  setRgb(cr, skin.meterNeedle);
  cairo_move_to(cr, px, py);
  cairo_line_to(cr, px + std::cos(na) * (radius - 8), py + std::sin(na) * (radius - 8));
  cairo_stroke(cr);

  cairo_restore(cr);
  const char* unit = grScale ? "dB GR" : "VU";
  text(cr, unit, fx + fw - 12, fy + fh - 10, 12, 2, skin.meterScale.r, skin.meterScale.g,
       skin.meterScale.b);
}

void Panel::drawLeds(cairo_t* cr, const Shared& shared) const {
  const float gr = shared.grDb.load(std::memory_order_relaxed);
  const int lit = std::min(12, static_cast<int>(std::lround(gr / 2.0f)));
  for (int i = 0; i < 12; ++i) {
    const double y = 146 + i * 16;
    Rgb c;
    if (i < 4) {
      c = {0.27, 0.88, 0.42};
    } else if (i < 8) {
      c = {0.96, 0.82, 0.29};
    } else {
      c = {1.0, 0.30, 0.24};
    }
    const bool on = i < lit;
    roundRect(cr, 754, y, 16, 12, 2);
    if (on) {
      setRgb(cr, c);
    } else {
      setRgb(cr, {c.r * 0.18, c.g * 0.18, c.b * 0.18});
    }
    cairo_fill(cr);
  }
}

void Panel::drawLevelMeter(cairo_t* cr, double x, double y, double w, double h,
                           double dbL, double dbR, double minDb, double maxDb,
                           int model, bool labelsRight) const {
  const Skin skin = skinFor(model);
  const double gap = 4.0;
  const double pairWidth = w * 2.0 + gap;
  const double outer = labelsRight ? x + pairWidth : x;

  // Two wells, one per channel, in a shared recess.
  for (int ch = 0; ch < 2; ++ch) {
    const double bx = x + ch * (w + gap);
    cairo_rectangle(cr, bx, y, w, h);
    cairo_set_source_rgb(cr, 0.055, 0.055, 0.06);
    cairo_fill(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.65);
    cairo_rectangle(cr, bx + 0.5, y + 0.5, w - 1, h - 1);
    cairo_stroke(cr);
  }

  // Scale ticks and labels beside the pair.
  static const double kMarks[] = {3, 0, -6, -12, -24, -36, -48};
  for (double m : kMarks) {
    const double f = (m - minDb) / (maxDb - minDb);
    const double ty = y + h - h * f;
    if (ty < y - 1 || ty > y + h + 1) {
      continue;
    }
    setRgb(cr, skin.legend, 0.55);
    cairo_set_line_width(cr, 1.0);
    const double tx0 = labelsRight ? outer : outer - 4;
    const double tx1 = labelsRight ? outer + 4 : outer;
    cairo_move_to(cr, tx0, ty);
    cairo_line_to(cr, tx1, ty);
    cairo_stroke(cr);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%+.0f", m);
    text(cr, buf, labelsRight ? outer + 8 : outer - 8, ty + 3, 9,
         labelsRight ? 0 : 2, skin.legend.r, skin.legend.g, skin.legend.b);
  }

  const double levels[2] = {dbL, dbR};
  for (int ch = 0; ch < 2; ++ch) {
    const double f = clamp01((levels[ch] - minDb) / (maxDb - minDb));
    const double fh = h * f;
    if (fh <= 0.5) {
      continue;
    }
    const double bx = x + ch * (w + gap);
    cairo_pattern_t* pat = cairo_pattern_create_linear(0, y + h, 0, y);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.24, 0.48, 0.62);
    cairo_pattern_add_color_stop_rgb(pat, 0.82, 0.42, 0.70, 0.82);
    cairo_pattern_add_color_stop_rgb(pat, 0.96, 0.93, 0.86, 0.55);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.95, 0.35, 0.28);
    cairo_rectangle(cr, bx, y + h - fh, w, fh);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
  }
}

void Panel::drawWidget(cairo_t* cr, const Widget& w, const Shared& shared,
                       const PanelState& state, int model) const {
  const Skin skin = skinFor(model);
  switch (w.kind) {
    case Kind::Power: {
      const bool on = shared.get(P_ENABLED) > 0.5;
      cairo_arc(cr, w.x, w.y, w.r, 0, 2 * kPi);
      setRgb(cr, {0.12, 0.12, 0.12});
      cairo_fill(cr);
      cairo_arc(cr, w.x, w.y, w.r - 1.5, 0, 2 * kPi);
      setRgb(cr, on ? Rgb{0.36, 0.86, 0.46} : Rgb{0.42, 0.42, 0.42});
      cairo_set_line_width(cr, 2.2);
      cairo_new_path(cr);
      cairo_arc(cr, w.x, w.y, 7, -kPi / 2 + 0.6, -kPi / 2 + 2 * kPi - 0.6);
      cairo_stroke(cr);
      cairo_move_to(cr, w.x, w.y - 9);
      cairo_line_to(cr, w.x, w.y - 1);
      cairo_stroke(cr);
      break;
    }
    case Kind::ModelTab: {
      const bool selected = std::fabs(shared.get(P_MODEL) - w.value) < 0.5;
      roundRect(cr, w.x, w.y, w.w, w.h, 4);
      setRgb(cr, selected ? skin.tabOn : skin.tabOff);
      cairo_fill(cr);
      const Rgb tc = selected ? skin.tabTextOn : skin.tabTextOff;
      text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 5, 15, 1, tc.r, tc.g, tc.b);
      break;
    }
    case Kind::PageTab: {
      const bool selected = static_cast<int>(w.value) == state.rightPage;
      roundRect(cr, w.x, w.y, w.w, w.h, 3);
      setRgb(cr, selected ? skin.tabOn : skin.tabOff);
      cairo_fill(cr);
      const Rgb tc = selected ? skin.tabTextOn : skin.tabTextOff;
      text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 4, 11, 1, tc.r, tc.g, tc.b);
      break;
    }
    case Kind::MeterSegment: {
      const bool selected = std::fabs(shared.get(P_METER) - w.value) < 0.5;
      roundRect(cr, w.x, w.y, w.w - 2, w.h, 3);
      setRgb(cr, selected ? Rgb{0.90, 0.90, 0.90} : Rgb{0.12, 0.12, 0.12});
      cairo_fill(cr);
      const Rgb tc = selected ? Rgb{0.05, 0.05, 0.05} : Rgb{0.70, 0.70, 0.70};
      text(cr, w.label, w.x + (w.w - 2) * 0.5, w.y + 14, 11, 1, tc.r, tc.g, tc.b);
      break;
    }
    case Kind::Knob: {
      drawKnob(cr, w, knobValue(shared, w), model);
      break;
    }
    case Kind::Button: {
      drawButton(cr, w, isLit(shared, w), model);
      break;
    }
    case Kind::Toggle: {
      drawToggle(cr, w, shared.get(w.param) > 0.5, model);
      break;
    }
    case Kind::Pill: {
      drawPill(cr, w, shared.get(w.param) > 0.5, model);
      break;
    }
    case Kind::Cycle: {
      static const char* const kOsNames[] = {"OS OFF", "OS 2X", "OS 4X"};
      const int v = std::clamp(static_cast<int>(shared.get(w.param) + 0.5),
                               0, 2);
      Widget copy = w;
      copy.label = kOsNames[v];
      drawPill(cr, copy, v != 0, model);
      break;
    }
  }
}

// A compact latching button for the header utility strip.
void Panel::drawPill(cairo_t* cr, const Widget& w, bool on, int model) const {
  const Skin skin = skinFor(model);
  roundRect(cr, w.x, w.y, w.w, w.h, 5);
  setRgb(cr, on ? skin.btnOn : skin.btnOff);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, {0.0, 0.0, 0.0}, 0.35);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1.0, w.h - 1.0, 5);
  cairo_stroke(cr);
  const Rgb tc = on ? skin.btnTextOn : skin.btnText;
  text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 3.5, 10, 1, tc.r,
       tc.g, tc.b);
}

void Panel::drawKnob(cairo_t* cr, const Widget& w, double value, int model) const {
  const Skin skin = skinFor(model);
  const double cx = w.x;
  const double cy = w.y;
  const double r = w.r;

  // Ticks: thin and quiet, they are a guide, not decoration.
  setRgb(cr, skin.legendDim, 0.55);
  cairo_set_line_width(cr, 1.0);
  for (int i = 0; i <= 10; ++i) {
    const double a = knobAngle(i / 10.0);
    cairo_move_to(cr, cx + std::cos(a) * r * 1.10, cy + std::sin(a) * r * 1.10);
    cairo_line_to(cr, cx + std::cos(a) * r * 1.19, cy + std::sin(a) * r * 1.19);
    cairo_stroke(cr);
  }

  // Continuous controls with no scale get direction marks.
  const ParamSpec& paramSpec = kParamSpecs[w.param];
  if (w.scale.empty() && !paramSpec.stepped) {
    const double a0 = knobAngle(0.0);
    const double a1 = knobAngle(1.0);
    // U+2212, not a hyphen: a hyphen is short and reads as a speck next to
    // the plus at the same point size.
    text(cr, "\u2212", cx + std::cos(a0) * (r * 1.44),
         cy + std::sin(a0) * (r * 1.44) + 4, 12, 1, skin.legendDim.r,
         skin.legendDim.g, skin.legendDim.b);
    text(cr, "+", cx + std::cos(a1) * (r * 1.44),
         cy + std::sin(a1) * (r * 1.44) + 4, 12, 1, skin.legendDim.r,
         skin.legendDim.g, skin.legendDim.b);
  }

  // Scale legends.
  const size_t n = w.scale.size();
  for (size_t i = 0; i < n; ++i) {
    const double f = n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
    const double a = knobAngle(f);
    const double lx = cx + std::cos(a) * (r * w.scaleRadius);
    const double ly = cy + std::sin(a) * (r * w.scaleRadius);
    text(cr, w.scale[i], lx, ly + 4, 10.5, 1, skin.legend.r, skin.legend.g,
         skin.legend.b);
  }

  // A soft contact shadow, not a drop shadow.
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.18);
  cairo_arc(cr, cx + 0.5, cy + 1.5, r * 1.03, 0, 2 * kPi);
  cairo_fill(cr);

  // Body: vertical gradient, darker at the base, with a hairline rim.
  cairo_pattern_t* body = cairo_pattern_create_linear(0, cy - r, 0, cy + r);
  const Rgb top = mix(skin.knobSkirt, {1, 1, 1}, 0.16);
  const Rgb bot = mix(skin.knobSkirt, {0, 0, 0}, 0.30);
  cairo_pattern_add_color_stop_rgb(body, 0.0, top.r, top.g, top.b);
  cairo_pattern_add_color_stop_rgb(body, 1.0, bot.r, bot.g, bot.b);
  cairo_arc(cr, cx, cy, r, 0, 2 * kPi);
  cairo_set_source(cr, body);
  cairo_fill(cr);
  cairo_pattern_destroy(body);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
  cairo_arc(cr, cx, cy, r - 0.5, 0, 2 * kPi);
  cairo_stroke(cr);

  // Cap: a restrained radial highlight, upper left.
  cairo_pattern_t* cap = cairo_pattern_create_radial(
      cx - r * 0.35, cy - r * 0.40, r * 0.05, cx, cy, r * 0.98);
  const Rgb hi = mix(skin.knobCap, {1, 1, 1}, 0.24);
  cairo_pattern_add_color_stop_rgb(cap, 0.0, hi.r, hi.g, hi.b);
  cairo_pattern_add_color_stop_rgb(cap, 0.75, skin.knobCap.r, skin.knobCap.g,
                                   skin.knobCap.b);
  cairo_pattern_add_color_stop_rgb(cap, 1.0, skin.knobCapEdge.r,
                                   skin.knobCapEdge.g, skin.knobCapEdge.b);
  cairo_arc(cr, cx, cy, r * 0.82, 0, 2 * kPi);
  cairo_set_source(cr, cap);
  cairo_fill(cr);
  cairo_pattern_destroy(cap);

  // Knurl only on the ring, not the whole face.
  if (skin.knurled) {
    cairo_set_line_width(cr, 0.8);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
    for (int k = 0; k < 60; ++k) {
      const double a = k * 2 * kPi / 60.0;
      cairo_move_to(cr, cx + std::cos(a) * r * 0.86, cy + std::sin(a) * r * 0.86);
      cairo_line_to(cr, cx + std::cos(a) * r * 0.99, cy + std::sin(a) * r * 0.99);
      cairo_stroke(cr);
    }
  }

  // Indicator: normalised against the parameter's own range, not assumed
  // to be 0..1 (the ratio ladder runs 0..8).
  const ParamSpec& spec = kParamSpecs[w.param];
  const double vmax = w.steps > 1 ? w.steps - 1 : spec.max;
  const double span = vmax - spec.min;
  const double frac = span > 0.0 ? (std::min(value, vmax) - spec.min) / span : 0.0;
  const double a = knobAngle(clamp01(frac));
  cairo_set_line_width(cr, 2.0);
  setRgb(cr, skin.knobIndicator);
  cairo_move_to(cr, cx + std::cos(a) * r * 0.26, cy + std::sin(a) * r * 0.26);
  cairo_line_to(cr, cx + std::cos(a) * r * 0.78, cy + std::sin(a) * r * 0.78);
  cairo_stroke(cr);

  const double labelY = w.labelBelow ? cy + r * 1.44 + 20 : cy - r * 1.44 - 18;
  text(cr, w.label, cx, labelY, 11, 1, skin.legend.r, skin.legend.g, skin.legend.b);
}

void Panel::drawButton(cairo_t* cr, const Widget& w, bool lit, int model) const {
  const Skin skin = skinFor(model);
  const Rgb base = lit ? skin.btnOn : skin.btnOff;

  // Flat with a hairline border: reads as a control, not a plastic key.
  cairo_pattern_t* pat = cairo_pattern_create_linear(0, w.y, 0, w.y + w.h);
  const Rgb hi = mix(base, {1, 1, 1}, lit ? 0.10 : 0.07);
  const Rgb lo = mix(base, {0, 0, 0}, 0.10);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, hi.r, hi.g, hi.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, lo.r, lo.g, lo.b);
  roundRect(cr, w.x, w.y, w.w, w.h, 3);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);

  setRgb(cr, mix(base, lit ? Rgb{1, 1, 1} : Rgb{0, 0, 0}, 0.35), 0.9);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 3);
  cairo_stroke(cr);

  const Rgb tc = lit ? skin.btnTextOn : skin.btnText;
  text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 4, 11.5, 1, tc.r, tc.g, tc.b);
}

void Panel::drawToggle(cairo_t* cr, const Widget& w, bool on, int model) const {
  const Skin skin = skinFor(model);
  roundRect(cr, w.x - w.w * 0.5, w.y, w.w, w.h, 4);
  setRgb(cr, mix(skin.knobSkirt, {0, 0, 0}, 0.3));
  cairo_fill(cr);
  const double leverY = on ? w.y + 12 : w.y + w.h - 12;
  cairo_arc(cr, w.x, leverY, 8, 0, 2 * kPi);
  setRgb(cr, skin.knobCap);
  cairo_fill(cr);
  text(cr, w.buttonText[0], w.x, w.y - 8, 12, 1, skin.legend.r, skin.legend.g, skin.legend.b);
  text(cr, w.buttonText[1], w.x, w.y + w.h + 18, 12, 1, skin.legend.r, skin.legend.g, skin.legend.b);
}

// ---------------------------------------------------------------------------
// Layout introspection (used by the layout test)
// ---------------------------------------------------------------------------

std::vector<Panel::WidgetRect> Panel::widgetRects() const {
  std::vector<WidgetRect> out;
  out.reserve(widgets_.size());
  for (const Widget& w : widgets_) {
    WidgetRect r;
    r.param = w.param;
    r.label = w.label;
    r.knob = w.kind == Kind::Knob;
    r.toggle = w.kind == Kind::Toggle;
    r.r = w.r;
    if (w.kind == Kind::Knob) {
      r.x = w.x - w.r;
      r.y = w.y - w.r;
      r.w = w.r * 2.0;
      r.h = w.r * 2.0;
    } else if (w.kind == Kind::Toggle) {
      r.x = w.x - w.w * 0.5;
      r.y = w.y;
      r.w = w.w;
      r.h = w.h;
    } else {
      r.x = w.x;
      r.y = w.y;
      r.w = w.w;
      r.h = w.h;
    }
    out.push_back(r);
  }
  return out;
}

} // namespace comp
