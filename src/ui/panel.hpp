// Comp Suite — panel.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "params.hpp"
#include "user_presets.hpp"

namespace comp {

constexpr int kPanelWidth = 1040;
constexpr int kPanelHeight = 640;

// UI-only state.  The meter mode lives in the host parameter P_METER so it
// is saved with the preset; the right-hand page does not.
struct PanelState {
  int rightPage = 1; // 0 = Side Chain, 1 = Output
  int presetIndex = -1;
  bool presetDirty = false;
  int presetOpen = 0;
  int presetScroll = 0;
  bool savePreset = false;
  bool renamePreset = false;
  bool deletePresetConfirm = false;
  bool presetOverwriteConfirm = false;
  std::string presetText;
  bool presetTextFresh = false;
  std::string presetError;
  std::string presetNotice;
  double presetNoticeUntil = 0.0;
  std::vector<UserPreset> userPresets;
  float needle = 0.0f;
  static constexpr int kHistory = 300;
  float grHistory[kHistory] = {};
  int grHead = 0;
  float inLevel = 0.0f;
  float outLevel = 0.0f;
  float inLevelL = 0.0f;
  float inLevelR = 0.0f;
  float outLevelL = 0.0f;
  float outLevelR = 0.0f;
  float outPeakHold = 0.0f;
};

// Drawing and hit-testing for the whole plugin face.  The layout depends on
// the selected model, and only the controls that model has are created.
class Panel {
public:
  Panel();
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Loads Jost from the given directory, falling back to a system font.
  void loadFont(const std::string& dir);

  // Advances needles, histories, and peak holds.  Call about 30 times a second.
  void tick(const Shared& shared, PanelState& state) const;

  void draw(cairo_t* cr, const Shared& shared, const PanelState& state);

  // Hit-testing against the layout from the last draw.  Each returns true
  // when a control changed.
  bool pointerDown(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  bool pointerMove(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  void pointerUp();
  bool handlePresetClick(Shared& shared, PanelState& state, double x, double y);
  void applyPreset(Shared& shared, PanelState& state, int index) const;
  void drawPresetBar(cairo_t* cr, const Shared& shared,
                     const PanelState& state) const;
  void drawPresetList(cairo_t* cr, const Shared& shared,
                      const PanelState& state) const;
  int presetCount(const PanelState& state) const;
  const char* presetName(const PanelState& state, int index) const;
  const char* presetCategory(const PanelState& state, int index) const;
  int presetModel(const PanelState& state, int index) const;
  std::vector<int> presetOrder(const PanelState& state) const;
  void reloadUserPresets(PanelState& state);
  void rescanUserPresets(PanelState& state);
  void setPresetNotice(PanelState& state, const std::string& text) const;
  void duplicatePreset(Shared& shared, PanelState& state);
  void exportPreset(const Shared& shared, PanelState& state) const;
  bool importPresetFile(Shared& shared, PanelState& state,
                        const std::string& path);
  bool importDroppedFile(Shared& shared, PanelState& state,
                         const std::string& path);
  bool commitPresetName(Shared& shared, PanelState& state);
  double presetResetValue(const PanelState& state, uint32_t param) const;
  bool wheel(Shared& shared, PanelState& state, double x, double y, double dy);
  void beginTextEntry(Shared& shared, PanelState& state, double x, double y);
  static constexpr int kTextEnter = 1;
  static constexpr int kTextEscape = 2;
  static constexpr int kTextBackspace = 3;
  bool textKey(Shared& shared, PanelState& state, int key, char ch);

  bool dragging() const { return dragParam_ >= 0; }

  // Geometry of the current layout, for the layout test.  Knobs report their
  // bounding box in x/y/w/h with r set; toggles are centred on x.
  struct WidgetRect {
    uint32_t param = 0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double r = 0.0;
    const char* label = "";
    bool knob = false;
    bool toggle = false;
  };
  std::vector<WidgetRect> widgetRects() const;

private:
  enum class Kind {
    Knob,
    Button,
    Toggle,
    ModelTab,
    MeterSegment,
    Power,
    PageTab,
    Pill,
    Cycle,
  };

  struct Widget {
    Kind kind;
    uint32_t param = 0;
    double value = 0.0;   // value written by Button / Tab / Segment / Toggle
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double r = 0.0;       // knob radius
    int steps = 0;        // 0 = use the parameter's range; else N positions
    double scaleRadius = 1.44;  // legend ring, as a multiple of the radius
    const char* label = "";
    std::vector<const char*> scale;
    bool labelBelow = false;
    std::vector<const char*> buttonText;
  };

  void layout(const Shared& shared, const PanelState& state);
  void drawHeader(cairo_t* cr, const Shared& shared,
                  const PanelState& state) const;
  void drawBody(cairo_t* cr, int model) const;
  void drawMeter(cairo_t* cr, const Shared& shared, const PanelState& state,
                 int model) const;
  void drawLeds(cairo_t* cr, const Shared& shared) const;
  void drawLevelMeter(cairo_t* cr, double x, double y, double w, double h,
                      double dbL, double dbR, double minDb, double maxDb,
                      int model, bool labelsRight) const;
  void drawWidget(cairo_t* cr, const Widget& w, const Shared& shared,
                  const PanelState& state, int model) const;
  void drawKnob(cairo_t* cr, const Widget& w, double value, int model) const;
  void drawTextEntry(cairo_t* cr, const Shared& shared, int model) const;
  bool commitTextEntry(Shared& shared, PanelState& state);
  bool hitTextEntry(double x, double y) const;
  void drawButton(cairo_t* cr, const Widget& w, bool lit, int model) const;
  void drawToggle(cairo_t* cr, const Widget& w, bool on, int model) const;
  void drawPill(cairo_t* cr, const Widget& w, bool on, int model) const;
  void text(cairo_t* cr, const char* s, double x, double y, double size,
            int align, double r, double g, double b) const;

  double knobValue(const Shared& shared, const Widget& w) const;
  bool isLit(const Shared& shared, const Widget& w) const;
  bool hitWidget(const Widget& w, double x, double y) const;

  std::vector<Widget> widgets_;
  int dragParam_ = -1;
  double dragStartY_ = 0.0;
  double dragStartValue_ = 0.0;
  int dragSteps_ = 0;

  // Double-click-to-reset bookkeeping.  A second press on the same knob
  // inside kDoubleClickMs restores the current preset's value.
  int lastClickParam_ = -1;
  double lastClickTime_ = 0.0;
  static constexpr double kDoubleClickMs = 400.0;

  bool editing_ = false;
  uint32_t editParam_ = 0;
  std::string editText_;
  double editBoxX_ = 0.0;
  double editBoxY_ = 0.0;
  static constexpr double kEditW = 150.0;
  static constexpr double kEditH = 32.0;

  cairo_surface_t* grain_ = nullptr;  // tiled noise for panel texture
  void* ft_ = nullptr;
  void* ftFace_ = nullptr;
  cairo_font_face_t* font_ = nullptr;
  cairo_font_face_t* fallbackFont_ = nullptr;
};

} // namespace comp
