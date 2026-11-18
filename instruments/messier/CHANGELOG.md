# Changes

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
