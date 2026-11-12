# Toy Piano Classic

First test object of this repository: the simplest possible instrument port
from the Daisy Seed3 firmware to a native VST3 plug-in.

**Status:** specified, not yet implemented.

## What it should prove

1. Firmware DSP code runs unchanged, or nearly so, inside a VST3 plug-in.
2. The chain build → install → load in a DAW → play over MIDI works on this machine.
3. The result can be checked against the firmware by measurement, not only by ear.

## Scope

| Feature | Target |
| --- | --- |
| Sound | Sine plus a quiet second harmonic (12 %), as in the firmware |
| Master pitch | ±24 semitones |
| Amp envelope | AD (attack, decay), one-shot |
| GUI | Simple: one control per parameter |
| Control | MIDI notes with velocity |

Not in scope: the 16 hardware key voices, LEDs, the Mod on Daisy matrix, the
Ambient and Tape variants, presets.

## Origin

`instruments/toy-piano/src/` in
[sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed), sound engine as
of commit `44b08ca` (2026-10-25, MIT, © TAUREON):

| File | Role | Portable as is |
| --- | --- | --- |
| `toy_piano.h`, `toy_piano.cpp` | Voices, AD envelope, sine table, soft limiter | Yes. One dependency: `daisysp::mtof` |
| `config.h` | Constants: voice count, time ranges, gains | Yes |
| `main.cpp` | Hardware, USB-MIDI, LEDs, pots | No. The plug-in shell replaces it |

## What the firmware engine does

* 32 voices: 16 fixed key voices and a pool of 16 for MIDI. Only the MIDI pool
  matters for a plug-in.
* A note-on triggers a one-shot AD envelope whose peak is `velocity / 127`.
  Note-off is ignored.
* Attack 2–300 ms, decay 50–3000 ms, both logarithmic. Times are latched at the
  strike; sounding notes keep theirs.
* Re-striking a sounding note reuses its voice without resetting phase or level.
  With all 16 voices busy, the oldest is stolen.
* The sample rate is a parameter of `Init()`. The second harmonic is dropped
  above a quarter of the sample rate to avoid aliasing.
* Output is mono. Voice gain is sized for 16 keys, so a single note at full
  velocity peaks near −27 dBFS; a soft limiter holds the sum below 0.8.
* There is no master pitch. The engine offers a per-voice pitch ratio
  (`Modulate`) that a master pitch can build on.

Open decisions are listed in [docs/PLANUNG.md](../../docs/PLANUNG.md).
