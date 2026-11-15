# Changes

## 0.3.0 (2026-11-15)

The three steps agreed during the review of 0.2.0, the knob colours asked
for on 2026-11-15, and the open point of the review of 0.2.1. Built and
tested automatically; not in a DAW yet, and not reviewed yet
([review brief](docs/CODEX_PRUEFAUFTRAG_0.3.0.md)).

* **4, 8 or 16 voices.** Chosen in the window, beside the title; four is the
  default and sounds as before. More voices are more of the same instrument:
  banks of four, each one the firmware's unchanged four-voice engine. A note
  is as loud as it was; a fifth note no longer takes over the first. The
  number of voices is parameter 111, *Polyphony*: saved with the project,
  not automatable, without a MIDI controller. Details in the README, section
  Voices.
* **Rest, for Plug-in Nap.** The plug-in now knows when nothing sounds and
  nothing waits. It then puts out exact zeros, sets the silence flags and
  reports no tail; while anything rings it reports an infinite tail as
  before. A host can use that to stop processing an idle instance. Whether
  Studio One 7 does is for the DAW test to show: PreSonus publishes no rule,
  and its Plug-in Nap was for effects only when it was introduced. Details
  and limits in the README, section Rest.
* **A lock instead of FREE.** The switch beside TUNE is a small padlock in a
  circle, without text: closed, Tune moves in semitones; open, it is free.
  It has a tooltip. The parameter behind it is still 110, *Tune Free*, with
  its meaning unchanged; the lock shows its opposite.
* **Knob rings in three colours**, in memory of the modules this engine
  descends from: white for pitch and output, magenta for the resonator,
  turquoise for the strike. Colours only; see the README, section Window.
* **Tune events of the internal helper (TC-R10).** `ControlEvent` built a
  Tune event without its semitone, which the bridge read as −24. The plug-in
  never used that path; a test did. The helper now passes Tune on to
  `TuneEvent`, and the adapter test checks the semitone that arrives.
* **State version 3**: the eleven saved values now include the number of
  voices. States of versions 1 and 2 load with four voices.
* Install folder in the documents: the development machine keeps its test
  builds in `C:\Program Files\Common Files\VST3\` since 2026-11-15.

One sentence of 0.1.0 to 0.2.2 no longer holds as it stood: the plug-in
delivers the firmware engine's output sample for sample *except at rest*,
where it puts out exact zeros and the firmware what is left of its
resonators, below −180 dBFS. The first note after a rest is the firmware's
again, bit for bit.

Component versions: engine 0.2.0 (banks, rest), adapter 0.3.0 (rest; the
output filter empties itself below −180 dBFS once exact zeros come in).

Not changed: the vendored DSP, the sound of four voices, sample rate
conversion, latency, the IDs and meaning of parameters 100 to 110, MIDI
controllers.

## 0.2.2 (2026-11-15)

The plug-in is now called **Kekulé**, after August Kekulé, who found the
benzene ring (Rings, ring, Kekulé). Decision of the owner. Nothing else
changes: no DSP, parameter or state code is touched.

* Hosts show Kekulé with the accent. The Unicode class info carries it; the
  8-bit class info, which has no defined encoding for an accent, says
  Kekule. The window title shows Kekulé.
* The bundle is `Kekule.vst3`, without the accent. Remove an installed
  `Torus Classic.vst3`: both have the same class IDs.
* Class IDs, parameters and the saved state are unchanged, so projects and
  presets saved with Torus Classic load.
* "Torus Classic" stays the internal name: directory, branch, library and
  test targets, namespaces.
* Codex's review of 0.2.1 is included ([review](docs/CODEX_REVIEW_0.2.1.md)):
  released for the DAW test; its point TC-R10 (P3) is not addressed here.

## 0.2.1 (2026-11-14)

Fixes from the Codex review of 0.2.0 ([review](docs/CODEX_REVIEW_0.2.0.md),
[what was done](docs/CODEX_REVIEW_0.2.0_UMSETZUNG.md)). No new features.

* **Filter after a bypass (TC-R06).** The low-pass kept its old state while
  fully open and gave it back when it was closed again, even after a long
  silence. Its integrators now run all the time; fully open the samples
  still pass exactly as they came.
* **Filter glide at high sample rates (TC-R05).** At 88.2 kHz and above the
  filter control stopped just short of its target, so "off" was never quite
  off. The glide now runs in double precision and lands exactly, at every
  supported rate.
* **Tune at the half semitone (TC-R07).** In a very narrow range around each
  half semitone, the display and the engine could round to different
  semitones. There is now one rounding, from the host's double value, used
  by the display, by automation and by a loaded state.
* **Note-offs of the screen keys (TC-R08).** Letting go of a key could be
  lost when 64 strikes were waiting, or when the host's MIDI output list had
  no room. Releases no longer wait in the queue and are never refused; a
  note-off the host cannot take now comes in a later block, and the host
  never gets a second note-on for a key it already holds.
* **Faulty samples at the output stage (TC-R09).** Filter and limiter now
  turn a sample that is not a number or infinite into silence and bound
  finite ones, so only finite samples leave the plug-in. The engine already
  delivered finite samples; this is a safeguard.
* README and a code comment still said full Damping sustains; since 0.2.0 it
  is Damping 0.

Not changed: engine, sample rate conversion, latency, parameters, state
format, window.

## 0.2.0 (2026-11-14)

Additions asked for after the first DAW test in Studio One.

* **Damping reversed.** A higher value now damps more (shorter sound); 0
  sustains. CC 23 follows. The engine is unchanged: it gets the mirror image.
* **Tune in semitones.** Tune sounds, reads and types in whole semitones. A
  small **FREE** switch beside the knob (parameter 110, *Tune Free*) turns
  free tuning back on. The Tune parameter itself stays continuous for the
  host; the rounding happens where the value meets the engine.
* **Filter.** A new knob (parameter 109, CC 74): a 12 dB per octave low-pass
  over the whole output, 100 Hz to 20 kHz; fully open it does nothing.
* **Limiter.** Last in the chain, after the filter, at the host's sample
  rate: no sample leaves the plug-in above −6 dBFS. Both channels share the
  gain; it drops at once and recovers over about 0.15 s. Below the ceiling
  the signal is untouched. No look-ahead, no latency.
* **Screen keys send MIDI.** The plug-in has an event output, "MIDI Out". A
  strike on the screen keyboard goes out as a note-on, letting go as a
  note-off; held keys are released if the plug-in is deactivated meanwhile.
* **State version 2** stores ten values (filter and FREE added). Version 1
  states from 0.1.x are converted when read: Damping mirrored, filter open,
  FREE on exactly if the saved Tune was no whole semitone.

Consequences worth knowing:

* The limiter changes loud passages. The claim that the plug-in equals the
  firmware engine sample for sample at 48 kHz now holds while the level stays
  below −6 dBFS and the filter is open; the test keeps the Volume at 50 % for
  that reason.
* Existing automation of Damping (and CC 23) now acts the other way round.
  Saved states are converted; automation data in a project is not.
* Filter and limiter run after the sample rate conversion. The filter's
  control is an event like the others and stays independent of the host's
  block size.

Not changed: the engine (`torus-classic.engine` stays 0.1.0), the sample rate
conversion, latency, the note timing rules, the window's size.

Checked for 0.2.0: see `out/torus-classic-0.2.0/logs/` after staging; the
build matrix (VS 2026 Release and Debug, VS 2022 Release), Steinberg's
validator, all CTests and the AddressSanitizer run were repeated on this
state. New or changed tests: `TestOutputStage` (adapter), the firmware
comparison with the two mappings, `TestKeyboardMidiOut`, the version 1
conversion in `TestState`, the new parameters in `TestController`. Window and
DAW behaviour of the additions are for the user to check; no window tests
were added for them.

## 0.1.1 (2026-11-14)

Fixes for the four findings of the Codex review of 0.1.0; see
[docs/CODEX_REVIEW_0.1.0.md](docs/CODEX_REVIEW_0.1.0.md) and
[docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md](docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md).

* A loaded state reaches the engine whole, never mixed with a state loaded at
  the same time (TC-R01).
* A knob tells the host of the begin and end of an edit once, not twice
  (TC-R02).
* With full event queues nothing acts before its time any more; notes and
  each control have queues of their own (TC-R03).
* A knob still held when the window closes ends its edit (TC-R04).

## 0.1.0 (2026-11-14)

First version: the dry Rings/Torus engine of the Daisy firmware *Physical
Modeling* as a VST3 instrument. See
[docs/BUILD_REPORT_0.1.0.md](docs/BUILD_REPORT_0.1.0.md).
