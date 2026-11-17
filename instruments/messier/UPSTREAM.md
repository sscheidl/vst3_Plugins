# Where the code comes from

Messier contains code copied from the Daisy Seed3 firmware *Nebulae on Daisy* in the
neighbouring repository [sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed),
`instruments/granular`. This file says what was taken, from which state, and what was
changed, with the reason for every change. The same facts are in
[`components.json`](components.json) for machines, with one line per file in
[`SHA256SUMS.upstream`](SHA256SUMS.upstream). The CTest `messier.provenance` checks both.

## Origin

| | |
| --- | --- |
| Repository | Daisy-Seed, local checkout next to this repository |
| Commit | `8226a31cf48a25236970dc55b96b4046b24ddffa` (`origin/main`, 2026-11-17 20:53) |
| Directory | `instruments/granular`, firmware version 0.2.2 |
| Last commit that touched it | `6da72bafc1fef871d3636529f92e6a82a37031c1` (2026-11-17 20:31) |
| How it was read | `git archive` / `git show` of that commit. The local checkout was on the branch `dcp` with version 0.2.0, so its working tree was not used |

On 2026-11-17, before copying, `origin/main` had moved on to `53fd8a0` (the merge of
Nebulae 0.3.0, 24-bit WAV files, on top of 0.2.2). Between `8226a31` and `53fd8a0` the
copied files are byte-identical except `wav.h`, whose changes (`WavInfo::bits` and
`Pcm24To16`) lie outside the part Messier takes. The build order names 0.2.2 at
`8226a31`; that is the state used. The unmerged branch `claude/nebulae-24bit` was not
used either. The plug-in builds without the Daisy-Seed checkout; Daisy-Seed was not
changed.

The firmware in turn contains, according to its `LICENSES/README.md`:

* the vocoder (`Mincer`) adapted from Csound `Opcodes/pvlock.c`, (C) 2009 V Lazzarini,
  LGPL-2.1-or-later, audited at Csound `efbcabb4d56e6237e2cabc9949cfadadda946cf9`; the
  windows correspond to `Engine/fgens.c`;
* window, FFT, vocoder, RMS and control-law code selected from the De-Pattern pedal port
  `073a1ae4f4ce41bda54f8c78819f3b2a3319df4a` (MIT, including Qu-Bit's notice);
* the control laws of Qu-Bit's original instrument, `4be42b5159ae5c63466d84f0a8ab3b2fb4afd5a6`
  (MIT).

Nothing was downloaded from those upstreams for this plug-in.

## What was taken

Only the hardware-free part of the firmware:

| Local | Origin (`instruments/granular/`) | Relation |
| --- | --- | --- |
| `core/reference_dsp.h` | `src/reference_dsp.h` | patched: F42 (P3 below) |
| `core/reference_dsp.cpp` | `src/reference_dsp.cpp` | verbatim |
| `core/pcm.h` | `src/pcm.h` | verbatim |
| `core/engine.h` | `src/engine.h` | patched: P1 |
| `core/engine.cpp` | `src/engine.cpp` | verbatim |
| `core/control.h`, `core/control.cpp` | `src/control.h`, `src/control.cpp` | verbatim |
| `core/fft.h` | `src/fft.h` | verbatim: the interface of the vocoder's FFT |
| `core/profile.h` | `src/profile.h` | verbatim: compiles to nothing with `NEBULAE_PROFILE=0` |
| `core/wav.h` | `src/wav.h` | patched: P2, reduced to what the engine uses |
| `tests/core/test_vocoder.cpp` | `tests/test_vocoder.cpp` | verbatim: the firmware's own test, run against Messier's FFT backend |
| `licenses/*.txt` | `LICENSES/*.txt` | verbatim, the five license texts |
| `tests/reference/*` | `src/*` (the ten files above) | verbatim except the F42 line (see below) |

Not taken: `main.cpp`, `hw_glue`, `admin`, `banks`, `sd_files`, `boot_intent`, `heap`,
`memory.ld`, `control_sysex`, `mod_bridge`, `modulation`, `wav.cpp` (the SD loader, the
RIFF parser and the panel LEDs), `fft_cmsis.cpp` (the CMSIS-DSP backend), the fakes of
the firmware tests.

## Changes to the vendored DSP (`core/`)

Three changes; one of them changes the sound.

**P1, `engine.h`: the file buffer by pointer.** One added member,
`Engine::SetFileBuffer(int16_t*)`. The firmware has one fixed file buffer, given to
`Init()`, and overwrites it while the engine is muted; Messier holds each file in its
own buffer, so that every failure leaves the old file playable and the audio thread
never waits for a copy. The engine layer calls it only between the acknowledgement of
the mute and the publication of the next generation, the moment at which the firmware
writes its buffer. No effect on the sound: the bit-exact reference test runs the
unpatched engine the firmware's way (one buffer, overwritten) through a file change.

**P2, `wav.h`: reduced.** Only `WavInfo` and `FileBridge`, which `engine.h` uses, with
a comment saying so. The declarations of the SD loader, the RIFF parser and the LED
helpers are gone; their code was not copied. No effect on the sound.

**P3, `reference_dsp.h`: F42, the grain level.** `GRAIN_AMP` is `0.90f` instead of
`0.45f`, the only change to the sound. From the analysis of the original instrument
(case `nebulae-v2.1.2` of the local L2A study, finding F42, kept outside this
repository), in Messier's words: the instrument passes partikkel an amplitude of 0.45
and the same sample table four times with the same read position and no wave-gain
table; partikkel's default wave gain is 0.5 per wave, and the four identical waves add
in phase, so a grain plays at 4 x 0.5 x 0.45 = 0.90 of its sample. The firmware's core
reads each grain once and multiplies by 0.45 (`engine.cpp`, the grain loop), with no
compensation elsewhere; the value came from the pedal port. The places checked are the
instrument (lines 518, 748-751, 767-769, 781), Csound `partikkel.c` (131-135, 263-265,
451; the default is the same in Csound 6.09.1, 6.12.2, 6.18.1 and today) and the core.
This follows from the code; it was neither measured nor heard on a device. The user
decided on 2026-11-17 that Messier plays at 0.90. `reference_dsp.h` stays under the
LGPL. If Daisy-Seed changes its firmware the same way, the patch goes with the next
copy of the core.

Effect: while the grains stay below the RMS limiter's ceiling of 0.2 (few or quiet
grains), Messier is 6.02 dB louder than the firmware 0.2.2; between 0.2 and 0.4 RMS the
difference shrinks, above 0.4 both sit at the limiter. With Blend between the two
engines the balance shifts toward the grains accordingly. The soft limit at the core's
output (from 0.9) acts more often than in the firmware; the build report has the
numbers.

## Messier's own code beside the copies

* `core/fft_desktop.cpp`: the backend of `fft.h`, a real 2048-point FFT in the Csound
  packing, computed as one complex 1024-point radix-2 FFT and a split. The firmware
  links CMSIS-DSP for it (Apache-2.0), which this repository does not contain; the
  build order allows no new library. MIT. Its tables are constants built once, it keeps
  no other state, so instances in several threads share nothing they write. The
  firmware's own test `tests/core/test_vocoder.cpp` runs unchanged against it: the
  backend against the core's radix-2 reference (`RFFT`, worst relative error 2.4e-7
  forward), and the production vocoder `Mincer` with it against `ReferenceMincer`,
  within the firmware's own gates.
* `engine/looper_engine.*`: drives `neb::Engine` through its public interface (see the
  README). Not a copy.
* `source/wav_reader.cpp`: `Pcm24To16` is the rounding of Daisy-Seed's Nebulae 0.3.0
  (`src/wav.h` at `53fd8a0`, MIT), so that 24-bit files become the same PCM16 as on the
  kit. The rest of the reader is new.
* The adapter, the window code and several headers in `source/` are copies from Kekulé
  0.3.1 and Mendelejew 0.1.4 in this repository (their headers say which);
  `source/spsc_queue.h` comes from Daisy-Seed `instruments/physical-modeling` by way of
  those two.

## Settings of the core

Compile-time: `NEBULAE_MOD=0`, `NEBULAE_PROFILE=0`, no `NEBULAE_REFERENCE_VOCODER`.
Run-time: `Engine::SetSchedule(kScheduleCommon)`, both vocoder channels in the same
block, as the original instrument. The firmware's default since 0.2.2 is the
compensated stagger, a work-around for the Cortex-M7 that delays the vocoder layer by
256 samples; a desktop does not need it. Quantum: 64 samples per `Engine::Process`.

Global state: the core has none that can be written, apart from `profile::block`, which
exists only with `NEBULAE_PROFILE=1`. The random generator is a member of `Engine`;
the windows and the vocoder's work memory are members too.

## The pinned reference (`tests/reference/`)

The same ten files, as they are at `8226a31`, except the F42 line in
`reference_dsp.h`: the comparison must not fail on the one intended difference. The
test `messier.provenance` puts the original line back and checks that the file then
hashes to the firmware's, and that the directory holds nothing else. The reference has
neither P1 nor P2: it is driven the firmware's way. It is compiled into its own test
library with the namespace `neb` renamed to `neb_reference` and with the same FFT
backend (the only one this repository has); the driver is `tests/reference_api.cpp`,
written apart from the engine layer. Test-only; the plug-in does not contain it.

## Checking and updating

`messier.provenance` (CTest) checks every listed file against its hash, that verbatim
files equal their origin, the F42 rule above, and that the version is the same in
`CMakeLists.txt`, `source/version.h` and `components.json`.

After taking over a newer state of the core, run

```powershell
pwsh instruments/messier/tools/update_provenance.ps1 -DaisySeed ..\Daisy-Seed -Commit <commit>
```

It reads the origin with `git show`, so the Daisy-Seed checkout may be on any branch.
