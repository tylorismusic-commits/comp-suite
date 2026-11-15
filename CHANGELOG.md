# Changelog

## Unreleased — 0.3.0

DSP structural pass. No new parameters: the port list and ABI stay frozen at
0.2.0, so existing sessions and presets still load.

### Optical models (2A / 3A)

Replaced the peak detector plus ratio/knee gain computer with the side-chain
structure this family actually has:

- **Three-band side-chain shaping** — 10 Hz low shelf (−9 dB), 17 kHz high
  shelf (−6 dB), 220 Hz dip (−3.25 dB, the R37 trim at its midpoint).
- **25/75 RMS/peak blend**, stereo linked on the louder channel.
- **Second gain stage** driven by the compressed side-chain through a 43 Hz
  +30 dB shelf. This is the low-frequency behaviour: bass raises the
  integrated level and pulls the cell back rather than pumping the whole mix
  under it.
- **Four-stage envelope** where each stage references the one before it, so a
  fast stage cannot release ahead of a slow one. That mutual reference is the
  cell's memory.
- **3A** uses the faster cell path and a shorter release on every stage.
- The Comp/Limit switch keeps the 1.2 / 1.3 threshold-curve mapping.

New `src/dsp/biquad.hpp`: RBJ low-shelf, high-shelf and peak sections, plus the
`settleK` one-pole used by the envelope.

Measured in `comp_dsp_test`:

- 40 Hz reduces ~1.4 dB less than 1 kHz at the same input level (the bass
  behaviour of the reference structure).
- Peak reduction spans 0.4 dB to 24.7 dB monotonically across the control.
- 2A and 3A differ in release tail: 1.13 dB vs 0.68 dB residual.
- Maximum reduction on a hot 40 Hz sine stays bounded.

### Feedback topology

- **76 Blue/Black and 2A/3A now detect in the feedback loop**, as the hardware
  does: the side-chain taps the output of the gain cell, not the input. That
  changes the knee and the recovery shape, and it is the main reason a real
  1176 sounds "thick" rather than "grabby". The VCA model stays feed-forward,
  because the bus compressor it follows genuinely is.
- **The static slope had to change.** A feedback loop multiplies the static
  curve by `1/(1 + slope)`, so the gain computer uses `ratio - 1` rather than
  `1 - 1/ratio`. Using the feed-forward slope collapsed the whole 76 ladder to
  about 2:1 and made the four ratio buttons indistinguishable.
- **The detector taps the gain cell before the colour stage.** Tapping after
  the saturation created a self-limiting loop: more reduction lowered the
  detected level, which lowered the reduction, and the ladder inverted.
- **Release lengthens with gain reduction.** The 1176's release is
  program-dependent in practice, and that damping is also what stops a digital
  feedback loop from hunting at high ratios. Without it, 20:1 on a sustained
  tone wobbled by ~1 dB at 1 Hz.
- No topology switch: it is a property of the model, not a user choice.
- Measured: the 76 ladder lands on 4:1 and 8:1 exactly, and 20:1 settles with
  0.2 dB spread. Maximum input, ALL ratio and fastest attack stay bounded.

### Limiter

- **Limiter threshold now defaults to 0 dB** instead of −12 dB, so every model
  and mode starts with the limiter fully open.
- **Corrected the limiter threshold labels.** They were spaced −24 / −12 / −6
  / −3 / 0 over a linear −24..0 dB mapping, so the knob read −6 where the
  threshold was actually −12. They now read −24 / −18 / −12 / −6 / 0.

### Known

- The 31 factory presets have not been retuned. This matters more now than
  before P1: feedback changes how input level maps to gain reduction, so a
  preset's level and amount will not match what it used to do. Retuning is the
  next step of this release.
- CPU for the optical models is ~2% of one core at 48 kHz stereo, up from
  ~0.6%, because the side-chain runs six filters and several transcendentals
  per sample. Optimisation is deferred to the 0.4.0 feature pass.

## 0.2.0

LV2 release. The LV2 build already existed in the source tree but was not
shippable: no presets, no resize, an install path outside the LV2 search
path, and metadata errors that `lv2lint` flags.

### LV2

- **Preset banks.** 33 factory presets are exported as `pset:Preset`
  resources in six banks, so Ardour, REAPER and other hosts list them in their
  own preset menus instead of only inside the plugin's browser.
- **Resizable UI.** The LV2 editor now uses the host's idle interface instead of
  a private 33 ms thread, and redraws are throttled to ~30 Hz. It advertises
  `PMinSize`/`PMaxSize` X11 size hints, which Ardour and suil use for the host
  window's minimum and maximum: without a minimum hint the plugin window
  becomes grow-only, the classic X11 embedding trap. The host owns the size;
  the panel is letterboxed into whatever allocation arrives, and configure
  events never bounce a resize back through the host (doing so re-enters suil's
  allocation loop and makes Ardour flicker and hang).
- **Gain reduction in the mixer strip** through the Harrison inline-display
  extension, matching Hearth Channel.
- **Correct units metadata.** Meter ports use `units:unit units:db`;
  `lv2:designation` had been used for units, which is the wrong property.
  Meter outputs are also marked `lv2:notOnGUI`.
- **Correct install path.** New `LV2_DIR` cache variable. Installing with
  `--prefix ~/.local` previously placed the bundle in `~/.local/lib/lv2`,
  which is not on the default LV2 search path.
- Metadata is generated from `src/params.hpp` and `src/presets.hpp` by
  `tools/gen_ttl.cpp`; `ctest` fails if the committed TTL is stale. The old
  `tools/gen_lv2_ttl.py` duplicated the parameter table and is removed.

### Tests

- LV2 host test now checks port count, parameter symbols, preset count,
  preset-to-plugin attachment, symbol resolution and that every preset sets
  its model.
- New Xvfb LV2 UI test: instantiates the editor, drives the idle interface,
  checks the X11 PMinSize/PMaxSize hints, resizes to an invalid size and checks
  the snapped result, and checks the minimum-scale clamp.

### Docs

- README: formats, install paths, preset banks, URI stability warning. The
  "CLAP only" claim and the wrong bundle path are fixed. Preset count
  corrected from 31 to 33.

## Unreleased (0.1.x)

- Fixed: presets reset the model to 76Blue (unused preset slots zero-filled
  and parameter 0 is Model).
- Fixed: host parameter events could undo a UI change in the same block.
- Fixed: LV2 meter ports declared an inverted range (0 to -120 dB).
- Fixed: NaN or infinite parameter values from a host are ignored, and huge
  values clamp to the declared range.
- Mono or partially connected host buffers no longer crash; the left channel
  is mirrored.
- Preset browser in the header: factory presets grouped by use, each tagged
  with its model, with prev/next arrows and a scrollable list.
- LV2 metadata now carries `doap:name`, so hosts show "Comp Suite" rather than
  the plugin URI.

## 0.1.0

First release: six compressor models in one plugin, as CLAP and LV2.

### Models

- **76Blue** — 1176-family FET limiter, Rev A "bluestripe" voicing.
- **76Black** — 1176-family FET limiter, Rev E "blackface" voicing.
- **2A** — LA-2A-family optical leveler, Compress/Limit.
- **3A** — LA-3A-family optical leveler, fixed ratio.
- **Xpress** — Distressor-family compressor with distortion modes and NUKE.
- **VCA** — SSL-style stereo bus compressor with peak detection.

### Controls

- One shared ratio ladder (1.5, 2, 3, 4, 6, 8, 12, 20, plus ALL on the 76s)
  so the same position means the same ratio across models. 2A and 3A use
  Comp/Limit instead.
- Input is an input attenuator: fully down passes silence, as on the hardware.
- Parallel **Mix** on every model.
- **Limiter** section (on/off plus threshold) on every model; hidden while the
  Side Chain page is open.
- Side-chain high-pass for Xpress and VCA.
- Distortion modes on Xpress (Off / Dist 2 / Dist 3 / Clip), applied after the
  gain cell as in the reference implementation.

### UI

- Logic-style layout: model tabs, meter window with GR / +8 / +4 / Graph / Off,
  LED gain-reduction column, input and output meters with dB markings, section
  labels and a footer strip.
- Each model keeps its own faceplate and knob finish.
- Jost ExtraBold throughout.
- Resizable from 0.5× to 2.5×, always locked to 1040:640.
- Knobs, buttons and stepped controls respond to drag, shift-drag fine mode,
  scroll wheel and click.

### Formats

- CLAP (`CompSuite.clap`, reports gain reduction through
  `clap.gain-adjustment-metering`).
- LV2 (`comp-suite.lv2`, 23 ports, X11 UI, four meter outputs).

### Notes

- Sound is modelled from topology and published behaviour, tuned against a
  stillwell-derived reference implementation measured on this machine. It is
  not measured against hardware units.
- Version 0.1.0 has no preset browser; hosts can save and restore state.
