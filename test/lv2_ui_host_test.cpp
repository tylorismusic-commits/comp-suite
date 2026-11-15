// Comp Suite — LV2 UI host test (X11 + Xvfb)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads comp_suite_ui.so directly, embeds it in a parent window and drives it
// through the idle interface the way a real host does.  This is the only
// automated coverage for the resize logic: without it, a broken ui_resize or
// a stale idle interface would only be caught by hand in Ardour.

#include <dlfcn.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "params.hpp"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

struct HostResize {
  LV2UI_Resize api{};
  int lastWidth = 0;
  int lastHeight = 0;
  int calls = 0;
};

int onResize(void* handle, int width, int height) {
  auto* host = static_cast<HostResize*>(handle);
  host->lastWidth = width;
  host->lastHeight = height;
  ++host->calls;
  return 0;
}

void onWrite(LV2UI_Controller, uint32_t, uint32_t, uint32_t, const void*) {}

using DescriptorFn = const LV2UI_Descriptor* (*)(uint32_t);

} // namespace

int main(int argc, char** argv) {
  const std::string bundle = argc > 1 ? argv[1] : "build/lv2/comp-suite.lv2";
  const std::string library = bundle + "/comp_suite_ui.so";

  void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module) {
    std::printf("FAIL  cannot load %s: %s\n", library.c_str(), dlerror());
    return 1;
  }
  check(true, "UI library loads");

  auto descriptor =
      reinterpret_cast<DescriptorFn>(dlsym(module, "lv2ui_descriptor"));
  check(descriptor != nullptr, "lv2ui_descriptor is exported");
  if (!descriptor) {
    return 1;
  }

  const LV2UI_Descriptor* ui = descriptor(0);
  check(ui != nullptr, "UI descriptor index 0 exists");
  if (!ui) {
    return 1;
  }

  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    std::printf("FAIL  cannot open display (run under xvfb-run)\n");
    return 1;
  }

  const int screen = DefaultScreen(dpy);
  Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
                                      1200, 800, 0, 0, 0);
  XMapWindow(dpy, parent);
  XFlush(dpy);

  HostResize host;
  host.api.handle = &host;
  host.api.ui_resize = &onResize;
  float scale = 1.0f;

  LV2_Feature parentFeature{LV2_UI__parent,
                            reinterpret_cast<void*>(
                                static_cast<uintptr_t>(parent))};
  LV2_Feature resizeFeature{LV2_UI__resize, &host.api};
  LV2_Feature scaleFeature{LV2_UI__scaleFactor, &scale};
  const LV2_Feature* features[] = {&parentFeature, &resizeFeature,
                                   &scaleFeature, nullptr};

  LV2UI_Widget widget = nullptr;
  LV2UI_Handle handle = ui->instantiate(
      ui, "https://github.com/tylorismusic-commits/comp-suite",
      (bundle + "/").c_str(), &onWrite, nullptr, &widget, features);
  check(handle != nullptr, "UI instantiates with a parent window");
  if (!handle) {
    return 1;
  }
  check(widget != nullptr, "UI returns an X11 widget");
  check(host.calls > 0, "UI asks the host for its preferred size");
  check(host.lastWidth == 1040 && host.lastHeight == 640,
        "preferred size is the 1040x640 logical layout");

  const auto* idle = static_cast<const LV2UI_Idle_Interface*>(
      ui->extension_data(LV2_UI__idleInterface));
  check(idle != nullptr, "UI provides the idle interface");
  check(ui->extension_data("http://lv2plug.in/plugins/not-a-thing") == nullptr,
        "unknown extension data is refused");

  const Window window =
      static_cast<Window>(reinterpret_cast<uintptr_t>(widget));

  // Ardour/suil only allows shrinking the plugin window when the X11 window
  // advertises a PMinSize hint; otherwise the minimum equals the current size
  // and resizing is grow-only.
  {
    XSizeHints hints{};
    long supplied = 0;
    const bool ok = XGetWMNormalHints(dpy, window, &hints, &supplied) != 0;
    check(ok && (hints.flags & PMinSize) != 0 && hints.min_width == 520 &&
              hints.min_height == 320,
          "PMinSize hint advertises the 0.5x minimum");
    check(ok && (hints.flags & PMaxSize) != 0 && hints.max_width == 2600 &&
              hints.max_height == 1600,
          "PMaxSize hint advertises the 2.5x maximum");
  }

  bool idled = true;
  if (idle) {
    for (int i = 0; i < 5; ++i) {
      if (idle->idle(handle) != 0) {
        idled = false;
      }
    }
  }
  check(idled, "idle() returns 0 and draws without crashing");

  // Feed a gain-reduction value the way the host would, then idle again.
  if (idle) {
    const float gr = 6.5f;
    ui->port_event(handle, 4 + comp::P_COUNT, sizeof(float), 0, &gr);
    idle->idle(handle);
  }

  // ---------------------------------------------------------------------
  // Resize: the host drags the widget to a size that is not a valid step.
  // The UI must snap it to 1040:640 and tell the host the corrected size.
  // ---------------------------------------------------------------------
  if (idle) {
    host.calls = 0;
    XResizeWindow(dpy, window, 1600, 800);
    XFlush(dpy);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
    }
    XWindowAttributes attrs{};
    XGetWindowAttributes(dpy, window, &attrs);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "snapped to an exact 1040:640 size (got %dx%d)", attrs.width,
                  attrs.height);
    const bool exactRatio =
        attrs.width > 0 && attrs.height > 0 &&
        static_cast<long>(attrs.width) * 640 ==
            static_cast<long>(attrs.height) * 1040;
    check(exactRatio, label);
    check(host.calls > 0 && host.lastWidth == attrs.width &&
              host.lastHeight == attrs.height,
          "UI reports the corrected size back to the host");
  }

  // Shrinking below the minimum must clamp rather than produce a 0-size window.
  if (idle) {
    XResizeWindow(dpy, window, 40, 20);
    XSync(dpy, False);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
    }
    XWindowAttributes attrs{};
    XGetWindowAttributes(dpy, window, &attrs);
    char label[128];
    std::snprintf(label, sizeof(label),
                  "resizing below the minimum clamps to 0.5x (got %dx%d)",
                  attrs.width, attrs.height);
    check(attrs.width >= 520 && attrs.height >= 320, label);
  }

  ui->cleanup(handle);
  XDestroyWindow(dpy, parent);
  XCloseDisplay(dpy);
  dlclose(module);
  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
