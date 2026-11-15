# Comp Suite

**Version 0.6.0** · GPL-3.0-or-later

A free CLAP and LV2 compressor with six models in one plugin:

| Model   | Modelled on                         | Core                          |
|---------|-------------------------------------|-------------------------------|
| 76Blue  | 1176-family FET, blue-stripe voicing | FET limiter                  |
| 76Black | 1176-family FET, black-face voicing  | FET limiter                  |
| 2A      | LA-2A-family optical leveler        | Photocell, side-chain network, Limit/Compress |
| 3A      | LA-3A-family optical leveler        | Faster photocell, fixed ratio |
| Xpress  | Distressor-family compressor        | Fixed threshold, ratio steps, distortion modes |
| VCA     | SSL-style stereo bus compressor     | RMS-detected VCA              |

The panel follows a Logic-style layout: model tabs across the top, a meter
window with a meter-mode selector, an LED gain-reduction column, input and
output meters, and a right-hand strip. Each model shows only the controls it
has. Labels use Jost ExtraBold.

## Knob artwork

Knobs are drawn procedurally (metallic cap, white pointer, contact shadow) so
every model shares one finish and the plugin ships no third-party artwork.

## Formats

- **CLAP** — `build/clap/CompSuite.clap` + `CompSuite.resources/`, reports gain
  reduction through `clap.gain-adjustment-metering`.
- **LV2** — `build/lv2/comp-suite.lv2/` (plugin + X11 UI + preset banks),
  `lv2lint`-clean, draws gain reduction in Ardour's mixer strip through the
  Harrison inline-display extension. Ports mirror the CLAP parameter ids:
  `port = 4 + ParamId`, followed by four meter outputs (GR, output peak,
  input peak, output VU).
- **Presets** — 33 factory presets, grouped into six banks. They are visible in
  the plugin's own browser and in LV2 host preset menus (Ardour, REAPER).

The plugin URI (`https://github.com/tylorismusic-commits/comp-suite`) is part
of the saved-session format. **Do not rename the repository or the GitHub
account**: every existing session would lose its plugin state.

## Building

Requires CMake 3.16+, a C++17 compiler, and the Cairo, FreeType, and X11
development packages. `lilv` (for the LV2 host test), `xvfb-run` and
`libxtst` are optional but recommended.

```sh
cmake -S . -B build
cmake --build build -j
cd build && ctest      # DSP, geometry, CLAP host, LV2 host, LV2 UI, metadata
```

Install for the current user. `LV2_DIR` matters: `~/.local/lib/lv2` is **not**
on the default LV2 search path, so set it at configure time (it is a cache
variable, not an install-time flag).

```sh
cmake -S . -B build -DLV2_DIR="$HOME/.lv2" -DCLAP_DIR="$HOME/.clap"
cmake --build build -j
cmake --install build
```

Or copy the built bundles by hand:

```sh
cp -r build/clap/CompSuite.clap build/clap/CompSuite.resources ~/.clap/
cp -r build/lv2/comp-suite.lv2 ~/.lv2/
```

A release tarball with both bundles and a PKGBUILD is produced by:

```sh
cmake --build build --target dist
```

To render every faceplate to PNG without a host, optionally at a scale:

```sh
build/comp_ui_preview assets/fonts /tmp/out 1.5
```

## Status and limits

- **Measured, not guessed:** the models are tuned against a reference
  implementation measured on this machine — Tukan's stillwell-derived JSFX
  (BSD-3 licensed), rendered headlessly in REAPER.  See "Measurement" below.

- **Knob indicators are normalised to each parameter's own range.** They were
  assumed to be 0..1, so the 0..8 ratio ladder snapped the pointer to the ends.
- **ADAA state follows drive changes.** The stored antiderivative is recomputed
  when the drive moves; before, near signal peaks the mismatch was amplified
  into a burst. This was the "slap" on kicks and much of the Xpress distortion
  character.
- **Input gain is smoothed** (25 ms) and the saturation drive is smoothed
  with it, so moving the input knob no longer crackles.
- **DC blocker** after the asymmetric stages: low-frequency transients no
  longer offset the stage (the "fart" on kicks).
- **VCA detection is peak-based**: with an RMS detector a normal-level signal
  sat below the default threshold and the ratio control appeared dead.
- **Stepped controls drag:** ratio, model and meter are stepped parameters.
  They previously only advanced on click, which made the VCA ratio knob look
  dead; they now drag at 18 px per step.
- **Xpress NUKE:** the ninth ratio slot, analogous to the 76s' ALL, with the
  faster attack and extra program push.
- **Shared ratio ladder:** every model with a selectable ratio uses one ladder
  (1.5, 2, 3, 4, 6, 8, 12, 20, plus ALL on the 76s), so the same position means
  the same ratio when switching models.  The 76 bank offers 4/8/12/20/ALL as
  presets and snaps to the nearest one.  Only 2A/3A differ, using Comp/Limit.
- **Input is an input attenuator:** at 0 the plugin passes no signal, as on the
  hardware; turning it up brings the signal in and drives the unit.
- **Distortion sits after the gain cell**, matching the reference
  implementation (its DIST block runs at the signal output).  The input level
  still decides how hard it bites.
- **Mix on every model**: each compressor has a parallel blend, not just
  Xpress and VCA.
- **The limiter section hides while the Side Chain page is open**, so the
  side-chain filter and limiter threshold never share the same corner.
- **Output limiter:** every model has a limiter section (on/off plus a
  -24..0 dB threshold) with a soft ceiling, matching the reference layout.
- **Distortion (Xpress):** Off is genuinely clean. Dist 2 and Dist 3 are
  level-scaled waveshapers rather than the reference's `x^0.04` blend, which
  is full-on at any level and sounds synthetic. Clip is a soft ceiling.
  Measured THD at -18 dBFS: Off -58 dB, Dist 2 -20 dB, Dist 3 -22 dB,
  Clip -14 dB.
- **Preset browser**: 33 factory presets in the header, grouped by use
  (Vocals, Drums, Bass, Guitars, Parallel, Mix Bus) and tagged with the model
  they load. Click the name to open the list, arrows to step, wheel to scroll.
  The same presets are exported as `pset:Preset` banks, so LV2 hosts list them
  in their own preset menus.
- **Detector topology follows the hardware:** 76 Blue/Black and 2A/3A detect
  in the feedback loop; the VCA is feed-forward. There is no switch.
- **Host support:** CLAP and LV2. The LV2 build has preset banks, a resizable
  X11 UI (0.5×–2.5×, the host owns the exact size and the panel is letterboxed
  into it), and gain reduction in Ardour's mixer strip.
- **Editor:** X11 embedding, resizable from 0.5× to 2.5× (520×320 to
  2600×1600). The window always keeps the 1040:640 aspect ratio: sizes are
  snapped to 1/80 scale steps, which gives an exact ratio at every size.
  Hosts can request sizes freely; the plugin returns the nearest valid one.
  Not yet included: Compare/Copy/Paste, and the Side Chain / Output tabs
  appear only on Xpress and VCA.
- **Metering:** the GR, input, and output meters are in place. VU ballistics
  are simplified.
- **Names:** "Comp Suite" is a placeholder. Hardware trademarks (1176, LA-2A,
  LA-3A, Distressor, SSL) are not used in the product name or metadata.

## Measurement

`tools/measure.py` renders a reference plugin in REAPER (`reaper
-renderproject` under Xvfb) and our DSP through `comp_offline_render`, on
identical signals, then reports:

- **Static curve**: output level per input step, plus the local slope
  (= 1/ratio).  Slopes are makeup-invariant, so auto-makeup does not distort
  the comparison.
- **Timing**: gain reduction against the dry signal through a
  -30 -> -10 -> -30 dBFS step, giving attack and release.
- **Harmonics**: H2..H7 at a bin-aligned frequency with a rectangular window.
  Bin alignment matters: with 100 Hz at 48 kHz, DFT leakage alone registers
  about -23 dB of "distortion".

```sh
DISPLAY=:0 python3 tools/measure.py suite 76blue 2a vca   # at defaults
DISPLAY=:0 python3 tools/measure.py match 76blue 2a vca   # at equal gain reduction
```

Requires REAPER plus the Tukan bundle for the reference side; our side
(`comp_offline_render`) needs neither.

### Results (100 Hz, -12 dBFS, default settings, 48 kHz)

| Model   | Ref H2 | Ours H2 | Ref THD | Ours THD |
|---------|--------|---------|---------|----------|
| 76Blue  | -26.3  | -23.3   | -25.0   | -22.8    |
| 76Black | -26.3  | -36.3   | -25.0   | -32.1    |
| 2A      | -26.4  | -28.6   | -25.0   | -28.4    |
| 3A      | -26.4  | -26.8   | -25.0   | -26.6    |
| Xpress  | -26.3  | -28.0   | -25.0   | -27.4    |
| VCA     | -26.7  | -36.8   | -25.2   | -35.8    |

Every reference lands within ~0.5 dB of the same harmonic signature, which
fits their shared Stillwell input stage.  Our VCA is deliberately the cleanest
because a real SSL-style bus compressor is far cleaner than an 1176.

### Editor bugs found in the host

- **Nothing drawn:** the panel was composed into an image and blitted, but
  cairo's Xlib backend buffers the composite. Without
  `cairo_surface_flush()` after the blit the window stayed black. The host
  test now asserts the window contains rendered pixels.
- **Dead knobs:** the window selected `PointerMotionMask`, which only
  reports motion with no button held. Dragging a knob generates
  `Button1Motion` events, so every knob was inert while buttons worked.
- **Xlib threading:** calling `XInitThreads()` from plugin init is too late
  (the host has already used Xlib). All editor Xlib calls now happen on the
  editor thread, with a handshake for open/resize/close.
- **Precision spikes:** ADAA in float32, plus an unprimed antiderivative,
  produced spikes near signal peaks.

### Bugs the measurements found

- **ADAA in float32**: `(F(x) - F(x1)) / dx` lost precision near signal
  peaks and produced spikes above unity.  Now computed in double, with the
  midpoint fallback for tiny steps.
- **First-sample spike**: a biased shaper's antiderivative is not zero at 0,
  so the ADAA state is now primed from the first sample.
- **Crossfade on first select**: choosing any model other than 76Blue
  triggered a 20 ms blend from the default model on the very first block.
- **Bypass after mix**: a disabled engine was not bit-transparent; enable is
  now applied before mix.  A bypassed render is now sample-exact.
- **Xpress gain staging**: its input mapping added +20 dB at the default,
  which clipped the output.

### Matched operating point

Comparing at default settings is misleading, because each reference reacts
differently to the same knob position.  `measure.py match` searches each
side's drive control for 8 dB of gain reduction at -10 dBFS, then measures
there.  Gain reduction is taken relative to a below-threshold level, so
auto-makeup cancels out.

| Model   | Ref drive | Our drive | Ref H2 | Ours H2 | Ref THD | Ours THD |
|---------|-----------|-----------|--------|---------|---------|----------|
| 76Blue  | -19.8 dB  | 0.02      | -26.3  | -23.3   | -25.0   | -22.5    |
| 76Black | 0.07      | 0.02      | -26.3  | -28.3   | -25.0   | -26.4    |
| 2A      | 0.23      | 0.60      | -26.4  | -29.4   | -25.0   | -29.3    |
| 3A      | 0.39      | 0.61      | -26.4  | -27.9   | -25.0   | -27.8    |
| Xpress  | 0.04      | 0.37      | -26.3  | -30.5   | -25.0   | -26.1    |
| VCA     | -0.12 dB  | 0.03      | -26.8  | -37.4   | -25.2   | -37.1    |

Every model is within about 5 dB of its reference except the VCA, which is
deliberately cleaner (a real SSL-style bus compressor is not an 1176).

### All-buttons mode

Both 76 revisions have it.  Blue pushes the effective ratio to 20:1, tightens
the attack, and drives the FET stage hardest; Black engages the same mode
with a gentler push, so it slams without Blue's distortion.  Measured at
matched drive, all-buttons adds ~2 dB more reduction on both, and Black stays
about 5 dB cleaner in H2 than Blue.

### Still open

- Attack and release could not be read reliably from the references: their
  measured release times come out at 5 ms, which is implausible for these
  units, so the timing columns are not trustworthy yet.  Our own times are
  sane (2A ~72 ms attack / ~940 ms release, VCA ~6 ms / ~770 ms).
- Independent references (real hardware recordings) would settle whether
  Tukan's shared harmonic signature is a fair target.
