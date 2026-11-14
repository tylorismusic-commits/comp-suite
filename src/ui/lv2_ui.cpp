// Comp Suite — LV2 UI (X11 + Cairo)
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "params.hpp"
#include "ui/geometry.hpp"
#include "ui/panel.hpp"

namespace {

constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/comp-suite#ui";
constexpr uint32_t kFirstParam = 4;
constexpr uint32_t kPortCount = 4 + comp::P_COUNT + 4;

struct Ui {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
  comp::Shared shared;
  comp::Panel panel;
  comp::PanelState state;

  Display* dpy = nullptr;
  Window win = 0;
  std::thread thread;
  std::atomic<bool> running{false};

  void run();
  void handleEvent(const XEvent& ev);
};

void Ui::handleEvent(const XEvent& ev) {
  switch (ev.type) {
    case ButtonPress: {
      const bool fine = (ev.xbutton.state & ShiftMask) != 0;
      if (ev.xbutton.button == Button4) {
        panel.wheel(shared, state, ev.xbutton.x, ev.xbutton.y, 1.0);
      } else if (ev.xbutton.button == Button5) {
        panel.wheel(shared, state, ev.xbutton.x, ev.xbutton.y, -1.0);
      } else if (ev.xbutton.button == Button1) {
        panel.pointerDown(shared, state, ev.xbutton.x, ev.xbutton.y, fine);
      }
      break;
    }
    case ButtonRelease:
      panel.pointerUp();
      break;
    case MotionNotify: {
      const bool fine = (ev.xmotion.state & ShiftMask) != 0;
      panel.pointerMove(shared, state, ev.xmotion.x, ev.xmotion.y, fine);
      break;
    }
    default:
      break;
  }
}

void Ui::run() {
  cairo_surface_t* windowSurface = cairo_xlib_surface_create(
      dpy, win, DefaultVisual(dpy, DefaultScreen(dpy)), comp::kPanelWidth,
      comp::kPanelHeight);
  cairo_surface_t* backing = cairo_image_surface_create(
      CAIRO_FORMAT_RGB24, comp::kPanelWidth, comp::kPanelHeight);
  cairo_t* cr = cairo_create(backing);

  while (running.load(std::memory_order_relaxed)) {
    while (XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      handleEvent(ev);
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

    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }

  cairo_destroy(cr);
  cairo_surface_destroy(backing);
  cairo_surface_destroy(windowSurface);
}

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char* /*pluginUri*/,
                           const char* bundlePath, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget* widget,
                           const LV2_Feature* const* features) {
  void* parent = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent)) {
      parent = (*f)->data;
    }
  }

  auto* ui = new Ui();
  ui->write = write;
  ui->controller = controller;

  ui->dpy = XOpenDisplay(nullptr);
  if (!ui->dpy) {
    delete ui;
    return nullptr;
  }
  const int screen = DefaultScreen(ui->dpy);
  const Window parentWindow =
      parent ? static_cast<Window>(reinterpret_cast<uintptr_t>(parent))
             : RootWindow(ui->dpy, screen);

  XSetWindowAttributes attrs{};
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | Button1MotionMask | ButtonMotionMask |
                     KeyPressMask;
  ui->win = XCreateWindow(ui->dpy, parentWindow, 0, 0, comp::kPanelWidth,
                          comp::kPanelHeight, 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWEventMask, &attrs);
  XStoreName(ui->dpy, ui->win, "Comp Suite");
  XMapRaised(ui->dpy, ui->win);
  XFlush(ui->dpy);

  std::string dir = bundlePath ? bundlePath : "";
  ui->panel.loadFont(dir);

  *widget = reinterpret_cast<LV2UI_Widget>(
      static_cast<uintptr_t>(ui->win));

  ui->running.store(true);
  ui->thread = std::thread([ui] { ui->run(); });
  return ui;
}

void uiCleanup(LV2UI_Handle handle) {
  auto* ui = static_cast<Ui*>(handle);
  ui->running.store(false);
  if (ui->thread.joinable()) {
    ui->thread.join();
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

const void* uiExtensionData(const char*) { return nullptr; }

const LV2UI_Descriptor kUiDescriptor = {kUiUri, uiInstantiate, uiCleanup,
                                        uiPortEvent, uiExtensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &kUiDescriptor : nullptr;
}
