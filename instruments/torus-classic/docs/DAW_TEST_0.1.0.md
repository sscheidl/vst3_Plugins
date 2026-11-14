# DAW test list: Torus Classic 0.1.0

The automatic tests and Steinberg's validator cannot answer these questions:
they need the three hosts and ears.

## Results so far

**2026-11-14, version 0.1.0, Studio One** (reported by the user; the host is
identified from a picture of its performance monitor, its version and the
sample rate were not stated):

* The plug-in loads and is displayed correctly (1.1).
* Screen keys and knobs work (1.10, 2.1); MIDI works, the pitch wheel
  included (1.2, 2.6).
* After saving, the plug-in loads again with the saved settings (3.1; window
  size and keyboard were not mentioned).
* The CPU load is negligible: the host's monitor shows 02 for the instrument.
* The monitor shows a plug-in latency of 1.1 ms, which is the 49 samples the
  plug-in reports at 44.1 kHz (5.3, in part).

Not reported yet: each of the six models by itself, automation (2.4, 2.5),
the CCs (2.7), several instances, export, resizing and display scales, and
everything in Waveform and Live. Version 0.1.0 told the host of every knob
edit twice (fixed in 0.1.1); recording automation by moving a knob (2.4) is
worth trying again with a later build.

Hosts: **Studio One 7**, **Tracktion Waveform 14**, **Ableton Live 12 Lite**.
Note per item and host: ok, or what happened. A crash or a hang ends the test
for that host; note the last step.

## New in 0.2.0, to try

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 8.1 | Turn Damping up | The sound gets shorter; fully down it sustains | | | |
| 8.2 | Turn Tune with FREE off, then with FREE on | Whole semitones with a whole-number display; continuous with decimals | | | |
| 8.3 | Close the FILTER knob while notes ring, then open it fully | The sound gets darker; fully open reads "off" and sounds as without filter | | | |
| 8.4 | Play the loudest settings you found (hard chords, FM, high Brightness) | The host's meter never passes −6 dBFS; no clipping. Note whether the limiter is audible | | | |
| 8.5 | Route the plug-in's "MIDI Out" to another track or record it; click screen keys | Notes arrive with the length the key was held | | | |
| 8.6 | Open a project saved with 0.1.0 | Sounds as before (apart from the limiter); Damping knob shows the mirrored value | | | |
| 8.7 | Record automation by moving a knob (2.4 again) | One clean automation pass; 0.1.0 reported every knob edit twice | | | |
| 8.8 | 0.2.1: play with FILTER half closed, open it fully, wait in silence, close it again | No short burst of old sound when closing it again | | | |

## Before starting

1. The folder `Torus Classic.vst3` from `out/torus-classic-0.1.0/` belongs in
   a VST3 folder. On the development machine it was copied to
   `%LOCALAPPDATA%\Programs\Common\VST3\` on 2026-11-14, next to Toy Piano
   Classic. Elsewhere that folder or `C:\Program Files\Common Files\VST3\`
   will do.
2. Compare its files with `SHA256SUMS.txt` in that folder if in doubt about
   the copy.
3. Let the host rescan. Expected entry: **Torus Classic**, vendor TAUREON,
   instrument.

## 1. Loading and playing

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 1.1 | Scan, then put the plug-in on an instrument track | Listed as instrument; window opens; no error | | | |
| 1.2 | Play single notes from a MIDI keyboard, model 1 | A struck, ringing tone; louder with higher velocity | | | |
| 1.3 | Release keys | The tone rings on and decays; releasing changes nothing | | | |
| 1.4 | Each of the six models: single notes, a four-note chord, a fast run | Six clearly different sounds; no dropouts, clicks or stuck noise | | | |
| 1.5 | Five and more notes in succession | The fifth replaces the oldest; never more than four tones | | | |
| 1.6 | Model 6 (Stereo Strings), turn Position | The stereo image changes; on headphones the right channel is out of phase at 0.5 | | | |
| 1.7 | Lowest and highest keys of the keyboard | Sound, no noise bursts | | | |
| 1.8 | Damping fully up, strike, wait | Long sustain; with strings and FM it does not end by itself | | | |
| 1.9 | Change the model while notes ring | A short fade, then the new model; no click | | | |
| 1.10 | Screen keyboard: click and drag across keys | Notes sound; keys light while held | | | |

## 2. Parameters, automation, MIDI controllers

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 2.1 | Turn each of the seven knobs while notes ring | Audible, smooth change; value text follows | | | |
| 2.2 | Host's generic parameter list | Nine parameters: seven knobs, Model (list of six names), Pitch Bend | | | |
| 2.3 | Type values into the host's parameter fields | `-7.5` for Tune, a model name, `50 %` for Volume are accepted; nonsense is refused | | | |
| 2.4 | Record automation by moving a knob, play back | The knob moves; the sound follows | | | |
| 2.5 | Draw a Model automation step and a Tune ramp | Model switches at the step; Tune glides | | | |
| 2.6 | Pitch wheel | ±2 semitones on ringing notes; back to pitch when released | | | |
| 2.7 | CC 20 to 25, 28, 33 from a controller | Tune, Structure, Brightness, Damping, Position, Excitation, Model, Volume move | | | |
| 2.8 | Notes on MIDI channels other than 1 | Sound as on channel 1 | | | |

Hosts decide whether pitch wheel and CCs reach a VST3 plug-in. If 2.6 or 2.7
does nothing, note whether the host offers a MIDI-learn or mapping page.

## 3. Saving and loading

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 3.1 | Change all knobs and the model, resize the window, hide the keyboard; save, close, reopen the project | All eight values, window size and keyboard are back | | | |
| 3.2 | Save a host preset, change values, load the preset | Values return; ringing notes stop at the moment of loading | | | |
| 3.3 | Duplicate the track | The copy has the same values | | | |
| 3.4 | Undo and redo a knob movement | Value and knob follow | | | |

## 4. Several instances

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 4.1 | Three instances with different models, played together | Each keeps its own sound and settings | | | |
| 4.2 | Remove one instance while the others play | The others continue undisturbed | | | |
| 4.3 | Eight instances, four-note chords on all | Note the CPU load the host shows, and the audio buffer size | | | |

## 5. Transport, sample rates, export

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 5.1 | Start, stop, loop, jump while notes ring | No crash; note whether tails are cut at stop (the host's choice) | | | |
| 5.2 | A recorded MIDI clip with tight chords, at 48 kHz | Timing sounds right; no flams beyond a strummed feel on dense chords | | | |
| 5.3 | Switch the audio device to 44.1 kHz and to 96 kHz | Same pitch and character as at 48 kHz; the host shows a plug-in delay near 1 ms | | | |
| 5.4 | Buffer sizes 64, 256 and 1024 | No difference in sound; no dropouts | | | |
| 5.5 | Export the clip offline twice | The two files are identical (compare in the host or by checksum) | | | |
| 5.6 | Export, then compare with real-time playback by ear | Same notes, same sound | | | |
| 5.7 | Freeze or bounce the track, if the host has it | Result as played | | | |

## 6. Window and display

| # | Step | Expected | S1 | Waveform | Live |
| --- | --- | --- | --- | --- | --- |
| 6.1 | Open, close, reopen the window several times, also while playing | Always opens with current values; no crash | | | |
| 6.2 | Drag an edge and a corner of the window | Scales between 50 % and 200 %, keeps its shape; note if the host does not allow resizing | | | |
| 6.3 | KEYS off and on | The window shrinks and grows; if the host refuses, the keyboard stays as it was | | | |
| 6.4 | Windows display scale 100 %, 125 %, 150 % (and a second monitor with another scale, if there is one) | Sharp, complete, nothing cut off | | | |
| 6.5 | Ctrl+click and mouse wheel on knobs | Reset to default; small steps | | | |

## 7. Listening

Compare with the Daisy kit running *Physical Modeling* with Dry/Wet fully
dry, same model and knob positions, if the kit is at hand.

| # | Question | Notes |
| --- | --- | --- |
| 7.1 | Do the six models sound like the kit's, dry? | |
| 7.2 | Is the level sensible next to other instruments? | |
| 7.3 | Any clicks, zipper noise or aliasing when turning knobs or bending? | |
| 7.4 | Is the missing reverb acceptable with a DAW reverb behind it? | |

## Reporting

For anything unexpected: host and version, sample rate, buffer size, the step
number, and what was seen or heard. A saved project that shows the problem
helps most.
