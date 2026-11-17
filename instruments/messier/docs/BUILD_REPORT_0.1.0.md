# Build report: Messier 0.1.0

Date: 2026-11-17. Work order: `docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md` (approved by the
user on 2026-11-17, kept untracked in the main checkout). Built and tested by Claude
(Opus 5.5) on the user's machine, in the worktree `.work/messier/wt` on the branch
`messier-0.1.0` (from `main` at `27a00b3`), **uncommitted**: the user commits when they
say so.

**Status: ready for the DAW test and for Codex' review.** All builds and automatic checks
listed here passed. **This is not a release approval:** the plug-in has not been loaded
in a DAW, nobody has listened to it, and the independent review by Codex is still to
come ([brief](CODEX_PRUEFAUFTRAG_0.1.0.md)). Section 10 lists what was not checked and
the known limits, among them one measured risk at 192 kHz with very small host blocks.

Every figure below comes from a run on this machine (Intel Core i7-8700K, Windows 11,
VS 2026 Build Tools MSVC 19.51.36256 and VS 2022 Build Tools MSVC 19.44); the logs are in
`out/messier-0.1.0/logs/`. Test names in `code font` are functions in the test sources.

## 1. What was built

A VST3 instrument in `instruments/messier/`, bundle `Messier.vst3`, vendor TAUREON,
version 0.1.0, VST3 category `Instrument|Sampler`, class IDs generated for it
(`43CBFFE0 D8854AE8 8611239B 8F294A56` processor, `CAB5C51E AFFF4C36 A45CF126 48CE1BAB`
controller).

| Target | Directory | Role |
| --- | --- | --- |
| `messier_core` | `core/` | the firmware core, ten files from Daisy-Seed `8226a31` with three patches (one of them F42), and this repository's FFT backend `fft_desktop.cpp` (136 lines) |
| `messier_engine` | `engine/` | the looper engine: `neb::Engine` through its public interface, file hand-over, Volume (355 lines) |
| `messier_adapter` | `adapter/` | Kekulé's adapter in Mendelejew's form, quantum 64, an input pair for 0.2.0 (655 lines) |
| `messier_shell` | `source/` | processor, controller, state, editor, WAV reader, file loader (about 3,600 lines, half of them copies from Kekulé and Mendelejew) |
| `Messier` | `source/factory.cpp` | the bundle |
| `messier_reference` | `tests/reference/`, `tests/reference_api.*` | test-only: the firmware core with the F42 line alone, namespace `neb_reference`, and its own driver |

Tests: `messier.vocoder`, `.engine`, `.adapter`, `.loader`, `.plugin`, `.provenance`
(about 2,400 lines including the firmware's vocoder test); `bench_messier` measures the
load.

## 2. Origin, patches, license

Details in [UPSTREAM.md](../UPSTREAM.md). In short:

* Source: Daisy-Seed `8226a31` (Nebulae on Daisy 0.2.2), read with `git archive`, because
  the local checkout was on `dcp` (0.2.0). `origin/main` had moved on to `53fd8a0`
  (0.3.0) before the copy; the copied files are the same there except the part of
  `wav.h` Messier does not take. 0.2.2 at `8226a31` as ordered.
* Taken: `reference_dsp.*`, `pcm.h`, `engine.*`, `control.*`, `wav.h` (reduced), and the
  two headers they include (`fft.h`, `profile.h`), plus the firmware's vocoder test and
  the five license texts. 26 files listed in `components.json`, 22 verbatim.
* Patches: P1 `Engine::SetFileBuffer` (one buffer per file; no sound change), P2 `wav.h`
  reduced (no sound change), P3 `GRAIN_AMP = 0.90f` (F42, the one sound change).
* **FFT, not foreseen by the order:** the core's production vocoder calls an FFT through
  `fft.h`, which the firmware implements with CMSIS-DSP. That would be a new library;
  `core/fft_desktop.cpp` implements the same contract (section 5).
* License: `core/reference_dsp.*` LGPL-2.1-or-later, the rest MIT; texts in `licenses/`
  (the Qu-Bit text byte for byte, `.gitattributes` as in Daisy-Seed), entries in
  `THIRD_PARTY_NOTICES.md`, the root README says that Messier contains an LGPL part. The
  staged bundle carries the license texts, `BUILD_INFO.txt` (branch, commit, state of the
  working tree, how to rebuild) and `SOURCE_SHA256SUMS.txt` (the hashes of every source
  file of the plug-in, because the build comes from an uncommitted working tree).

`messier.provenance` checks the hashes, that the verbatim files equal their origin, that
`tests/reference/reference_dsp.h` and `core/reference_dsp.h` become the firmware's file
when the F42 line is put back, that `tests/reference/` holds nothing else, and one version
number in `CMakeLists.txt`, `source/version.h`, `components.json` and the component
headers. Checked against itself too: changing a second value in the reference file fails
it ("differs from the firmware by more than the F42 line").

## 3. Build matrix

| Build | Result |
| --- | --- |
| VS 2026, Release, all four plug-ins | 0 errors; validator 47 passed, 0 failed for each bundle; `ctest` 16 of 16 (Toy Piano, Kekulé, Mendelejew, Messier) |
| VS 2026, Debug, all four plug-ins | 0 errors; validator 47/0 for each bundle; `ctest` 16 of 16 (the heavy tests shorten their runs under `_DEBUG`, the firmware's vocoder test does not) |
| VS 2022, Release, all four plug-ins, fresh build folder | 0 errors; validator 47/0 for each bundle; `ctest` 16 of 16 |
| MSVC AddressSanitizer (VS 2026, Release with `/fsanitize=address`), Messier tests | the five Messier test programs (engine, adapter, loader, plugin, the firmware's vocoder test) built with `/fsanitize=address /Zi`, linked against `clang_rt.asan_dynamic-x86_64.dll`, run in full: all passed, no report from the sanitizer. MSVC has no UBSan or TSan; the two-thread tests ran without a thread checker |
| `editorhost.exe Messier.vst3` | window opened, closed by `CloseMainWindow()`, exit code 0 (a smoke test, not a look at the window) |

Warnings: none in Messier's own code (`/W4 /permissive-`) in the final builds of all three configurations. The vendored core compiles with the conversion warnings switched off, as Kekulé's does. The firmware's vocoder test, kept verbatim, warns C4146 (`-k*n` with an unsigned `k`: those test values come out positive instead of negative); switched off for that target and noted for Daisy-Seed in section 10.

The final round (after the last source change) rebuilt VS 2026 Release and Debug and VS 2022 Release and ran `ctest -R messier` (6 of 6) in each; the full runs of all 16 tests above were made on the state just before, which differed only in the window's status poll and in two test assertions.

## 4. Floating point and denormals

As in Kekulé: the SDK compiles with `/fp:fast`; every Messier target and the reference use
`/fp:precise`. While the adapter renders, denormals count as zero (FTZ/DAZ); the
reference comparisons render the firmware core in the same mode.

## 5. The FFT backend

`core/fft_desktop.cpp`: a real 2048-point FFT in Csound packing (DC, Nyquist, then re/im
pairs), forward unscaled, inverse scaled to give the input back, computed as a complex
1024-point radix-2 FFT of even and odd samples plus a split. Tables are constants built
once in a function-local static (first used in the engine's constructor, outside the
audio thread); no other state, so two instances in two threads share nothing they write.

`messier.vocoder` is the firmware's `tests/test_vocoder.cpp`, unchanged, run against it:

* the backend against the core's radix-2 `RFFT` over 30 inputs (noise at three levels,
  impulses, DC, Nyquist, single bins, silence): worst relative error forward 2.4e-07,
  round trip 4.8e-07, within the firmware's bound of 2e-5;
* `PcmReader` against `PcmView::Read`, `WrapFmod` against `std::fmod`, bit for bit;
* the production vocoder `Mincer` with this backend against `ReferenceMincer`, 25
  scenarios: the ten well-conditioned ones agree to -90.7 dB or better, the others stay
  within the firmware's gate (at most +4.2 dB above the reference's own rounding spread,
  band energies within 0.52 dB). 31.7 million checks.

## 6. Reference comparison

**Engine layer** (`test_engine.cpp`, `TestAgainstFirmware`): `LooperEngine` against the
firmware core in `tests/reference/` (F42 line only), driven by `tests/reference_api.cpp`,
which does what the firmware does: one fixed file buffer, `Init()` with it, a file change
by gate, acknowledgement, overwriting the buffer and publishing. Both at 48 kHz, quanta of
64, vocoder schedule set to `kScheduleCommon` at run time, the firmware's seed (a fresh
`neb::Engine`). Five scenarios of 12 s each: stereo 48 kHz, mono 44.1 kHz, stereo
44.1 kHz, mono 48 kHz, and stereo 48 kHz with a change to the mono 44.1 kHz file in the
middle. 3,281 to 3,408 actions per scenario: every pot, the six secondaries, notes on
and off on four channels with velocities, all-notes-off, the wheel (on all 16 channels for
the firmware), Freeze, Reset. **Result: 0 of 1,152,000 samples differ in each scenario**
(compared with `memcmp`), rms 0.074 to 0.080.

**Plug-in** (`test_plugin.cpp`, `TestAgainstFirmware`): the VST3 processor with a loaded
project (state and WAV file), at 48 kHz in varying host blocks (1 to 2048 samples),
against the same firmware core fed with each event on the first quantum at or after its
sample, values mapped as the processor maps them (parameters as pot values, velocity
rounding, the Reset edge, the wheel as 14 bits on all channels). 986 events over 10 s:
**0 of 960,000 samples differ**, rms 0.076, no allocation in `process()`.

That the vocoder schedule is a real setting: a fresh firmware engine uses the compensated
stagger (2); Messier sets 0 (`TestSettingsOfTheCore`).

## 7. F42 and the other findings of the analysis

| Finding | Original | Core 0.2.2 | Messier 0.1.0 | Checked by |
| --- | --- | --- | --- | --- |
| F42 grain level | 0.90 | 0.45 | **0.90**: one grain of a constant file with a rectangular window plays at 0.9000 of its sample | `TestGrainLevel` (exact: sample x `GRAIN_AMP`, and `GRAIN_AMP == 0.90f`) |
| F58 normalisation | per channel to peak 1.0 | none | none, as the core | the reader keeps the samples (`test_loader.cpp`, `TestFormats`) |
| F44 vocoder at 44.1 kHz | 147 cents low | correct | correct, as the core; README names it as a deliberate difference | bit-exact against the core with 44.1 kHz files |
| F41 displacement | pool of 10 saturates | bounded pool, new grains dropped | as the core | bit-exact against the core; README describes it |

What F42 changes in level, from the code (not measured against a device): up to 6.02 dB
louder than the firmware while the grain sum stays below the limiter's 0.2 RMS; the
difference shrinks between 0.2 and 0.4 RMS and disappears above. How often the soft limit
at the core's output (above 0.9) now acts, measured with `bench_messier` over 10 s per
setting (section 9): never with the defaults, with Blend 0.5, with sparse grains or with
dense grains and Blend 0.5; 0.004 % of the samples with dense rectangular grains alone
(Density and Overlap at maximum, Blend 100 %), with a loud file and with a quiet one
alike. There is no comparison figure for 0.45: the core has no switch for it, and the
reference carries the same patch.

## 8. Timing, rates, files, state

* **Block sizes and sample formats** (`TestBlockSizes`): the same performance in host
  blocks of 1, 7, 64, 256, 333, 1024 and varying lengths, 32 and 64 bit, at 48 and
  44.1 kHz: identical output in every case (`==` on all samples). 64-bit buffers carry the
  float values exactly.
* **Events** (`test_adapter.cpp`, `TestEventTiming`): every event lands on the first
  64-sample quantum not before its time, at 44.1, 48, 88.2, 96 and 192 kHz, in fixed and
  varying blocks.
* **Latency** (`TestLatency`): an impulse appears exactly `LatencySamples()` after its
  host time: 49, 0, 90, 97, 195 samples at 44.1, 48, 88.2, 96, 192 kHz (1.0 to 1.1 ms);
  the processor reports the adapter's value (`TestSampleRates`).
* **Drift** (`TestLongRun`): 120 s at each of the five rates, varying blocks: the number of
  internal samples rendered is exactly what the output needs, the tone's phase is the host
  clock's (largest deviation 4.9e-07, one slipped sample would be 0.03 to 0.14); no
  allocation.
* **Rates at the plug-in** (`TestSampleRates`): 2 s at 44.1, 88.2, 96 and 192 kHz: output
  length exact, 96,000 samples rendered at 48 kHz each time, no allocation; peaks 0.35 to
  0.38.
* **Files** (`test_loader.cpp`): PCM16, PCM24 (rounded like Daisy-Seed's `Pcm24To16`,
  checked value by value including both clamps), float32 (rounded, clamped, NaN as 0),
  EXTENSIBLE for both, mono and stereo, 8 and 192 kHz, chunks in any order with odd sizes,
  a path with umlauts and a euro sign. 19 refused files: rates outside 8 to 192 kHz,
  three channels, 8 bit, 32-bit integer, 64-bit float, ADPCM, an unknown EXTENSIBLE
  sub-format, two fmt or data chunks, no fmt, no data, an empty data chunk, a data chunk
  cut short, a header cut short, garbage, RF64, four bytes, a wrong block alignment. A
  missing file, a folder and an empty path are "missing". The size limit is checked from
  the header before anything is allocated (limit 127 bytes refuses a 128-byte file, 128
  accepts it; the product limit is 256 MiB).
* **Loader** (`TestLoader`): reading on the worker thread, the status, a failed request
  keeps the playing file and says why, 21 requests in a row end with the newest, a missing
  project file stays named and silent, an empty path is no file, retired files are freed
  by the worker within 50 ms.
* **State** (`TestState`): round trip byte for byte, the path included. 212 damaged or
  cut states refused by processor and controller alike, with the state unchanged: every
  shorter length, version 2, NaN, 1.5, -0.25, a path length of -1 and of 32 KiB + 1,
  invalid UTF-8, a NUL in the path. A project whose file is missing: silence, the path is
  kept when saving again, the status says "missing". A state without a path: no file.
* **Export** (`TestExportStart`): `setState` before the first block, then one block of 1024
  samples: the sample sounds after the core's 256-sample fade-in (rms 0.126).
* **Loading while playing** (`TestLoadWhilePlaying`): a load message as the controller
  sends it, while `process()` runs in 256-sample blocks: the new file is swapped in, no
  allocation in `process()`, the saved state names the new file; a damaged file keeps the
  old one; an empty path is refused. The swap itself (`test_engine.cpp`, `TestSwap`): the
  largest sample-to-sample step during the swap 0.034, in steady play 0.082; the peak
  after the request 0.20 (0.84 before); one quantum in the middle is silent (rms 1.6e-07);
  no allocation; a second request replaces the waiting file; swapping to no file is
  silence.

## 9. Instances, real time, load

* **Two instances** (`TestTwoInstances`, engine and plug-in): an instance renders the same
  samples whatever another does, interleaved in one thread or playing at the same time in
  a second thread at another rate with another file.
* **Activation** (`TestActivation`): deactivating and activating restarts the firmware
  engine with its seed; the same project renders the same samples.
* **No allocation** in `process()`, counted per thread through a replaced `operator new` in
  every plug-in test, also during file swaps, state loads and keyboard notes: 0. Limits of
  that counter: it sees `operator new` only, not `malloc` or allocations in other DLLs.
  `process()` takes no lock on any path in the code; that is a statement from reading, not
  a measurement.
* **Load** (`bench_messier`, Release, one thread, the engine through the adapter as in the
  processor, 10 s per row, a 10 s stereo file at 48 kHz):

| Setting | Host rate | Block | Share of one core | Slowest block | Peak | Samples above 0.9 |
| --- | --- | --- | --- | --- | --- | --- |
| defaults (Blend 0: vocoder only) | 48 kHz | 64 | 1.82 % | 235 us of 1333 us | 0.560 | 0.000 % |
| defaults (Blend 0: vocoder only) | 48 kHz | 256 | 1.83 % | 501 us of 5333 us | 0.560 | 0.000 % |
| defaults (Blend 0: vocoder only) | 192 kHz | 64 | 3.07 % | 496 us of 333 us | 0.634 | 0.000 % |
| defaults (Blend 0: vocoder only) | 192 kHz | 256 | 3.14 % | 394 us of 1333 us | 0.634 | 0.000 % |
| half and half | 48 kHz | 64 | 1.82 % | 332 us of 1333 us | 0.828 | 0.000 % |
| half and half | 48 kHz | 256 | 1.83 % | 529 us of 5333 us | 0.828 | 0.000 % |
| half and half | 192 kHz | 64 | 3.14 % | 440 us of 333 us | 0.918 | 0.000 % |
| half and half | 192 kHz | 256 | 3.04 % | 386 us of 1333 us | 0.918 | 0.000 % |
| Density, Overlap at maximum, Blend 0.5 | 48 kHz | 64 | 2.25 % | 304 us of 1333 us | 0.902 | 0.000 % |
| Density, Overlap at maximum, Blend 0.5 | 48 kHz | 256 | 2.27 % | 494 us of 5333 us | 0.902 | 0.000 % |
| Density, Overlap at maximum, Blend 0.5 | 192 kHz | 64 | 3.51 % | 389 us of 333 us | 0.902 | 0.000 % |
| Density, Overlap at maximum, Blend 0.5 | 192 kHz | 256 | 3.58 % | 476 us of 1333 us | 0.902 | 0.000 % |
| the same, Pitch at maximum | 48 kHz | 64 | 2.28 % | 392 us of 1333 us | 0.809 | 0.000 % |
| the same, Pitch at maximum | 48 kHz | 256 | 2.35 % | 588 us of 5333 us | 0.809 | 0.000 % |
| the same, Pitch at maximum | 192 kHz | 64 | 3.61 % | 424 us of 333 us | 0.837 | 0.000 % |
| the same, Pitch at maximum | 192 kHz | 256 | 3.52 % | 406 us of 1333 us | 0.837 | 0.000 % |
| granular only, dense, rectangle | 48 kHz | 64 | 2.29 % | 439 us of 1333 us | 0.950 | 0.004 % |
| granular only, dense, rectangle | 48 kHz | 256 | 2.30 % | 462 us of 5333 us | 0.950 | 0.004 % |
| granular only, dense, rectangle | 192 kHz | 64 | 3.58 % | 435 us of 333 us | 1.033 | 0.004 % |
| granular only, dense, rectangle | 192 kHz | 256 | 3.57 % | 430 us of 1333 us | 1.033 | 0.004 % |
| granular only, sparse (F42 range) | 48 kHz | 64 | 1.74 % | 311 us of 1333 us | 0.628 | 0.000 % |
| granular only, sparse (F42 range) | 48 kHz | 256 | 1.73 % | 501 us of 5333 us | 0.628 | 0.000 % |
| granular only, sparse (F42 range) | 192 kHz | 64 | 2.99 % | 511 us of 333 us | 0.695 | 0.000 % |
| granular only, sparse (F42 range) | 192 kHz | 256 | 2.94 % | 314 us of 1333 us | 0.695 | 0.000 % |
| dense, mono 44.1 kHz file | 44.1 kHz | 256 | 1.91 % | 631 us of 5805 us | 0.824 | 0.000 % |
| granular only, dense, quiet file | 48 kHz | 256 | 2.25 % | 486 us of 5333 us | 0.950 | 0.004 % |

"Slowest block" is the slowest single host block of the run against its duration. Every
512 internal samples the vocoder computes one hop for both channels at once (two forward
and one inverse FFT each, the window fill and the phase loops), about 0.3 to 0.5 ms here;
it falls entirely into one host block. **At 192 kHz with 64-sample blocks that block takes
longer than its 333 us** (all settings: 0.39 to 0.51 ms in the final run; an earlier run,
with builds running beside it, had 0.31 to 2.4 ms). The average stays at 3 to 4 % of a core, so a host with a larger buffer or with
its own buffering absorbs it; a host that really processes 64-sample blocks at 192 kHz in
real time may drop out. Not changed in 0.1.0: spreading the hop over several blocks would
change the core. At 48 kHz the slowest 64-sample block took 0.44 ms of 1.33 ms.

## 10. Not checked, and known limits

Not checked:

* **No DAW, no listening, no look at the window.** The user does these
  ([DAW test list](DAW_TEST_0.1.0.md)). In particular not heard: whether F42's 0.90 sounds
  as the original, whether the soft limit becomes audible with dense grains.
* No comparison with the Daisy kit or with the original instrument; the reference is the
  firmware core compiled on this machine.
* The file dialog, the status line and the knob layout were never seen; there are no
  window tests beyond the geometry checks copied from Kekulé (keyboard keys, window
  sizes, zoom limits).
* Hosts other than the test doubles: what a real host does with the sidechain bus, with
  `kInfiniteTail`, with the messages between the two halves (a host that offers no
  messages leaves the window without file status and the LOAD button without effect).
* Memory pressure with several large files (256 MiB each, up to three per instance during
  a swap).

Known limits, also in the README:

* The 192 kHz / 64-sample block case above.
* No clip behind the rate conversion: at host rates other than 48 kHz the output can
  exceed 1.0 slightly (1.033 measured with dense rectangular grains at 192 kHz).
* Files above 48 kHz are read without a low-pass (as the core does).
* The wheel bends only a held note (as the firmware).
* Release of retired files depends on the loader's thread running; if the retire queue is
  ever full, new files wait instead of being taken (no leak, no allocation, but no swap).

Found and fixed during the work, without a test that would show it: the window's timer
did not ask for the file status (`Editor::notify` came from Kekulé without the poll), so
"Loading ..." would have stayed until the window was reopened. Fixed by reading the code;
there is no window test for it (the user tests the window).

For Daisy-Seed, not changed here: `tests/test_vocoder.cpp` pushes `-k*n` with an unsigned
`k`, so the negative multiples it means to test of `WrapFmod` come out as large positive
numbers (MSVC warning C4146; GCC and Clang compute the same).

Remaining defects: none known.

## 11. For the reviewer

* Start with UPSTREAM.md and `engine/looper_engine.cpp` (how the core is driven and where
  P1 is used), then `source/processor.cpp` (`process`, `Restart`, `RetireReleased`,
  `TakeIncoming`, `setState`) and `source/file_loader.cpp` (ownership and threads).
* The reference driver `tests/reference_api.cpp` is the independent side of the bit-exact
  comparison; judge whether it is independent enough.
* Self-determined points, with reasons: `docs/PLANUNG.md`, section "Messier".
