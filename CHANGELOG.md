# Changelog

## 0.7.0 — 2026-11-15

### Added

- **Transfer-curve display.** A fifth meter mode draws the compressor's
  input/output curve with a live operating point, in the style of digital
  compressor displays. It follows the selected model's threshold, ratio and
  knee, so it is useful on every model, not only Clean. The meter selector now
  offers GR / +8 / +4 / Graph / Curve / Off.
- **README** rewritten around the current eight-model state, with screenshots
  and per-format download instructions.
- **Split release assets.** Releases now publish the LV2 and CLAP bundles as
  their own archives as well as the combined tarball.

### Notes

- `P_METER` now runs 0-5. Sessions saved with an older build still load; the
  new value only selects the additional display.

## 0.6.2 — 2026-11-15

### Changed

- **Clean now uses the same -40..0 dB threshold range as the VCA and Molot.**
  At identical knob positions it was compressing roughly twice as much as the
  others because its threshold sat 10 dB lower. Measured at -6 dBFS with
  threshold/ratio/attack/release at 0.5: Clean 10.6 dB became 5.6 dB, against
  Molot 5.5 and VCA 7.0. Clean's presets were re-solved for the new range.

### Fixed

- Host value text for Clean's Output (-30..+60 dB) and Molot's Make-up
  (+/-12 dB) and side-chain filter (40..260 Hz) reported the wrong ranges.
- Molot's ratio readout now says "Limit" above 10.9:1, where the transfer
  function switches to limiter mode.

## 0.6.1 — 2026-11-15

### Fixed

- **Clean distorted heavily.** `timeCoeff()` returns the *retention* factor, but
  the Clean compressor used it as the update weight, inverting both the RMS
  detector and the gain smoother. The "RMS" tracked the instantaneous signal,
  so the gain moved at audio rate: measured -16 dB THD with the colour stage
  off, versus -62 dB now.
- **Molot's Attack Mode was reversed.** The reference maps mode 0 (Alpha) to
  the sharp/exp curve; the port had it the other way round.
- **Molot's Saturation switch did nothing.** The code compared the normalised
  distortion value (0-1, i.e. P_DISTORTION/3) against 0.5, so turning SAT on
  (value 1 = 0.33) left it off.
- **Host parameter text named the wrong models.** The CLAP string table still
  had six entries and clamped, so Clean and Molot both read as "VCA" — three
  VCA entries in REAPER's model list. It now uses the full label table.
- **Value text for every parameter.** Knee, Sat Drive, Attack Mode, Stereo
  Mode, Auto Makeup, Sidechain source, True Peak, VCA Link and VCA Auto now
  report readable values instead of bare numbers, and Input/Threshold/Attack/
  Release report dB and ms for the Clean and Molot models.

### Note

- Parameter *names* stay generic ("Threshold", "Input") across models so host
  automation remains stable; the model selector and value text carry the
  model-specific meaning.

## 0.6.0 — 2026-11-15

### Added

- **Molot model** — a character compressor in the Molot family, with the olive
  and maroon palette from the reference. Ported from the MIT JSFX reference:
  diode-shaped attack/release, polynomial soft-knee transfer function, optional
  saturator, and six stereo modes (Mono, Dual Mono, M, S, M/S, Stereo). The
  reference's 99-entry `pow()` approximation table is replaced by `pow()`
  itself.
- New parameters: **Sat Drive**, **Attack Mode** (Alpha/Sigma) and **Stereo
  Mode**.
- Four Molot presets: Drum Smash, Vocal Grit, Mix Bus, Bass Grip.

### Changed

- **Clean is now the first tab**, left of 76Blue, so the transparent model is
  the default choice. The model parameter is unchanged — the tab strip maps
  display order to model value, so saved sessions and presets still point at
  the right model.
- **Clean's colour stage moved after the output limiter**, immediately before
  the plugin output, as on the reference. With Off the model is a plain digital
  compressor; the limiter and the colour no longer interact.

### Fixed

- **Clean's distortion modes were far too heavy.** They were normalised
  `tanh(kx)/k` with k up to 6, which cost ~10 dB at a full-scale peak, so
  "Hard" read as heavy compression rather than subtle colour. Drive is now 0.35
  and 0.75 (0.08 and 0.32 dB at a -6 dBFS peak), and Clip is a soft ceiling at
  0.85 that is silent below it instead of a hard clip at 0 dBFS.

## 0.5.0 — 2026-11-15

### Added

- **Clean model** — a transparent digital compressor, styled after a
  workstation's "Platinum Digital" mode (navy panel, silver knobs, cyan
  accent, following the reference). Stereo-linked RMS detector, soft-knee gain
  computer with a continuous Knee control, attack 0.1-200 ms, release 5-5000 ms,
  and Off/Soft/Hard/Clip distortion as the only optional colour. Off is
  bit-clean.
- New `knee` parameter (0-1, mapped to 0-18 dB), shown on the Clean model.
- Four Clean factory presets: Vocal Control, Drum Control, Mix Glue, Bass
  Control.

### Infrastructure for more models

- The model tab bar is now sized from the model count rather than hard-coded to
  six, and the preview tool renders every model in the enum.
- `P_MODEL` now runs 0-6.

## 0.4.5 — 2026-11-15

### Changed

- **Right column aligned to one axis (x=888).** The Side Chain / Output tabs,
  the Limiter button, the Threshold knob and the Xpress distortion buttons had
  been centred on three different x positions, so the tabs read left-justified
  and the distortion buttons right-justified. All now share the same centre;
  the tabs are 68 px wide and the distortion buttons 60 px so the group fits
  between the gain-reduction LEDs and the output meter's scale.
- **Input and output knobs moved clear of the stereo meters.** Input moved from
  x=112 to 120 and the input meter from x=28 to 20, so the knob's scale labels
  no longer sit against the meter's. Output moved from x=930/934 to 912, clear
  of the output meter's dB numbers.
- Side Chain / Output tab labels drop to 11 px so the text has padding inside
  the narrower tabs.

## 0.4.4 — 2026-11-15

### Changed

- **Stereo level meters.** Input and output are now drawn as L/R pairs instead
  of a single mono bar. The engine publishes per-channel peaks, CLAP forwards
  them to the editor, and the LV2 build exposes `in_peak_l`, `in_peak_r`,
  `out_peak_l` and `out_peak_r` output ports in place of the mono
  `in_peak`/`out_peak`.
- **Side Chain / Output tabs moved** out of the output meter's scale numbers.
  They now sit in the gap between the gain-reduction LEDs and the meter scale:
  x 776-932 at the same height.

### Note

- The LV2 meter ports changed symbol. Meter outputs carry no session state, so
  saved sessions are unaffected; a host that had explicitly bound the old
  `in_peak`/`out_peak` ports would need to re-scan.

## 0.4.3 — 2026-11-15

### Changed

- **All 76 presets now start with Input at 50%** (the "24" mark). The previous
  retune preserved each preset's gain reduction from v0.2.0, but those targets
  were themselves too hot: presets opened already deep into compression with no
  headroom, so the input knob had nowhere useful to go. Set by ear against drum
  loops rather than against the old numbers.

  Measured range after the change: 3.2-6.4 dB of reduction on a -6 dBFS tone and
  7.2-12.2 dB at 0 dBFS, so the presets still differ from each other (Vocal
  Gentle 3.2 vs Vocal Slam 6.4) while all leaving room to push.

  The retune tool still targets -12 dBFS, which is fine for the other models but
  reads as "no reduction" for the 76 at this input setting; the 76 presets are
  now tuned by ear and only the engine's behaviour is regression-tested.

## 0.4.2 — 2026-11-15

### Fixed

- **The 76 nulled the audio on transients.** A feedback model's static slope is
  `ratio - 1` (19 dB/dB at 20:1), and it was unbounded: a 6 dB burst asked for
  114 dB of extra reduction, `dbToGain` returned ~1e-6, and the output dropped
  ~50 dB below its steady level before recovering. Measured on a 32 ms burst:
  76 dB of reduction, output at -70 dBFS. The gain computer now has a soft
  ceiling (linear below 28 dB, asymptotic to 40 dB), so the same burst peaks at
  37.5 dB and the output stays audible. Real control laws saturate; this one
  now does too.
- **Release easing.** The program-dependent release multiplier dropped from 4x
  to 3x now that the ceiling bounds the loop. The 20:1 hunting test still
  passes at 0.28 dB spread.
- **Limiter switch could not be turned off.** It was a plain button that always
  wrote 1.0. It is a latching pill now.
- **VCA presets left auto release on.** Selecting a preset while Auto Release
  was engaged left the release knob doing nothing. Every VCA preset now writes
  auto-release off and link fully on.
- **Two 76 presets were set past the usable range.** Snare Crack and Drum Smash
  sat at 9 and 20 dB of standing reduction, so there was no headroom and no
  breathing. Retuned to 5 and 10 dB; all 33 presets are back within 0.03 dB of
  their intended reduction.

### Added

- Tests: transient nulling (max reduction bounded), and every VCA preset resets
  auto release.

## 0.4.1 — 2026-11-15

### Fixed

- **External side-chain pinned the 76 models.** The gain computer applied the
  feedback slope (`ratio - 1`) whenever the model was feedback, including when
  an external side-chain had bypassed the loop. At 0.5 amplitude that produced
  50+ dB of reduction and clamped the detector. The loop slope now only applies
  while the loop is actually closed; an external side-chain uses the
  feed-forward slope like the other models.
- **Control placement.** The new Auto Makeup / True Peak / Sidechain switches
  sat in the model rows, where they collided with the output knob, the Xpress
  distortion buttons, and (on every model) the right-hand output meter. They
  are now compact latching pills in the otherwise empty right side of the
  header, so they cannot collide with model-specific controls. The VCA Auto
  Release switch moved up so both of its labels stay on the faceplate.

### Added

- `comp_layout` test: builds every model and both right-hand pages, and fails
  if any two controls overlap or if a control leaves the faceplate. This is
  what caught the collisions above, and it guards the next layout change.

## 0.4.0 — 2026-11-14

Feature pass. This release is allowed to add parameters and ports.

### External side-chain

- New stereo side-chain inputs on both formats: CLAP exposes them as a second
  input port, LV2 as `sc_in_l`/`sc_in_r` after the parameter block.
- A **Sidechain / Internal-External** switch decides whether detection follows
  the host routing or the plugin's own input. External side-chain bypasses the
  feedback loop, as it must.
- The LV2 side-chain ports carry `lv2:portProperty lv2:isSideChain`.

### Auto-makeup

- Tracks the average gain reduction over 2 s and restores the level, clamped to
  +24 dB. Applied to the wet signal only, so a parallel blend keeps its dry
  share. Off by default; the manual Makeup/Gain controls are unchanged.

### True peak

- The output limiter can detect inter-sample peaks with a 4x Catmull-Rom
  detector (the same one Brick uses). Off by default.

### VCA

- **Link** control: 1 = both channels follow the louder one (bus behaviour),
  0 = independent detectors (dual mono). Defaults to fully linked.
- **Auto Release** toggle: two-stage automatic release, roughly 100 ms at
  shallow reduction rising to about 1 s as it deepens.

### Detectors

- Attack hold added to the FET and VCA detectors: a short peak hold before
  release stops low-frequency cycles from modulating the gain.
- The factory presets were re-solved against the v0.2.0 targets after the hold
  change; worst deviation 0.04 dB.

### Deferred

- **2x oversampled FET gain cell.** ADAA already linearises the static shaper,
  and the FET's 0.7 ms gain smoothing bounds the modulation bandwidth, so the
  remaining aliasing is small. Doing it properly means adding latency
  compensation that changes when models are switched, which deserves its own
  change rather than a bolt-on at the end of a release.

## 0.3.0 — 2026-11-14

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

### Presets

All 33 factory presets were retuned **by measurement, not by ear**. The
feedback pass changed how input level maps to reduction, so the old values no
longer meant what they did.

- `tools/retune_presets` binary-searches each preset's drive control (input for
  76/Xpress, peak reduction or threshold for the optical and VCA models).
- The targets were each preset's gain reduction measured on the **v0.2.0**
  release, so the intent of every preset — gentle, glue, slam — is preserved
  rather than flattened onto one number.
- Worst deviation from the intended reduction after retuning: 0.03 dB.

### Known

- CPU for the optical models is ~2.3% of one core at 48 kHz stereo, up from
  ~0.6%, because the side-chain runs six filters and several transcendentals
  per sample. The FET models are ~1.7%. Optimisation is deferred to 0.4.0.

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
