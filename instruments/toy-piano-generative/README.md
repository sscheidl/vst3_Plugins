# Toy Piano Generative

VST3 instrument, version 0.1.0. The sound engine of
[Toy Piano Classic](../toy-piano-classic/) played by a generative step
sequencer: sixteen steps that are drawn at random from a scale and keep
rewriting themselves by rule, in time with the host.

**Status:** builds, passes Steinberg's validator and its own tests. It has
not been loaded in a DAW, its window has not been looked at, and **nobody has
listened to it**, see [Still to be checked](#still-to-be-checked).

This is a test bed. The sequencer is a statistical rebuild after a behaviour
specification of an existing generative sequencer, written by the owner and
kept outside this repository: the same rules and the same probabilities, with
its own random generator, so not the same notes. No code of the original is
used. Where this plug-in deliberately does something else, it says so under
[Choices and deviations](#choices-and-deviations).

## What it does

While the host plays and **RUN** is on, the sequencer takes one step per
sixteenth of the host's tempo and song position. A step that holds a note
strikes the Toy Piano voice; a rest does nothing. Notes are one-shots, as in
Toy Piano Classic: nothing ends them but their own decay.

MIDI notes and the screen keyboard do one of two things, chosen with
**TRANSPOSE**: on, a key moves the running sequence so that its tonic sounds
at that key; off, a key plays a note by hand.

## The sequencer's rules

**The pattern.** Sixteen steps. Each holds a note or a rest. A stored note is
the tonic, plus nothing or one octave, plus an entry of the scale's list. The
Octave setting is added when a step is played, not stored.

**Drawing a step.** With a chance of 100 % minus *Notes* the step becomes a
rest. Otherwise it becomes a note: the lower or the upper octave with equal
chance, and with 30 % the tonic itself, else one entry of the scale's list,
each entry equally likely. The lists end with one more tonic, which makes the
tonic more likely still:

| Scale | List, semitones above the tonic | Share of notes on the tonic's pitch class |
| --- | --- | --- |
| Major | 0 2 4 5 7 9 11 0 | 47.5 % |
| Minor | 0 2 3 5 7 8 10 0 | 47.5 % |
| Pentatonic | 0 3 5 7 10 0 | 53.3 % |
| Odd | −2 0 1 13 0 7 8 8 | 47.5 % |
| Octave | 0 12 0 | 100 % |
| Fifths | 0 7 12 0 | 82.5 % |
| Tonic | 0 | 100 %, and always the lower octave |

**A new instance** starts with a pattern of its own: every step is the tonic
with the chance *Notes*, else a rest.

**Choosing another scale** draws all sixteen steps anew. If that leaves no
note and *Notes* is above 0, the first step gets the tonic, one or two octaves
up.

**Changing the tonic** transposes the stored notes by the difference. Rests
stay rests.

**The clock.** Every sixteenth is one tick. With *Divide* N a step lasts N
sixteenths; its note sounds on the first of them. After *Length* steps the
pattern starts over. Whenever the sequencer starts, it starts at step 1.

**Within one tick:** advance, then mutate, then play. So a change that a tick
makes to the step under the playhead is heard in that same tick.

**Mutation**, by algorithm. Each tick tries once:

| Algorithm | What a tick does |
| --- | --- |
| 1 | With the chance *Mutation*, one of all sixteen steps is drawn anew. Steps beyond *Length* keep changing unheard. |
| 2 | As 1. Then, with the chance *Mutation* × *Notes*, three steps in a row are written as a run up the scale's list, one or two entries at a time, an octave higher once the run passes the end of the list. |
| 3 | No mutation. Plays the scale's list itself, entry after entry; the stored pattern is kept but not heard. |
| 4 | As 1, and every second pass through the pattern is silent. |
| 5 | Works on the step under the playhead. Up to 50 %: with half the chance *Mutation*, a note gets a new pitch; rests stay, so the rhythm never changes. Above 50 %: with the chance *Mutation* − 50 %, the step is drawn anew, rest included. |

**The Mutation knob** is not linear: its first half covers 0 to 20 %, its
second half 20 to 100 %, so that small amounts are easy to set.

The sequencer itself is plain C++ without any plug-in type, in
[sequencer/](sequencer/), so it can be tested on its own and reused elsewhere.

## Controls

Sound, as in Toy Piano Classic, plus the tuning:

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Master Pitch | −24 to +24 semitones, whole steps | 0 | Sounding notes follow within a few milliseconds |
| Tuning | 0 to 100 % | 0 % | 0 %: every note at its exact pitch, A4 = 440 Hz. 100 %: every note at the next whole hertz below, A4 = 439 Hz, A1 = 54 Hz (32 cents flat). Taken at the strike |
| Attack | 2 to 300 ms, logarithmic | 2 ms | Applies from the next strike |
| Decay | 50 to 3000 ms, logarithmic | 583 ms | Applies from the next strike |
| Volume | silence, then −60 to +24 dB | 0 dB | In front of the engine's soft limiter |

Sequencer:

| Control | Range | Default |
| --- | --- | --- |
| RUN | off, on | on |
| Algorithm | 1 to 5 | 1 |
| Mutation | 0 to 100 % | 0 % |
| Notes (Note Probability) | 0 to 100 % | 65 % |
| Scale | Major, Minor, Pentatonic, Odd, Octave, Fifths, Tonic | Minor |
| Tonic | C, C#, D … B | A |
| Octave | 0 to 5 | 2 |
| Length | 1 to 16 steps | 16 |
| Divide | 1, 2, 3, 4, 6, 8, 16, 32, 64 sixteenths per step | 1 |
| TRANSPOSE (Keys Transpose) | off, on | on |

All of them can be automated. Their host parameter IDs are in
[source/parameters.h](source/parameters.h); the sound parameters keep the IDs
they have in Toy Piano Classic.

### Transport

* The sequencer steps only while the host's transport plays and RUN is on.
  Steps lie on the sixteenths of the song position and start on their exact
  sample, whatever the host's block size.
* Every start of the transport, and switching RUN on, starts the pattern at
  step 1. The pattern itself and the random generator carry on; they are not
  reset.
* When the host stops, no new steps come. Notes that sound ring out.
* A jump of the song position while playing, as at the end of a loop, does not
  restart the pattern.
* If the host names no tempo, 130 BPM is used; if it names no song position,
  the sixteenths are counted from the start.

### Register

A sequencer note sounds two octaves above its number. At the default setting
(tonic A, Octave 2) the pattern lies between A3 and G5, around the Toy Piano's
own keys C4 to C6. A note that would lie above MIDI note 127 sounds an octave
lower.

### Keys that transpose

With TRANSPOSE on, a key says where the tonic sounds: the key's pitch class
becomes the Tonic, its octave the Octave. The keys C1 to B6 cover Octave 0 to
5; a key below or above takes the nearest octave. The change acts at once and
is heard from the next step on; notes that sound are not bent. It stays after
the key is released.

* A key of the screen keyboard changes Tonic and Octave as if their knobs had
  been turned. The key at which the tonic sounds is tinted.
* A MIDI note from the host does the same at its exact sample. It does not
  sound. The plug-in reports the new Tonic and Octave to the host.
* **<<** and **>>** move the screen keyboard by an octave (it starts at C3 to
  C5 and reaches from C1 to C7) and move the sequence by the same octave at
  once. Each stops at its own limit: the keyboard at C1 and C7, the Octave at
  0 and 5.

With TRANSPOSE off, keys and MIDI notes play notes, exactly as in Toy Piano
Classic.

### Window

Two rows of knobs, RUN, TRANSPOSE, and the screen keyboard, which **KEYS**
shows and hides. The window scales as a whole between 50 % and 200 % and keeps
its aspect ratio; size, keyboard and its octave are saved with the project.
Ctrl+click resets a knob; the mouse wheel moves a stepped knob by one step.

## MIDI

| MIDI | Effect |
| --- | --- |
| Note-on, any channel | TRANSPOSE on: sets Tonic and Octave. Off: strikes a note; velocity sets its level |
| Note-off | Ignored |
| Pitch wheel | ±2 semitones, added to Master Pitch |
| CC 16, 73, 75 | Master Pitch, Attack, Decay |

No MIDI controller is tied to a sequencer parameter. The sequencer's notes are
not sent to the host as MIDI.

## Saved with the project

The sound settings, the sequencer's settings, RUN, TRANSPOSE, **the sixteen
steps and the state of the random generator**. A reloaded project continues
with the pattern it had and mutates on from there as the original would have.
Loading does not draw a new pattern. Where the playhead stood is not saved.

## Choices and deviations

Where the specification left a choice, or where this plug-in does something
else on purpose. The first four are readings of unclear points, accepted by
the owner as assumptions and still to be checked against the original:

1. **First step.** After a start, step 1 sounds on the first sixteenth.
2. **The run of algorithm 2.** The entry it starts on, its stride of one or two
   entries and its octave are each drawn with equal chances; one octave holds
   for the whole run; its chance is Mutation × Notes as one draw.
3. **Tuning at 100 %** is "the next whole hertz below", which also takes a
   full hertz from a pitch that is whole already (every A from 55 Hz up).
4. **Whole percent.** Mutation and Notes are whole percent; the dead zones at
   the ends of a hardware knob are left out.

Decided by the owner:

* **Statistical rebuild:** same rules and chances, another random generator.
* **Exact tuning by default;** the whole-hertz pitch is an amount to dial in.
* **Rests in the start-up pattern** where the original plays a low note.
* **Twelve tonics.** The specification has the seven naturals; a keyboard also
  has black keys, and all twelve are allowed.
* **Keys, MIDI notes and octave buttons transpose** the sequence.

Decided while building:

* The register two octaves up, and folding at note 127.
* How the sequencer follows the transport (above).
* A new instance gets a random start, so two instances play different
  patterns.
* The scale named "Odd" has no name in the specification.

## Left out of this version

Parameter locks per step; gate length, envelope shape and glide of the
original's own voice; its second oscillator; LFO routes to length, octave and
gate; sync pulse input and output; a step display; fine tuning; a button for
"a new pattern" (changing the scale does it); sending the sequencer's notes as
MIDI.

## Build

From the repository root, after fetching the SDK as described in the
[main README](../../README.md):

```bash
cmake --preset windows-vs2026-x64
```

```bash
cmake --build --preset windows-vs2026-x64-release
```

```bash
ctest --preset windows-vs2026-x64-release
```

The build ends with Steinberg's validator run on the plug-in. Result:
`build/windows-vs2026-x64/VST3/Release/Toy Piano Generative.vst3` (a folder).
To install, copy that folder to `C:\Program Files\Common Files\VST3` as
administrator and rescan in the DAW.

## Verified

VERIFIED_TABLE

All of it is automated and ran on one machine: Windows 11 x64, MSVC 19.51
(Visual Studio Build Tools 2026) and MSVC 19.44 (Build Tools 2022). The
statistical checks use fixed seeds; each measured share must lie within five
standard deviations of the value its rule states.

## Still to be checked

The list for the DAW is in [docs/DAW_TEST_0.1.0.md](docs/DAW_TEST_0.1.0.md).
In short: nobody has listened to it; the window has not been looked at; how
Studio One hands over tempo, position, loops and tempo changes; whether the
host shows a transposition made by a MIDI note; automation of the sequencer
parameters; save and reload; CPU load.

## Limits of this version

* With Mutation above 0 two passes over the same bars, and two exports, sound
  different: the pattern carries on where it was, it does not rewind with the
  song.
* A pattern that mutated does not mark the project as changed. Save to keep it.
* Parameter changes take effect at block boundaries; the sequencer's settings
  are heard from its next step. Only notes, transposing MIDI notes and steps
  are placed on their exact sample.
* A tempo change inside a block is followed from the next block on.
* Whether a host shows a parameter change that the plug-in reports (a MIDI
  note that transposes) is up to the host.
* What Toy Piano Classic lists as its limits holds here too: no presets, the
  screen keyboard's notes start with the next audio block, the window scales
  but does not reflow.

## Sources

| Path | Contents |
| --- | --- |
| `sequencer/` | The sequencer: plain C++, no plug-in types |
| `source/step_clock.h` | The host's transport as sixteenths with sample positions |
| `source/engine.*` | Pitch offset, volume, Nyquist guard, harmonic fade as in Toy Piano Classic; the tuning |
| `source/parameters.h`, `value_text.h` | Parameters, ranges, the mapping of notes and keys |
| `source/state_io.h`, `handover.h` | The saved state; passing the pattern between the audio thread and the host's threads without a lock |
| `source/processor.*` | Audio side: transport, sequencer, MIDI, state |
| `source/controller.*` | Parameters as the host sees them, keys that transpose, window settings |
| `source/editor.*`, `keyboard_*` | The window, the screen keyboard and its octave buttons |
| `tests/` | The sequencer's own test and the plug-in test |

The sound engine is not copied: the plug-in links the unchanged firmware
engine in [`../toy-piano-classic/core/`](../toy-piano-classic/core/). The
window, controller and engine wrapper are copies of Toy Piano Classic 0.3.2,
extended.
