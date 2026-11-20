# Messier 1.0 beta 1

Messier is a VST3 instrument for Windows x64. It plays a WAV file as a loop through a
phase vocoder and a granular processor, with pitch and speed independent of each other.
Its sound engine is the hardware-free core of the Daisy Seed firmware *Nebulae on Daisy*
0.2.2, itself an adaptation of the default Granular Looper of Qu-Bit Electronix' Nebulae
v2.1.2. Nothing here is made, endorsed or supported by Qu-Bit Electronix.

This is a **closed beta**: free and non-profit, for testers. It comes **without
warranty**, as its licenses say.

## Made with AI

AI coding was essential to Messier. The plug-in was written largely with Claude Code
(Anthropic) and reviewed with Codex (OpenAI) and other AI reviewers (Claude and ChatGPT).
The author directed the work and tested it. Every review and its fixes are documented in
the repository (`instruments/messier/docs/`).

## Install

Copy the folder `Messier.vst3` into `C:\Program Files\Common Files\VST3\` and let the host
rescan its plug-ins. No Visual C++ runtime is needed. The host lists the version as 1.0.0;
the window shows "1.0 beta 1".

## Known limitations

* Windows x64 only. So far tested in Studio One only.
* High load at 192 kHz with very small host buffers: a block that contains a vocoder step
  can take longer than the block itself. Measured for this beta (one core of the author's
  machine): on average 3 to 4 % of a core at 192 kHz, but single blocks of 64 samples
  took 370 to 540 us of their 333 us. If you hear dropouts at 192 kHz, raise the buffer
  size.
* After a restart (activation, loading a project) the secondary controls glide up from 0
  over about 60 ms, as on the original module.
* Only the file source: live input, recording and the input controls are not there yet;
  the sidechain input exists but is not read.
* A project stores the WAV file's path, not the audio. A moved or missing file leaves the
  project silent, with its path kept.
* Files are kept as 16-bit PCM (24-bit and float files are rounded, without dither), up
  to 256 MiB, about 23 minutes of stereo at 48 kHz. Files above 48 kHz are read without a
  low-pass, so content above 24 kHz folds back.
* One pitch wheel for all MIDI channels.
* In custom skin tables, `negative`, `wave`, `wave_outside` and `loop_edge` have no
  effect yet.

## Bug chasing welcome

Please hunt for bugs and report them as GitHub issues, with the bug report form:
<https://github.com/sscheidl/vst3_Plugins/issues/new?template=bug_report.yml>

Earlier reports, to see whether yours is known: <https://github.com/sscheidl/vst3_Plugins/issues>

A good report contains:

* the host and its version;
* the sample rate and buffer size;
* the WAV file's format: bits, rate, mono or stereo, length;
* the steps that lead to the problem, and what you expected instead;
* the project file, if you can share it.

## Licenses

Messier is under the MIT license, except its phase vocoder (`core/reference_dsp.h` and
`core/reference_dsp.cpp`), which is adapted from Csound (`Opcodes/pvlock.c`, windows as
in `Engine/fgens.c`) and is under the GNU Lesser General Public License 2.1 or later. The
license texts are in the `licenses` folder that comes with the bundle.

The release folder's `BUILD_INFO.txt` names the commit of
<https://github.com/sscheidl/vst3_Plugins> the bundle was built from. With it, the
phase vocoder can be changed or replaced and the bundle rebuilt; the plug-in's README
gives the steps.

VST is a registered trademark of Steinberg Media Technologies GmbH.
