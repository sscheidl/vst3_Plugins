# Build report: Mendelejew 0.1.0 (project Elements, phase 1)

> **Since then:** Codex reviewed 0.1.0
> ([CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md)) and found three faults
> and two DSP deviations; 0.1.1 fixes all five
> ([what was done](CODEX_REVIEW_0.1.0_UMSETZUNG.md), measurements of 0.1.1 in
> [DSP_MEASUREMENTS_0.1.1.md](DSP_MEASUREMENTS_0.1.1.md)). This report stays
> the record of 0.1.0; where it says a mode keeps its decay exactly, or that
> the plug-in's output stays within ±1, that held for 0.1.0 only at high
> resonance and at a 48 kHz host.

Date: 2026-11-14. Work order: "Phase 1 – Mutable Instruments Elements als
natives VST3" (handed over in the session; its requirements are summarised
in the pull request).
Built and tested by Claude (Fable 5.2) on the user's machine.

**The name.** The plug-in is called Mendelejew, the user's decision of
2026-11-14 after the first build under the working title "Elements". Only
the product name changed: the name the host shows, the bundle, the window
title. "Elements" remains the internal project name (directory, branch,
build targets, namespaces, test names), and where this report says
"Elements" for the plug-in's parts it means the project. The builds and
tests of section 6 were repeated after the change.

**Status: built, validated, tested automatically. Not a release approval.**
The plug-in has not been loaded in a DAW, nobody has listened to it, and the
independent review by Codex has not taken place. The work order's closing
condition ("lokal gebaut, geladen, mit MIDI gespielt und bei echten internen
48 kHz geprüft") is met as far as a machine can meet it: built, loaded by
Steinberg's validator and by an in-process host, played with MIDI events by
tests, checked at 48 kHz against the original. Playing it by hand in a DAW
is the user's part and is open; section 8 says what was not checked.

Every figure below was produced by a run on this machine. The complete
output of the final runs is in `out/elements-0.1.0/logs/` (not versioned);
the DSP measurements are also kept in
[DSP_MEASUREMENTS_0.1.0.md](DSP_MEASUREMENTS_0.1.0.md). Test names in
`code font` are functions in the test sources.

## 1. What was built

A VST3 instrument in `instruments/elements/`, bundle `Mendelejew.vst3`, vendor
TAUREON, version 0.1.0, category Instrument/Synth, with its own class IDs
(`419AD745 43C74420 A606A40F 1DE4FA7B` processor,
`43CEC3AF 41924864 B3076AE1 6AF0085D` controller; generated for this
plug-in).

| Target | Directory | Role |
| --- | --- | --- |
| `elements_core` | `core/` | The Elements DSP and the stmlib parts it needs, patched for a run-time sample rate. No framework, no allocation |
| `elements_engine` | `engine/` | Instrument: voice, controls, smoothing, velocity, pitch wheel, output. Its headers show no DSP and no framework type |
| `elements_adapter` | `adapter/` | Host block sizes, sample rate conversion, event timing |
| `elements_shell` | `source/` | VST3 processor, controller, state, window |
| `Mendelejew` | `source/factory.cpp` | The plug-in; the one target that carries the product name |
| `elements_reference` | `tests/reference/` | The unchanged original DSP, for tests only |

Local paths after the build:

* `build/windows-vs2026-x64/VST3/Release/Mendelejew.vst3` (and `.../Debug/`)
* the binary inside: `Contents/x86_64-win/Mendelejew.vst3`

The build installs nothing. At the user's request the Release bundle was
copied to `%LOCALAPPDATA%\Programs\Common\VST3\` for the DAW test.

## 2. Starting point

* Repository `sscheidl/vst3_Plugins`, working copy
  `Playground/vst3_Plugins`, clean at the start, on branch
  `torus-classic-0.1.0` at `78e5b9a`. Torus Classic exists on that branch
  only, not on `main`. The feature branch `elements-0.1.0` therefore starts
  there, and the pull request is made against that branch so that it shows
  this plug-in alone.
* **There is no JUCE in this repository**, although the work order mentions
  it. Both existing plug-ins sit directly on the Steinberg VST3 SDK 3.8.0
  with VSTGUI (decision of 2026-11-12, `docs/PLANUNG.md`). Mendelejew does
  the same; no second framework was introduced.
* Baseline before any change: configure, Release build and tests of the
  branch as found. 6 of 6 tests passed, validator 47 passed, 0 failed for
  both plug-ins (`baseline-*.log`).
* An untracked file `instruments/torus-classic/docs/CODEX_REVIEW_0.2.0.md`
  appeared in the working copy during the work (Codex's review of Torus
  Classic). It is not part of this branch and was not touched.

## 3. The DSP port

Sources, licenses, every patch and the three other ports that were compared
are in [UPSTREAM.md](../UPSTREAM.md). In short:

* The original MIT code from `pichenettes/eurorack` and `pichenettes/stmlib`
  is used. 47 files are byte-identical copies (34 of them the test
  reference), 15 DSP files are patched, 1 is derived (`rng.h`), 3 are new.
  Nothing comes from the GPL ports (VCV Rack, MAGDA).
* None of the three other ports runs the DSP at a true 48 kHz: VCV and MAGDA
  run it at 32 kHz and resample; nt_elements only redefines `kSampleRate`
  and, read from its source, plays 7 semitones sharp at 48 kHz.
* The hardware layer (drivers, control reader, UI) is not used. What the
  control reader does to the pots is re-implemented in the engine.

## 4. Sample rate

The requirement: a true internal 48 kHz, not a 32 kHz core with upsampling.

**How it is met.** The core takes the rate at `Init()`. At 48 kHz it renders
blocks of 24 samples; every filter coefficient, delay length, playback
increment, probability and time constant is computed for 48 kHz. The table
in UPSTREAM.md lists each item of the work order's list (resonator
coefficients, delays and damping, exciter time constants, strike samples,
reverb, smoothing, tables and constants) with what the original does and
what the port does.

**What "the same instrument" was taken to mean.** A first, obvious conversion
(same cutoff frequencies, resonance scaled by the rate) was built and
measured. It gave a measurably different instrument: the original's upper
partials are flat by up to 170 cents because of its tangent approximation,
ring longer than their nominal resonance says, and are excited much less by
bright strikes because its filters fall to nothing at 16 kHz; at 48 kHz none
of that held, and bright high notes rang 20 to 40 % longer overall. The port
therefore converts each part so that it behaves in time and frequency as it
does at 32 kHz. These conversions are derived, not tuned by ear, and each is
measured below.

**Proof of native processing.**

* `Engine::SampleRate()` is 48000 and `Quantum()` 24 in every plug-in test,
  at every host rate (`TestBuses`, `TestBlockSizesAndRates`).
* At a 48 kHz host the adapter converts nothing: the plug-in's output is the
  engine's, bit for bit (`TestAdapter`), with no latency.
* The port has content above 16 kHz, which a 32 kHz core cannot have: up to
  −45 dB re full scale between 15.5 and 24 kHz (bright sampled strike).
* At 32 kHz the same code is the original bit for bit, so what differs at
  48 kHz comes from the conversions and from nothing else.

**Other host rates.** The engine stays at 48 kHz; the sinc converter taken
from Torus Classic follows. Its latency, 49 samples at 44.1 kHz and about a
millisecond at every converted rate, is reported to the host. A4 is within a
cent at 44.1, 48, 88.2, 96 and 192 kHz (`TestBlockSizesAndRates`).

**96 kHz: HOLD.** See section 6.

## 5. Measurements against the original

All from `test_elements_dsp`; full tables in
[DSP_MEASUREMENTS_0.1.0.md](DSP_MEASUREMENTS_0.1.0.md). "Original" is the
unchanged code at 32 kHz.

| What | Result at 48 kHz |
| --- | --- |
| Port at 32 kHz against the original | 48 scenarios of 2 s, random patches, gates, notes, strengths, freeze: 6 144 000 samples identical |
| Pitch of the fundamental, MIDI notes 36 to 108 | Within 0.04 cent of the original's (the resolution of the measurement); both within 0.4 cent of equal temperament |
| Frequency of a partial near 5 to 7 kHz | Within 0.1 cent of the original's |
| Decay time of the fundamental, 9 cases | Ratio port/original 0.983 to 1.004 |
| Decay time of that high partial | Ratio 0.996 to 1.003 |
| Decay time of the whole sound (bright patch) | Ratio 0.997 to 1.011 |
| Level by band, mallet and plectrum (4 cases) | Within 0.2 dB in every band up to 15.5 kHz |
| Level by band, sampled strike, default timbre | Within 0.3 dB except −1.4 dB at the fundamental, +0.9 dB at 8 to 12 kHz, +5.4 dB at 12 to 15.5 kHz |
| Level by band, sampled strike, high timbre | −2.1 dB in total, single bands to −5.6 dB: the original aliases here (README, "Known differences") |
| Level by band, blow (3 cases, mean of 8 runs) | Within 0.5 dB up to 8 kHz; +3 to +5.5 dB at 12 to 15.5 kHz |
| Level, bow and particles (mean of 8 runs) | Totals within 2.3 dB; the original's own scatter between runs is 2 to 3.6 dB |
| Exciter low-pass, all 257 cutoffs | Within 1.3 dB of the original's response wherever that is above −40 dB |

The comparison of bowed sounds is the weakest of these: the bowed resonator
is a noise-driven nonlinear system, single bands differ by up to 7 dB between
the two means, and eight runs do not separate a systematic difference of a
decibel or two from chance. The exciter signal that drives it agrees within
0.2 dB.

## 6. Test protocol

Final runs: Visual Studio 2026 Build Tools, preset `windows-vs2026-x64`.

| Test of the work order | Result | Evidence |
| --- | --- | --- |
| Toy Piano | **PASS** (builds; its 2 tests pass; validator 47/0) | `ctest-vs2026-*.log`, `build-vs2026-*.log` |
| Torus | **PASS** (builds; its 4 tests pass; validator 47/0) | the same |
| Elements Debug (`Mendelejew.vst3`) | **PASS** | `build-vs2026-debug.log` |
| Elements Release (`Mendelejew.vst3`) | **PASS**, no compiler warning in `instruments/elements` | `build-vs2026-release.log` |
| VST3 validation | **PASS**: 47 tests passed, 0 failed, in both configurations | the same |
| MIDI | **PASS**: note-on, note-off, velocity (15 dB range at full Velocity), pitch wheel (±2 semitones within 2 cents) | `TestNotes`, `TestVoice`, `TestPitch` |
| Exciters | **PASS**: each alone makes its sound and ends with the key; levels against the original in section 5 | `TestExciters`, `MeasureSpectra` |
| Resonator | **PASS**: Brightness, Damping, Geometry, Position act as described; modes against the original in section 5 | `TestResonatorParameters`, `MeasureDecay` |
| Reverb | **PASS**: tail present, decays, channels decorrelated (correlation −0.09), bounded, freeze holds | `TestSpace` |
| Presets | **PASS**: all 19 saved values return exactly; a loaded state sounds like the same values set by hand; damaged states are refused whole | `TestState` |
| 48 kHz internal | **PASS** | section 4 |
| Audio stability | **PASS**: 90 s of random notes and controls at 48 kHz, no non-finite sample, peak 0.69, silent 30 s after the last release; no hanging gate after overload, state load or deactivation | `TestStability`, `TestAdapter`, `TestState` |
| Regression | **PASS**: no file of the two existing plug-ins was changed (`git diff torus-classic-0.1.0 -- instruments/toy-piano-classic instruments/torus-classic` is empty); their tests pass | git, `ctest-vs2026-*.log` |
| 32 kHz reference test | **PASS** | section 5 |
| 96 kHz | **HOLD** | below |

Also checked: block-size independence (identical output for block sizes 1
to 2048 at five host rates), 64-bit buffers, no allocation in `process()`
and in `Engine::Render()`, parameter flush without audio, the screen
keyboard's messages and MIDI output, the status message, strict text input,
the provenance of every upstream file.

Load, measured by `MeasureLoad` (all three exciters, reverb, 52 modes): about
3 % of one core at 48 kHz, about 6 % at 96 kHz.

**96 kHz in detail.** The core runs at 96 kHz (blocks of 48) and passes the
same measurements: pitch and partials as at 48 kHz, decay ratios 0.968 to
1.006, mallet and plectrum within 0.2 dB in every band, 90 s without a
non-finite sample. One deviation is known: a very bright mallet pulse keeps
content far above 16 kHz, and the soft limiter in front of the left output
turns that into −3.5 dB for the exciter signal heard there at SPACE 0. The
plug-in does not use 96 kHz: at such a host it runs the engine at 48 kHz and
converts. Open before PASS: that deviation, plug-in tests with the engine at
96 kHz, and listening.

## 7. What was reused from Toy Piano Classic and Torus Classic

Copies in the namespace `elements_synth`; nothing was moved to `common/`,
because that would have changed Torus Classic while its review is open.

| Component | From | Change |
| --- | --- | --- |
| Build set-up: root CMake, presets, SDK submodule, validator after every build, `/fp:precise` per target | repository | one line added to the root `CMakeLists.txt` |
| Streaming adapter `adapter/stream_adapter.*` | Torus Classic 0.2.0 | quantum 24 instead of 12; note-off and all-notes-off; pitch wheel in semitones; output stage removed |
| Sinc converter `adapter/sinc_kernel.*` | Torus Classic 0.2.0 | namespace only |
| `source/parameter_state.h`, `latest_value.h` (values between the threads, loaded states) | Torus Classic 0.2.0 | namespace only |
| `source/spsc_queue.h` | Daisy-Seed by way of Torus Classic | namespace only |
| `source/value_text.h` (strict text input) | Toy Piano Classic 0.3.2 by way of Torus Classic | namespace only |
| `source/processor.*` | Torus Classic 0.2.0 | engine, note-off handling, status message |
| `source/controller.*` | Torus Classic 0.2.0 | parameters, MIDI map, `notify` |
| `source/editor.*` (window sizing, scaling, drag handling, knobs) | Torus Classic 0.2.0, there after Toy Piano Classic | layout, status line, freeze switch; model selector removed |
| `source/widgets.*`, `view_layout.h` (screen keyboard, switches) | Torus Classic 0.2.0 | model selector removed |
| `source/state_io.h`, `engine_bridge.h`, `factory.cpp` | after Torus Classic 0.2.0 | rewritten for this plug-in's state and events |
| `tests/test_support.h`, the host double in `tests/test_plugin.cpp` | Torus Classic 0.2.0 | adapted |
| `tools/stage_result.ps1`, the provenance check | after Torus Classic 0.2.0 | simplified |

## 8. Known defects, limits, and what was not checked

No functional defect is known. Limits and differences from the original are
listed in the README ("Known differences", "96 kHz"). In addition:

* **Not checked by anyone:** the look of the window, the handling of knobs,
  the keyboard on screen, resizing; any DAW; any listening. The window's
  code is Torus Classic's with a new layout that nobody has seen yet. The
  list for the DAW test is [DAW_TEST_0.1.0.md](DAW_TEST_0.1.0.md).
* **Not reviewed** by a second party. The brief for Codex is
  [CODEX_PRUEFAUFTRAG_0.1.0.md](CODEX_PRUEFAUFTRAG_0.1.0.md).
* **Built with one toolchain:** Visual Studio 2026. The 2022 preset and an
  AddressSanitizer build, both done for Torus Classic, were not run.
* The first note after a reset is quieter if it comes within the first half
  millisecond: the DSP glides to its strength from zero, as the original
  does after power-on.
* Parameter changes take the hardware's smoothing: about 35 ms to half way,
  Position and Space twice that. Fast automation is rounded accordingly.
* The comparison of bowed sounds is statistical (section 5).
* A plug-in instance computes its filter tables when it is created: about
  40 ms, once.
* The debug build's tests compare fewer scenarios and leave the measurements
  against the original to the release build.

## 9. Decisions that are the user's

1. **The name.** Decided on 2026-11-14: Mendelejew. Whether the name is free
   as a product name was looked at with one web search only, which showed no
   audio plug-in of that name; that is not a trademark search.
2. **Damping.** Higher rings longer, as on the hardware. Torus Classic was
   reversed at the user's request in 0.2.0; whether Mendelejew should follow
   is open. One line in the engine, plus the state.
3. **Velocity.** The mapping of key velocity to the hardware's STRENGTH is a
   choice (README, parameter 117); the default of 50 % is a guess at what
   plays well.
4. **96 kHz** and the items left out of phase 1 (string models, external
   input, more than one voice).

## 10. How to build

See the [README](../README.md), "Build". From a fresh clone: fetch the SDK
submodule as the main README says, then

```bash
cmake --preset windows-vs2026-x64
```

```bash
cmake --build --preset windows-vs2026-x64-release
```

```bash
ctest --preset windows-vs2026-x64-release
```
