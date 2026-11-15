// Comp Suite — LV2 plugin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Six compressor models in one plugin.  The control ports mirror the CLAP
// parameter ids: port = kPortFirstParam + ParamId.

#include <lv2/core/lv2.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "dsp/engine.hpp"
#include "dsp/inline_display.hpp"
#include "params.hpp"

namespace {

constexpr const char* kPluginUri = "https://github.com/tylorismusic-commits/comp-suite";

// Audio ports first, then controls, then meter outputs.
enum Port : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_FIRST_PARAM = 4,
  // External side-chain inputs.  They sit after the parameters so existing
  // parameter port indices never move.
  PORT_SC_IN_L = PORT_FIRST_PARAM + comp::P_COUNT,
  PORT_SC_IN_R,
  PORT_GR_METER,
  PORT_OUT_PEAK_L,
  PORT_OUT_PEAK_R,
  PORT_IN_PEAK_L,
  PORT_IN_PEAK_R,
  PORT_OUT_VU,
  PORT_COUNT
};

struct Plugin {
  float sampleRate = 48000.0f;
  float lastPort[comp::P_COUNT] = {};
  bool portsSeen = false;
  comp::Shared shared;
  comp::Engine engine;
  comp::Meters meters;

  std::unique_ptr<float*[]> ports;

  float* audioInL = nullptr;
  float* audioInR = nullptr;
  float* audioOutL = nullptr;
  float* audioOutR = nullptr;
  float* scInL = nullptr;
  float* scInR = nullptr;
  float* gr = nullptr;
  float* outPeakL = nullptr;
  float* outPeakR = nullptr;
  float* inPeakL = nullptr;
  float* inPeakR = nullptr;
  float* outVu = nullptr;

  comp::InlineDisplay display;
  const comp::InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*,
                       const LV2_Feature* const* features) {
  auto* p = new Plugin();
  p->sampleRate = static_cast<float>(rate);
  p->engine.prepare(p->sampleRate);
  // Start from the defaults already in Shared.
  p->engine.setControls(comp::controlsFrom(p->shared));

  // Ardour's mixer-strip gain-reduction display.
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, comp::kInlineDisplayQueueDrawURI) &&
        (*f)->data) {
      p->queueDraw = static_cast<const comp::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  return p;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data) {
  auto* p = static_cast<Plugin*>(handle);
  switch (port) {
    case PORT_AUDIO_IN_L: p->audioInL = static_cast<float*>(data); break;
    case PORT_AUDIO_IN_R: p->audioInR = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_L: p->audioOutL = static_cast<float*>(data); break;
    case PORT_AUDIO_OUT_R: p->audioOutR = static_cast<float*>(data); break;
    case PORT_SC_IN_L: p->scInL = static_cast<float*>(data); break;
    case PORT_SC_IN_R: p->scInR = static_cast<float*>(data); break;
    case PORT_GR_METER: p->gr = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_L: p->outPeakL = static_cast<float*>(data); break;
    case PORT_OUT_PEAK_R: p->outPeakR = static_cast<float*>(data); break;
    case PORT_IN_PEAK_L: p->inPeakL = static_cast<float*>(data); break;
    case PORT_IN_PEAK_R: p->inPeakR = static_cast<float*>(data); break;
    case PORT_OUT_VU: p->outVu = static_cast<float*>(data); break;
    default:
      // Control input: stash the pointer so run() can read it.
      if (!p->ports) {
        p->ports = std::make_unique<float*[]>(PORT_COUNT);
        std::memset(p->ports.get(), 0, sizeof(float*) * PORT_COUNT);
      }
      if (port < PORT_COUNT) {
        p->ports[port] = static_cast<float*>(data);
      }
      break;
  }
}

void activate(LV2_Handle handle) {
  auto* p = static_cast<Plugin*>(handle);
  p->engine.reset();
}

void deactivate(LV2_Handle) {}

void run(LV2_Handle handle, uint32_t frames) {
  auto* p = static_cast<Plugin*>(handle);
  if (!p->audioInL || !p->audioOutL) {
    return;
  }
  // Mono or partially connected hosts: mirror the left channel.
  if (!p->audioInR) {
    p->audioInR = p->audioInL;
  }
  if (!p->audioOutR) {
    p->audioOutR = p->audioOutL;
  }

  // Pull host parameter values in, then configure the engine.  Only values
  // that actually changed are applied, so a port the host has not refreshed
  // yet cannot undo a change made in the UI.
  if (p->ports) {
    for (uint32_t i = 0; i < comp::P_COUNT; ++i) {
      float* v = p->ports[PORT_FIRST_PARAM + i];
      if (!v) {
        continue;
      }
      if (!p->portsSeen || *v != p->lastPort[i]) {
        p->shared.setFromHost(i, static_cast<double>(*v));
        p->lastPort[i] = *v;
      }
    }
    p->portsSeen = true;
  }
  p->engine.setControls(comp::controlsFrom(p->shared));

  // In-place when the host provides the same buffer, otherwise copy.
  if (p->audioOutL != p->audioInL) {
    std::memcpy(p->audioOutL, p->audioInL, frames * sizeof(float));
  }
  if (p->audioOutR != p->audioInR) {
    std::memcpy(p->audioOutR, p->audioInR, frames * sizeof(float));
  }

  p->engine.process(p->audioOutL, p->audioOutR, static_cast<int>(frames),
                    p->meters, p->scInL, p->scInR ? p->scInR : p->scInL);

  if (p->gr) {
    *p->gr = p->meters.grDb;
  }
  if (p->outPeakL) {
    *p->outPeakL = p->meters.outPeakLDb;
  }
  if (p->outPeakR) {
    *p->outPeakR = p->meters.outPeakRDb;
  }
  if (p->inPeakL) {
    *p->inPeakL = p->meters.inPeakLDb;
  }
  if (p->inPeakR) {
    *p->inPeakR = p->meters.inPeakRDb;
  }
  if (p->outVu) {
    *p->outVu = p->meters.outVuDb;
  }

  // Feed the Ardour mixer-strip display.  The render itself happens on the
  // host's GUI thread; the audio thread only publishes a number and asks for
  // a redraw every 32 blocks.
  p->display.setGainReductionDb(p->meters.grDb);
  if (p->queueDraw && p->queueDraw->queue_draw &&
      ((p->drawCounter++ & 31u) == 0u)) {
    p->queueDraw->queue_draw(p->queueDraw->handle);
  }
}

void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }

comp::InlineDisplaySurface* renderInlineDisplay(void* instance, uint32_t width,
                                                uint32_t height) {
  auto* p = static_cast<Plugin*>(instance);
  return p ? p->display.render(width, height) : nullptr;
}

const comp::InlineDisplayInterface kInlineDisplay = {&renderInlineDisplay};

const void* extensionData(const char* uri) {
  if (uri && !std::strcmp(uri, comp::kInlineDisplayInterfaceURI)) {
    return &kInlineDisplay;
  }
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, instantiate, connectPort, activate, run, deactivate,
    cleanup, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
