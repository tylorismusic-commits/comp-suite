# Changelog

## Unreleased

- Fixed: presets reset the model to 76Blue (unused preset slots zero-filled
  and parameter 0 is Model).
- Fixed: host parameter events could undo a UI change in the same block.
- Fixed: LV2 meter ports declared an inverted range (0 to -120 dB).
- Fixed: NaN or infinite parameter values from a host are ignored, and huge
  values clamp to the declared range.
- Mono or partially connected host buffers no longer crash; the left channel
  is mirrored.
- Preset browser in the header: 31 factory presets grouped by use, each tagged
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
