# Messier

A VST3 instrument for Windows x64 that plays a WAV file as a loop through a phase
vocoder and a granular processor, pitch and playback speed independent of each other.
The sound engine is the hardware-free core of the Daisy Seed3 firmware *Nebulae on
Daisy* 0.2.2 ([Daisy-Seed](https://github.com/sscheidl/Daisy-Seed),
`instruments/granular`), itself an adaptation of the default Granular Looper of Qu-Bit
Electronix' Nebulae v2.1.2. Native C++ on the Steinberg VST3 SDK and VSTGUI; no Csound,
no other library.

Named after Charles Messier and his catalogue of nebulae. Manufacturer TAUREON,
version 0.1.2, bundle `Messier.vst3`. Nothing in this repository is made, endorsed or
supported by Qu-Bit Electronix; their name and the product name of the original appear
only where origin and license require it (this file, [UPSTREAM.md](UPSTREAM.md),
[licenses/](licenses/)).

**Status 0.1.2:** builds, passes Steinberg's validator, tested automatically (bit for
bit against the separately compiled firmware core, see the
[build report](docs/BUILD_REPORT_0.1.0.md)). Codex' review of 0.1.0 found four defects
([review](docs/CODEX_REVIEW_0.1.0.md), [fixes](docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md)), its
check of 0.1.1 five more ([check](docs/CODEX_REVIEW_0.1.1.md),
[fixes](docs/CODEX_REVIEW_0.1.1_UMSETZUNG.md)); 0.1.2 fixes them, Codex' next check is
open. Not tried in a DAW, not listened to. The [DAW test list](docs/DAW_TEST_0.1.0.md) is
open.

**The window is provisional.** The build order's window (section 8: an Atlas nebula map
with the waveform, loop bounds, playhead, spray and the real grains, Orion as the visual
theme, a broad Blend slider above seven knobs) is not built yet; it follows once its
layout and colours are settled. Until then the window is the plain knob layout described
below.

Only the file source in 0.1.0. Live recording, overdub, circular recording, the dry
blend of the input, input gain and grain FM follow in 0.2.0, through the sidechain input
that is already there (see [Sidechain](#sidechain)).

## Sound

The eight controls are the pots of the Daisy kit, as values from 0 to 1; the core's
laws apply, snap bands around the centre included. The host shows the value the law
makes of the pot; typed text is converted back.

| ID | Parameter | Shown | Default | MIDI |
| --- | --- | --- | --- | --- |
| 100 | Start | where the loop starts, 0 to 100 % of the file | 0 % | CC 20 |
| 101 | Speed | playhead speed, -4x to +4x; the centre is +1x | +1x | CC 21 |
| 102 | Size | loop length as a share of the file after Start (the pot squared) | 100 % | CC 22 |
| 103 | Density | grains per second, 0.125 to 983 (the core's tanh/cubic law) | pot 0.4, 8.8 /s | CC 23 |
| 104 | Pitch | -3 to +2 octaves; the centre is unchanged | 0 oct | CC 24 |
| 105 | Overlap | grain length relative to the grain period, 0 to 100 % | 50 % | CC 25 |
| 106 | Blend | 0 % vocoder only, 100 % grains only, equal-power between | 0 % | CC 26 |
| 107 | Window | grain shape, seven shapes crossfaded continuously: Gaussian, Blackman-Harris, Ramp Down, Bartlett, Ramp Up, Sinc, Rectangle | Gaussian | CC 27 |
| 110 | Spray | random grain position, +- share of the file | 0 | - |
| 112 | Pan Random | random stereo position of the grains | 0 | - |
| 113 | Displacement | random delay of the grains, up to 10 s | 0 | - |
| 114 | Pitch Random | random grain pitch, up to +-2 octaves | 0 | - |
| 115 | Size Random | random grain length, +- on the squared Overlap | 0 | - |
| 117 | Mask | probability that a grain stays silent, up to 99 % | 0 | - |
| 120 | Freeze | the playhead stands still; grains and the vocoder go on (a spectral freeze, not a mute) | off | - |
| 121 | Reset | playback back to Start, on the rising edge of the value (for automation); the window's RESET button sends its presses directly to the processor and is not recorded as automation | - | - |
| 122 | Volume | behind the core, 0 to 100 %; 100 % leaves the core's output untouched | 100 % | CC 33 |
| 123 | Pitch Bend | +-2 semitones, from the pitch wheel; not saved | centre | wheel |
| 199 | Sample Changes | hidden and read-only: the processor's report that a file from the window became the sound; not saved | - | - |

The secondaries are the firmware's SOURCE page under their pot. Speed's and Blend's
secondaries (input gain, grain FM) need the audio input; their IDs 111 and 116 are kept
free for 0.2.0, as are 124 to 126 (Source, Record, Circular). As in the firmware, the
secondaries have no MIDI controller.

A step of a control, from automation or MIDI, takes effect on the next 64-sample
quantum; the core smooths the pots over 20 ms (Window over 100 ms), Volume glides over
at most 5 ms.

### Notes

As in the firmware: a note transposes (C4, note 60, is unchanged), sends playback back
to Start and sets the level by its velocity; the newest held note counts. With no note
held, the loop plays freely at its base pitch and full level, also after a note was
released. So note-offs matter here, unlike in Kekulé. Notes keep their MIDI channel;
releasing a note on another channel does not end it.

One difference from the firmware: one pitch wheel for all channels, because VST3
delivers the wheel as a parameter. The wheel bends only while a note is held (the
firmware applies the bend to the held note).

The screen keyboard below the controls plays like MIDI on channel 1 with velocity 100,
note-on on the press, note-off on the release, and sends both to the host's MIDI output
for recording. The octave keys `<<` and `>>` move it by octaves (C2 to C8), as in
Kekulé 0.3.1.

## The sample

**LOAD...** opens a file dialog. Accepted:

* RIFF/WAVE with 16 or 24 bit PCM or 32 bit float, also as `WAVE_FORMAT_EXTENSIBLE`;
* mono or stereo, 8 to 192 kHz.

The file is kept in the core's format, PCM16 at the file's own rate; the core converts
the rate when it reads, so the loop plays at the right pitch and speed at any file
rate. More than 16 bits are rounded to the nearest value, without dither (24-bit files
the same way as Nebulae on Daisy 0.3.0). Files above 48 kHz are read without a low-pass:
their content above 24 kHz folds back. At most 256 MiB as PCM16, about 23 minutes of
stereo at 48 kHz; a larger file is refused and the old one plays on.

A file is read on a worker thread and checked completely; only the finished file goes
to the audio thread, which fades the old one out (5.3 ms), swaps, and fades the new one
in from Start, as the firmware does. Unlike the firmware, every failure leaves the old
file playable. The window shows the file's name, format and length, or what went wrong:
not found, not a valid WAV (with the reason), too large, or out of memory (also then the
old file plays on). A file chosen in the window marks the project as changed, so that the
host offers to save it.

**Projects** store the file's path (UTF-8), not the audio. Opening a project reads the
file at once, in the thread that loads the project, so that an export straight after
opening does not start in silence. If the file is not there, the plug-in stays silent,
keeps the path (saving the project again keeps it too) and the window shows the path
with "File not found". Move a file back to its path, or load it again, to hear it.
The project's values and its file always reach the engine together: if the file cannot be
taken over at once, the whole project waits a few milliseconds and the previous sound
plays on, never the new values with the old file.

Memory: a loaded file takes its size as PCM16 (a 3-minute stereo file at 48 kHz: 33 MB).
During a swap the old and the new file exist at the same time, briefly also a third
one waiting.

## Timing and level

* The core always runs at 48 kHz, in quanta of 64 samples (1.33 ms, the control period
  of the original; the firmware uses 256). Other host rates go through the sinc
  converter of Kekulé, in the output direction only.
* Latency: 0 at 48 kHz, otherwise the converter's delay of about 1 ms (49, 90, 97 and
  195 samples at 44.1, 88.2, 96 and 192 kHz), reported to the host.
* Events act on the first quantum boundary that is not before their time; the result
  depends on the absolute positions only, not on how the host cuts its blocks.
* Tail: infinite. The loop plays without a held note.
* Activation and loading a project start the firmware engine anew: playhead at Start,
  the random generator at its seed. A project renders the same every time.
* Level: nothing is added to the core's output stage (the grains' RMS limiter at 0.2,
  the soft limit from 0.9 to about 0.95). Volume comes after it. At host rates other
  than 48 kHz the converter can overshoot that a little (up to 1.03 measured with dense
  rectangular grains); there is no clip after it.

## Differences from the firmware and from the original

| Point | Original (Nebulae v2.1.2) | Nebulae on Daisy 0.2.2 | Messier 0.1.0 |
| --- | --- | --- | --- |
| Grain level (F42) | effectively 0.90 (four equal waves of 0.5, amplitude 0.45) | 0.45 | **0.90 as the original**: up to 6 dB louder than the firmware while grains are sparse or quiet, see [UPSTREAM.md](UPSTREAM.md) |
| File normalisation (F58) | every file peak-normalised to 1.0, left and right separately | none | as the firmware: none |
| Vocoder with 44.1 kHz files (F44) | 147 cents too low and reading at 0.919 of the grains' position (the rate is compensated twice) | correct | as the firmware: correct; a deliberate difference from the original |
| Displacement (F41) | delayed grains fill the pool of 10; above about 0.1 most grains are dropped | bounded pool, new grains are dropped | as the firmware |
| Vocoder schedule | both channels in the same block | right channel one block later, vocoder layer 5.3 ms late (a Cortex-M7 work-around) | as the original: the same block |
| Quantum | 64 samples | 256 | 64 |
| Pitch wheel | - | per MIDI channel | one for all channels |
| Sources | file and live | file and live | file only |

The other differences of the firmware from the original remain (see Daisy-Seed's
README): ten grains per channel, a bounded adaptation of partikkel, Pitch Random over
+-2 octaves as the manual says, the corrected backward-frame interpolation of the
vocoder.

## Sidechain

The plug-in has a stereo audio input of type aux, "Sidechain", inactive by default. It
takes stereo, mono or no input. 0.1.0 does not read it: the core gets silence. It exists
now so that the bus layout of 0.2.0 is the same and projects keep loading. The adapter
already passes an input pair to the engine on the quantum grid.

## Install

Copy `Messier.vst3` (the whole folder) into `C:\Program Files\Common Files\VST3\`.
The bundle needs no Visual C++ runtime.

## License

This plug-in is MIT except one part: the phase vocoder in
[`core/reference_dsp.h`](core/reference_dsp.h) and [`core/reference_dsp.cpp`](core/reference_dsp.cpp)
is adapted from Csound's `Opcodes/pvlock.c` (and its windows correspond to
`Engine/fgens.c`) and is under the **GNU Lesser General Public License 2.1 or later**.
Changes to these two files are under the same license; new code is not put into them.
The texts are in [licenses/](licenses/): Qu-Bit MIT, De-Pattern MIT, Csound LGPL 2.1,
the Csound FGENS notice and Daisy-Seed's project MIT. This is the arrangement Daisy-Seed
documents, taken over; it is not legal advice.

Replacing the LGPL part: the bundle is built from the commit named in its
`BUILD_INFO.txt`. Check that commit out, change `core/reference_dsp.*`, and build as
below; `messier_core` is recompiled and linked into a new `Messier.vst3`. Nothing in the
bundle prevents the replacement.

## Build and test

From the repository root, as in the [main README](../../README.md):

```bash
cmake --preset windows-vs2026-x64
```

```bash
cmake --build --preset windows-vs2026-x64-release --target Messier
```

```bash
ctest --preset windows-vs2026-x64-release -R messier
```

| Test | What it checks |
| --- | --- |
| `messier.vocoder` | the firmware's own vocoder test, unchanged, against this repository's FFT backend |
| `messier.engine` | the engine layer bit for bit against the separately compiled firmware core; the grain level 0.90; two instances; file swaps; Volume; the display laws |
| `messier.adapter` | block sizes, event timing at five rates, latency, no drift over two minutes, queue limits, no allocation |
| `messier.loader` | WAV formats and conversion, damaged and cut files, size limit, missing files, a non-ASCII path, the loader thread |
| `messier.plugin` | the VST3 plug-in against the firmware core at 48 kHz, block sizes and 32/64 bit, rates, state, missing samples, loading while playing, two instances, keyboard, messages, parameter texts, MIDI mapping, buses |
| `messier.provenance` | the copies are what [components.json](components.json) says; the reference differs by the F42 line only |

`bench_messier` (built with the tests, run by hand) measures the load; the numbers are
in the build report.

Failing allocations are tested by fault injection: in Release builds every case (the
loader below 16 bytes, and every allocation of six host calls in turn); in Debug builds
the loader's cases from 40 bytes, the spare empty file included. Below that the Debug
standard library of MSVC ends the process by design (its container proxies), outside the
plug-in's code; the plug-in ships as a Release build.

## Structure

| Path | Layer |
| --- | --- |
| [`core/`](core/) | 1: the firmware core, vendored ([UPSTREAM.md](UPSTREAM.md)), and `fft_desktop.cpp` |
| [`engine/`](engine/) | 2: the looper engine around `neb::Engine` |
| [`adapter/`](adapter/) | 3: sample rate, quanta, event time (from Kekulé) |
| [`source/`](source/) | 4: processor, controller, state, editor, WAV reader, file loader |
| [`tests/`](tests/) | tests, the reference copies, the bench |
| [`tools/`](tools/) | `update_provenance.ps1`, `stage_result.ps1` |
| [`docs/`](docs/) | build report, DAW test list, review brief |
