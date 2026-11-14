# Changes

## 0.1.0 (2026-11-14)

First version: phase 1 of the Elements project. The plug-in is called
Mendelejew; "Elements", the working title of the first build, remains the
internal project name. The class IDs are those of the first build.

* The DSP of the Mutable Instruments Elements module (bow, blow and strike
  exciters, modal resonator, reverb) from pichenettes/eurorack, adapted to a
  sample rate set at run time. At 32 kHz it is the original bit for bit; the
  instrument runs it at 48 kHz.
* An instrument engine free of plug-in framework types: one voice with
  last-note priority, the hardware's smoothing of its pots, key velocity as
  the hardware's STRENGTH, pitch wheel, the hardware's output conversion.
* A VST3 instrument on that engine: 19 saved parameters and the pitch wheel,
  state, a window with two rows of knobs, a status line and a screen
  keyboard. Streaming adapter, parameter hand-over between the threads and
  windowing are taken from Torus Classic 0.2.0.
* Tests against the unchanged original, which is kept in the repository for
  that purpose only.

Not in this version: more than one voice, the string models and the "ominous
voice" of later firmware versions, external audio input, the engine at
96 kHz in the plug-in (the core supports it; status HOLD).
