# Changes

## 0.2.0 (2026-11-16)

After the owner's first test of 0.1.0 in Studio One ("Das Plugin und der
Algorithmus funktioniert. Es entstehen melodische, zufällige Melodien"), three
additions they asked for:

* **Mutation lock.** A small padlock at the lower right of the Mutation knob,
  and the parameter Mutation Lock. Closed, the pattern is frozen: no algorithm
  changes it until the lock is opened. Tonic and scale still work.
* **Mutate every.** A small knob at the lower left of Mutation, with the number
  in its middle. At "-" mutation runs on every sixteenth as before; at 1 to 16
  the pattern repeats that many passes unchanged, then the next pass begins
  changed, by as much as one pass would have changed it.
* **Ladder dims what it does not use.** While algorithm 3 is chosen, Mutation,
  Notes, the lock and the small knob are dimmed; it plays the scale itself and
  never mutates. A pattern that stopped changing at full Mutation was this.
* **Names for the algorithms.** Drift, Runs, Ladder, Breathe, Focus instead of
  1 to 5. Resting the mouse on the name, or on the knob, shows what the
  algorithm does. Typed values take the name or the number.
* **Ten more scales**, seventeen in all: Dorian, Phrygian, Lydian, Mixolydian,
  harmonic and melodic minor, major pentatonic, blues, whole tone, hirajoshi.
  The former Pentatonic is now Minor Pent.

The saved state is version 2. Projects from 0.1.0 load: the lock open,
mutation on every sixteenth, each scale under its new number. Automation of Scale recorded with 0.1.0 points to
other scales now, as the knob has more positions.

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
