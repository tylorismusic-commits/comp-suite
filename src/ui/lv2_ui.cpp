// Comp Suite — LV2 UI (X11 + Cairo)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI is driven by the host through the idle interface rather than its own
// thread: Xlib is not thread-safe, and every X call then happens on the same
// thread that dispatches the host's port events.
//
// The panel is laid out once at 1040x640 logical units and scaled uniformly.
// The host resizes the widget (Ardour's resize handle, a tiling WM, ...), we
// snap to the nearest 1/80 step so the aspect ratio stays exact, and we ask
// the host for the corrected size through the ui:resize feature.

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>

#include <cmath>
#include <cstring>
#include <string>

#include "params.hpp"
#include "ui/geometry.hpp"
#include "ui/panel.hpp"

namespace {

constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/comp-suite#ui";
constexpr uint32_t kFirstParam = 4;

struct Ui {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
  const LV2UI_Resize* hostResize = nullptr;

  comp::Shared shared;
  comp::Panel panel;
  comp::PanelState state;

  Display* dpy = nullptr;
  Window win = 0;
  cairo_surface_t* windowSurface = nullptr;
  cairo_surface_t* backing = nullptr;
  cairo_t* cr = nullptr;
  cairo_format_t format = CAIRO_FORMAT_RGB24;

  comp::UiSize size{comp::kPanelWidth, comp::kPanelHeight};
  // Last size handed to the host, so a ConfigureNotify that already matches
  // does not bounce back and forth.
  uint32_t toldWidth = 0;
  uint32_t toldHeight = 0;

  double scale() const {
    return static_cast<double>(size.width) / comp::kPanelWidth;
  }

  void rebuild() {
    if (cr) {
      cairo_destroy(cr);
      cr = nullptr;
    }
    if (backing) {
      cairo_surface_destroy(backing);
      backing = nullptr;
    }
    backing = cairo_image_surface_create(
        format, static_cast<int>(size.width), static_cast<int>(size.height));
    cr = cairo_create(backing);
    if (windowSurface) {
      cairo_xlib_surface_set_size(windowSurface, size.width, size.height);
    }
  }

  // Snaps a requested physical size to the nearest valid one and applies it.
  void applySize(uint32_t width, uint32_t height, bool tellHost) {
    if (width == 0 || height == 0) {
      return;
    }
    const comp::UiSize snapped = comp::snapSize(width, height);
    if (snapped.width == size.width && snapped.height == size.height) {
      return;
    }
    size = snapped;
    if (win) {
      XResizeWindow(dpy, win, size.width, size.height);
    }
    rebuild();
    if (tellHost && hostResize && hostResize->ui_resize &&
        (size.width != toldWidth || size.height != toldHeight)) {
      toldWidth = size.width;
      toldHeight = size.height;
      hostResize->ui_resize(hostResize->handle, static_cast<int>(size.width),
                            static_cast<int>(size.height));
    }
  }

  void handleEvent(const XEvent& ev) {
    const double s = scale();
    switch (ev.type) {
      case ConfigureNotify:
        applySize(static_cast<uint32_t>(ev.xconfigure.width),
                  static_cast<uint32_t>(ev.xconfigure.height), true);
        break;
      case Expose:
        // Hosts that never call idle still need a first paint.
        if (ev.xexpose.count == 0) {
          draw();
        }
        break;
      case ButtonPress: {
        const bool fine = (ev.xbutton.state & ShiftMask) != 0;
        const double x = ev.xbutton.x / s;
        const double y = ev.xbutton.y / s;
        if (ev.xbutton.button == Button4) {
          panel.wheel(shared, state, x, y, 1.0);
        } else if (ev.xbutton.button == Button5) {
          panel.wheel(shared, state, x, y, -1.0);
        } else if (ev.xbutton.button == Button1) {
          panel.pointerDown(shared, state, x, y, fine);
        }
        break;
      }
      case ButtonRelease:
        panel.pointerUp();
        break;
      case MotionNotify: {
        const bool fine = (ev.xmotion.state & ShiftMask) != 0;
        panel.pointerMove(shared, state, ev.xmotion.x / s, ev.xmotion.y / s,
                          fine);
        break;
      }
      default:
        break;
    }
  }

  void draw() {
    if (!cr || !windowSurface) {
      return;
    }

    // Push changed controls to the host.
    for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
      if (!shared.takeDirty(i)) {
        continue;
      }
      const float v = static_cast<float>(shared.get(i));
      if (write) {
        write(controller, kFirstParam + i, sizeof(float), 0, &v);
      }
    }

    panel.tick(shared, state);
    cairo_save(cr);
    cairo_scale(cr, scale(), scale());
    panel.draw(cr, shared, state);
    cairo_restore(cr);
    cairo_surface_flush(backing);

    cairo_t* xcr = cairo_create(windowSurface);
    cairo_set_source_surface(xcr, backing, 0, 0);
    cairo_paint(xcr);
    cairo_destroy(xcr);
    // Required: cairo buffers the composite for Xlib surfaces.
    cairo_surface_flush(windowSurface);
    XFlush(dpy);
  }

  int idle() {
    while (dpy && XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      handleEvent(ev);
    }
    draw();
    return 0;
  }
};

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char* /*pluginUri*/,
                           const char* bundlePath, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget* widget,
                           const LV2_Feature* const* features) {
  void* parent = nullptr;
  const LV2UI_Resize* hostResize = nullptr;
  const float* scaleHint = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent)) {
      parent = (*f)->data;
    } else if (!std::strcmp((*f)->URI, LV2_UI__resize)) {
      hostResize = static_cast<const LV2UI_Resize*>(f[0]->data);
    } else if (!std::strcmp((*f)->URI, LV2_UI__scaleFactor)) {
      scaleHint = static_cast<const float*>(f[0]->data);
    }
  }

  auto* ui = new Ui();
  ui->write = write;
  ui->controller = controller;
  ui->hostResize = hostResize;

  ui->dpy = XOpenDisplay(nullptr);
  if (!ui->dpy) {
    delete ui;
    return nullptr;
  }

  const int screen = DefaultScreen(ui->dpy);
  const double initialScale =
      scaleHint && *scaleHint > 0.0f ? comp::clampScale(*scaleHint) : 1.0;
  ui->size = comp::sizeForScale(initialScale);

  const Window parentWindow =
      parent ? static_cast<Window>(reinterpret_cast<uintptr_t>(parent))
             : RootWindow(ui->dpy, screen);

  XSetWindowAttributes attrs{};
  attrs.background_pixel = BlackPixel(ui->dpy, screen);
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                     KeyPressMask | StructureNotifyMask;
  ui->win = XCreateWindow(ui->dpy, parentWindow, 0, 0, ui->size.width,
                          ui->size.height, 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWEventMask, &attrs);
  XStoreName(ui->dpy, ui->win, "Comp Suite");
  XMapRaised(ui->dpy, ui->win);

  ui->format = DefaultDepth(ui->dpy, screen) == 32 ? CAIRO_FORMAT_ARGB32
                                                   : CAIRO_FORMAT_RGB24;
  ui->windowSurface = cairo_xlib_surface_create(
      ui->dpy, ui->win, DefaultVisual(ui->dpy, screen), ui->size.width,
      ui->size.height);
  ui->rebuild();
  XFlush(ui->dpy);

  std::string dir = bundlePath ? bundlePath : "";
  ui->panel.loadFont(dir);

  // Tell the host the size we actually want.  Many hosts embed the widget at
  // whatever size they are given, so this is the first and often only chance.
  if (ui->hostResize && ui->hostResize->ui_resize) {
    ui->toldWidth = ui->size.width;
    ui->toldHeight = ui->size.height;
    ui->hostResize->ui_resize(ui->hostResize->handle,
                              static_cast<int>(ui->size.width),
                              static_cast<int>(ui->size.height));
  }

  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->win));
  return ui;
}

void uiCleanup(LV2UI_Handle handle) {
  auto* ui = static_cast<Ui*>(handle);
  if (ui->cr) {
    cairo_destroy(ui->cr);
  }
  if (ui->backing) {
    cairo_surface_destroy(ui->backing);
  }
  if (ui->windowSurface) {
    cairo_surface_destroy(ui->windowSurface);
  }
  if (ui->dpy) {
    XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
  }
  delete ui;
}

void uiPortEvent(LV2UI_Handle handle, uint32_t port, uint32_t size,
                 uint32_t format, const void* buffer) {
  auto* ui = static_cast<Ui*>(handle);
  if (format != 0 || size != sizeof(float) || !buffer) {
    return;
  }
  const float v = *static_cast<const float*>(buffer);
  if (port >= kFirstParam && port < kFirstParam + comp::P_COUNT) {
    ui->shared.setFromHost(port - kFirstParam, static_cast<double>(v));
    return;
  }
  switch (port) {
    case kFirstParam + comp::P_COUNT: ui->shared.grDb.store(v); break;
    case kFirstParam + comp::P_COUNT + 1: ui->shared.outPeakDb.store(v); break;
    case kFirstParam + comp::P_COUNT + 2: ui->shared.inPeakDb.store(v); break;
    case kFirstParam + comp::P_COUNT + 3: ui->shared.outVuDb.store(v); break;
    default: break;
  }
}

int uiIdle(LV2UI_Handle handle) {
  return static_cast<Ui*>(handle)->idle();
}

const LV2UI_Idle_Interface kIdleInterface = {uiIdle};

const void* uiExtensionData(const char* uri) {
  if (uri && !std::strcmp(uri, LV2_UI__idleInterface)) {
    return &kIdleInterface;
  }
  return nullptr;
}

const LV2UI_Descriptor kUiDescriptor = {kUiUri, uiInstantiate, uiCleanup,
                                        uiPortEvent, uiExtensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &kUiDescriptor : nullptr;
}
