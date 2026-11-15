// Comp Suite — CLAP plugin
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/all.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo-xlib.h>
#include <dlfcn.h>
#include <sys/time.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "dsp/engine.hpp"
#include "dsp/common.hpp"
#include "params.hpp"
#include "version.hpp"
#include "ui/geometry.hpp"
#include "ui/panel.hpp"

namespace comp {
namespace {

// Set COMP_DEBUG=1 to trace the editor thread to /tmp/compsuite-ui.log.
bool debugOn() {
  static const bool on = std::getenv("COMP_DEBUG") != nullptr;
  return on;
}

void dbg(const char* fmt, ...) {
  if (!debugOn()) {
    return;
  }
  FILE* f = std::fopen("/tmp/compsuite-ui.log", "a");
  if (!f) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

constexpr const char* kPluginId = "com.tylorismusic.compsuite";
constexpr const char* kPluginName = "Comp Suite";
constexpr const char* kVendor = "tylorismusic";

std::string bundleResourceDir() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&bundleResourceDir), &info) && info.dli_fname) {
    std::string path = info.dli_fname;
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos) {
      path = path.substr(0, slash);
    }
    return path + "/CompSuite.resources";
  }
  return "CompSuite.resources";
}

// ---------------------------------------------------------------------------
// X11 editor.  Owns the window and the Cairo surface, and runs its own
// thread so the host's audio and main threads are never blocked on drawing.
// ---------------------------------------------------------------------------

class Editor {
public:
  explicit Editor(Shared& shared) : shared_(shared) {}

  // Called when the user changes a control, so the host can ask for a
  // process call and pick up the new values.
  std::function<void()> requestFlush;

  double scale() const { return scale_.load(std::memory_order_relaxed); }
  UiSize size() const { return sizeForScale(scale()); }

  // Host thread: record the request only.  All Xlib calls happen on the
  // editor thread, because Xlib is not thread-safe and calling
  // XInitThreads() after the host has used Xlib is undefined behaviour
  // (that made the editor fail to load in REAPER).
  void resize(double scale) {
    scale_.store(clampScale(scale), std::memory_order_relaxed);
    resizePending_.store(true, std::memory_order_relaxed);
  }

  bool open(void* parent) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
      return opened_;
    }
    parentWindow_ =
        parent ? static_cast<Window>(reinterpret_cast<uintptr_t>(parent)) : 0;
    opened_ = false;
    openFailed_ = false;
    quitting_ = false;
    thread_ = std::thread([this] { run(); });
    cv_.wait_for(lock, std::chrono::seconds(3),
                 [this] { return opened_ || openFailed_; });
    return opened_;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      quitting_ = true;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
  }

  ~Editor() { close(); }

private:
  void run() {
    dbg("thread start");
    Display* dpy = XOpenDisplay(nullptr);
    dbg("display=%p", static_cast<void*>(dpy));
    if (!dpy) {
      std::lock_guard<std::mutex> lock(mutex_);
      openFailed_ = true;
      cv_.notify_all();
      return;
    }
    const int screen = DefaultScreen(dpy);
    const Window parent = parentWindow_ ? parentWindow_ : RootWindow(dpy, screen);
    UiSize current = size();

    XSetWindowAttributes attrs{};
    // Button1MotionMask is essential: PointerMotionMask only reports motion
    // with no button held, so dragging a knob would never deliver events.
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                       KeyPressMask;
    const Window win =
        XCreateWindow(dpy, parent, 0, 0, current.width, current.height, 0,
                      CopyFromParent, InputOutput, CopyFromParent, CWEventMask,
                      &attrs);

    const UiSize minSize = sizeForScale(kMinUiScale);
    const UiSize maxSize = sizeForScale(kMaxUiScale);
    XSizeHints hints{};
    hints.flags = PAspect | PMinSize | PMaxSize;
    hints.min_aspect.x = hints.max_aspect.x = kPanelWidth;
    hints.min_aspect.y = hints.max_aspect.y = kPanelHeight;
    hints.min_width = static_cast<int>(minSize.width);
    hints.min_height = static_cast<int>(minSize.height);
    hints.max_width = static_cast<int>(maxSize.width);
    hints.max_height = static_cast<int>(maxSize.height);
    XSetWMNormalHints(dpy, win, &hints);
    XStoreName(dpy, win, kPluginName);
    XMapRaised(dpy, win);
    XFlush(dpy);

    panel_.loadFont(bundleResourceDir());
    {
      std::lock_guard<std::mutex> lock(mutex_);
      display_ = dpy;
      window_ = win;
      opened_ = true;
      cv_.notify_all();
    }

    // Use the window's own visual and depth.  Hosts often give the plugin a
    // parent with a non-default visual (ARGB), and cairo refuses to draw on a
    // surface whose visual does not match the drawable.
    XWindowAttributes wa{};
    XGetWindowAttributes(dpy, win, &wa);
    dbg("window=%lu depth=%d visual=%p size=%dx%d", win, wa.depth,
        static_cast<void*>(wa.visual), current.width, current.height);
    cairo_surface_t* windowSurface =
        cairo_xlib_surface_create(dpy, win, wa.visual, current.width, current.height);
    dbg("xlib surface status=%s",
        cairo_status_to_string(cairo_surface_status(windowSurface)));
    const cairo_format_t imageFormat =
        wa.depth == 32 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
    cairo_surface_t* backing = nullptr;
    cairo_t* cr = nullptr;

    auto rebuildBacking = [&]() {
      if (cr) {
        cairo_destroy(cr);
        cr = nullptr;
      }
      if (backing) {
        cairo_surface_destroy(backing);
        backing = nullptr;
      }
      backing = cairo_image_surface_create(imageFormat, current.width,
                                           current.height);
      cr = cairo_create(backing);
    };
    rebuildBacking();

    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quitting_) {
          break;
        }
      }
      const UiSize want = size();
      if (want.width != current.width || want.height != current.height) {
        current = want;
        XResizeWindow(dpy, win, current.width, current.height);
        cairo_xlib_surface_set_size(windowSurface, current.width, current.height);
        rebuildBacking();
      }
      while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        handleEvent(ev);
      }
      panel_.tick(shared_, state_);
      cairo_save(cr);
      cairo_scale(cr, scale(), scale());
      panel_.draw(cr, shared_, state_);
      cairo_restore(cr);
      cairo_surface_flush(backing);
      cairo_t* xcr = cairo_create(windowSurface);
      cairo_set_source_surface(xcr, backing, 0, 0);
      cairo_paint(xcr);
      cairo_destroy(xcr);
      // Required: cairo's Xlib backend buffers the composite, and without
      // this flush nothing reaches the server (the window stays black).
      cairo_surface_flush(windowSurface);
      XFlush(dpy);
      static int frames = 0;
      if (debugOn() && (frames++ % 120) == 0) {
        XWindowAttributes ga;
        XGetWindowAttributes(dpy, win, &ga);
        dbg("frame %d geom=%dx%d mapped=%d", frames, ga.width, ga.height,
            ga.map_state == IsViewable);
      }
      if (requestFlush && shared_.anyDirty()) {
        requestFlush();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    if (cr) {
      cairo_destroy(cr);
    }
    if (backing) {
      cairo_surface_destroy(backing);
    }
    if (windowSurface) {
      cairo_surface_destroy(windowSurface);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      display_ = nullptr;
      window_ = 0;
    }
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
  }

  // Event coordinates are physical pixels; the panel works in logical units.
  void handleEvent(const XEvent& ev) {
    const double s = scale();
    switch (ev.type) {
      case ButtonPress: {
        const bool fine = (ev.xbutton.state & ShiftMask) != 0;
        const double x = ev.xbutton.x / s;
        const double y = ev.xbutton.y / s;
        if (ev.xbutton.button == Button4) {
          panel_.wheel(shared_, state_, x, y, 1.0);
        } else if (ev.xbutton.button == Button5) {
          panel_.wheel(shared_, state_, x, y, -1.0);
        } else if (ev.xbutton.button == Button1) {
          panel_.pointerDown(shared_, state_, x, y, fine);
        }
        break;
      }
      case ButtonRelease:
        panel_.pointerUp();
        break;
      case MotionNotify: {
        const bool fine = (ev.xmotion.state & ShiftMask) != 0;
        panel_.pointerMove(shared_, state_, ev.xmotion.x / s, ev.xmotion.y / s,
                           fine);
        break;
      }
      default:
        break;
    }
  }

  Shared& shared_;
  PanelState state_;
  Panel panel_;
  std::atomic<double> scale_{1.0};
  std::atomic<bool> resizePending_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
  Display* display_ = nullptr;
  Window window_ = 0;
  Window parentWindow_ = 0;
  bool opened_ = false;
  bool openFailed_ = false;
  bool quitting_ = false;
  std::thread thread_;
};

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

struct Plugin {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  Shared shared;
  Engine engine;
  Editor editor{shared};
  double sampleRate = 48000.0;
  Meters meters;
  std::atomic<float> grForHost{0.0f};
  // The host caches parameter flags, so a model change is announced with a
  // rescan.  rescan() is main-thread only, so audio-thread changes queue a
  // main-thread callback.
  std::atomic<int> infoModel{-1};
  std::atomic<int> reportedOversample{-1};
  std::atomic<uint32_t> reportedLatency{0xffffffffu};
  std::atomic<bool> rescanQueued{false};
  std::atomic<bool> active{false};
};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION,
    kPluginId,
    kPluginName,
    kVendor,
    "https://github.com/tylorismusic-commits/comp-suite",
    "",
    "",
    kVersion,
    "Ten compressor models in one plugin: Clean, Easy, 76Blue, 76Black, 2A, 3A, Xpress, VCA, Molot and Red.",
    nullptr,
};

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                 CLAP_PLUGIN_FEATURE_COMPRESSOR,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

void writeName(char* dst, size_t size, const char* src) {
  std::strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

// --- Parameters -------------------------------------------------------------

int currentModel(const Plugin* s) {
  return static_cast<int>(s->shared.get(P_MODEL) + 0.5);
}

// Controls that exist for one model (or one pair) only.  CLAP describes
// CLAP_PARAM_IS_HIDDEN as "currently not used", which is exactly this: a Beef
// knob on a 76Blue does nothing, so it should not be in the host's generic
// list.  Hidden parameters keep their index and value, so automation and
// saved states survive a model change.
bool paramAppliesToModel(uint32_t param, int model) {
  const int blue = static_cast<int>(Model::Blue76);
  const int black = static_cast<int>(Model::Black76);
  const int la2a = static_cast<int>(Model::Opto2A);
  const int la3a = static_cast<int>(Model::Opto3A);
  const int xpress = static_cast<int>(Model::Xpress);
  const int vca = static_cast<int>(Model::Vca);
  const int clean = static_cast<int>(Model::Clean);
  const int molot = static_cast<int>(Model::Molot);
  const int red = static_cast<int>(Model::Red);
  const int easy = static_cast<int>(Model::Easy);
  switch (param) {
    case P_INPUT:
      return model == blue || model == black || model == xpress ||
             model == clean || model == molot || model == red || model == easy;
    case P_OUTPUT:
      // Both optical models read P_OUTPUT; only the 2A shows the switch.
      return model == blue || model == black || model == la2a ||
             model == la3a || model == xpress || model == clean ||
             model == red || model == easy;
    case P_MAKEUP:
      return model == la2a || model == la3a || model == vca || model == clean ||
             model == molot || model == red;
    case P_RATIO:
      return model != easy;  // Easy's ratio follows its Character switch
    case P_ATTACK:
    case P_RELEASE:
      return model != la2a && model != la3a && model != easy;
    case P_KNEE:
      return model == clean || model == molot || model == red;
    case P_DISTORTION:
      return model == xpress || model == clean || model == molot;
    case P_SC_HPF:
      return model == xpress || model == vca || model == clean ||
             model == molot || model == red || model == easy;
    case P_BEEF:
    case P_AIR:
    case P_DRUM_TYPE:
    case P_EASY_MODE:
    case P_EASY_CHAR:
    case P_SAT_TYPE:
      return model == easy;
    case P_SAT_DRIVE:
      return model == easy || model == molot;
    case P_ATTACK_MODE:
    case P_STEREO_MODE:
      return model == molot;
    case P_VCA_LINK:
      return model == vca || model == easy;
    case P_VCA_AUTO:
      return model == vca || model == red;
    default:
      return true;
  }
}

uint32_t paramsCount(const clap_plugin_t*) { return P_COUNT; }

bool paramsInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info) {
  if (index >= P_COUNT) {
    return false;
  }
  const ParamSpec& s = kParamSpecs[index];
  std::memset(info, 0, sizeof(*info));
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (s.stepped) {
    info->flags |= CLAP_PARAM_IS_STEPPED;
  }
  if (s.hidden || !paramAppliesToModel(index, currentModel(self(p)))) {
    info->flags |= CLAP_PARAM_IS_HIDDEN;
  }
  // Power is a normal parameter.  It must NOT be flagged CLAP_PARAM_IS_BYPASS:
  // hosts then treat it with inverted semantics (0 = not bypassed) and write 0
  // at load, which would switch the compressor off.  Host FX bypass is
  // separate and handled by the host itself.
  writeName(info->name, sizeof(info->name), s.name);
  writeName(info->module, sizeof(info->module), s.module);
  info->min_value = s.min;
  info->max_value = s.max;
  info->default_value = s.def;
  return true;
}

bool paramsValue(const clap_plugin_t* p, clap_id id, double* value) {
  if (id >= P_COUNT) {
    return false;
  }
  *value = self(p)->shared.get(id);
  return true;
}

namespace {

const char* const kDistortionNames[4] = {"Off", "Dist 2", "Dist 3", "Clip"};
const char* const kMeterNames[6] = {"GR",   "+8", "+4",
                                    "Graph", "Off", "Curve"};

} // namespace

// Hosts show these strings in their generic parameter lists.  Without them
// every control reads as a bare number ("0.50") and you cannot tell what you
// are selecting.
bool paramsValueToText(const clap_plugin_t* p, clap_id id, double value,
                       char* out, uint32_t size) {
  const Plugin* s = self(p);
  const int model = currentModel(s);
  switch (id) {
    case P_MODEL:
      // Format the value being asked about, not the current selection.
      std::snprintf(
          out, size, "%s",
          kModelLabels[std::clamp(static_cast<int>(value + 0.5), 0,
                                  static_cast<int>(Model::Count) - 1)]);
      return true;
    case P_ENABLED:
      std::snprintf(out, size, "%s", value > 0.5 ? "On" : "Off");
      return true;
    case P_INPUT:
      if (model <= 1 || model == 4) {
        const double db = -80.0 + 103.0 * std::sqrt(std::clamp(value, 0.0, 1.0));
        if (db <= -79.0) {
          std::snprintf(out, size, "Off");
        } else {
          std::snprintf(out, size, "%+.1f dB", db);
        }
      } else if (model == 6) {
        std::snprintf(out, size, "%+.1f dB", -30.0 + 60.0 * value);
      } else if (model == 7) {
        std::snprintf(out, size, "%+.1f dB", -12.0 + 24.0 * value);
      } else if (model == 8 || model == 9) {
        std::snprintf(out, size, "%+.1f dB", -18.0 + 36.0 * value);
      } else {
        std::snprintf(out, size, "%.0f%%", value * 100.0);
      }
      return true;
    case P_THRESHOLD:
      if (model == 2 || model == 3) {
        std::snprintf(out, size, "%.0f", value * 100.0);
      } else if (model == 5) {
        std::snprintf(out, size, "%.1f dB", -30.0 + 40.0 * value);
      } else if (model == 6) {
        std::snprintf(out, size, "%.1f dB", -40.0 + 40.0 * value);
      } else if (model == 7 || model == 8) {
        std::snprintf(out, size, "%.1f dB", -40.0 + 40.0 * value);
      } else {
        std::snprintf(out, size, "%.0f%%", value * 100.0);
      }
      return true;
    case P_RATIO: {
      const int step = ratioStepFrom(static_cast<float>(value));
      if (model == 8) {
        std::snprintf(out, size, "%.1f:1", RedCompressor::ratioForStep(step));
      } else if (model == 7 && ratioFromStep(step) > 10.9) {
        std::snprintf(out, size, "Limit");
      } else if (model == 2 || model == 3) {
        std::snprintf(out, size, "%s", step >= 4 ? "Limit" : "Compress");
      } else if (model <= 1 && step >= 8) {
        std::snprintf(out, size, "ALL");
      } else {
        std::snprintf(out, size, "%.1f:1", ratioFromStep(step));
      }
      return true;
    }
    case P_ATTACK: {
      double ms;
      if (model <= 1) {
        ms = 0.8 * std::pow(0.025, std::round(value * 6.0) / 6.0);
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
        std::snprintf(out, size, "%.0f%%", value * 100.0);
        return true;
      }
      if (ms < 1.0) {
        std::snprintf(out, size, "%.0f us", ms * 1000.0);
      } else {
        std::snprintf(out, size, "%.0f ms", ms);
      }
      return true;
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
        std::snprintf(out, size, "%.0f%%", value * 100.0);
        return true;
      }
      std::snprintf(out, size, ms < 1000.0 ? "%.0f ms" : "%.2f s",
                    ms < 1000.0 ? ms : ms / 1000.0);
      return true;
    }
    case P_MAKEUP:
      if (model == 7) {
        std::snprintf(out, size, "%+.1f dB", -12.0 + 24.0 * value);
      } else {
        std::snprintf(out, size, "%+.1f dB", (value - 0.5) * 20.0);
      }
      return true;
    case P_OUTPUT:
      if (model == 2 || model == 3) {
        std::snprintf(out, size, "%s", value > 0.5 ? "+6" : "0");
      } else if (model <= 1) {
        std::snprintf(out, size, "%+.1f dB", -12.0 + 24.0 * value);
      } else if (model == 6) {
        std::snprintf(out, size, "%+.1f dB", -30.0 + 60.0 * value);
      } else {
        std::snprintf(out, size, "%+.1f dB", -12.0 + 24.0 * value);
      }
      return true;
    case P_MIX:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return true;
    case P_SC_HPF:
      if (value < 0.01 && model == 9) {
        std::snprintf(out, size, "Auto");
      } else if (value < 0.01) {
        std::snprintf(out, size, "Off");
      } else if (model == 7) {
        std::snprintf(out, size, "%.0f Hz", 40.0 + 220.0 * value);
      } else {
        std::snprintf(out, size, "%.0f Hz", 20.0 * std::pow(20.0, value));
      }
      return true;
    case P_DISTORTION:
      std::snprintf(out, size, "%s", kDistortionNames[std::clamp(
                                         static_cast<int>(value + 0.5), 0, 3)]);
      return true;
    case P_METER:
      std::snprintf(out, size, "%s", kMeterNames[std::clamp(
                                         static_cast<int>(value + 0.5), 0, 5)]);
      return true;
    case P_LIMITER_ON:
      std::snprintf(out, size, "%s", value > 0.5 ? "On" : "Off");
      return true;
    case P_LIMITER_THRESH:
      std::snprintf(out, size, "%.1f dB", -24.0 + 24.0 * value);
      return true;
    case P_KNEE:
      // Clean and Red are in dB; Molot's knee is a fraction of its threshold.
      if (model == 6) {
        std::snprintf(out, size, "%.1f dB", 18.0 * value);
      } else if (model == 8) {
        std::snprintf(out, size, "%.1f dB", 12.0 * value);
      } else if (model == 7) {
        std::snprintf(out, size, "%.0f%%", value * 100.0);
      } else {
        std::snprintf(out, size, "%.1f dB", 18.0 * value);
      }
      return true;
    case P_SAT_DRIVE:
      if (model == 9) {
        std::snprintf(out, size, "%.0f%%", value * 100.0);
      } else {
        std::snprintf(out, size, "%+.1f dB", -12.0 + 24.0 * value);
      }
      return true;
    case P_BEEF:
    case P_AIR:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return true;
    case P_DRUM_TYPE:
      std::snprintf(
          out, size, "%s",
          kDrumTypeLabels[std::clamp(static_cast<int>(value + 0.5), 0, 5)]);
      return true;
    case P_EASY_MODE:
      std::snprintf(out, size, "%s", kEasyModeLabels[value > 0.5 ? 1 : 0]);
      return true;
    case P_EASY_CHAR:
      std::snprintf(out, size, "%s", kEasyCharLabels[value > 0.5 ? 1 : 0]);
      return true;
    case P_SAT_TYPE:
      std::snprintf(
          out, size, "%s",
          kSatTypeLabels[std::clamp(static_cast<int>(value + 0.5), 0, 2)]);
      return true;
    case P_OVERSAMPLE:
      std::snprintf(
          out, size, "%s",
          kOversampleLabels[std::clamp(static_cast<int>(value + 0.5), 0, 2)]);
      return true;
    case P_ATTACK_MODE:
      std::snprintf(out, size, "%s", kAttackModeLabels[value > 0.5 ? 1 : 0]);
      return true;
    case P_STEREO_MODE:
      std::snprintf(
          out, size, "%s",
          kStereoModeLabels[std::clamp(static_cast<int>(value + 0.5), 0, 5)]);
      return true;
    case P_AUTO_MAKEUP:
    case P_TRUE_PEAK:
    case P_VCA_AUTO:
      std::snprintf(out, size, "%s", value > 0.5 ? "On" : "Off");
      return true;
    case P_SC_SOURCE:
      std::snprintf(out, size, "%s", value > 0.5 ? "External" : "Internal");
      return true;
    case P_VCA_LINK:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return true;
    default:
      std::snprintf(out, size, "%.2f", value);
      return true;
  }
}

bool paramsTextToValue(const clap_plugin_t* p, clap_id id, const char* text,
                       double* value) {
  if (!text) {
    return false;
  }
  const int model = currentModel(self(p));
  auto matchList = [&](const char* const* list, int n, double* out) {
    for (int i = 0; i < n; ++i) {
      if (strcasecmp(text, list[i]) == 0) {
        *out = i;
        return true;
      }
    }
    return false;
  };
  double v = 0.0;
  switch (id) {
    case P_MODEL:
      if (matchList(kModelLabels, static_cast<int>(Model::Count), &v)) {
        *value = v;
        return true;
      }
      break;
    case P_DISTORTION:
      if (matchList(kDistortionNames, 4, &v)) {
        *value = v;
        return true;
      }
      break;
    case P_METER:
      if (matchList(kMeterNames, 5, &v)) {
        *value = v;
        return true;
      }
      break;
    case P_RATIO: {
      // Accept "8", "8:1" and "ALL".
      if (model <= 1 && strcasecmp(text, "ALL") == 0) {
        *value = 8.0;
        return true;
      }
      char* endp = nullptr;
      const double num = std::strtod(text, &endp);
      if (endp != text) {
        int best = 0;
        double bestDelta = 1e9;
        for (int i = 0; i < 9; ++i) {
          const double d = std::fabs(ratioFromStep(i) - num);
          if (d < bestDelta) {
            bestDelta = d;
            best = i;
          }
        }
        *value = best;
        return true;
      }
      break;
    }
    default:
      break;
  }
  char* end = nullptr;
  v = std::strtod(text, &end);
  if (end == text) {
    return false;
  }
  *value = v;
  return true;
}

void applyEvents(Plugin* s, const clap_input_events_t* in) {
  const uint32_t count = in->size(in);
  for (uint32_t i = 0; i < count; ++i) {
    const clap_event_header_t* hdr = in->get(in, i);
    if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) {
      continue;
    }
    if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
      const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(hdr);
      // A pending UI change wins over the host's cached value: hosts that
      // resend their whole parameter set each block would otherwise undo a
      // preset (or a knob move) before we could send it back.
      if (pv->param_id < P_COUNT && !s->shared.isDirty(pv->param_id)) {
        s->shared.setFromHost(pv->param_id, pv->value);
      }
    }
  }
}

// Sends the values the editor changed back to the host, so they can be
// automated and recorded.
void emitDirty(Plugin* s, const clap_output_events_t* out) {
  if (!out) {
    return;
  }
  for (uint32_t p = 0; p < P_COUNT; ++p) {
    if (!s->shared.takeDirty(p)) {
      continue;
    }
    clap_event_param_value_t ev{};
    ev.header.size = sizeof(ev);
    ev.header.time = 0;
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.header.flags = 0;
    ev.param_id = p;
    ev.note_id = -1;
    ev.port_index = -1;
    ev.channel = -1;
    ev.key = -1;
    ev.value = s->shared.get(p);
    out->try_push(out, &ev.header);
  }
}

// Queue a parameter-info rescan if the model changed since the last one.
// Safe on the audio thread: it only sets a flag and asks the host for a
// main-thread callback.
void noteHostChanges(Plugin* s) {
  const bool modelChanged =
      currentModel(s) != s->infoModel.load(std::memory_order_relaxed);
  const int os = static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5);
  const bool latencyChanged =
      os != s->reportedOversample.load(std::memory_order_relaxed);
  if (!modelChanged && !latencyChanged) {
    return;
  }
  if (!s->rescanQueued.exchange(true, std::memory_order_relaxed)) {
    if (s->host && s->host->request_callback) {
      s->host->request_callback(s->host);
    }
  }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                 const clap_output_events_t* out) {
  Plugin* s = self(p);
  if (in) {
    applyEvents(s, in);
  }
  emitDirty(s, out);
  noteHostChanges(s);
}

const clap_plugin_params_t kParamsExt = {
    paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue,
    paramsFlush,
};

// --- State ------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
  Plugin* s = self(p);
  double values[P_COUNT];
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    values[i] = s->shared.get(i);
  }
  const double scale = s->editor.scale();
  const int64_t written = stream->write(stream, values, sizeof(values));
  const int64_t writtenScale = stream->write(stream, &scale, sizeof(scale));
  return written == static_cast<int64_t>(sizeof(values)) &&
         writtenScale == static_cast<int64_t>(sizeof(scale));
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) {
  Plugin* s = self(p);
  double values[P_COUNT];
  const int64_t got = stream->read(stream, values, sizeof(values));
  if (got != static_cast<int64_t>(sizeof(values))) {
    return false;
  }
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    s->shared.setFromHost(i, values[i]);
  }
  noteHostChanges(s);
  // The UI scale is optional, so states saved before it still load.
  double scale = 0.0;
  if (stream->read(stream, &scale, sizeof(scale)) == static_cast<int64_t>(sizeof(scale)) &&
      scale > 0.0) {
    s->editor.resize(scale);
  }
  return true;
}

const clap_plugin_state_t kStateExt = {stateSave, stateLoad};

// --- Audio ports ------------------------------------------------------------

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) {
  return isInput ? 2 : 1;
}

bool audioPortsInfo(const clap_plugin_t*, uint32_t index, bool isInput,
                    clap_audio_port_info_t* info) {
  std::memset(info, 0, sizeof(*info));
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;

  if (isInput) {
    if (index > 1) {
      return false;
    }
    if (index == 0) {
      info->id = 0;
      writeName(info->name, sizeof(info->name), "Input");
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    } else {
      info->id = 2;
      writeName(info->name, sizeof(info->name), "Sidechain");
      info->flags = 0;
    }
    return true;
  }
  if (index != 0) {
    return false;
  }
  info->id = 1;
  writeName(info->name, sizeof(info->name), "Output");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  return true;
}

const clap_plugin_audio_ports_t kAudioPortsExt = {audioPortsCount, audioPortsInfo};

// --- GUI --------------------------------------------------------------------

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) {
  return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) {
  *api = CLAP_WINDOW_API_X11;
  *isFloating = false;
  return true;
}

bool guiCreate(const clap_plugin_t*, const char*, bool) { return true; }

void guiDestroy(const clap_plugin_t* p) { self(p)->editor.close(); }

// X11 embedding uses physical pixels, so the host does not set a scale.
bool guiSetScale(const clap_plugin_t*, double) { return false; }

bool guiGetSize(const clap_plugin_t* p, uint32_t* width, uint32_t* height) {
  const UiSize s = self(p)->editor.size();
  *width = s.width;
  *height = s.height;
  return true;
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = true;
  hints->aspect_ratio_width = kPanelWidth;
  hints->aspect_ratio_height = kPanelHeight;
  return true;
}

// Returns the nearest valid size; the editor always keeps 1040:640.
bool guiAdjustSize(const clap_plugin_t*, uint32_t* width, uint32_t* height) {
  const UiSize s = snapSize(*width, *height);
  *width = s.width;
  *height = s.height;
  return true;
}

bool guiSetSize(const clap_plugin_t* p, uint32_t width, uint32_t height) {
  self(p)->editor.resize(scaleForRequest(width, height));
  return true;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
  return self(p)->editor.open(window->x11 ? reinterpret_cast<void*>(
                                                static_cast<uintptr_t>(window->x11))
                                          : nullptr);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t*) { return true; }

bool guiHide(const clap_plugin_t*) { return true; }

const clap_plugin_gui_t kGuiExt = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

// --- Gain adjustment metering -----------------------------------------------

double gainMeterGet(const clap_plugin_t* p) {
  return -static_cast<double>(self(p)->grForHost.load(std::memory_order_relaxed));
}

const clap_plugin_gain_adjustment_metering_t kGainMeterExt = {gainMeterGet};

uint32_t latencyGet(const clap_plugin_t* p) {
  return static_cast<uint32_t>(self(p)->engine.latencySamples());
}

const clap_plugin_latency_t kLatencyExt = {latencyGet};

// --- Plugin lifecycle -------------------------------------------------------

bool pluginInit(const clap_plugin_t*) { return true; }

void pluginDestroy(const clap_plugin_t* p) {
  delete self(p);
}

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t,
                    uint32_t) {
  Plugin* s = self(p);
  s->sampleRate = sampleRate;
  s->engine.prepare(static_cast<float>(sampleRate),
                    static_cast<float>(s->shared.get(P_OVERSAMPLE)));
  s->active.store(true, std::memory_order_relaxed);
  s->reportedOversample.store(
      static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5),
      std::memory_order_relaxed);
  // REAPER only picks up a new latency when the plugin calls changed(), so do
  // that here (being-activated is the one legal moment) - but only when the
  // value actually changed, so a host that reacts by restarting cannot loop.
  const uint32_t latencyNow = s->engine.latencySamples();
  if (latencyNow != s->reportedLatency.exchange(latencyNow)) {
    if (s->host && s->host->get_extension) {
      const auto* latency = static_cast<const clap_host_latency_t*>(
          s->host->get_extension(s->host, CLAP_EXT_LATENCY));
      if (latency && latency->changed) {
        latency->changed(s->host);
      }
    }
  }
  return true;
}

void pluginDeactivate(const clap_plugin_t* p) {
  self(p)->active.store(false, std::memory_order_relaxed);
}

bool pluginStartProcessing(const clap_plugin_t*) { return true; }

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* p) {
  self(p)->engine.reset();
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* proc) {
  Plugin* s = self(p);
  const uint32_t frames = proc->frames_count;
  if (proc->in_events) {
    applyEvents(s, proc->in_events);
  }
  emitDirty(s, proc->out_events);

  if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
    return CLAP_PROCESS_CONTINUE;
  }
  // Be tolerant of mono or partially connected buffers.
  const clap_audio_buffer_t& inBuf = proc->audio_inputs[0];
  const clap_audio_buffer_t& outBuf = proc->audio_outputs[0];
  if (!outBuf.data32 || !inBuf.data32 || !outBuf.data32[0] || !inBuf.data32[0]) {
    return CLAP_PROCESS_CONTINUE;
  }
  float* left = outBuf.data32[0];
  float* right = outBuf.channel_count > 1 && outBuf.data32[1] ? outBuf.data32[1] : left;
  const float* inL = inBuf.data32[0];
  const float* inR = inBuf.channel_count > 1 && inBuf.data32[1] ? inBuf.data32[1] : inL;
  if (left != inL) {
    std::memcpy(left, inL, frames * sizeof(float));
  }
  if (right != inR) {
    std::memcpy(right, inR, frames * sizeof(float));
  }

  // The 76s have five ratio presets on the shared ladder.  If the ratio
  // arrives from another model at a value the bank does not have, snap it to
  // the nearest preset so a button is always lit and host/UI agree.
  {
    const int model = static_cast<int>(s->shared.get(P_MODEL) + 0.5);
    if (model <= 1) {
      static const int kPresets[5] = {3, 5, 6, 7, 8};
      const int step = ratioStepFrom(static_cast<float>(s->shared.get(P_RATIO)));
      int best = kPresets[0];
      for (int preset : kPresets) {
        if (std::abs(preset - step) < std::abs(best - step)) {
          best = preset;
        }
      }
      if (best != step) {
        // Mark it dirty as well: the host's generic view should be told the
        // ratio landed on a real button, not left showing the old value.
        s->shared.set(P_RATIO, static_cast<double>(best));
      }
    }
  }
  s->engine.setControls(controlsFrom(s->shared));

  // Optional external side-chain: the second input port, if the host routed
  // anything to it.
  const float* scL = nullptr;
  const float* scR = nullptr;
  if (proc->audio_inputs_count > 1) {
    const clap_audio_buffer_t& scBuf = proc->audio_inputs[1];
    if (scBuf.data32 && scBuf.data32[0]) {
      scL = scBuf.data32[0];
      scR = scBuf.channel_count > 1 && scBuf.data32[1] ? scBuf.data32[1] : scL;
    }
  }
  s->engine.process(left, right, static_cast<int>(frames), s->meters, scL,
                    scR);

  s->grForHost.store(s->meters.grDb, std::memory_order_relaxed);
  s->shared.grDb.store(s->meters.grDb, std::memory_order_relaxed);
  s->shared.inPeakDb.store(s->meters.inPeakDb, std::memory_order_relaxed);
  s->shared.outPeakDb.store(s->meters.outPeakDb, std::memory_order_relaxed);
  s->shared.outVuDb.store(s->meters.outVuDb, std::memory_order_relaxed);
  s->shared.inPeakLDb.store(s->meters.inPeakLDb, std::memory_order_relaxed);
  s->shared.inPeakRDb.store(s->meters.inPeakRDb, std::memory_order_relaxed);
  s->shared.outPeakLDb.store(s->meters.outPeakLDb, std::memory_order_relaxed);
  s->shared.outPeakRDb.store(s->meters.outPeakRDb, std::memory_order_relaxed);
  s->shared.scPeakDb.store(s->meters.scPeakDb, std::memory_order_relaxed);
  s->shared.scActive.store(s->meters.scActive, std::memory_order_relaxed);
  noteHostChanges(s);
  return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
    return &kParamsExt;
  }
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
    return &kStateExt;
  }
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
    return &kAudioPortsExt;
  }
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
    return &kGuiExt;
  }
  if (std::strcmp(id, CLAP_EXT_GAIN_ADJUSTMENT_METERING) == 0) {
    return &kGainMeterExt;
  }
  if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
    return &kLatencyExt;
  }
  return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p) {
  Plugin* s = self(p);
  if (!s->rescanQueued.exchange(false, std::memory_order_relaxed)) {
    return;
  }
  const int model = currentModel(s);
  const bool modelChanged =
      s->infoModel.exchange(model, std::memory_order_relaxed) != model;
  const int os = static_cast<int>(s->shared.get(P_OVERSAMPLE) + 0.5);
  const bool latencyChanged =
      s->reportedOversample.exchange(os, std::memory_order_relaxed) != os;
  if (!s->host || !s->host->get_extension) {
    return;
  }
  if (modelChanged) {
    const auto* params = static_cast<const clap_host_params_t*>(
        s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    if (params && params->rescan) {
      params->rescan(s->host, CLAP_PARAM_RESCAN_INFO);
    }
  }
  if (latencyChanged && s->active.load(std::memory_order_relaxed)) {
    // CLAP only allows the latency to change while the plugin is being
    // activated; ask the host to restart.  The new latency is reported from
    // pluginActivate(), after the engine has actually switched rates.
    if (s->host->request_restart) {
      s->host->request_restart(s->host);
    }
  }
}

Plugin* createPlugin(const clap_host_t* host) {
  Plugin* s = new Plugin();
  s->host = host;
  s->plugin.desc = &kDescriptor;
  s->plugin.plugin_data = s;
  s->plugin.init = pluginInit;
  s->plugin.destroy = pluginDestroy;
  s->plugin.activate = pluginActivate;
  s->plugin.deactivate = pluginDeactivate;
  s->plugin.start_processing = pluginStartProcessing;
  s->plugin.stop_processing = pluginStopProcessing;
  s->plugin.reset = pluginReset;
  s->plugin.process = pluginProcess;
  s->plugin.get_extension = pluginGetExtension;
  s->plugin.on_main_thread = pluginOnMainThread;
  s->engine.prepare(48000.0f);
  // Latency may only change while the plugin is being activated, so the
  // oversampling switch is applied on the host's restart, not mid-block.
  s->engine.setDeferOversample(true);
  s->editor.requestFlush = [s] {
    if (s->host && s->host->request_process) {
      s->host->request_process(s->host);
    }
  };
  return s;
}

// --- Factory and entry ------------------------------------------------------

uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host,
                                   const char* id) {
  if (std::strcmp(id, kPluginId) != 0) {
    return nullptr;
  }
  return &createPlugin(host)->plugin;
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*) {
  // Do not call XInitThreads() here: the host has already used Xlib by the
  // time plugins are loaded, and initialising threading afterwards is
  // undefined behaviour (it stopped the editor loading in REAPER).  The
  // editor instead confines every Xlib call to its own thread.
  return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* factoryId) {
  return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace
} // namespace comp

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    comp::entryInit,
    comp::entryDeinit,
    comp::entryGetFactory,
};
