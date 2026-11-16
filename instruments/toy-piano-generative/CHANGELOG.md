# Changes

## 0.1.0 (2026-11-16)

First version: the sound engine of Toy Piano Classic 0.3.2, played by a
generative step sequencer. A new plug-in with its own class IDs; Toy Piano
Classic itself is unchanged.

* **Sequencer.** Sixteen steps drawn from one of seven scales, five mutation
  algorithms, note probability, tonic, octave, length and clock divide. It
  follows the host's transport, one step per sixteenth, on the exact sample.
  Plain C++ in `sequencer/`, without plug-in types.
* **Keys that transpose.** With TRANSPOSE on, a key of the screen keyboard or
  a MIDI note puts the sequence's tonic on that key; `<<` and `>>` move the
  keyboard and the sequence by an octave. All twelve pitch classes can be the
  tonic.
* **Tuning.** A knob from the exact pitch (A4 = 440 Hz) to the next whole
  hertz below (A4 = 439 Hz, A1 = 54 Hz).
* **State.** The pattern and the random generator are saved with the project;
  a reloaded project continues as it was.
* **Window.** Toy Piano Classic's, with a second row of knobs for the
  sequencer. A drag of a knob now reaches the host as one edit, with a
  beginning and an end.

Not in this version: parameter locks, gate and glide rules, LFO routes, sync
pulses, a step display. The README lists what was decided, what was assumed
and what was left out.

Built, validated and tested automatically. Not loaded in a DAW, not looked at,
not listened to.
