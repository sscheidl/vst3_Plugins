# Changes

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
