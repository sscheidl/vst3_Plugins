# Core: the firmware sound engine, unchanged

`toy_piano.h`, `toy_piano.cpp`, `config.h` and `spsc_queue.h` are byte-identical
copies from
[sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed),
`instruments/toy-piano/src/`, taken at commit `10f906f` (branch `dcp`,
2026-11-06). MIT License, © 2026 TAUREON.

| File | Git blob in Daisy-Seed |
| --- | --- |
| `config.h` | `3e5b843f79c5b3446ad233785c559d4cdb0c6c05` |
| `toy_piano.cpp` | `96c45af43c9abd7f5b993f1937699272d9cb9d46` |
| `toy_piano.h` | `cfc7c83d28f9dae7db9dd9af8baed61e64899ad7` |
| `spsc_queue.h` | `a9bd3691f0f0c529dc5cfa1534fa61ab51b39797` |

`spsc_queue.h` is the firmware's lock-free queue for USB-MIDI notes. Since
version 0.3.0 it carries the notes of the on-screen keyboard from the UI thread
to the audio thread.

Do not edit these four files here. That they run without changes is the point
of the study; `git hash-object <file>` must keep printing the blob above.
Anything the plug-in needs beyond the firmware belongs in `../source/`.

`compat/Utility/dsp.h` replaces the DaisySP header the engine includes. It
holds the single function used, `daisysp::mtof`.
