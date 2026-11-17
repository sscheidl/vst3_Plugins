# Changes

## 0.1.0 (2026-11-17)

First version, after the build order of 2026-11-17
(`docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md`, kept by the user).

* VST3 instrument around the hardware-free core of Nebulae on Daisy 0.2.2 (Daisy-Seed
  `8226a31`): phase vocoder and granular processor on one WAV file, File source only.
* The eight pots, the six secondaries that act without an input, Freeze, Reset,
  Volume; MIDI notes with velocity and note-off, one pitch wheel for all channels,
  CC 20 to 27 and 33, host automation; screen keyboard with octave keys.
* WAV files with 16 or 24 bit PCM or 32 bit float, also EXTENSIBLE, mono or stereo,
  8 to 192 kHz, up to 256 MiB as PCM16; read on a worker thread, swapped in with the
  firmware's fade; every failure keeps the old file. Projects store the path and read
  the file while loading.
* Grain level 0.90 as the original instrument (F42), the only change to the core's
  sound; vocoder on the common schedule; 64-sample quanta.
* A sidechain input (aux, inactive), not read yet: the bus layout of 0.2.0.
* This repository's own FFT backend for the core instead of CMSIS-DSP.
