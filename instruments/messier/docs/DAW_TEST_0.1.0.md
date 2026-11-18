# DAW test list: Messier 0.1.0 and later

What the automatic tests cannot show: how the plug-in behaves in a real host, how the
window looks and handles, and how it sounds. For Studio One 7 first; Waveform 14 and
Live 12 Lite where noted. Tick, or write what happened.

## Results so far

None yet (2026-11-17).

## Before starting

* Not before Codex has released a build for the DAW test (0.1.0 was not released;
  0.1.1 to 0.1.3 fix the findings of three reviews; Codex' next check is open). The window is provisional
  (see the README); judge its handling, not its final look.
* Install: copy `Messier.vst3` from `out/messier-0.1.3/` into
  `C:\Program Files\Common Files\VST3\` (administrator rights). Studio One may need a
  rescan of its plug-ins.
* Have a few WAV files ready: a 16-bit stereo loop at 48 kHz, a 24-bit file, a 32-bit
  float file, a mono file at 44.1 kHz, one long file (several minutes), one with a
  name outside ASCII (umlauts), and one damaged file (any text file renamed to `.wav`).
* For F42 (see below) a quiet, sparse sound helps: a single sustained tone.

## 1. Loading and playing

- [ ] Messier appears as an instrument (vendor TAUREON, category Sampler) and loads.
- [ ] The window opens: title, LOAD..., "No sample", eight knobs and Volume, six small
      knobs, FREEZE, RESET, the keyboard.
- [ ] LOAD... opens the Windows file dialog, filtered to WAV.
- [ ] A loaded file plays at once as a loop, without a held note; the file line shows
      its name, rate, channels, bit depth and length.
- [ ] Each of the test files loads; the 24-bit and the float file sound as the 16-bit
      one would. The file with umlauts loads and shows its name correctly.
- [ ] The damaged file: the old file plays on; the line says why the new one failed.
- [ ] Loading another file while one plays: a short fade, no click, the new file
      starts at Start.
- [ ] A very long file: the window stays responsive while it loads ("Loading ...").

## 2. Controls, notes, MIDI

- [ ] Every knob changes the sound as its name says; the value under the knob reads
      sensibly (Speed as a factor, Pitch in octaves, Density in grains per second,
      Window by name).
- [ ] Speed and Pitch snap to +1x and 0 octaves near the centre.
- [ ] Freeze: the playhead stands still, the sound goes on (spectral freeze). RESET
      sends playback back to Start, every press (since 0.1.2 the window's presses are not
      recorded as automation; an automation lane for Reset still works).
- [ ] After loading a file in the window and closing it, the host offers to save the
      song (also with the window closed before the file has finished loading).
- [ ] Double-click on a knob returns it to its default; typing a value in the host's
      parameter field works (for example "1x", "-1 oct", "Rectangle", "50 %").
- [ ] MIDI notes: C4 plays at the base pitch, other notes transpose and restart from
      Start, velocity sets the level; releasing all notes returns to the base pitch at
      full level.
- [ ] Pitch wheel: bends the held note by up to +-2 semitones; without a held note it
      does nothing (as in the firmware).
- [ ] CC 20 to 27 move the eight pots, CC 33 Volume (if the host passes them on).
- [ ] Screen keyboard: plays and releases; the octave keys move it; a recorded track
      gets the notes with their ends.
- [ ] Automation of a few knobs, Freeze and Reset is recorded and played back.

## 3. Saving and loading

- [ ] A saved song reopens with the same file, the same settings, window size,
      keyboard and octave.
- [ ] Export right after opening a song: the export starts with sound, not silence.
- [ ] Move the WAV file away, reopen the song: silence, the file line names the
      missing path; saving the song again and putting the file back restores it.
- [ ] Two songs with different files open one after the other: each has its own.

## 4. Several instances

- [ ] Two or three instances with different files play together; changing one does
      not change another.
- [ ] Duplicating a track with Messier duplicates the file reference.

## 5. Transport, sample rates, export, load

- [ ] Stop and start of the transport: the loop keeps playing (infinite tail), no
      clicks.
- [ ] At 44.1, 96 and 192 kHz the loop plays at the right pitch and speed; the host
      shows a latency of about 1 ms (0 at 48 kHz).
- [ ] At 192 kHz with a small buffer (64 samples): watch for dropouts. The build
      report says a block that contains a vocoder hop can take longer than a 64-sample
      block at that rate; a larger buffer should avoid it.
- [ ] Offline export (faster than real time) sounds like playback.
- [ ] CPU display of the host with dense settings (Density and Overlap at maximum):
      the build report measured 2 to 4 % of one core.

## 6. Window and display

- [ ] Resizing keeps the proportions, 50 to 200 %; the size is restored with the song.
- [ ] KEYS shows and hides the keyboard.
- [ ] Texts fit their places at 100 % and on a scaled display (long file names are cut
      off, the full path is the tooltip).
- [ ] The amber colour of error lines is readable.

## 7. Listening

- [ ] Against the Daisy kit with Nebulae on Daisy 0.2.2, if at hand, same file, same
      settings: Messier should sound the same except for the level of sparse grains
      (next point) and the vocoder 5.3 ms earlier (common schedule).
- [ ] F42: Blend at 100 % (grains only), Density low, a quiet file: Messier is up to
      6 dB louder than the firmware 0.2.2, as the original instrument was. With dense
      grains the difference disappears (both at the limiter).
- [ ] Dense grains with the rectangle window: does the soft limit at the output (from
      0.9) become audible?
- [ ] A 44.1 kHz file through the vocoder (Blend 0): in tune (the original was 147
      cents low there; Messier is not, on purpose).
- [ ] Displacement above about 0.2: grains drop out (F41, as in firmware and original).

## Reporting

Write the results into "Results so far" above, with host, version, buffer size and
sample rate, or tell Claude; for a defect, the steps that lead to it.
