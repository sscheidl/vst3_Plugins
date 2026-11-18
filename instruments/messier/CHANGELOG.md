# Changes

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
