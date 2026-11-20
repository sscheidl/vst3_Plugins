# DAW test list: Messier 0.1.0 and later

What the automatic tests cannot show: how the plug-in behaves in a real host, how the
window looks and handles, and how it sounds. For Studio One 7 first; Waveform 14 and
Live 12 Lite where noted. Tick, or write what happened.

## Results so far

None yet (2026-11-17).

## Before starting

* Not before Codex has released a build for the DAW test (0.1.0 was not released;
  0.1.1 to 0.1.3, 0.1.5 to 0.1.8 and 0.1.10 fix the findings of nine reviews; 0.1.4 adds
  the window's data path and a provisional map; Codex' next check is open). The window is provisional
  (see the README); judge its handling, not its final look.
* Install: copy `Messier.vst3` from `out/messier-0.1.11/` into
  `C:\Program Files\Common Files\VST3\` (administrator rights). Studio One may need a
  rescan of its plug-ins.
* Have a few WAV files ready: a 16-bit stereo loop at 48 kHz, a 24-bit file, a 32-bit
  float file, a mono file at 44.1 kHz, one long file (several minutes), one with a
  name outside ASCII (umlauts), and one damaged file (any text file renamed to `.wav`).
* For F42 (see below) a quiet, sparse sound helps: a single sustained tone.

## 1. Loading and playing

- [ ] Messier appears as an instrument (vendor TAUREON, category Sampler) and loads.
- [ ] The window opens: title, LOAD..., "No sample", eight knobs and Volume, six small
      knobs, FREEZE, RESET, the keyboard. Closing and opening it again several times
      works (0.1.6 builds it under a new guard against failing allocations).
- [ ] LOAD... opens the Windows file dialog, filtered to WAV.
- [ ] A loaded file plays at once as a loop, without a held note; the file line shows
      its name, rate, channels, bit depth and length.
- [ ] Each of the test files loads; the 24-bit and the float file sound as the 16-bit
      one would. The file with umlauts loads and shows its name correctly.
- [ ] The damaged file: the old file plays on; the line says why the new one failed.
- [ ] Loading another file while one plays: a short fade, no click, the new file
      starts at Start.
- [ ] A very long file: the window stays responsive while it loads ("Loading ...").
- [ ] Drag and drop (0.1.8): drag a WAV from the Explorer onto the map: "Drop WAV to
      load" appears; dropping loads it as LOAD... does. The same onto the file line.
      A text file or a folder: the pointer shows "not allowed", nothing loads. Several
      files with one WAV among them: that WAV loads. Dropping onto a knob does nothing.
      After a drop the host offers to save the song, and the file line shows the new
      name (0.1.9). An unplayable WAV (8 bit, more than two channels, compressed, too
      large) is refused already while it hovers: "not allowed" pointer and the reason on
      the map (0.1.9). LOAD... opens in the folder of the file playing.

## 2. Controls, notes, MIDI

- [ ] Every knob changes the sound as its name says; the value under the knob reads
      sensibly (Speed as a factor, Pitch in octaves, Pitch Random in semitones, Density
      in grains per second). WINDOW shows a small curve of the mixed grain window; its
      tooltip names the nearest shape.
- [ ] Speed: 0x exactly at 12 o'clock (the playhead stands), -4x fully left, +4x fully
      right; +1x (the default, double-click) at about 1 o'clock, with a small snap.
      Pitch: 0 octaves at the centre, with a snap. Pitch Random up to +-12 semitones.
- [ ] Map (0.1.8): a click on the map or waveform moves START there (the loop's left
      edge jumps to the click, the START knob follows); dragging moves it along; the
      host records it as automation of Start. The mouse wheel over the map widens and
      narrows the spray band, with Shift in finer steps; the SPRAY knob follows.
- [ ] STOP (0.1.9): the sound fades out without a click; pressed again it goes on
      where it stopped, fading in. Studio One's bypass of the plug-in does the same;
      reopening a bypassed song opens it bypassed, a stopped song opens playing.
      (0.1.10) Automate STOP in a lane with a short on/off within one bar: the gap is
      heard at its place, with any buffer size. Screen keys struck while stopped do
      nothing and send no MIDI; after STOP off the pitch is that of the keys still held.
- [ ] Screen key and MIDI on channel 1 (0.1.10): hold C5 on a MIDI keyboard (channel 1),
      click C5 on the screen and let go: the note stays. Close the window while both
      hold C5: the MIDI note stays.
- [ ] LATCH: a struck screen key stays held (pitch stays changed); another key moves the
      hold, the same key or LATCH off lets it go; without LATCH as before.
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

- [ ] Resize grip (0.1.11): the hatched corner at the lower right shows the diagonal
      resize pointer; dragging it resizes the window smoothly, with and without the
      keyboard, at 100 % and at Windows display scaling of 150 %, down to 50 % and up to
      200 %. The host may refuse a size: then the window stays as it is.
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

The plate (since 0.1.4 the map strip; judge whether it shows the right things):

- [ ] With a file loaded, the waveform appears under the map; with no file, "no sample".
- [ ] START and SIZE move the shaded loop; the white playhead runs inside it and jumps
      back to its start at the end; SPEED changes its pace and direction; FREEZE stops it.
- [ ] SPRAY widens the band around the playhead; the grain dots appear within it.
- [ ] Grain dots appear while grains sound: without PAN RND at the very top and bottom
      (hard left and right), with PAN RND spread between; DENSITY changes how many.
- [ ] Loading another file: the old waveform stays until the new file is heard, then
      the new one appears; a failed load leaves the old waveform.
- [ ] The map moves smoothly enough at 100 % and 200 % zoom; the host's CPU meter does
      not rise noticeably with the window open compared with it closed.

The new window (0.2.0, Slate; judge the look as well as what it shows):

- [ ] Material: chassis, bezel, glass, knob caps, buttons, screws and keys look as the
      delivered bitmaps, at 100 % and at 150 or 200 % (2x bitmaps): no seams in the
      bezel or the buttons, no dark or light fringes round caps, thumb, keys or nebula.
- [ ] The nebula drifts slowly (minutes per cycle), violet and blue, not too much red; it
      fades as BLEND goes to Granular.
- [ ] The seconds axis above the plate fits the file's length; the grid's faint lines
      stand at its labels.
- [ ] Grains as shooting stars: a head that flares and burns out, a horizontal tail
      behind it, against the direction it reads, longer with higher PITCH (since 0.2.2
      the core's own speed, from the first appearance on).
- [ ] FREEZE: the playhead turns dashed with a snowflake on the plate.
- [ ] The START slider's thumb follows START (also from automation) and stands where
      the loop's bracket begins; a click or drag on plate, waveform or slider sets START;
      the START row shows seconds and percent.
- [ ] The size menu (window symbol) offers 50 to 200 % and ticks the current size;
      the palette menu ticks Slate, the other three are greyed "(bitmaps to come)".
- [ ] A song saved with 0.1.x opens with Slate and its saved size; a song saved with
      0.2.0 keeps preset, size, keyboard and octave.
- [ ] STOP (header) and LATCH work as before (since 0.2.2 LATCH sits under SPRAY and
      stays, dimmed, while the keyboard is hidden; the octave keys hide with it).
- [ ] Texts: brand, subtitle, knob names and values readable and not cut at 50 %, 100 %
      and 200 %; "TAUREON 0.2.2" does not touch STOP.
- [ ] CPU: the window open costs no more than with 0.1.11 (since 0.2.1 the plate is drawn
      about 40 times a second).

0.2.1 (from the first look at 0.2.0):

- [ ] The values under the knobs change while a knob is dragged.
- [ ] Dragging a knob, or START, does not freeze or stutter the plate; the loop and the
      thumb follow START at once.
- [ ] The waveform on the glass (trial): readable behind the grains, not too bright;
      keep it there or back below the plate?
- [ ] The nebula visibly moves, slowly.
- [ ] RESET: every knob and START to their defaults, FREEZE off, playback from START.
- [ ] A click on the snowflake ends Freeze.
- [ ] The sans serif of the controls reads well at 50, 100 and 200 %.
- [ ] The size and preset menus tick the current entry.

0.2.2 (Codex' check of 0.2.1 and the design round; compare with the previews in
`out/messier-0.2.2/preview/`):

- [ ] Light metal knobs and START thumb with dark pointers; blue-violet arcs, values,
      START rail and WINDOW curve; orange only on loop brackets, the legend's loop and
      the position triangle. Is the light metal right, or should it change before the
      final renderings?
- [ ] SPRAY as a large knob first in the upper row, works as before (also the wheel on
      the plate); LATCH and KEYS under it, level with FREEZE and RESET.
- [ ] Without the keyboard: LATCH dimmed and inert, KEYS brings the keyboard back.
- [ ] "- OCT" and "OCT +" beside the keyboard, as large as the other keys; each dims at
      its limit (C2 to C4 lowest, C6 to C8 highest).
- [ ] STOP, FREEZE, LATCH and KEYS look pressed in while on, also at 50 %.
- [ ] STOP and the two symbol keys stand as a group with even gaps; no screws.
- [ ] The position triangle sits below the seconds labels; "14 s" stays readable when
      the playhead stands on it.
- [ ] Grains: warm-white heads in front of nebula and waveform, their paths glowing on
      for about 0.3 s (on a long file at 1x only a few units; longer or shorter?); a new
      file clears the old paths at once.
- [ ] The nebula drifts right while playing forward, left backward, turns smoothly when
      SPEED crosses 0, rests at 0x, STOP and Freeze, and does not jerk at the loop's end.
- [ ] The waveform a little brighter inside the loop than 0.2.1, outside still dim; the
      playhead readable over bright waveform.
- [ ] Knob names in the lighter, wider spaced sans serif read well at 50, 100 and 200 %.
- [ ] Automating START or SIZE from the host: the loop brackets follow (MS-R46).
- [ ] Two windows of one instance open (if the host allows), or closing the window while
      a file loads: no crash (MS-R44).
- [ ] A short loop (SIZE low) at +1x: the playhead runs forward (MS-R45).

0.2.3:

- [ ] Bright grains show four fine rays; with many grains (high DENSITY) the plate stays
      calm and the playhead readable.
- [ ] PITCH at -3, 0 and +2 octaves and automated across 0: rays, halo and tails turn
      rose below 0 and blue-violet above, warm at 0; the heads' core stays ivory.
- [ ] Stopping the host's transport (displays stop coming): the nebula comes to rest.
- [ ] Watched for a few minutes while playing: the nebulae never jump.

0.2.4 (the GUI standard):

- [ ] Switch between Slate, Silver, Plum and Gold (0.2.5: in the places of Copper and
      Photo plate): only material and colours change, every control stays where it is.
      On Plum waveform, playhead and
      grains read well on the light glass.
- [ ] DETAILS: closed in a new instance; opening it grows the window by the row of the
      five secondary knobs, closing shrinks it; their values and automation stay. A
      saved project opens with DETAILS as it was.
- [ ] KEYS in the header shows and hides the keyboard row (LATCH, - OCT, keys, OCT +);
      the octave keys dimmed at their limits.
- [ ] START at the start, in the middle and near the end: the thumb stands exactly on
      the left loop bracket.
- [ ] Projects of 0.2.3 open with their preset, size, keyboard and transposition.

0.2.5:

- [ ] Plum: light glass with grey waveform and dark grains, the waveform a little clearer
      inside the loop. Gold: dark green glass with faint scanlines and lines across, a
      green waveform without glowing edges, inside the loop a little stronger; warm grains
      and orange markers readable.
- [ ] Projects that used Copper or Photo plate open in Plum or Gold.
- [ ] HUE (DETAILS, left): labels, knob arcs, START fill and the held key follow it live;
      a double-click brings back the preset's colour; each preset keeps its own hue in the
      project.
- [ ] (0.2.6: TRAIL is gone, see below.)
- [ ] A dense cloud of grains (DENSITY high): every grain that ends leaves its path
      (MS-R55); after STOP the fading paths fade out, no grain head comes back (MS-R57).
- [ ] START at 100 % of a very short file: the thumb on the loop bracket (MS-R56).

0.2.6 (the user's look at 0.2.5):

- [ ] HUE turns only the hue: the arcs, values, START fill and held key keep their
      brightness; near-grey labels hardly change. Double-click: the preset's colour.
- [ ] WAVE (DETAILS, right, where TRAIL was): 25 to 200 %, the waveform dimmer or
      brighter, the loop still clearer; double-click 100 %; saved with the project.
- [ ] The grains' paths glow a fixed 0.45 s, without the long streaks of high TRAIL values.
- [ ] A project saved with 0.2.5 opens (its trail is ignored, WAVE at 100 %).

0.2.7 (the file name in Studio One; Codex' skin-test changes):

- [ ] Load one WAV, then another with a different name: the name beside LOAD and the
      file information below it follow each load, with the window open throughout.
- [ ] A file that cannot be read (a renamed .txt): "Could not load ..." with the reason;
      the name stays at the file that plays.
- [ ] The name is upright, spelt and cased as on disk; a very long name ends in ".."
      before the catalogue number, the tooltip shows the whole path.
- [ ] KEYS and DETAILS in all four combinations: the chassis artwork (Plum's vine) stays
      where it is relative to the knobs.
- [ ] A new Plum instance: WAVE at 200 %; double-click on WAVE brings back the preset's
      default (Plum 200 %, the others 100 %). The palette menu reads Slate, Silver, Gold, Plum.

0.2.9 (the Fable audit; most of it only a host shows):

- [ ] LOAD..., the size menu and the preset menu still open and work. With a floating
      plug-in window: open LOAD's dialog, close the plug-in window from the host (or remove
      the track), then cancel the dialog: no crash (C1).
- [ ] After choosing in a menu or the file dialog, the next click into the host acts at
      once (C2).
- [ ] STOP on, load another file: the plate shows its waveform at once; STOP off: the
      new file starts from START, nothing of the old file is heard (F2).
- [ ] Two windows of one instance (if the host allows): a preset chosen in one shows in
      the other; the saved project has it (F3).
- [ ] Load a broken file ("Could not load ..."), then a preset or project: the line shows
      the project's file (E3).
- [ ] LATCH a key, then KEYS off: the note ends. LATCH a key, then load a preset: no key
      stays lit (E5, C10).
- [ ] Open DETAILS, click a small knob, close DETAILS, press an arrow key: nothing changes (C5).
- [ ] Ctrl+click on HUE or WAVE: the preset's colour or WAVE value, as with a double-click (C6).
- [ ] Hover over "No. n": its own tooltip (C11).

0.2.10 (the ChatGPT review of 0.2.9):

- [ ] Two windows of one instance (if the host allows): change the size in one, then
      drag one window to a small screen or make it as large as the screen allows: both
      windows settle, neither keeps jumping (GD.1).
- [ ] STOP on, load another file, STOP off: the new file fades in from START, as when it
      is loaded while playing (GC.2).
- [ ] The size and preset menus: open one, close the plug-in window from the host while
      it is open, then click elsewhere: no crash (GA.1).

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
