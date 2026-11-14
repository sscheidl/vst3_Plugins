# Where the code comes from

Mendelejew contains the DSP of the Mutable Instruments Elements module, written
by Emilie Gillet and published under the MIT license. This file says what was
taken, from which state, and what was changed.
[`SHA256SUMS.upstream`](SHA256SUMS.upstream) has one line per file; the test
`elements.provenance` checks it.

## Origin

| | Repository | Commit |
| --- | --- | --- |
| Elements | [pichenettes/eurorack](https://github.com/pichenettes/eurorack), directory `elements/` | `f4cfa7697095a28d292bff758beb51deb49b5a86` (2026-10-12) |
| stmlib | [pichenettes/stmlib](https://github.com/pichenettes/stmlib) | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (2023-05-30), the commit eurorack pins as its submodule |

Both were cloned for this plug-in on 2026-11-14.

**License.** The repository's README says "Code (STM32F projects): MIT
license"; Elements is one of those, and every source file taken carries the
MIT notice with "Copyright 2014 Emilie Gillet" (stmlib: 2012 to 2014). The
notices remain in the files. `resources.cc`, which holds the lookup tables and
the sampled strikes and noise, carries the same notice. Texts:
[`licenses/`](licenses/).

**Name.** The same README says: "Mutable Instruments is a registered
trademark. The name 'Mutable Instruments' should not be used on any of the
derivative works you create from these files. We do not recommend you to keep
the original name of the Mutable Instruments module for your derivative
works." The plug-in carries neither: it is called Mendelejew. "Elements"
remains only the internal name of this project, for its directory, branch,
build targets and namespaces, and the name of the upstream code it says where
it comes from (see [README.md](README.md)).

## What was taken

| Here | From | Relation |
| --- | --- | --- |
| `core/elements/dsp/` (17 files) | `eurorack:elements/dsp/` | 2 unchanged (`multistage_envelope.h`, `patch.h`), 15 patched, see below |
| `core/elements/resources.{h,cc}` | `eurorack:elements/` | unchanged |
| `core/elements/dsp/rng.h` | `stmlib:utils/random.h` | new file with that generator's arithmetic |
| `core/elements/dsp/sample_rate.{h,cc}`, `matched_low_pass.h` | none | new, written for this plug-in |
| `core/stmlib/` (8 files) | `stmlib:` | unchanged |
| `tests/reference/eurorack/` (34 files) | `eurorack:elements/`, `stmlib:` | unchanged, test only |

Not taken: everything hardware (`drivers/`, `cv_scaler`, `ui`, `meter`,
`elements.cc`, the bootloader, the hardware design), the resource generator
scripts, and from the DSP the two string models (`string.{h,cc}`) and the
"ominous voice" (`ominous_voice.{h,cc}`) that later firmware versions hide
behind button combinations. They exist only in the test reference, which is
the complete original DSP.

What the hardware's control reader does to the pots (smoothing, limits) is
re-implemented in `engine/engine.cpp` from `cv_scaler.cc`; no code was copied
for it.

## The other ports that were looked at

Read as technical references only; no line was taken from any of them.

| Port | License of its own code | How it handles the sample rate |
| --- | --- | --- |
| [VCVRack/AudibleInstruments](https://github.com/VCVRack/AudibleInstruments) (`src/Elements.cpp`) | GPL-3.0-or-later | The unchanged DSP at 32 kHz in blocks of 16; Speex sample rate converters to and from the host rate |
| [thorinside/nt_elements](https://github.com/thorinside/nt_elements) (disting NT) | MIT | Redefines `kSampleRate` as the device's rate, but its lookup tables are still generated for 32 kHz and block size, delays and per-sample constants are unchanged: read from the source, at 48 kHz it would play 7 semitones sharp with every time scaled by 2/3. Not measured |
| MAGDA "Materia" ([Conceptual-Machines/magda-core](https://github.com/Conceptual-Machines/magda-core)) | GPL-3.0-or-later | The DSP at 32 kHz in blocks of 16 (a fork with MSVC fixes and a complete `MultistageEnvelope::Init`); a four-point interpolator to the host rate |

None of them runs the DSP at a true 48 kHz. Two things were learned from
them and done here independently: the part's memory has to be zeroed before
`Init()` because the original leaves members uninitialized (VCV, MAGDA), and
the envelope's unused segments have to be initialized (MAGDA).

## Changes to the vendored DSP (`core/`)

Every changed file says so below its license notice. The rule for all of
them: **at 32 kHz the arithmetic is the original's.** The test
`test_elements_dsp` holds the port to that: 48 scenarios of two seconds each,
random patches and gate patterns, 6.1 million samples, compared with the
unchanged code bit for bit.

### The sample rate at run time

The original is written for 32 kHz and blocks of 16 samples: in the constant
`kSampleRate`, in the contents of tables, in buffer sizes, and in many
constants that mean "per sample". `SampleRate` (`sample_rate.h`) holds the
rate, 32000, 48000 or 96000 Hz, and converts numbers given for 32 kHz.
`Part::Init()` takes the rate and the size of the reverb buffer.

| What | In the original | Here |
| --- | --- | --- |
| Block size, control rate | 16 samples: 2 kHz. The envelope table, the mode update schedule and every per-block smoothing count blocks | `block_size()`: 16, 24 or 48 samples. Always 0.5 ms, so everything that counts blocks is unchanged |
| Pitch | `lut_midi_to_f_*`: frequency over 32 kHz | Unchanged; the frequency is passed on as "over 32 kHz" and each unit converts |
| Resonator modes (`resonator.cc`) | `set_f_q<FREQUENCY_FAST>(f, 1 + f * q)` with `f` over the sample rate. The tangent approximation leaves modes above about 9 kHz flat (34 cents at 10.7 kHz, more above); decay and response to a pulse depend on `sin(angle)` of the mode | Each mode gets the angle per second, the decay, the response to a pulse and the gain at resonance it has at 32 kHz: `g = tan(atan(g32) / ratio)`, resonance times `k`, output times `1 / k`, `k` being the ratio of the two sines per second. The clamp at 0.49 stays in terms of 32 kHz: the same number of modes |
| Bowed modes | Delay of `1 / f` samples, folded below 1024; band-pass resonance `1 + f * 1500` | The same delay in time, to the nearest sample; the resonance with the same `k`. `kMaxDelayLineSize` is 4096 (room for 96 kHz) |
| Stereo modulation | `0.5f / kSampleRate` in the patch | The rate in use |
| Tube (`tube.cc`) | Delay `1 / f` samples, at most 2048; reflection adds the wave to itself one sample earlier; output low-pass coefficient `f * (...)` | Delay and its limit in time; the reflection one 32 kHz sample earlier (1.5 samples at 48 kHz, interpolated; 3 at 96 kHz); the low-pass with the same time constant |
| Exciter low-pass (`exciter.cc`) | State variable filter with damping 2 and coefficients from `lut_approx_svf_g/h`: two one-pole sections with a zero at 16 kHz | `MatchedLowPass`: per section the zero at 16 kHz as a pair of zeros, and a pole and one further zero fitted so that the response in dB is closest to the original's from 50 Hz to 14.5 kHz, weighted by the original's gain. Computed for every cutoff table entry in `SampleRate::Init()` (about 40 ms, once per rate); within 1.3 dB of the original at 48 kHz and 0.3 dB at 96 kHz wherever the original is above -40 dB |
| Pulse heights | `lut_approx_svf_gain` | Times the rate ratio: the same area in seconds |
| Sample playback | Phase increments per sample; the strikes are 48 kHz recordings played 7 semitones up, the noise a 32 kHz recording | Increments divided by the rate ratio: the same speed. At 48 kHz the strikes run at almost one sample per sample |
| Granular restart, bow flips | Probability per sample | Divided by the rate ratio |
| White noise (bow) | One random value per sample | Times the square root of the rate ratio: the same density per Hz |
| Plectrum | Delay `4096 p² + 64` samples; decays 0.997 and 0.9 per sample | The delay in time; the decays per second |
| Particles | `0.15 * kSampleRate` | The rate in use |
| Diffuser (`fx/diffuser.h`) | All-pass lengths 126, 180, 269, 444 in a buffer of 1024 | Lengths times the rate ratio, rounded; buffer of 4096 |
| Reverb (`fx/reverb.h`) | Delay lengths, modulation offsets and depths in samples; LFOs `0.5 / 32000` and `0.3 / 32000`; low-pass coefficient per sample; buffer of 32768 | All times the rate ratio (lengths rounded); LFOs by the rate in use; the low-pass with the same time constant; buffer of 32768 up to 48 kHz, 65536 at 96 kHz. Still stored in 16 bits |
| `fx/fx_engine.h` | Buffer size and delay lengths are template arguments | Set at run time (`Init`, `Allocate`); the arithmetic of `Context` is unchanged |
| Level meters | Coefficients per sample | The same time constants |

### Other changes

* **Random generator** (`rng.h`, `exciter.*`, `part.*`): the original uses
  `stmlib::Random`, one state for the whole program. Here each `Part` owns a
  generator with the same arithmetic and the same start value. Two plug-in
  instances then neither share a sequence nor write to one variable from two
  threads.
* **Uninitialized members**: the original relies on zeroed memory for
  `Part::panic_`, `Resonator::clock_divider_` and `lfo_phase_`,
  `Exciter::phase_` and `particle_range_`, the tube (never initialized at
  all), the reverb's states, and the envelope's unused segments. They are now
  set in the `Init()` functions, to the values zeroed memory gives.
  `Part::Panic()`, declared but not defined in the original, is defined.
* **Not part of this copy**: `Part` and `Voice` without the string models,
  the chord table, the DC blocker that only they use, and the "ominous voice"
  (see above).
* **`resonator.cc`** no longer includes `elements/drivers/debug_pin.h`.
* **`resources.cc`** is unchanged in the repository. It defines one empty
  array, which MSVC rejects; the build compiles a generated copy without that
  definition (`CMakeLists.txt`).

## Updating

To move to a newer upstream state: copy the files over `tests/reference/`,
re-apply the patches above to `core/`, run
`python tools/update_provenance.py <eurorack> <stmlib>` and the tests. The
bit-exact comparison at 32 kHz says at once whether a patch changed the
sound.
