# Mendelejew

A VST3 instrument around the modal synthesis DSP of the Mutable Instruments
Elements module: bow, blow and strike exciters, a modal resonator, a stereo
reverb. Monophonic, played from MIDI. Version 0.1.0, phase 1 of the project.

What it is meant to prove: that the original DSP, written for a fixed 32 kHz,
can run at a **true internal 48 kHz** and still be the same instrument, in a
form that later serves firmware (Daisy Seed3, ESP32-S3) as well as a plug-in.

**Status: built, validated and tested automatically. Not yet loaded in a DAW,
not listened to, not reviewed.** Details and every measured number are in the
[build report](docs/BUILD_REPORT_0.1.0.md).

> **About the name.** The plug-in is called Mendelejew, after Dmitri
> Mendeleev (in the German spelling), who arranged the elements into the
> periodic table. "Elements" is the name of Mutable Instruments' module, and
> Mutable Instruments asks that derivative works do not keep a module's name
> and never carry the company's ("Mutable Instruments" is a registered
> trademark). In this repository "elements" is the internal name of the
> project only: its directory, branch, build targets and namespaces. This
> software is not made, endorsed or supported by Mutable Instruments.

## What you get

| | |
| --- | --- |
| Plug-in | `Mendelejew.vst3`, vendor TAUREON, category Instrument, Synth |
| Audio | No input, one stereo output. The hardware's two outputs: left carries the sound with the stereo side, at low SPACE the exciter signal; right the main output |
| MIDI | Notes with velocity, note-off, pitch wheel (±2 semitones), six controllers. One voice, last-note priority |
| Sample rate | The engine always runs at 48 kHz. A 48 kHz host gets its output as it is; at any other host rate a sinc converter follows (about 1 ms latency, reported to the host) |
| Window | Two rows of knobs, a status line with the rates in use, a screen keyboard |

## Parameters

Parameter IDs are fixed. The values from 0 to 1 are the hardware's pots.

| ID | Name | Range | Default | |
| --- | --- | --- | --- | --- |
| 100 | Contour | 0..1 | 1.0 | Envelope of bow and blow: short and percussive to slow and sustained |
| 101 | Bow | 0..1 | 0 | Level of the bow exciter |
| 102 | Bow Timbre | 0..1 | 0.5 | |
| 103 | Blow | 0..1 | 0 | Level of the blow exciter; above two thirds a tube joins in |
| 104 | Flow | 0..1 | 0.5 | Which stretch of air noise |
| 105 | Blow Timbre | 0..1 | 0.5 | |
| 106 | Strike | 0..1 | 0.8 | Level of the strike exciter; above 0.8 the strike itself bleeds into the output |
| 107 | Mallet | 0..1 | 0.5 | From sampled strikes over mallet and plectrum to bouncing particles |
| 108 | Strike Timbre | 0..1 | 0.5 | |
| 109 | Coarse | −30..+30 st | 0 | Whole semitones |
| 110 | Fine | −2..+2 st | 0 | |
| 111 | Geometry | 0..1 | 0.2 | From plates over strings to bars and bells |
| 112 | Brightness | 0..1 | 0.5 | |
| 113 | Damping | 0..1 | 0.25 | **Higher rings longer**, as on the hardware |
| 114 | Position | 0..1 | 0.3 | Where the structure is excited |
| 115 | Space | 0..1 | 0.5 | Below 0.1 dry with the exciter on the left; then stereo width; above 0.6 reverb |
| 116 | Freeze | Off, On | Off | Freezes the reverb: what the hardware does when a voltage pushes SPACE to the top |
| 117 | Velocity | 0..100 % | 50 % | How much the key velocity moves the hardware's STRENGTH; at 100 % from −15 dB to +15 dB around the nominal level, at 0 % not at all |
| 118 | Volume | 0..100 % | 100 % | |
| 119 | Pitch Bend | −2..+2 st | 0 | Fed by the pitch wheel; not saved |

MIDI controllers, on every channel: 21 Geometry, 22 Brightness, 23 Damping,
24 Position, 33 Volume, 91 Space. Whether they arrive is up to the host.

The defaults are the patch the hardware starts with. The hardware's
attenuverters and control voltage inputs have no counterpart: there is
nothing to patch. Its external audio inputs are not there either.

Playing: every key strikes. If a key goes down while another is held, the gate
closes for half a millisecond first, as a MIDI interface would do it for the
hardware. Letting go of the sounding key hands the pitch back to the most
recent key still held, without a new strike.

Parameter changes glide as the hardware's pots do (it low-passes them; half
way after about 35 ms, Position and Space about twice that). Loading a
project or preset sets everything at once and ends the sounding note.

## The sample rate

The original runs at 32 kHz in blocks of 16 samples. Here the DSP runs at
48 kHz in blocks of 24: the same half millisecond. It is not the 32 kHz code
with a converter behind it; every filter, delay and table is computed for
48 kHz. [UPSTREAM.md](UPSTREAM.md) lists what was changed for that.

The aim was the same instrument, not a different one with a wider band. So
each part is converted to behave in time and frequency as it does at 32 kHz:
a resonator mode keeps its frequency, decay and level; delays keep their
length in seconds; the exciters' filter keeps its response up to 16 kHz.

Measured against the unchanged original (see the build report):

* The port at 32 kHz **is** the original: 6.1 million samples identical.
* At 48 kHz the fundamental lies within 0.04 cent of the original's (the
  resolution of the measurement), a partial near 6 kHz within 0.1 cent; the
  decay time of either within 2 %.
* Mallet and plectrum sounds agree within 0.2 dB in every band up to
  15.5 kHz; sampled strikes within 1.5 dB up to 12 kHz; blown sounds within
  0.5 dB up to 8 kHz.

What still differs, and why, is listed under "Known differences" below.

**96 kHz: HOLD.** The DSP core accepts 96 kHz, and the same measurements pass
there with one known exception, but the plug-in does not use it: at a 96 kHz
host it runs the engine at 48 kHz and converts. The exception: a very bright
mallet pulse is three times as high and as narrow at 96 kHz and keeps content
far above 16 kHz; the resonator does not respond to that, but the soft limiter
in front of the left output does, and the exciter signal heard there at SPACE
0 comes out 3.5 dB lower. Before the engine follows the host to 96 kHz, that
has to be dealt with, and the plug-in tests and a listening comparison have
to be done at that rate.

## Known differences from the original

* **Sampled strikes** (Mallet below 0.4). The original plays 48 kHz
  recordings at 32 kHz without a filter, which folds their content above
  16 kHz down into the audible band. At 48 kHz they play at almost their
  recorded rate and that aliasing is absent. With the default Strike Timbre
  the bands differ by about 1 dB; with a high Timbre, where the original
  aliases heavily, by up to 5 dB.
* **Above the highest mode.** Between the highest mode of a note and 16 kHz
  the original's level falls to nothing at 16 kHz. Here it does not; that
  region, more than 60 dB below full scale, comes out up to 6 dB higher.
* **Above 16 kHz** the original has nothing. The port has what its filters
  and nonlinearities leave there: below −45 dB re full scale in every case
  measured at 48 kHz, mostly far below.
* **Blown air noise** above 8 kHz is 1 to 3 dB lower: the noise recording is
  interpolated linearly, and from a higher rate that filters differently.
* **Bowed and particle sounds** are driven by a random generator and differ
  from run to run by about 3 dB in the original itself. The port's averages
  lie within that.
* Not included: the string models and the "ominous voice" of later firmware
  versions, the hardware's "signature" (small per-unit differences derived
  from the serial number; the plug-in uses the values of an unseeded unit),
  external audio input.

## Build

From the repository root, as for the other plug-ins (see the
[main README](../../README.md)):

```bash
cmake --preset windows-vs2026-x64
```

```bash
cmake --build --preset windows-vs2026-x64-release
```

```bash
ctest --preset windows-vs2026-x64-release -R elements
```

The build ends with Steinberg's validator run on the plug-in, which lands in
`build/windows-vs2026-x64/VST3/Release/Mendelejew.vst3`. To try it without a
DAW:

```powershell
& "build\windows-vs2026-x64\bin\Release\editorhost.exe" "build\windows-vs2026-x64\VST3\Release\Mendelejew.vst3"
```

To install, copy the `Mendelejew.vst3` folder into a VST3 folder the DAW scans,
for one user `%LOCALAPPDATA%\Programs\Common\VST3\`. Nothing is installed
automatically.

## Layout

| Path | Layer | Contents |
| --- | --- | --- |
| `core/` | 1 | The DSP, adapted to a run-time sample rate, and the stmlib parts it needs. No plug-in framework, no allocation |
| `engine/` | 2 | The instrument: voice, controls and their smoothing, velocity, pitch wheel, output. `engine.h` and `controls.h` show no DSP type and no framework type; this is what firmware would build on |
| `adapter/` | 3 | Streaming: host block sizes, sample rate conversion, event timing. From Torus Classic |
| `source/` | 4 | VST3 processor, controller, state, window |
| `tests/` | | Three test programs and the unchanged original as reference |
| `docs/` | | Build report, review brief |

## Tests

| Test | What it shows |
| --- | --- |
| `elements.dsp` | The core at 32 kHz is the original, bit for bit. At 48 and 96 kHz: pitch, decay of single partials, level by band, each against the original. Writes its measurements to `dsp-measurements-Release.md` in the build directory; a Debug build compares fewer scenarios and leaves the measurements out |
| `elements.engine` | Voice and note priority, pitch, the three exciters, the resonator's parameters, reverb and freeze, smoothing, 90 s of random input at 48 and 96 kHz without a non-finite sample or a hanging note, the adapter's timing and block-size independence |
| `elements.plugin` | The plug-in as a host sees it: buses, notes, velocity, pitch wheel, automation, state, block sizes, five host sample rates, 64-bit buffers, the screen keyboard's messages |
| `elements.provenance` | The files from upstream are what `SHA256SUMS.upstream` says |

## License

MIT, like the repository. The DSP is © Emilie Gillet, MIT; see
[UPSTREAM.md](UPSTREAM.md) and [`licenses/`](licenses/).
