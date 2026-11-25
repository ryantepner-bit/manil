# MANDELBROT on LMMS: audit and migration report

Status: living document, updated at every integration stage.
Rule for this file: a feature is listed as **working** only after it was built by CI and tested
(render test on Linux, and in the real Windows application). Everything else is marked
**in progress** or **planned**.

## 1. What was audited

| Source | Language / toolkit | Licence | Pinned revision |
|---|---|---|---|
| MANDELBROT v0.3 (this repo, `Source/`) | C++20, JUCE 9 | ours (GPL-compatible) | `main` before the LMMS work |
| LMMS (`sysfce2/DAW_lmms`) | C++20, Qt 5 | GPL-2.0-or-later | `a2f57e70ce9c3468b4b6d21955bbe65a0989048a` |
| zrythm (`XiangCoder/zrythm-DAW`) | C++23, Qt 6 / QML + JUCE | AGPL-3.0-or-later **plus trademark terms** (name and logo may not be used for a modified build) | mirror HEAD at audit time |
| synkopa (`miskibin/synkopa`) | Rust | AGPL-3.0-only; drum library CC0 | `20e8a9a36cd5a5c8d40dedd2ebb3502870afacc4` |
| cecilia5 (`belangeo/cecilia5`) | Python (wxPython + pyo) | GPL-3.0-or-later | HEAD at audit time |
| pyo (`belangeo/pyo`): the C DSP engine every cecilia5 module is built from | C | LGPL-3.0-or-later | HEAD at audit time |

### Licence compatibility (decision)

* LMMS is GPL-2.0-**or-later**, so it can be combined with GPL-3 and, through section 13 of the
  GPL-3, with AGPL-3 code.
* cecilia5 (GPL-3+) and pyo (LGPL-3+) code can be added directly.
* synkopa is AGPL-3.0-**only**; zrythm is AGPL-3.0-or-later. Any code ported from them makes the
  combined program AGPL-3.0. For a desktop app this changes nothing in practice (AGPL only adds
  obligations for software offered over a network). The user approved a GPL licence; AGPL is the
  GPL family's network variant. **Combined MANDELBROT licence: AGPL-3.0-only.** Every ported file
  keeps its original copyright header and names its source.
* The zrythm name and logo are **not** used anywhere (its trademark terms).
* The CC0 drum library needs no attribution, but it is credited anyway.

### Dependencies

* Nothing new beyond what LMMS already uses (Qt 5, FFTW 3, libsndfile, libsamplerate, SDL2 and so on).
  The spectral effect uses its own small radix-2 FFT, so it adds no dependency.
* zrythm (Qt 6 / QML / JUCE) and synkopa (Rust, cargo) cannot be linked into an LMMS build. Their
  code is ported to C++ / Qt 5 by hand where it is used.
* cecilia5 itself is Python. Its modules are graphs of pyo objects, so the actual DSP is pyo's C
  code, which is what gets ported.

## 2. Architecture

```
LMMS (unchanged core, song editor, pattern editor, piano roll, mixer, 60+ plugins)
 ├─ src/core/fractal/        NEW  pure C++ fractal + music engine (no Qt; unit tested on its own)
 │    MandelMath             migrated from MANDELBROT/Source/Fractal
 │    Scales                 migrated from MANDELBROT/Source/Core
 │    PartGenerator          migrated + rewritten from MANDELBROT/Source/Live (per track, per tick)
 │    PhraseHistory          NEW  remembers everything played, anti-repetition, recall
 ├─ FractalEngine (core)     NEW  global probe, drift, style, key; read by every generator
 ├─ InstrumentTrack          PATCHED  each instrument track gets a FRACTAL tab (role, density, ...)
 │                                     and generates notes live while the song plays
 ├─ FractalWindow (gui)      NEW  the set, the probe, what it is reading, history browser, capture
 ├─ Ambient                  NEW  a full ambient workspace: drones, cecilia5 DSP, long spaces
 └─ plugins/Mandel*          NEW  native real-time effects / instruments ported from pyo/cecilia5/synkopa
```

## 3. Migration table

| Item | From | Action | Status |
|---|---|---|---|
| Song editor, pattern editor (FL-style channel rack), piano roll, automation, mixer | LMMS | reused as is | base |
| Break slicer | LMMS SlicerT | reused, driven by the fractal Break role | planned |
| Mandelbrot maths (iteration, period, distance, orbit, neighbourhood analysis) | MANDELBROT `Fractal/MandelMath` | migrated: JUCE types replaced by std | in progress |
| Scales | MANDELBROT `Core/Scales` | migrated | in progress |
| Live sequencer (drums, bass, chords, lead phrases A A' B A'', arp, break) | MANDELBROT `Live/LiveSequencer` | rewritten per track for LMMS ticks; same musical logic | in progress |
| Phrase history + anti-repetition | new | written | in progress |
| Sample classifier (kick / snare / hat ...) | MANDELBROT `Audio/SampleLibrary` | migrated for the kit builder | planned |
| Granular cloud effect **Mandel Cloud** | pyo `Granulator` (granulatormodule.c) / cecilia5 granulator modules | ported to C++: same pointer/phase-offset grain scheduler and Hanning envelope, reading a 6 s live buffer instead of a table; adds per-grain chord-tone pitches from the fractal engine, scatter, width, freeze (`plugins/MandelCommon/CeciliaDsp.cpp`, `plugins/MandelCloud`) | written, DSP unit-tested offline; LMMS build pending CI |
| Spectral effects **Mandel Spectral** | pyo `PVAnal`, `PVSynth`, `PVVerb` (phasevocmodule.c); cecilia5 spectral modules | ported to C++ with its own radix-2 FFT (no FFTW dependency): analysis, PVVerb decay/damp, freeze, blur, bin shift, resynthesis. Offline test: identity resynthesis error 1e-5 at 2048-sample latency; shift 440 to 660 Hz; freeze holds 330 Hz after input stops | written, DSP unit-tested offline; LMMS build pending CI |
| Resonator **Mandel Resonator** | pyo `Waveguide` (delaymodule.c); cecilia5 waveguide bank | ported line by line (Lagrange fractional delay, lowpass, DC blocker, feedback = 100^(-1/(f*dur))); 6 voices per side tuned to the chord the fractal engine is playing. Offline test: 110/220/523.25/1000 Hz measured 110.00/220.00/523.26/1000.06 | written, DSP unit-tested offline; LMMS build pending CI |
| pyo `WGVerb`, `STRev` | pyo | not ported: LMMS ReverbSC already covers the plain reverb | excluded |
| Music box instrument (modal tines) | synkopa `musicbox.rs` | ported from Rust | planned |
| Chord voicing (nearest inversion voice leading) | synkopa `chords.rs::voice` | ported from Rust | planned |
| Drum library (154 one-shots) | synkopa `crates/engine/library` (CC0) | bundled | in progress |
| JUCE app, its synth / effects / UI | MANDELBROT `Source/` | **kept unchanged** in this repo; its own workflow still builds it | kept |
| zrythm | zrythm | audited; design reference only so far (graph engine and QML UI do not fit Qt 5 LMMS). Candidate ports: musical scale + chord descriptor tables | under review |
| Excluded | synkopa vocal tuning, RMVPE pitch model, stems (neural models, Rust-only) | excluded: no C++ equivalent, very large | excluded |

## 4. Build and test process

* `.github/workflows/mandelbrot-lmms.yml` fetches LMMS at the pinned revision, copies `lmms/`
  over it and builds:
  * **Windows x64** (MinGW, LMMS's own CI recipe) to an NSIS installer that lets you pick the install
    folder (E:);
  * **Linux test build**, which renders the test projects in `ci/renders/` and the MANDELBROT templates
    to MP3 and prints level statistics.
* Logs and renders go to the `ci-logs-linux` and `ci-logs-windows` branches.
* The pure C++ engine (`lmms/src/core/fractal`) has its own unit tests
  (`lmms/src/core/fractal/tests/engine_test.cpp`), and the ported DSP has an offline test
  (`lmms/plugins/MandelCommon/tests/dsp_test.cpp`); both are run before each push.
* The previous working version (the JUCE MANDELBROT, `Source/` + `build.yml`) is never touched.

## 5. Stage log

| Stage | Content | Result |
|---|---|---|
| 0 | CI that builds unmodified LMMS for Windows + Linux | scripts written; waiting for the workflow file |
| 1 | Fractal engine (Conductor, PartGenerator, PhraseHistory), LMMS hooks, FRACTAL tab, fractal window, templates | engine tests 43/43 (includes the chord/key the effects read), 0 exact repeats in 120 phrases; LMMS compile pending CI |
| 2 | Native Cecilia5/pyo DSP: Mandel Cloud, Mandel Spectral, Mandel Resonator; fractal link (chord, key, bar readings published by the Conductor); used in the Ambient template | DSP tests pass offline; LMMS compile pending CI |
