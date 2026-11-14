# Where the code comes from

Torus Classic contains code copied from the Daisy Seed3 firmware *Physical
Modeling* in the neighbouring repository
[sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed). This file says
what was taken, from which state, and what was changed. The same facts are in
[`components.json`](components.json) for machines, with one line per file in
[`SHA256SUMS.upstream`](SHA256SUMS.upstream).

## Origin

| | |
| --- | --- |
| Repository | Daisy-Seed, local checkout next to this repository |
| Commit | `10f906fcd5a036fd88d20851186337e46fa38198` (2026-11-06) |
| Directory | `instruments/physical-modeling`, firmware version 0.1.1 |
| Last commit that touched it | `2c4db79f8f99ba49ef7b5d9ea55a84b566dcedae` (2026-11-01) |
| Working tree at copy time | no uncommitted change below that directory (`git status`, 2026-11-14) |

The firmware in turn vendors, according to its own `torus/UPSTREAM.md` (kept
verbatim in [`tests/reference/torus/UPSTREAM.md`](tests/reference/torus/UPSTREAM.md)):

* Torus, the Daisy Patch port of Mutable Instruments Rings, from DaisyExamples
  `259ed82c7d4c0d7699f695945e6e111b7dc7cb27`, `patch/Torus`;
* stmlib `5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86`;
* original Rings for reference only.

Nothing was downloaded from those upstreams for this plug-in. The Daisy-Seed
copy carries corrections the upstream files do not have (per-voice strikes,
FM voice fixes, the lookup table endpoint, model 6 routing); they are listed
in the file above and are all part of what was taken here.

Copyright of the DSP: Emilie Gillet and the respective upstream contributors,
MIT; the notices remain in every source file. Firmware files outside the
vendored DSP: TAUREON, MIT. License texts: [`licenses/`](licenses/).

## What was taken

| Here | From `instruments/physical-modeling/` | Relation |
| --- | --- | --- |
| `core/torus/` (18 files) | `torus/` | 9 unchanged, 9 patched, see below |
| `core/torus/dsp/rng.h` | `third_party/stmlib/utils/random.h` | new file with that generator's arithmetic |
| `core/stmlib/` (9 files) | `third_party/stmlib/` | unchanged |
| `engine/synth.{h,cpp}` | `src/physical_modeling.{h,cpp}` | adapted: reverb removed |
| `engine/parameter_manager.h` | `src/parameter_manager.h` | adapted |
| `engine/audio_output.h`, `engine/config.h` | `src/` | adapted |
| `source/spsc_queue.h` | `src/spsc_queue.h` | namespace renamed |
| `tests/reference/` (39 files) | `src/`, `torus/`, `third_party/stmlib/` | unchanged, test only |

Not taken, because the desktop plug-in has no use for them: `src/main.cpp`
and everything hardware (keys, pots, switches, LEDs, ADC, bootloader, linker
script, SDRAM placement), the USB-MIDI parser and queue handling, CPU
telemetry and the profiling hooks, Mod on Daisy and the composed FX builds,
the firmware's own host tests, and from Torus the reverb (`dsp/fx/`), the
unused onset detector and strummer.

## Changes to the vendored DSP (`core/`)

Each change is marked in the source with a comment starting "Torus Classic".
None alters an arithmetic expression of the sound; the test
`torus-classic.engine` shows the output equal to the unpatched firmware
engine, sample for sample.

| File | Change | Why |
| --- | --- | --- |
| `dsp/rng.h` (new) | Class `torus::Rng`: the linear congruential generator of `stmlib::Random`, same constants, same float conversion, state in the object. | `stmlib::Random` keeps one state for the whole process. Several plug-in instances in one host must not draw from one sequence. |
| `dsp/part.h`, `dsp/part.cpp` | `Part` owns an `Rng` (`Seed()`, `rng_state()`) and passes it to its pluckers and strings. | As above. Draws happen in the same order as before, so a `Part` seeded like the firmware's global generator produces the same noise. |
| `dsp/plucker.h`, `dsp/string.h`, `dsp/string.cpp` | `Process()` takes the `Rng&` instead of calling `Random::GetFloat()`. | As above. |
| `dsp/part.h` | `#include "dsp/fx/reverb.h"` removed. | `Part` has not used the reverb since the firmware made it global; the plug-in has none. |
| `dsp/part.h`, `dsp/part.cpp` | `model_gains_` and the chord table `chords` are `const`; `chords` is also `static`. | Shared by all instances and never written: now the compiler enforces it. |
| `resources.cpp` | `InitResources()` fills the sine table once per process under `std::call_once`. The include of the firmware's `dsp_memory.h` and its SDRAM placement macro are gone. | The firmware refills the table at every `Synth::Init`. A new plug-in instance must not write a table a running instance reads. Only the constructor of `DryEngine` calls it, never the audio thread. |
| `resources.cpp`, `resources.h` | The two empty pointer arrays `lookup_table_int16_table` and `lookup_table_uint32_table` removed; `lookup_table_table` is an array of `const` pointers. | An empty array is not valid C++ and MSVC rejects it; nothing reads the two. |
| `dsp/optimization.h`, `dsp/part.cpp`, `dsp/resonator.cpp` | Include of `dsp/profile.h` and the two `profile::Scope` timing objects removed; `profile.h` not taken. | Firmware CPU telemetry for ARM. |

The build defines `TEST` and `_USE_MATH_DEFINES` for this code, as the
firmware's own host tests do: `TEST` selects stmlib's portable C++ in place
of ARM assembly. No source change was needed for MSVC beyond the empty arrays.

Other shared state was looked for and not found: the lookup tables in
`resources.cpp` and `stmlib/dsp/units.cc` are `const`, and no function keeps
a `static` local.

## Changes to the firmware engine (`engine/`)

`engine/synth.cpp` is `Synth` from `src/physical_modeling.cpp` without the
reverb. Removed: the `torus::Reverb` instance and its 65536-byte buffer, the
reverb parameters and their setters, the reverb send, the reverb's
`Process()` call (which the firmware runs even at Dry/Wet 0) and the dry/wet
`Mix()`. Kept, statement for statement: parameter smoothing, the note FIFO
of 256 strikes, at most four triggers per 12-sample quantum, the 192-sample
fades around a model change, velocity times Excitation at the strike, the
right-channel inversion of model 5 after `Part`'s cross-mix by Position, and
`Output()`: 0.8 times the master, linear to 0.6, a knee that stays below 0.8,
non-finite samples silenced.

Other differences:

* `Init(seed)` takes the seed of the instance's noise source and also resets
  the parameters. It no longer calls `torus::InitResources()`: a reset can run
  on the audio thread, the table set-up may not. `JumpModel()` selects a model
  without the fade, for a start from a known state.
* `ParameterManager` holds eight parameters instead of fourteen: Frequency,
  Structure, Brightness, Damping, Position, Excitation, Model, Master. Decay,
  Wet, ReverbDamping, Diffusion, InputGain, the fixed Polyphony entry, the pot
  filter and the hardware switch debounce are gone. `Jump()` and `JumpBend()`
  are new. The smoothing of the remaining parameters is unchanged.
* `Process(left, right, size)` with its check for 12/24/36/48 samples became
  `Render(left, right)` for exactly one quantum; the streaming adapter does
  the rest.
* The Mod on Daisy and composed-FX `#if` branches are not there.
* `audio_output.h` refuses to compile with `/fp:fast`: its guard against
  non-finite samples needs IEEE semantics.

`engine/dry_engine.{h,cpp}` is new: the interface the layers above see.

The sixth model, `RESONATOR_MODEL_STRING_AND_REVERB`, is called **Stereo
Strings** in the plug-in. It is the dry synthesis branch of the firmware's
"String & Reverb": the string damping curve, the dispersion, the stereo
cross-mix and the inverted right channel are all kept; only the reverb the
name referred to is gone.

## The pinned reference (`tests/reference/`)

The firmware engine as copied, reverb included, compiled into the test
programs only. The tests compare the port with it at Dry/Wet exactly 0. To
live in one program with the port, its namespaces `torus`, `stmlib` and
`physical_modeling` are renamed by compiler definitions
(`torus_classic_reference` in `CMakeLists.txt`); the files themselves are
byte-identical to their origin apart from line endings. One exception at
build time: MSVC rejects the two empty arrays in `torus/resources.cpp`, so
CMake compiles a copy of that file with exactly those two lines removed. The
pinned file in the repository stays as it is.

## Checking and updating

`ctest -R torus-classic.provenance` hashes every file listed in
`components.json` and compares: unchanged files must equal their origin's
hash, changed files the hash recorded for them. Hashes are SHA-256 of the
content with CRLF replaced by LF, which is what Git stores.

After taking over a newer firmware state or changing a vendored file, run
`tools/update_provenance.ps1` with the Daisy-Seed checkout present, review
the differences, and update this file. The build itself needs neither the
script nor the Daisy-Seed checkout.
