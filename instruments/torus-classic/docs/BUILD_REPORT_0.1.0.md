# Build report: Torus Classic 0.1.0

> **Later than this report (2026-11-14):** Codex reviewed 0.1.0 and found four
> functional defects ([CODEX_REVIEW_0.1.0.md](CODEX_REVIEW_0.1.0.md)); the
> statement below that no defect is known no longer holds for 0.1.0. They are
> fixed since 0.1.1, see
> [CODEX_REVIEW_0.1.0_UMSETZUNG.md](CODEX_REVIEW_0.1.0_UMSETZUNG.md). A first
> DAW result is recorded in [DAW_TEST_0.1.0.md](DAW_TEST_0.1.0.md). The rest
> of this report is left as written.

Date: 2026-11-14. Work order:
[CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md](../../../docs/CLAUDE_BUILDAUFTRAG_TORUS_CLASSIC_0.1.0.md).
Built and tested by Claude (Fable 5.2) on the user's machine.

**Status: ready for the DAW test.** All builds and automatic checks listed
here passed; no functional defect is known. **This is not a release
approval:** the plug-in has not been loaded in a DAW, nobody has listened to
it, and no review by another party has taken place. A second read by a
separate Claude instance during the work found four edge-case defects, which
are fixed (section 8); it does not replace that review. Section 9 lists what
was not checked.

Every figure below was produced by a run on this machine; the complete
output of the final runs is in `out/torus-classic-0.1.0/logs/`. Test names
in `code font` are functions in the test sources, so each claim can be traced
to the code that checks it.

## 1. What was built

A VST3 instrument in `instruments/torus-classic/`, bundle `Torus Classic.vst3`,
vendor TAUREON, version 0.1.0, with its own class IDs
(`1385AD46 5D5A4FAA 9392E1F1 04BCF372` processor,
`5203BCC0 7FD24B79 A399D335 E3AB7DA2` controller; generated for this plug-in,
different from Toy Piano Classic's).

Four layers, one static library each, plus the pinned reference for tests:

| Target | Directory | Lines of own or adapted code | Role |
| --- | --- | --- | --- |
| `torus_classic_core` | `core/` | vendored | Torus DSP and stmlib parts, 10 files patched |
| `torus_classic_engine` | `engine/` | about 410 | Dry instrument; interface `dry_engine.h` |
| `torus_classic_adapter` | `adapter/` | about 570 | Sample rate, 12-sample steps, event time |
| `torus_classic_shell` | `source/` | about 2300 | VST3 processor, controller, state, editor |
| `torus_classic_reference` | `tests/reference/` | vendored, verbatim | Firmware engine with reverb; tests only |

The test sources add about 3800 lines.

Source state: this repository at `e553451` plus the uncommitted Torus Classic
changes. Work began on `3aa0333` with Toy Piano Classic 0.3.2 uncommitted in
the working tree; another session committed that as `e553451` during this
work. Daisy-Seed at `10f906fcd5a036fd88d20851186337e46fa38198`, its
`instruments/physical-modeling` unmodified; details in
[UPSTREAM.md](../UPSTREAM.md).

Changes outside `instruments/torus-classic/`: one line in the root
`CMakeLists.txt` (`add_subdirectory`), one table row each in `README.md` and
`instruments/README.md`, three rows in `THIRD_PARTY_NOTICES.md`, one section
in `docs/PLANUNG.md`. No Toy Piano Classic file was touched. During the build
work nothing was installed, `dist/` and the VST3 folders were not touched,
nothing was committed or pushed, Daisy-Seed was only read.

After this report was written the user asked for the bundle to be copied to
the VST3 folder. The staged Release bundle now also sits in
`%LOCALAPPDATA%\Programs\Common\VST3\Torus Classic.vst3` (2026-11-14), next
to Toy Piano Classic; all four files compare equal by SHA-256 to the staged
ones. A review brief for Codex is in
[CODEX_PRUEFAUFTRAG_0.1.0.md](CODEX_PRUEFAUFTRAG_0.1.0.md).

## 2. Build matrix

Machine: Intel Core i7-8700K (6 cores, 3.7 GHz), 32 GB, Windows 11 Pro
10.0.26300. CMake 4.4.2. VST3 SDK 3.8.0 build 66 (`9fad977`).

Commands, as the work order gives them, for each configuration:

```powershell
cmake --preset windows-vs2026-x64
cmake --build --preset windows-vs2026-x64-release --parallel 4
ctest --preset windows-vs2026-x64-release
```

| Configuration | Compiler | Build | Validator on `Torus Classic.vst3` | CTest |
| --- | --- | --- | --- | --- |
| VS 2026, Release | MSVC 19.51.36256.0 | ok | 47 passed, 0 failed | 6 of 6 passed |
| VS 2026, Debug | MSVC 19.51.36256.0 | ok | 47 passed, 0 failed | 6 of 6 passed |
| VS 2022, Release | MSVC 19.44.35227.0 | ok | 47 passed, 0 failed | 6 of 6 passed |

The six CTest entries are the four of Torus Classic and the two of Toy Piano
Classic (`toy-piano-classic.core`, `toy-piano-classic.plugin`), which were
built and run alongside and pass unchanged. The count of 47 was read from this plug-in's own validator runs, not taken
over from Toy Piano Classic; that plug-in's bundle was validated in the same
builds and also shows 47 passed, 0 failed.

The validator's rates include 1234.5678 Hz and 1234567.8 Hz. The first
version of the adapter refused rates above 1 MHz; the validator reported
that as an error and the build failed. The supported range is now 1 kHz to
4 MHz.

Warnings: none from `adapter/`, `source/` and the tests at `/W4`. The
vendored DSP and `engine/`, which includes its headers, are compiled at the
SDK's `/W3` with seven warnings about the upstream code switched off
(`C4244`, `C4305`, `C4267`, `C4018`, `C4100`, `C4456`, `C4458`), for
those two targets only; they show none either.

Bundle checks (VS 2026 Release): the bundle holds
`Contents/x86_64-win/Torus Classic.vst3` (3 537 408 bytes),
`Contents/Resources/moduleinfo.json`, `desktop.ini` and `PlugIn.ico`.
`moduleinfo.json` names version 0.1.0, vendor TAUREON, the two class IDs
above, sub-categories Instrument and Synth. `dumpbin /dependents` lists only
Windows system DLLs (KERNEL32, USER32, GDI32, SHELL32, ole32, d2d1, DWrite,
d3d11, SHLWAPI, IMM32, dwmapi, OPENGL32): the MSVC runtime is linked
statically.

## 3. Floating-point options

The SDK's build files compile everything with `/fp:fast`. All Torus Classic
libraries and test programs are compiled with `/fp:precise` instead (set in
`instruments/torus-classic/CMakeLists.txt`); only `source/factory.cpp`, which
contains no arithmetic, the SDK and VSTGUI keep `/fp:fast`.

* `engine/audio_output.h` stops the build with `#error` if compiled with
  `/fp:fast`, because its guard against non-finite samples needs IEEE
  semantics. `test_engine.cpp` does the same for itself.
* `TestOutputStage` (engine test, same options as the engine) feeds
  not-a-number and infinities to the output stage as signal and as master
  value: silenced, output always below 0.8.
* Not-a-number and infinite values are also sent through the plug-in's
  interfaces: note velocities (`TestNoteHandling`), parameter values
  (`TestPauseAndFlush`), saved states (`TestState`, `TestController`),
  the window zoom (`TestController`), typed text (`TestController`). All are
  refused or clamped as documented.

## 4. Dry reference comparison

**Reference:** the firmware engine `physical_modeling::Synth` exactly as
copied, reverb included, compiled into its own library by the same compiler
with the same options. Its Dry/Wet parameter starts at 0.25 and is smoothed,
so both engines first render 400 silent quanta (0.1 s) from the same seed
after `Set(Wet, 0)`; the tests check that the firmware's smoothed mix is then
exactly 0. The firmware's reverb keeps running in the reference; the mix
multiplies it by 0.

**Tolerance, fixed before the first run:** none. Every sample must compare
equal. The reasoning is in the test source above `CompareWithFirmware`. The
only permitted difference is the sign of a zero, which the firmware's mix
`dry + (wet - dry) * 0` can change; those are counted.

**Engine level** (`TestDryReference`, `torus-classic.engine`): each engine
gets the same calls before the same quanta.

| Score | Samples compared | Differing | Zeros of other sign |
| --- | --- | --- | --- |
| Notes, six models (single notes, chords, stealing, re-strikes, notes 0, 1, 126, 127, twelve at once) | 6 × 57 600 | 0 | 516, model 6 only |
| Sweeps of all controls and bends, six models | 6 × 72 000 | 0 | 396, model 6 only |
| Limits: tune and bend ends, all 32 combinations of five controls at 0 and 1, six models | 6 × 79 680 | 0 | 636, model 6 only |
| Fifteen model changes with notes during the fades | 73 032 | 0 | 54 |
| Random performance, two seeds, 24 s each | 2 × 192 000 | 0 | 163 |
| **Total** | **1 712 712** | **0** | 1765 |

After every score the two noise generators have the same state, and trigger
counts and active model agree in every quantum. The first run passed; no
tolerance was changed.

**Plug-in level** (`TestAgainstFirmware`, `torus-classic.plugin`): the
processor is driven through `process()` at 48 kHz with changing block
lengths, notes and parameter changes at arbitrary sample offsets; the
firmware engine gets each event before the first quantum at or after its
sample. Three 20-second performances, each starting from another model, all
parameters, bends and model changes included: **5 760 000 samples, 0
differing**, with denormals flushed to zero in both. Tolerance fixed
beforehand for the firmware in its default float mode: 1e-30 (reasoning in
the test source); measured largest difference 1.12e-39. In Debug builds the
performances are 3 s each (864 000 samples), with the same result.

**Not claimed:** equality with the ARM firmware binary, with GCC builds, or
with Rings hardware. The firmware's own golden hash was recorded with GCC and
glibc and is not reproduced here.

## 5. Block sizes, events, sample formats

* **Adapter alone, 48 kHz** (`TestPassThrough`): a counting engine through
  blocks of 1, 7, 12, 16, 48, 64, 333, 1024, 2048 and a changing sequence:
  300 000 samples each, every engine sample once and in order; the engine
  never runs more than one quantum ahead.
* **Event timing** (`TestEventTiming`): 126 notes around quantum and block
  boundaries, at 44.1, 48, 88.2, 96 and 192 kHz, in all ten block patterns.
  Each note reaches the engine in the quantum the rule names (first quantum
  at or after its internal time), never early, less than 12 internal samples
  late, and in the same quantum for every block pattern.
* **Real engine through the adapter** (`TestBlockSizesWithEngine`): one
  absolute event list per rate (218 to 861 events, 2 s), ten block patterns:
  identical samples. At 48 kHz also identical to the engine driven directly.
* **Through the VST3 interface** (`TestBlockSizes`): 202 events over 1.5 s
  per rate, ten block patterns, 32-bit and 64-bit buffers: 20 renderings per
  rate, all identical, at all five rates. 64-bit buffers carry the float
  values exactly.
* **Dense chords:** nine notes on one sample are part of every performance;
  the engine test checks the order and the four-per-quantum rule directly
  (`TestNoteQueue`).

## 6. Sample rates, latency, filter

The SDK contains no sample rate converter (searched: `base`, `public.sdk`,
`pluginterfaces`). The adapter uses its own Kaiser-windowed sinc
(`adapter/sinc_kernel.*`): 48 zero crossings per side at the lower rate,
designed for a 100 dB stopband, one exact coefficient row per fractional
position for rates with up to 2048 positions, otherwise 1024 rows with linear
interpolation. Positions are exact fractions, so there is no rounding drift.

**Acceptance limits, fixed before the first measurement** (in
`TestResamplerResponse`): passband gain within ±0.01 dB; everything that is
not the wanted tone at least 90 dB below a full-scale tone. Method: sine
tones from 37 Hz to 23.99 kHz in 9 % steps plus the band edges, least-squares
fit of the tone, residual and stopband leakage measured over at least 8192
samples.

| Host rate | Taps | Latency (samples) | Passband to | Worst passband deviation | Worst spurious | Worst stopband leak |
| --- | --- | --- | --- | --- | --- | --- |
| 44 100 | 106 | 49 | 19 105 Hz | −0.00007 dB | −110.9 dB | −100.5 dB |
| 48 000 | none | 0 | unchanged | exact | none | none |
| 88 200 | 96 | 90 | 20 795 Hz | −0.00009 dB | −100.7 dB | no stopband below 24 kHz |
| 96 000 | 96 | 97 | 20 795 Hz | −0.00009 dB | −100.4 dB | as above |
| 192 000 | 96 | 195 | 20 795 Hz | −0.00009 dB | −100.7 dB | as above |

Also measured and within the limits: 22 050, 32 000, 176 400, 384 000,
44 056, 47 999, 1234.5678, 12 345.678, 123 456.78 and 1 234 567.8 Hz. The
limits were met at the first run and not changed.

* **Latency** (`TestLatency`): an impulse appears exactly
  `LatencySamples()` after its host time, symmetric around its peak, with
  exact zeros before the filter's reach. Fitted tone phases agree with the
  reported latency within 1.4e-6 rad. The processor reports the same values
  through `getLatencySamples()` (`TestSampleRates`).
* **Sample count and drift** (`TestLongRun`): 180 s per rate in changing
  blocks (12 s in Debug). The engine has rendered exactly 8 640 000 internal
  samples at every rate, the number the formula gives; the 997 Hz tone at the
  end deviates from the ideal by at most 1.2e-6, where one slipped sample
  would show as 0.03 to 0.14.
* **Spectrum against the 48 kHz rendering** (`TestSampleRates`): all six
  models, a four-note chord with a bend and a control change, at 44.1, 88.2,
  96 and 192 kHz. Limit fixed beforehand: 0.02 dB for every component within
  60 dB of the strongest, 37 to 56 components per model between 90 Hz and
  15 kHz. Largest difference measured: 0.0031 dB. This compares the tuning
  and spectrum Torus actually has; no model is held to an ideal 440 Hz. The
  modal model alone is checked against absolute pitch (`TestPitch`: A4
  440.00 Hz, octave, ±12 semitones of Tune, ±2 of bend).

## 7. Instances, life cycle, state, editor

**Instances.** Engine level (`TestInstanceIsolation`): A's output is
identical alone, interleaved with a second engine that is reset, destroyed
and recreated, beside three threads creating and destroying engines (about
14 000 quanta rendered by them meanwhile), and when its quanta are rendered
by alternating threads. The same test shows that two *firmware* engines do
disturb each other, so the check can fail. Plug-in level (`TestInstances`):
the same with processors, the other instances at other sample rates, with
restarts, about 50 instances created and destroyed during A's renderings; also in offline mode and with A's blocks moved from thread
to thread.

**Life cycle** (plug-in test):

* `TestKeyboardQueue`: screen keys sound like MIDI at the block start; 64
  wait at most, the 65th is refused with `kResultFalse`; none survive
  deactivation; none older than 500 ms are played after a pause, also when
  parameter flushes without audio happen in between.
* `TestPauseAndFlush`: a pause without `process()` changes nothing; 60 002
  parameter flushes without audio leave five waiting values, not a pile;
  flushed values are in `getState()` at once and in the engine at the next
  audio; a block without output buffers still advances time.
* `TestActivationAndRateChange`: after deactivate, new rate, activate, at all
  five rates: time zero, no waiting events, silence, then the same samples
  as a fresh instance.
* `TestState`: 127 damaged states (wrong versions, every truncated length,
  not-a-number, infinities and out-of-range values in every position) are
  refused; `getState()` and the running sound are unchanged, compared with
  an instance that never saw them. Loading restarts the sound with the new
  values, centres the pitch wheel and drops waiting events and screen keys
  struck before the load; a key struck after it plays. Tens of thousands
  of states loaded from a second thread during 3 s of audio: no failure, the
  last state wins.
* `TestQueueLimits` (adapter test): 1024 events wait at most; further notes
  are refused and counted; parameter values are never lost.

**Controller** (`TestController`): nine parameters with the IDs, titles,
defaults and step counts of the work order; value texts; 27 kinds of junk
text refused by all nine parameters with the output value untouched,
including `+-5`; the model's display, text input, normalized value and
engine selection agree for all 128 MIDI values, a fine grid and both sides
of every step boundary; the MIDI map on all 16 channels of bus 0 has exactly
nine entries; controller and processor restore the same state; damaged
component and window states are refused.

**Editor** (`TestEditorInWindow`): the real VSTGUI window in hidden parent
windows, with a host stand-in that can refuse sizes and one that records
edits. Covered: automation with the window closed and open; a model click
as begin/perform/end edit; KEYS with host agreement and refusal, also at the
smallest zoom; a loaded window state the host does not follow; forced tiny
windows; display scale before and after opening; the size constraint; two
views at once; removal and re-attachment of the same view; reopening;
release without `removed()` with the native child window gone and later
parameter changes harmless; KEYS and resizing produce no parameter edit.

## 8. Diagnostics

**AddressSanitizer.** The whole tree (SDK, VSTGUI, plug-in, tests) was built
a second time with `/fsanitize=address` (MSVC 19.51, Release options, static
runtime) in a separate build directory, again after the last source change. `torus-classic.engine`,
`torus-classic.adapter` and `torus-classic.plugin` pass with no report; the
validator passed 47 of 47 against the instrumented bundle. Limits: MSVC
offers no undefined-behaviour or thread sanitizer, so signed overflow,
misaligned access and data races are not instrumented. Data-race freedom
rests on the design (atomics, a wait-free hand-over, a bounded queue; no
shared DSP memory) and on the stress tests above, which can miss a race.

**Debug build.** MSVC's run-time checks (`/RTC1`) and checked iterators are
active; all tests pass. The tests use their own `CHECK`, which stays active
in Release.

**Allocation.** The test programs replace `operator new` and count per
thread. No allocation in `DryEngine::Reset`, `NoteOn`, controls, model
changes and `Render` (`TestNoAllocation`, engine); none in the adapter's
`AddEvent`, `Process` and `Reset` at five rates (`TestNoAllocation`,
adapter); none inside any `process()` call of the plug-in tests that check
it: firmware comparison, block sizes, keyboard queue, flushes, state loads,
also while another thread loads states. Limits: `malloc`, `VirtualAlloc`,
aligned `operator new` and allocations inside system DLLs are not seen.
`process()` takes no lock: the only mutex in the plug-in serialises
`setState()` callers, and the `std::call_once` that guards the shared sine
table is reached from the engine's constructor only (by reading the code;
no test can show the absence of a lock).

**Second read.** Before the final builds, a separate Claude instance that had
seen none of the reasoning behind the code read the adapter, engine, shell
and the patched DSP spots against the work order, without building or
changing anything. It confirmed the index, overflow and latency arithmetic
of the adapter, the filter tables, the state hand-over, the editor's lifetime
handling and the parameter mapping, and reported four defects, all in
`source/processor.cpp` or reachable from it. All four are fixed:

| Finding | Fix | Test |
| --- | --- | --- |
| A screen key taken during a parameter flush without audio waited in the adapter, past its 500 ms lifetime, and sounded when audio resumed | Keyboard notes are read only in blocks with audio | `TestKeyboardQueue`, last part |
| A screen key struck before a state load played on the freshly loaded patch | Loaded states carry their time; keys struck up to then are dropped | `TestState`, "loading while notes ring" |
| `setActive(true)` discarded a state that no block had applied yet without storing its values again; automation of a block running during `setState()` could leave one stale value | `setActive` applies a pending state the way `process()` does | none: the interleaving cannot be forced from a test; by reading the code |
| `std::call_once` was reachable from `process()` through the engine reset on a state load (never blocking in practice) | Only `DryEngine`'s constructor calls `torus::InitResources()` | none; by reading the code |

**Performance.** `bench_torus_classic`, VS 2026 Release, this machine, one
core, 20 s of audio per row, blocks of 64, four voices re-struck twice a
second, default controls. Share of real time:

| Model | 48 kHz | 44.1 kHz | 96 kHz | 192 kHz |
| --- | --- | --- | --- | --- |
| Modal Resonator | 1.05 % | 1.36 % | 1.61 % | 2.27 % |
| Sympathetic Strings | 1.41 % | 1.71 % | 1.98 % | 2.63 % |
| Inharmonic Strings | 1.39 % | 1.62 % | 1.97 % | 2.62 % |
| FM Voice | 0.83 % | 1.08 % | 1.42 % | 2.05 % |
| Western Chords | 1.42 % | 1.65 % | 2.02 % | 2.65 % |
| Stereo Strings | 1.40 % | 1.64 % | 1.99 % | 2.68 % |

Single quanta of the engine (budget 250 µs each), 80 000 per model: median
1.7 to 3.1 µs, 99.9th percentile 16 to 28 µs, longest 71 to 151 µs (an
earlier run showed one of 254 µs). The long ones are single events
consistent with the operating system interrupting the measuring thread; the
benchmark cannot tell.

Model change, the one quantum in which the new model is configured, 300
changes per target model: median 1.8 to 4.9 µs, longest 4 to 52 µs; the
string models cost most, because their delay lines are cleared. The fade
around the change does not shorten that quantum. The deadline overruns the
Daisy kit showed at model changes are not to be expected at these figures on
this machine; whether a DAW under load shows peaks was not measured.

Engine reset (activation, state load), 2000 times: median 2.9 µs, longest
32 µs (196 µs in an earlier run), for 111 880 bytes.

Denormal numbers: in the fourth second after a strike with Damping 0, one
quantum costs 98 µs with denormals computed (39 % of real time) and 2.3 µs
with them flushed to zero (0.9 %). The adapter therefore sets flush-to-zero
and denormals-are-zero while it renders and restores the previous mode.

These are measurements on one machine. They are no guarantee for other
machines or for real-time behaviour inside a DAW.

**Memory per instance:** engine state 111 880 bytes (the firmware's `Synth`
with reverb: 177 640); adapter 17 KB at 48 kHz, 81 KB at 44.1 kHz, 75 KB at
88.2 kHz, 19 KB at 96 kHz, 20 KB at 192 kHz, at most about 0.5 MB at rates
with interpolated tables, the queue of 1024 waiting events included; the
processor object adds 2 KB of conversion buffers and the keyboard queue. The sine table (20 KB) is shared
by all instances and written once.

## 9. Not checked, and known limits

Open, for the user's DAW test ([list](DAW_TEST_0.1.0.md)):

* Loading, playing and listening in Studio One 7, Waveform 14 and Live 12
  Lite. No DAW has seen this plug-in.
* Whether the hosts deliver pitch wheel and CCs through the MIDI map.
* Whether the hosts honour the reported latency and the infinite tail, and
  what they do to tails at transport stop.
* Resizing, display scaling and KEYS in real hosts. One picture of the
  window was taken in the SDK's `editorhost` at 120 % display scale, with a
  model click and a key press; nothing else was looked at.
* CPU load and peaks at model changes inside a DAW.

Not covered by the automatic tests:

* Mouse interaction with the knobs and the keyboard view. The tests call the
  editor's functions; they do not send mouse events to VSTGUI.
* Undefined behaviour and data races beyond what AddressSanitizer, the Debug
  run-time checks and the stress tests show (section 8).
* Host positions beyond 180 s of continuous audio. Positions are exact
  64-bit fractions; the arithmetic was reviewed for overflow up to 2^31
  samples per block and years of run time, but not run that long.
* Other CPUs, other Windows versions, other compilers than the two listed.
* Equality with the ARM firmware (section 4).

Known properties that a reviewer should weigh:

* The sample rate converter is own code, not a library; the work order asked
  to prefer SDK functionality (there is none) or a small dependency. Reason
  and figures: section 6; decision noted in `docs/PLANUNG.md`.
* Notes are quantised to 0.25 ms and at most four start per quantum
  (README, "Timing"). Parameter automation acts as steps per quantum.
* At rates other than 48 kHz, peaks between the engine's samples can exceed
  the engine's 0.8 ceiling. In the test performances the largest was 0.63.
* All instances start their noise from the same seed: two instances given
  the same notes produce the same sound.
* `setProcessing()` does not reset the plug-in; resets are activation and
  state loading.
* When the host changes the sample rate, the plug-in does not call
  `restartComponent(kLatencyChanged)`: the rate changes only while the
  plug-in is inactive, and the new latency is in place when the host
  activates it and asks. A host that caches the old value across that would
  compensate wrongly.
* Screen keyboard notes need the host's message connection between
  controller and processor, and are dropped after 500 ms without audio.

## 10. For the reviewer

The parts where an error would matter most, and the checks that exist:

| Topic | Code | Checked by |
| --- | --- | --- |
| Reverb removal leaves the dry path unchanged | `engine/synth.cpp` against `tests/reference/src/physical_modeling.cpp` | `TestDryReference`, `TestAgainstFirmware` |
| Per-instance noise, shared sine table | `core/torus/dsp/rng.h`, `part.*`, `resources.cpp` | `TestInstanceIsolation`, `TestInstances`, noise state compared with the firmware's |
| Event time, causality, latency formula | `adapter/stream_adapter.cpp` (`Prepare`, `AddEvent`, `Process`) | `TestEventTiming`, `TestLatency`, `TestLongRun`, `TestBlockSizesWithEngine` |
| Queue overflow rules | `StreamAdapter::AddEvent` | `TestQueueLimits`, `TestPauseAndFlush` |
| State hand-over to the audio thread | `source/latest_value.h`, `Processor::setState`, `ApplyLoadedState` | `TestState` |
| Window lifetime with careless hosts | `source/editor.cpp`, `Controller::editorDestroyed` | `TestEditorInWindow`, also under AddressSanitizer |
| Provenance | `components.json`, `SHA256SUMS.upstream` | `torus-classic.provenance` |

To repeat everything: the three command groups of section 2, then
`build/windows-vs2026-x64/bin/Release/bench_torus_classic.exe`.
`tools/stage_result.ps1` fills `out/torus-classic-0.1.0/` from a Release
build; `tools/update_provenance.ps1` rewrites the manifest and hash list. For the
sanitizer build, configure a second build directory with
`-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /fsanitize=address /Zi"` and put
the compiler's `bin/Hostx64/x64` directory on the `PATH` when running the
tests.
