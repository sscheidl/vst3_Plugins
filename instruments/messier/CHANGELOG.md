# Changes

## 0.2.9 (2026-11-20)

The findings of the first full audit (`docs/FABLE_AUDIT_0.2.8.md`, five Fable reviewers);
the sound of a playing file is unchanged. Details in `docs/BUILD_REPORT_0.2.9.md`.

* C1, C2: LOAD's file dialog and the size and preset menus start after the click, from
  the main queue. The host can close the window while they are open without a button
  being on the call stack, and the window no longer keeps the mouse after them.
* F1: a project refused for lack of memory is reported as a failed request for its file.
  The 0.2.8 status check no longer refuses every status after it.
* F2: a file loaded while STOP or Bypass rests the output is installed at once, unheard.
  The window shows its waveform, and the old file is not heard fading out on resume.
* F3: a change in one window reaches the other windows of the instance; a view the host
  keeps while closed opens with the controller's settings.
* E3: a failed window load no longer stays on the file line after a project load.
* E5, C10: a key held by LATCH is let go when the keyboard is hidden. After a project load
  or a new connection the keyboard shows no key held. A window opens unlatched.
* C3: the menus no longer send the host an edit of a parameter that does not exist.
* C5: a hidden knob loses the keyboard focus. C6: Ctrl+click on HUE or WAVE restores the
  preset's value, as the double-click does.
* C7: a custom PNG is decoded once in full before it is taken. C9: a colour table with a
  byte-order mark keeps its first line. C8: `negative` is documented as without effect.
* C11: the catalogue number shows its own tooltip.
* F6: a file read from the window gives up when a newer request or project overtakes it,
  or when the plug-in is removed; removing it no longer waits for the whole read.
* A3: the processor is no longer declared distributable (the messages assume one process).
* Engine 0.1.7: `LooperEngine::FinishSwapSilently`.

## 0.2.8 (2026-11-20)

The five findings of Codex' short review of 0.2.7 (`docs/CODEX_REVIEW_0.2.7.md`); the
sound is unchanged. Details in `docs/BUILD_REPORT_0.2.8.md`.

* MS-R58: a file status is taken only with the texts its states need (a file for a
  sound, the file asked for while loading or after a failure) and with a known state. The
  processor sends no status when the host does not take one of its texts.
* MS-R59: a status serial outside the 32-bit range is refused, not cut.
* MS-R60: a skin PNG is read up to 32 MiB from the open file; its signature and IHDR size
  are checked against the exact @1x/@2x size before the same bytes are decoded from memory.
* MS-R61: a colour table is read up to 64 KiB from the open file; a larger one is refused.
* MS-R62: a window state with a zoom outside 50 to 200 % is refused whole, like any other
  field out of range (the zoom was clamped before).

## 0.2.7 (2026-11-19)

Window and messages; the sound and the processor's state are unchanged. Details in
`docs/BUILD_REPORT_0.2.7.md`.

* The file name beside LOAD follows every load in Studio One. Its messages keep no empty
  binary attribute, so the file status's empty texts (the detail, before any load all
  four) were missing and the controller refused every status; the window kept the name
  from the project. An empty text is no longer sent, and a missing one reads as empty.
* The file name is set upright (Bahnschrift 18) like the rest of the header, spelt as it
  is; a name too long for its place ends in "..", and the tooltip has the whole path.
* Custom skins in every build (the user's wish, 2026-11-19; until now a test build's
  switch, `MESSIER_SKIN_FOLDER`, which is gone). The window takes bitmaps and colour tables
  from `Documents\Messier-Skin` first, then from `Messier-Skin` beside the bundle, file by
  file. A missing, broken or wrongly sized file falls back to the built-in one. The palette
  menu has **Reload skin folders** and **Open skin folder**; the latter writes
  `Messier skin files.txt` with every name and size. The README lists the files.
* Taken over from Codex' changes for the skin test (2026-11-19): the chassis is drawn at
  the height of the fully open window in every section state and cut off below, so
  the artwork stays aligned with the controls; Plum's WAVE default is 200 % (controller state version 7, 0 meaning the
  preset's default); the palette menu reads Slate, Silver, Gold, Plum.

## 0.2.6 (2026-11-19)

The user's look at 0.2.5; window only. Details in `docs/BUILD_REPORT_0.2.6.md`.

* HUE turns only the hue: control, value and ink colours keep their own lightness and
  saturation (HSL), so the preset's brightness and contrast stay.
* TRAIL is gone: high values drew long streaks. The grains' paths glow a fixed 0.45 s.
* WAVE takes TRAIL's place, a slider like HUE: the waveform's brightness from 25 to 200 %
  (100 by default, double-click), inside and outside the loop alike.
* The controller's state is version 6: WAVE in percent in the place of version 5's trail;
  version 5 is still read (its trail ignored).

## 0.2.5 (2026-11-19)

Window only; the sound, the parameters and the processor's state are unchanged. Details
in `docs/BUILD_REPORT_0.2.5.md`.

* HUE and TRAIL (the user's worktree, kept as it was): a spectrum slider for the accent
  colour per preset and a knob for the grains' fade time (150 to 900 ms, 450 by
  default), both under DETAILS; the controller's state is version 5.
* Plum and Gold take the places and stored IDs of Copper and Photo plate: 32 new bitmaps
  and the package's four palettes; Gold's glass gets faint lines across.
* MS-R55: each grain carries the number of its start (engine 0.1.6, the display 536
  bytes); the window matches grains by it.
* MS-R56: the START thumb takes the core's clamp to the last frame.
* MS-R57: a grain predicted to have ended keeps fading when the display comes to rest.

## 0.2.4 (2026-11-19)

Window only; the sound, the parameters and the processor's state are unchanged. Details
in `docs/BUILD_REPORT_0.2.4.md`.

* The GUI standard of 2026-11-19 (the accepted graphite design): one layout for all four
  GUI presets. A compact header (brand, LOAD and the file, KEYS, STOP, size, preset),
  the primary knobs in three groups with FREEZE and RESET beside them, DETAILS for the
  five secondary knobs (closed for a new instance), LATCH and the octave keys in the
  keyboard row. The window is 880 x 560, 84 more with the keyboard, 96 more with DETAILS.
* All four presets delivered (72 bitmaps at 1x and 2x, their colour tables); Slate is the
  graphite standard and stays the default.
* The controller's state is version 3: DETAILS as one byte at the end; versions 1 and 2
  are still read (DETAILS closed).
* START: the thumb and the loop brackets come from one mapping, so the thumb stands on the
  left bracket.
* MS-R53: ended grains are found on the audio's own time (the rendered quanta between
  two displays); each grain of the new display continues at most one of the display
  before.
* MS-R54: a grain predicted to have ended fades out on the wall clock, also when no
  display comes.

## 0.2.3 (2026-11-19)

Window only; the sound is unchanged. Details in `docs/BUILD_REPORT_0.2.3.md`.

* MS-R48 (P2, Codex' review of 0.2.2): the window's first display request no longer runs
  while the SDK is still opening the window.
* MS-R49 to MS-R52 (Codex' probe checks and review of 0.2.2): an ended grain keeps its glow
  although a running grain has the same pan, speed and window rate; a late waveform
  overview no longer restarts the playhead's prediction and the drift; without new
  displays the nebula's drift comes to rest after 0.5 s; the nebula's tiles no longer
  jump when they swell after a long drift; the display reader refuses a grain window
  longer than 1000 s.
* Brief point 13: bright grain heads get four fine diffraction rays (across and down)
  that fade outwards and with the head, fainter when many grains sound. PITCH tints
  halo, rays and tails: warm at 0 octaves, towards a muted rose down to -3, towards a
  light blue-violet up to +2, at most 55 % of the way; the head's core stays ivory.

## 0.2.2 (2026-11-19)

Codex' check of 0.2.1 (`docs/CODEX_REVIEW_0.2.1.md`) and the user's design round for
Slate; details in `docs/BUILD_REPORT_0.2.2.md`. The sound is unchanged.

* Light satin metal for the knob caps and the START thumb (the small caps a step
  darker), dark knob pointers; key faces in dark satin metal, pressed in while on (a
  shadow under the upper edge, a lit lower edge). Interim bitmaps drawn by
  `tools/make_metal_bitmaps.py` at 1x and 2x.
* Blue-violet for the controls: knob arcs, START rail, WINDOW curve, values, the struck
  key, the active keys' texts, the small strokes in header and file line. Orange stays
  for the position markers only: loop brackets, the loop in the legend, the position
  triangle.
* SPRAY is a large knob at the head of the upper row; LATCH and KEYS sit under it, as
  FREEZE and RESET under Volume. LATCH stays in place without the keyboard, dimmed. The
  header groups STOP with the two symbol keys.
* The octave keys read "- OCT" and "OCT +", as large as the other keys, and dim at the
  keyboard's limits.
* The position triangle has a lane of its own below the axis labels (taken from the
  plate's height; the window keeps its size).
* Grains in front: warm-white heads with a soft warm halo; their path glows on for
  0.3 s, also after the grain ended; a new file clears it.
* The nebula drifts with the effective signed tempo (forward right, backward left,
  eased, at most about 22 units a second), at rest at 0x, STOP and Freeze.
* The waveform is a little brighter between the loop brackets (40 % instead of 30 %);
  the playhead has a dark edge, so it reads over bright waveform.
* No screws; the controls' texts in Bahnschrift's regular weight, spaced wider; the
  places for the audio input's knobs stay empty until 0.3.0.
* MS-R44: a parameter change no longer asks the processor from inside the controller's
  call (later, on VSTGUI's main queue, for that window only); the controller calls its
  windows from a held list, only while registered; several windows ask no more often
  than one.
* MS-R45, MS-R43: the display carries the core's own playhead and grain speeds; the
  window no longer measures them from two displays (no wrong direction on short loops or
  files, nothing kept after a pause).
* MS-R46: the loop follows START and SIZE for 0.3 s after a change in the window, then the
  loop the core reports (also automation the processor alone gets).
* MS-R47: a waveform bitmap that cannot be made is tried again only after 2 s or for
  another key; the three parts of the waveform are drawn once each.
* MS-R40, MS-R41 (rest): a menu is taken out when it is a child of the frame; the file
  info's colour is set on its own.

## 0.2.1 (2026-11-18)

From the user's first look at 0.2.0 in Studio One and Codex' check of 0.2.0
(`docs/CODEX_REVIEW_0.2.0.md`); details in `docs/BUILD_REPORT_0.2.1.md`. The sound is
unchanged.

* The values under the knobs follow the knob at once. Before, they waited for the
  window's timer, which Studio One holds back while a knob is dragged.
* The plate stays fluid: between two displays playhead and grains move on at their
  measured speeds, the plate is drawn about 40 times a second, and edits from the window
  or the host keep it going when the timer is held back.
* The loop follows START and SIZE at once (drawn from the parameters, not from the next
  display).
* Trial: the waveform lies on the glass, behind the grains, in a cool tint; the plate is
  taller for it (the strip below is gone).
* The nebula moves visibly but slowly (periods of 45 to 90 s).
* RESET sets every knob and START to its default and ends Freeze, then sends the
  playhead back to START as before. Parameter 121 (automation) is unchanged.
* A click on the snowflake ends Freeze.
* The controls' texts (knob names, buttons, scale, legend) are in a sans serif
  (Bahnschrift); brand, version and file name stay serif.
* MS-R40: a menu that cannot be shown no longer stays in the window (a crash when closing).
* MS-R41: a preset change is complete or not at all; a project's window size and keyboard
  are taken even if its look cannot be made.
* MS-R42: the size and preset menus tick the current entry.
* MS-R43: a grain gets a tail only after two moves that agree; ambiguous or implausible
  matches start anew.

## 0.2.0 (2026-11-18)

The window of the window study, version 3, with the user's decisions of 2026-11-18;
details in `docs/BUILD_REPORT_0.2.0.md`. The sound is unchanged.

* New window, 880 x 640 units (724 with the keyboard): old metal, a measuring plate of
  glass in a bezel with a seconds axis and a grid of crosses, the waveform and the wide
  START slider below it on the same time axis. The grains are shooting stars: the head
  flares with the grain's window and burns out, the tail follows the grain's measured
  motion through the file. The nebula drifts and fades towards Granular. While Freeze
  holds, the playhead is dashed and carries a snowflake.
* START is the slider under the plate (no knob any more); BLEND is a knob. The knob
  columns follow the study: Window before Blend; the places of input gain and grain FM
  are marked "(0.3.0)".
* GUI presets: the palette symbol in the header; saved with the song (controller state
  version 2; songs of 0.1.x open with Slate). Slate is built from Codex' bitmaps at 1x and
  2x; Silver, Copper and Photo plate are listed, disabled until their bitmaps come.
* Window size menu (50 to 200 %) in the header, beside the grip of 0.1.11.
* STOP sits left of KEYS; LATCH beside the keyboard, which keeps its 15 keys.
* The waveform overview carries the file's sample rate, for the seconds axis.
* The input features planned for 0.2.0 (live, overdub, circular, dry blend, input gain,
  grain FM) move to 0.3.0.

## 0.1.11 (2026-11-18)

The two edge cases of Codex' check of 0.1.10 (`docs/CODEX_REVIEW_0.1.10.md`), and a
resize grip; details in `docs/BUILD_REPORT_0.1.11.md`. The sound is unchanged.

* New: a hatched grip in the window's lower right corner; dragging it resizes the
  window, keeping its aspect ratio, as a corner drag of the window does.
* If the system cannot open the LOAD... dialog, LOAD... works again at once (MS-R39);
  before, it stayed blocked until the window was opened anew.
* The engine's note queue holds twice every note that can be held at once (adapter
  0.1.2), so letting go of very many held notes during a pause cannot end a note still
  held (MS-R38).

## 0.1.10 (2026-11-18)

The four findings of Codex' checks of 0.1.8 and 0.1.9 (`docs/CODEX_REVIEW_0.1.8.md`,
`docs/CODEX_REVIEW_0.1.9.md`), each fixed as a class; details in
`docs/BUILD_REPORT_0.1.10.md`. The sound while playing is unchanged.

* Stop and Bypass act on their own sample (MS-R35). The output no longer depends on how
  the host divides its blocks, and an on and off within one block is heard. Notes and
  parameter points are put on the engine's own time, so one that comes while the engine
  rests acts when it goes on.
* While resting, a screen key is dropped like a host note, and no MIDI goes out for it
  (MS-R36). A release of a note nobody holds is not passed on, so a busy MIDI track
  during a long pause cannot fill the engine's note queue.
* The screen keyboard and host MIDI on channel 1 share a note: it ends when the last of
  them lets go, also when a reconnect lets go of the screen's keys (MS-R34).
* Every call of the host, VSTGUI or the system into the window's code is an exception
  boundary: drawing, mouse, wheel, drag and drop, timer, file dialog, sizes (MS-R37).

## 0.1.9 (2026-11-18)

From the user's DAW test of 0.1.8; details in `docs/BUILD_REPORT_0.1.9.md`. The sound is
unchanged while playing.

* The file line follows a drop: after a load from the window, the open window asks for
  the status until that load has finished (and every half second anyway). The LOAD...
  dialog opens in the folder of the file playing.
* A drag over the window is judged by the WAV's headers while it hovers: a file Messier
  cannot play is refused at once, with the "not allowed" pointer and the reason on the
  map.
* New: STOP in the header (parameter 131, not saved), a pause with a 5 ms fade that goes
  on where it stopped; and Bypass (parameter 130, the VST3 bypass flag, saved: state
  version 2, version 1 still read), which Studio One's bypass now switches.

## 0.1.8 (2026-11-18)

The four findings of Codex' check of 0.1.7 (`docs/CODEX_REVIEW_0.1.7.md`), and drag and
drop of WAV files; details in `docs/BUILD_REPORT_0.1.8.md`. The sound is unchanged.

* New: a WAV file dragged from the Explorer onto the map or the file line loads as with
  LOAD... (the first .wav of a drop; other files are refused). While a WAV is over the
  window, the map shows "Drop WAV to load".
* New: the map takes the mouse: a click or drag sets START to that place in the file,
  the wheel widens or narrows SPRAY (finer with Shift). The map now also takes drops
  (it took no mouse before, so VSTGUI would not have offered it the drop).
* New: LATCH next to KEYS: a key struck on the screen keyboard stays held until another
  key, the same key again or LATCH off.
* From the user's DAW test of 0.1.7: Speed has 0x exactly at 12 o'clock (-4x .. +4x;
  +1x, the default, at 7/12), Pitch Random reaches +-12 semitones and shows semitones,
  and the WINDOW knob shows the mixed grain window as a small curve instead of text (the
  host shows the nearest shape's name). The core is unchanged: the plug-in gives it the
  pot position that belongs to the value. Songs saved with 0.1.7 or earlier keep their
  pot values, so Speed and Pitch Random sound different there.
* MS-R30, MS-R31: every request of the controller carries the moment it was made. The
  processor refuses requests made before it was last disconnected, and loads made
  before its last project load; on a disconnect it releases the screen keys the old
  connection still holds, so no late release is needed or taken.
* MS-R32: the controller keeps the count of files of the last eight processors, so a
  return to a processor seen before marks only what it loaded since.
* MS-R33: the offsets on parameter 199 are spaced 4099 apart for the processors of one
  process; equal values of two processors need thousands of loads in between.

## 0.1.7 (2026-11-18)

The four findings of Codex' check of 0.1.6 (`docs/CODEX_REVIEW_0.1.6.md`), fixed as a
class rather than case by case; details in `docs/BUILD_REPORT_0.1.7.md`. The sound is
unchanged.

* MS-R26, MS-R27: marking the project as changed is simpler and can no longer be hidden.
  Every change of parameter 199 marks it, and so does every status with more files than
  the last of the same processor; nothing is subtracted. A file may be marked twice,
  once on each way, which changes nothing for a host. Each processor adds its own offset
  to parameter 199, so a late value of an old processor rarely equals a new one's.
* MS-R28: connection generations grow across all controllers of the process; the
  processor refuses requests of an older connection (a key release excepted) before
  they change anything, and stamps each reply with the generation of its request.
* MS-R29: the WAV reader gives the caller's stream back its exception mask in every
  case.

## 0.1.6 (2026-11-18)

The six findings of Codex' check of 0.1.5 (`docs/CODEX_REVIEW_0.1.5.md`), fixed; details
in `docs/BUILD_REPORT_0.1.6.md`. The sound is unchanged.

* MS-R20: after a connect, before the first status, every change of parameter 199 marks
  the project; the status then adds only what is missing, for a new processor as for a
  reconnect to the same one. One `setDirty` per file, at most 64 for one report: the
  contract is at least one for every change.
* MS-R21: every message of the controller carries its connection generation, which the
  processor stamps on its replies; status and display replies of an earlier connection,
  or overtaken by newer ones, are not taken. The cached waveform must match the file's
  number and length.
* MS-R22: a RESET press waiting in the adapter survives a change of sample rate.
* MS-R23: receiving a message lets no exception out, also when the host makes the
  attribute list on first access.
* MS-R24: building the window lets no exception out: what was built is let go, and the
  host gets an error instead of an empty window.
* MS-R25: the WAV reader tells a missing file from a file-system error and a want of
  memory, also inside the stream.

## 0.1.5 (2026-11-18)

The four findings of Codex' check of 0.1.3 (`docs/CODEX_REVIEW_0.1.3.md`), fixed; details
in `docs/CODEX_REVIEW_0.1.3_UMSETZUNG.md`. The sound is unchanged.

* MS-R16: the WAV reader reports a want of memory with its own code (`kOutOfMemory`), no
  longer as "too large"; a project whose valid file cannot be decoded for want of memory
  is refused as a whole instead of being taken over silent with its path.
* MS-R17: the status names the processor it comes from; a controller connected to a new
  processor counts that processor's loads from its own zero (also from parameter 199
  alone, before any status), and lets go of the old processor's waveform. The status
  carries the full count, not only 16 bits of it.
* MS-R18: a RESET press survives a process call without samples and an activation, also
  when the adapter has already put it on a step not yet rendered.
* MS-R19: the factory's create functions, `initialize`, `setupProcessing` and the rate
  change in `setActive` let no exception out; without memory they answer `kOutOfMemory`,
  and the streaming adapter (0.1.1) stays exactly as it was.

## 0.1.4 (2026-11-18)

The data path of the window (work order section 8), before its Atlas/Orion design;
details in `docs/BUILD_REPORT_0.1.4.md`. The sound is unchanged.

* The engine reports what the nebula map shows: the file the core reads, the loop as
  the core applies START and SIZE, the playhead, the spray range and the sounding grains
  (read position, panorama, phase). Core patch P4: three read-only accessors.
* The loader makes a waveform overview (2048 columns of lowest and highest sample) from
  the buffer the engine plays, off the audio thread, tagged with the file's number; the
  window shows only the overview of the file the core reads, also through a swap.
* While a window is open it asks about 33 times a second; the audio thread publishes
  only then, without locks or allocation. Without a window nothing is drawn or
  published.
* A provisional map strip between the file line and the knobs shows it; the window is
  130 pixels taller (700 x 436, 522 with the keyboard).

## 0.1.3 (2026-11-18)

The five findings of Codex' check of 0.1.2 (`docs/CODEX_REVIEW_0.1.2.md`), fixed; details
in `docs/CODEX_REVIEW_0.1.2_UMSETZUNG.md`. The sound is unchanged.

* MS-R11: parameter texts (shown or typed in the host, also without the window) let no
  exception out.
* MS-R12: parameter 199 carries the count of files from the window (65536 steps); the
  controller marks the project changed from it directly, whether or not a status reply
  follows, and a late status reply counts nothing twice.
* MS-R13: RESET presses survive an activation within their 0.5 s. Documented: presses that
  fall into one 64-sample step of the engine that it has not yet rendered act once.
* MS-R14: a project that cannot be taken over completely for lack of memory is refused as
  a whole; the path that is saved stays that of the project still playing.
* MS-R15: opening the window cannot lose the editor object when memory runs out.

## 0.1.2 (2026-11-18)

The five findings of Codex' check of 0.1.1 (`docs/CODEX_REVIEW_0.1.1.md`), fixed; details
in `docs/CODEX_REVIEW_0.1.1_UMSETZUNG.md`. The sound is unchanged.

* MS-R06: the controller's host calls (state, parameters, messages, window) let no
  exception out; a window that cannot show a project does not make it unreadable.
* MS-R07: a project's file has its own hand-over and goes into the engine only with the
  values of the same project. Audio blocks between the two play the complete old
  project, also around an activation. Files from the window carry a sequence number; an
  older one does not come back after a project.
* MS-R08: the window's RESET button sends each press as a message to the processor, no
  longer as an edit of parameter 121: presses survive a host that merges edits and a
  window that closes. Parameter 121 stays for automation. Presses that reach one audio
  block act once; presses while no audio runs are dropped after 0.5 s.
* MS-R09: a file from the window becomes known to the controller through a hidden,
  read-only parameter (199, "Sample Changes") that the processor writes, with or without
  a window; the host gets one `setDirty` per such file.
* MS-R10: the loader's spare empty file is only touched under its lock.

## 0.1.1 (2026-11-17)

The four defects of Codex' review of 0.1.0 (`docs/CODEX_REVIEW_0.1.0.md`), fixed; details
in `docs/CODEX_REVIEW_0.1.0_UMSETZUNG.md`. The sound is unchanged.

* MS-R01: an allocation that fails while a file is read (on the loader's thread or while a
  project loads) no longer ends the host's process. A file from the window then fails with
  "out of memory" and the old file plays on; a project falls silent. Host calls that build
  strings (state, messages) answer `kOutOfMemory` instead of throwing.
* MS-R02: a loaded project's values and its file go into the engine together. When no file
  can be taken (files still waiting to be freed), the whole project waits and the old
  sound plays on, instead of the new values with the old file.
* MS-R03: the RESET button sends the press at once and the release 50 ms later from the
  window's timer, so that a host keeping one value per parameter and position still
  delivers the press. A second press while held follows after the release.
* MS-R04: a file chosen in the window tells the host that the project changed
  (`setDirty`), once per load; failed loads, status polls and loading a project do not.
* The window's timer asks for the file status (found while writing the report of 0.1.0).
* The window is provisional: the Atlas/Orion window of the build order's section 8 is
  still to come, after layout and colours are settled (MS-R05, the user's decision of
  2026-11-17).

## 0.1.0 (2026-11-17)

First version, after the build order of 2026-11-17
(`docs/CLAUDE_BUILDAUFTRAG_MESSIER_0.1.0.md`, kept by the user).

* VST3 instrument around the hardware-free core of Nebulae on Daisy 0.2.2 (Daisy-Seed
  `8226a31`): phase vocoder and granular processor on one WAV file, File source only.
* The eight pots, the six secondaries that act without an input, Freeze, Reset,
  Volume; MIDI notes with velocity and note-off, one pitch wheel for all channels,
  CC 20 to 27 and 33, host automation; screen keyboard with octave keys.
* WAV files with 16 or 24 bit PCM or 32 bit float, also EXTENSIBLE, mono or stereo,
  8 to 192 kHz, up to 256 MiB as PCM16; read on a worker thread, swapped in with the
  firmware's fade; every failure keeps the old file. Projects store the path and read
  the file while loading.
* Grain level 0.90 as the original instrument (F42), the only change to the core's
  sound; vocoder on the common schedule; 64-sample quanta.
* A sidechain input (aux, inactive), not read yet: the bus layout of 0.2.0.
* This repository's own FFT backend for the core instead of CMSIS-DSP.
