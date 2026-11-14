# Changes

## 0.1.1 (2026-11-14)

Fixes from the Codex review of 0.1.0 (`docs/CODEX_REVIEW_0.1.0.md`; what was
done: `docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md`).

* EL-R01: a release from the screen keyboard that finds its queue full is no
  longer dropped; it is played once the notes queued before it have been
  taken. Strikes that do not fit are still dropped.
* EL-R02: a note-off that the host's MIDI output list does not take, or that
  falls into a block without such a list, is sent again in the next blocks;
  the key gets no new note-on there before it.
* EL-R03: the output stays within ±1 also behind the sample rate converter.
* EL-R04: resonator modes are converted by their poles: frequency and decay
  of their ringing as at 32 kHz at any resonance, not only at high ones.
* EL-R05: the resonator's start filters, which some modes keep for the first
  half millisecond, are converted too.

32 kHz is still the original bit for bit; the measurements against it at 48
and 96 kHz moved by at most 0.1 dB.

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
