# sound-design-suite

One-stop shop for simple and beautiful sound design, built for music producers.

One plugin ("Sound Suite"), a handful of big knobs, each one musically safe at every position: in time, gain-matched, mono-safe low end, and fully left is always a true bypass.

Design docs (in the project's shared folder): `design/rhythm-generator-structure.md` and `design/build-test-plan-and-v1-effects.md`.

## Status

Build step 1 of the plan: infrastructure plus the first real module.

| Piece | State |
|---|---|
| VST3, AU (macOS), Standalone builds | ✓ |
| Macro parameters: Gate/Chop, Width/Auto-pan, Space | registered; **Width is wired (widening only)**, Gate/Chop and Space are not yet |
| Advanced view: stereo width 0–200% (narrowing below 100%), mono-below crossover | ✓ |
| Zone tables (`tuning/*.json`) with hot-reload in debug builds | ✓ (values are placeholders) |
| Shared host clock (one timeline, per-module rates, host time signature, loop jumps, free-running when stopped) | ✓, not consumed yet |
| Macro registry: one list drives parameters, tuning, UI and tests | ✓ |
| Saved sessions record each macro's tuning version and dice seed | ✓ |
| Tail and latency reported per module | ✓ (Width reports none; Space will report its decay) |
| Unit + safety tests, offline render tool, CI | ✓ |
| Auto-pan, Gate/Chop, Space DSP | next (build steps 2–4) |

## Build

Requires CMake 3.22+, a C++20 compiler and Ninja. JUCE 8 and Catch2 are fetched automatically.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Outputs land in `build/SoundSuite_artefacts/Release/{VST3,AU,Standalone}`. Add `-DSOUNDSUITE_COPY_AFTER_BUILD=ON` to copy the plugins into your system plugin folders on each build so your DAW picks them up.

Linux needs: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev`.

## Layout

```
Source/
  PluginProcessor.*   processing, state save/load
  PluginEditor.*      simple view (macro knobs + zone names) and advanced view ("Show parameters")
  Parameters.h        parameter IDs and layout
  dsp/Clock.h         host transport -> shared timeline; phase() and note values per module
  dsp/Module.h        what each module reports: tail length, latency
  dsp/Width.h         crossover + mid/side width, gain-matched, bit-exact bypass at 100%
  macros/MacroRegistry.h  the one list of macros
  macros/ZoneTable.h  loads tuning JSON, morphs between anchors
  macros/TuningMigrations.h  maps old sessions' knob positions after a retune
  analysis/Metrics.h  loudness, correlation, click and null measurements
tuning/               one JSON per macro (the taste lives here) + tuning.lock
tests/unit            clock, zone tables, width
tests/safety          the "musically safe" contract, run through the real processor
tools/render          offline renderer + metrics
corpus/               test loops for tuning sessions (empty for now)
```

## Tuning a macro

Each `tuning/<macro>.json` is a list of anchors (`at` 0..1, a `name`, and parameter values). Numbers morph linearly between neighbouring anchors; text values (note lengths, pattern names) switch at the midpoint. The first anchor must sit at 0 and must be bypass.

In a **Debug** build the plugin re-reads these files about 15 times a second while its window is open, so you can edit a value, save, and hear it. Release builds compile the files in. After editing, run `ctest`: the safety tests re-check every macro position.

**Versions.** Sessions save which tuning version each knob was set against. Once a table has shipped, any change to it must bump its `"version"`, or old sessions would silently play differently. Mark a table shipped by starting its `"status"` with `shipped`; from then on `tuning/tuning.lock` enforces the rule: change the table without bumping and `ctest` fails, printing the lock line to paste after you bump. Tables that aren't shipped yet can be retuned freely. Editing only `"status"` or `"notes"` doesn't count. If the change moves zones around, add a case to `Source/macros/TuningMigrations.h` so old sessions land in the zone they were set to.

The Width macro only ever widens. Narrowing is the advanced view's **Stereo width** parameter, which multiplies with the macro.

## Adding a macro

1. Add a line to `silo::macros` in `Source/macros/MacroRegistry.h` (ID and display name).
2. Add `tuning/<id>.json` (first anchor at 0 = bypass, `"version": 1`) and its line in `tuning/tuning.lock` (run `ctest`, paste what it prints).
3. Wire the DSP in `processBlock`, and add the module to `chain` in `PluginProcessor.h` so its tail and latency are counted.

Parameters, the knob in the editor, hot-reload, saved-state versioning and the safety tests pick it up from the registry; the tests fail if the tuning file is missing or doesn't match a registered macro.

## Timing

`processBlock` turns the host transport into one `silo::Transport` per block and advances the clock once at the end; nothing else moves time. Each module picks its own rate: `transport.phaseAt (i, *silo::beatsForNote ("1/16", transport.beatsPerBar))`. Cycles that fit evenly into a bar (1/16, 1/8T, 1 bar) restart on the host's downbeat; longer or uneven ones (2 bars, 1/4. in 4/4) run from the song start so they cross bar lines without jumping. Bar length follows the host's time signature (6/8 = 3 quarter-note beats).

## Render tool

```bash
build/tools/render/soundsuite-render --in loop.wav --out wide.wav --width_pan 0.25
build/tools/render/soundsuite-render --in loop.wav --out sweep.wav --sweep width_pan
build/tools/render/soundsuite-render --in loop.wav --out narrow.wav --width_manual 40
```

Any parameter ID works as `--<id> <value>` (macros 0..1, advanced parameters in their own units). The output runs past the input by the plugin's reported tail, or by `--tail <seconds>`, so reverb and delay tails are rendered and measured. Prints input and output metrics as JSON.

## Tests

`ctest` runs:

- **Unit:** clock timing (tempos, loop jumps, triplets, stopped transport), zone-table morphing and validation, width DSP.
- **Safety** (every macro at 11 positions, several sample rates and block sizes, including odd ones): zero nulls against the input below −120 dB; loudness within ±1 dB; lows stay mono; mono fold-down loses ≤ 3 dB extra; no clicks while sweeping; identical output at block size 1 and 1024; no NaN/Inf; **no heap allocation inside `processBlock`**; state survives save/reload.

Loudness is RMS for now, a stand-in for LUFS. Thresholds are placeholders to tighten once real renders exist.

CI (GitHub Actions) builds on macOS, Windows and Linux, runs the tests, runs `pluginval` at strictness 10 on the VST3 and `auval` on the AU.
