// Comp Suite — minimal CLAP host smoke test.
// Loads the built bundle, drives audio and parameters, round-trips state,
// and opens the editor on the current X display (run under xvfb-run).
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/all.h>

#include "params.hpp"
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XShm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++gFailures;
  }
}

constexpr uint32_t kFrames = 512;

struct HostEvents {
  std::vector<clap_event_param_value_t> inputs;
  std::vector<clap_event_param_value_t> outputs;
};

uint32_t inSize(const clap_input_events_t* list) {
  return static_cast<uint32_t>(static_cast<HostEvents*>(list->ctx)->inputs.size());
}

const clap_event_header_t* inGet(const clap_input_events_t* list, uint32_t index) {
  return &static_cast<HostEvents*>(list->ctx)->inputs[index].header;
}

bool outTryPush(const clap_output_events_t* list, const clap_event_header_t* hdr) {
  auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
  static_cast<HostEvents*>(list->ctx)->outputs.push_back(*ev);
  return true;
}

clap_host_t makeHost() {
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "comp-suite-test";
  host.vendor = "tylorismusic";
  host.version = "1.0";
  return host;
}

const clap_host_t kHost = makeHost();

clap_event_param_value_t paramEvent(uint32_t id, double value) {
  clap_event_param_value_t ev{};
  ev.header.size = sizeof(ev);
  ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev.header.type = CLAP_EVENT_PARAM_VALUE;
  ev.param_id = id;
  ev.note_id = -1;
  ev.port_index = -1;
  ev.channel = -1;
  ev.key = -1;
  ev.value = value;
  return ev;
}

struct MemStream {
  std::vector<unsigned char> data;
  size_t readPos = 0;
};

int64_t streamWrite(const clap_ostream_t* s, const void* buf, uint64_t size) {
  auto* m = static_cast<MemStream*>(s->ctx);
  const auto* bytes = static_cast<const unsigned char*>(buf);
  m->data.insert(m->data.end(), bytes, bytes + size);
  return static_cast<int64_t>(size);
}

int64_t streamRead(const clap_istream_t* s, void* buf, uint64_t size) {
  auto* m = static_cast<MemStream*>(s->ctx);
  const size_t n = std::min<size_t>(size, m->data.size() - m->readPos);
  std::memcpy(buf, m->data.data() + m->readPos, n);
  m->readPos += n;
  return static_cast<int64_t>(n);
}

} // namespace

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "build/clap/CompSuite.clap";
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  check(lib != nullptr, "bundle loads");
  if (!lib) {
    std::printf("%s\n", dlerror());
    return 1;
  }
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  check(entry != nullptr, "clap_entry symbol present");
  if (!entry || !entry->init(path)) {
    return 1;
  }

  auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  check(factory && factory->get_plugin_count(factory) == 1, "one plugin in factory");

  const clap_plugin_t* plugin =
      factory->create_plugin(factory, &kHost, "com.tylorismusic.compsuite");
  check(plugin != nullptr, "plugin instantiates");
  if (!plugin) {
    return 1;
  }
  check(plugin->init(plugin), "plugin init");

  auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  auto* meter = static_cast<const clap_plugin_gain_adjustment_metering_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GAIN_ADJUSTMENT_METERING));
  check(params && params->count(plugin) == comp::P_COUNT,
        "parameter count matches the table");
  check(state != nullptr, "state extension present");
  check(gui != nullptr, "gui extension present");
  check(meter != nullptr, "gain-adjustment metering present");

  clap_param_info_t info{};
  check(params->get_info(plugin, 0, &info) && std::strcmp(info.name, "Model") == 0,
        "parameter 0 is Model");

  check(plugin->activate(plugin, 48000.0, kFrames, kFrames), "activate");
  check(plugin->start_processing(plugin), "start processing");

  // Process a loud sine through the 76Blue model with the 'all' ratio, so it
  // reduces gain, and check the output stays finite and bounded.
  HostEvents events;
  events.inputs.push_back(paramEvent(0, 0));   // model 76Blue
  events.inputs.push_back(paramEvent(4, 8.0)); // ratio: ALL (top of the ladder)
  events.inputs.push_back(paramEvent(2, 0.8)); // input drive
  clap_input_events_t inEvents{&events, inSize, inGet};
  clap_output_events_t outEvents{&events, outTryPush};

  std::vector<float> left(kFrames), right(kFrames);
  float* chans[2] = {left.data(), right.data()};
  clap_audio_buffer_t inBuf{};
  inBuf.data32 = chans;
  inBuf.channel_count = 2;
  clap_audio_buffer_t outBuf{};
  outBuf.data32 = chans;
  outBuf.channel_count = 2;

  bool finite = true;
  float peakOut = 0.0f;
  for (int block = 0; block < 200; ++block) {
    for (uint32_t i = 0; i < kFrames; ++i) {
      const double t = (block * kFrames + i) / 48000.0;
      left[i] = right[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 110.0 * t));
    }
    clap_process_t proc{};
    proc.frames_count = kFrames;
    proc.audio_inputs = &inBuf;
    proc.audio_outputs = &outBuf;
    proc.audio_inputs_count = 1;
    proc.audio_outputs_count = 1;
    proc.in_events = &inEvents;
    proc.out_events = &outEvents;
    plugin->process(plugin, &proc);
    for (uint32_t i = 0; i < kFrames; ++i) {
      if (!std::isfinite(left[i]) || !std::isfinite(right[i])) {
        finite = false;
      }
      peakOut = std::max(peakOut, std::fabs(left[i]));
    }
    events.inputs.clear();
  }
  check(finite, "host-driven processing stays finite");
  check(peakOut > 0.05f && peakOut < 4.0f, "processed output has sane level");
  check(meter->get(plugin) < -1.0, "gain-adjustment metering reports reduction");

  double value = 0.0;
  check(params->get_value(plugin, 4, &value) && std::fabs(value - 8.0) < 1e-9,
        "ratio value round-trips through the host");

  // State round trip: save, change, restore.
  MemStream saved;
  clap_ostream_t out{&saved, streamWrite};
  check(state->save(plugin, &out) && saved.data.size() > 0, "state saves");
  events.inputs.push_back(paramEvent(4, 3.0));
  clap_input_events_t changeEvents{&events, inSize, inGet};
  clap_process_t changeProc{};
  changeProc.frames_count = 0;
  changeProc.in_events = &changeEvents;
  changeProc.out_events = &outEvents;
  changeProc.audio_inputs = &inBuf;
  changeProc.audio_outputs = &outBuf;
  changeProc.audio_inputs_count = 1;
  changeProc.audio_outputs_count = 1;
  plugin->process(plugin, &changeProc);
  events.inputs.clear();
  params->get_value(plugin, 4, &value);
  check(std::fabs(value - 3.0) < 1e-9, "ratio changed to 4:1 before restore");

  MemStream readBack;
  readBack.data = saved.data;
  clap_istream_t in{&readBack, streamRead};
  check(state->load(plugin, &in), "state loads");
  params->get_value(plugin, 4, &value);
  check(std::fabs(value - 8.0) < 1e-9, "state restore brings back ratio");

  // Editor on the current display, if there is one.
  if (gui) {
    Display* probe = XOpenDisplay(nullptr);
    if (probe) {
      XCloseDisplay(probe);
      const bool api = gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false);
      check(api, "x11 embedding supported");
      check(gui->create(plugin, CLAP_WINDOW_API_X11, false), "gui create");
      Display* dpy = XOpenDisplay(nullptr);
      Window parent = XCreateSimpleWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)), 0, 0,
                                          1040, 640, 0, 0, 0);
      XMapWindow(dpy, parent);
      XFlush(dpy);
      clap_window_t win{};
      win.api = CLAP_WINDOW_API_X11;
      win.x11 = parent;
      check(gui->set_parent(plugin, &win), "gui set_parent");
      // The editor creates its window as a child of `parent`.  Find it and
      // remember where it sits on screen so synthetic events land on it.
      int originX = 0, originY = 0;
      {
        Window rootRet, parentRet, *kids = nullptr;
        unsigned n = 0;
        if (XQueryTree(dpy, parent, &rootRet, &parentRet, &kids, &n) && n > 0) {
          Window child = 0;
          for (unsigned i = 0; i < n; ++i) {
            XWindowAttributes a;
            if (XGetWindowAttributes(dpy, kids[i], &a) && a.width >= 1000) {
              child = kids[i];
            }
          }
          if (child) {
            Window rr;
            int wx, wy;
            XTranslateCoordinates(dpy, child, RootWindow(dpy, DefaultScreen(dpy)),
                                  0, 0, &originX, &originY, &rr);
            unsigned int gw = 0, gh = 0, gb = 0, gd = 0;
            XGetGeometry(dpy, child, &rr, &wx, &wy, &gw, &gh, &gb, &gd);
          }
          XFree(kids);
        }
        std::printf("  editor origin on screen: %d,%d\n", originX, originY);
      }
      check(gui->show(plugin), "gui show");
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      uint32_t w = 0, h = 0;
      check(gui->get_size(plugin, &w, &h) && w == 1040 && h == 640, "gui size is 1040x640");

      // The panel must actually contain pixels, not just a mapped window.
      {
        Window rootRet, parentRet, *kids = nullptr;
        unsigned n = 0;
        Window child = 0;
        if (XQueryTree(dpy, parent, &rootRet, &parentRet, &kids, &n)) {
          for (unsigned i = 0; i < n; ++i) {
            XWindowAttributes a;
            if (XGetWindowAttributes(dpy, kids[i], &a) && a.width >= 1000) {
              child = kids[i];
            }
          }
          XFree(kids);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        bool painted = false;
        if (child) {
          XImage* img = XGetImage(dpy, child, 0, 0, 200, 120, AllPlanes, ZPixmap);
          if (img) {
            long sum = 0;
            for (int y = 0; y < 120; ++y) {
              for (int x = 0; x < 200; ++x) {
                const unsigned long p = XGetPixel(img, x, y);
                sum += ((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff);
              }
            }
            painted = sum > 0;
            XDestroyImage(img);
          }
        }
        check(painted, "editor window contains rendered pixels");
      }

      uint32_t aw = 1234, ah = 100;
      check(gui->adjust_size(plugin, &aw, &ah) && aw == 520 && ah == 320,
            "adjust_size clamps to the minimum and keeps the aspect ratio");
      check(gui->set_size(plugin, 1560, 900) && gui->get_size(plugin, &w, &h) &&
                w == 1560 && h == 960,
            "set_size enforces the 1040:640 aspect ratio");
      check(gui->set_size(plugin, 3000, 3000) && gui->get_size(plugin, &w, &h) &&
                w == 2600 && h == 1600,
            "set_size clamps to the maximum scale");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      check(gui->set_size(plugin, 1040, 640), "set_size back to nominal");
      // A real drag must move a knob.  This catches the X11 event-mask bug
      // where drags produced no MotionNotify and every knob was dead while
      // buttons still worked.
      auto drag = [&](int x, int y, int dy) {
        x += originX;
        y += originY;
        XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
        XFlush(dpy);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
        XFlush(dpy);
        const int steps = 10;
        for (int i = 1; i <= steps; ++i) {
          XTestFakeMotionEvent(dpy, -1, x, y + dy * i / steps, CurrentTime);
          XFlush(dpy);
          std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
        XFlush(dpy);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
      };
      auto selectModel = [&](double model) {
        events.inputs.push_back(paramEvent(0, model));
        clap_input_events_t sel{&events, inSize, inGet};
        params->flush(plugin, &sel, nullptr);
        events.inputs.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
      };
      auto value = [&](uint32_t id) {
        double v = 0.0;
        params->get_value(plugin, id, &v);
        return v;
      };

      selectModel(0.0);  // 76Blue
      double inBefore = value(2);
      drag(112, 520, -50);
      std::printf("  76 input %.2f -> %.2f\n", inBefore, value(2));
      check(value(2) > inBefore + 0.05, "dragging 76 INPUT moves it");
      double outBefore = value(8);
      drag(930, 500, -50);
      std::printf("  76 output %.2f -> %.2f\n", outBefore, value(8));
      check(value(8) > outBefore + 0.05, "dragging 76 OUTPUT moves it");

      selectModel(4.0);  // Xpress: DISTORTION advances on click at (890,260)
      drag(880, 411, 0);  // the DIST 3 button
      std::printf("  Xpress distortion mode -> %.2f\n", value(11));
      check(value(11) == 2.0, "clicking DIST 3 selects that mode");

      // Text values must be readable, not bare numbers.
      {
        char buf[128] = {0};
        params->value_to_text(plugin, 0, 0.0, buf, sizeof(buf));
        check(std::strcmp(buf, "76Blue") == 0, "model reads as a name");
        params->value_to_text(plugin, 11, 2.0, buf, sizeof(buf));
        check(std::strcmp(buf, "Dist 3") == 0, "distortion reads as a mode");
        params->value_to_text(plugin, 13, 0.0, buf, sizeof(buf));
        check(std::strcmp(buf, "Off") == 0, "limiter reads as On/Off");
      }

      selectModel(5.0);  // VCA: RATIO knob at (400,424)
      double ratioBefore = value(4);
      drag(400, 424, 60);
      std::printf("  VCA ratio %.2f -> %.2f\n", ratioBefore, value(4));
      check(value(4) < ratioBefore, "dragging VCA RATIO moves it");

      selectModel(2.0);  // 2A: GAIN at (560,446)
      double gainBefore = value(7);
      drag(560, 446, -50);
      std::printf("  2A gain %.2f -> %.2f\n", gainBefore, value(7));
      check(value(7) > gainBefore + 0.05, "dragging 2A GAIN moves it");

      // Mono buffers must not crash: the plugin mirrors the left channel.
      {
        std::vector<float> mono(kFrames);
        float* one[2] = {mono.data(), nullptr};
        clap_audio_buffer_t monoBuf{};
        monoBuf.data32 = one;
        monoBuf.channel_count = 1;
        clap_process_t monoProc{};
        monoProc.frames_count = kFrames;
        monoProc.audio_inputs = &monoBuf;
        monoProc.audio_outputs = &monoBuf;
        monoProc.audio_inputs_count = 1;
        monoProc.audio_outputs_count = 1;
        monoProc.in_events = nullptr;
        monoProc.out_events = nullptr;
        for (uint32_t i = 0; i < kFrames; ++i) {
          mono[i] = 0.3f * std::sin(0.05f * static_cast<float>(i));
        }
        plugin->process(plugin, &monoProc);
        bool ok = true;
        for (uint32_t i = 0; i < kFrames; ++i) {
          if (!std::isfinite(mono[i])) {
            ok = false;
          }
        }
        check(ok, "mono buffers process without crashing");
      }

      gui->hide(plugin);
      gui->destroy(plugin);
      XDestroyWindow(dpy, parent);
      XCloseDisplay(dpy);
      check(true, "gui create/embed/destroy completes");
    } else {
      std::printf("SKIP  editor (no X display)\n");
    }
  }

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(lib);

  std::printf("%d failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
