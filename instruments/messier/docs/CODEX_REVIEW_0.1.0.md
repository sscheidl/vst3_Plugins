# Codex review: Messier 0.1.0

**VERDICT: KEINE FREIGABE for the DAW test.**

Reviewed on 2026-11-17: commit
`26b85096d83974d113b4dff3000f57cc378c085e`, branch `messier-0.1.0`, in
`.work/messier/wt`. Its direct parent is
`27a00b393daf200499954f6819725c8ac8ae0fd1`. The worktree was clean at the start.
The review covers the 83-file change, the review brief, the untracked work order
in the main checkout, and the subsequent GUI decisions in this chat.
The work-order bytes read for this review have SHA-256
`76511d9ad676ce8543439ab9ba8422b176d45bf150af4a7753650c767e3249db`.

The independent Release build, all 16 existing tests, and the validator checks
pass. Additional reproductions establish four functional defects, including
process termination on a failed loader allocation and a partial project-state
handover under backpressure. The agreed GUI concept is also absent; its final
layout and palette are explicitly still undecided.

Only this report is added to the worktree. No product source, existing test,
build order, staged result, branch or commit was changed during this review.
No editor window, DAW or listening test was run. Constructing an editor object
to call its Reset method below did not open or attach a window.

## Findings

Paths and line numbers below refer to `instruments/messier/` at the reviewed
commit, except where another path is explicitly named.

### MS-R01 [P1] A failed loader allocation terminates the host process

**Proven by fault injection, not by exhausting system memory.**

Evidence: `source/wav_reader.cpp:144`, `source/wav_reader.cpp:156`, and
`source/file_loader.cpp:142`.

Only the PCM array uses `new(std::nothrow)`. The `FileData` allocation and the
1 MiB conversion vector can still throw. `FileLoader::Run()` calls the reader
and `Finish()` without an exception boundary; an allocation exception escaping
the worker terminates the process. Other allocations in the worker and its
status/error handling have the same missing protection.

Trigger: loading an otherwise supported WAV when a reservation fails, for
example with several large sample instances or transient memory pressure.
This contradicts the promised failure behavior: keep the old file playable
and report a load error. The size limit alone does not guarantee an allocation
will succeed.

Reproduction: a separate executable linked the unmodified reader and loader.
It loaded a valid 46-byte, one-frame mono PCM16 WAV. Its replacement global
`operator new` threw `std::bad_alloc` only for the conversion-vector allocation
(1,048,615 bytes with the MSVC allocator's alignment overhead). The same
executable without injection returned a ready file and exit 0. With injection:

```text
CONTROL: ready=1 idle=1
INJECTED_ALLOCATION_FAILURE: 1048615 bytes
control_exit=0 injected_failure_exit=-1073740791
```

The failure exit is `0xC0000409`; this is evidence of process termination in
this reproduction, not a claim of a buffer overrun. No real system-wide memory
shortage was created.

**Minimal correction:** contain allocation failures at the non-audio loading
boundaries, including the worker, and turn them into a stable failure result.
Preserve the current file for a failed GUI request. Error publication itself
must not allow another allocation exception to escape. Add a fault-injection
test covering the PCM allocation, conversion buffer and status publication,
including the synchronous project-load path.

### MS-R02 [P2] Project parameters can be adopted with the previous sample

**Proven with sequential public `setState()` and `process()` calls.**

Evidence: `source/processor.cpp:124`, `source/processor.cpp:129`,
`source/processor.cpp:155`, `source/processor.cpp:362`, and
`source/processor.cpp:403`.

`setState()` publishes the file and parameters through separate handovers.
When the retire queue/backlog is full, `TakeIncoming()` deliberately returns
null. Nevertheless, `process()` consumes the loaded parameter state and
`Restart()` substitutes `engine_.NextFile()`, which is still the old sample.
There is no matching file-generation condition before adopting the parameters.
A file taken later follows the ordinary fade/swap path, after the state has
already been consumed.

Deferring a GUI file swap under backpressure is reasonable. Restarting a loaded
project with the previous file and the new parameters is a different behavior:
the resulting sound is neither complete project state. It can also put the
wrong sample at the beginning of an immediate export.

Reproduction: in a fresh processor, alternate 200 project states referring to
101-frame and 202-frame WAVs at 48 kHz. Alternate Freeze with the file, call
`setState()`, then render 64 samples. No project-load calls overlap. Both files
are tiny; the periodic retire collector is unchanged. The run produced 83
sample/parameter mismatches:

```text
load=20 expected_frames=101 actual_frames=202 freeze=0 expected_freeze=0
load=22 expected_frames=101 actual_frames=202 freeze=0 expected_freeze=0
load=24 expected_frames=101 actual_frames=202 freeze=0 expected_freeze=0
load=26 expected_frames=101 actual_frames=202 freeze=0 expected_freeze=0
PROJECT_LOAD_BACKPRESSURE: mismatches=83/200
```

The exact count depends on collector scheduling; the demonstrated defect does
not require a data race or a missing file.

**Minimal correction:** hand over a project generation containing the matching
file and parameter values, and adopt that generation as one operation. If
retirement capacity prevents adoption, retain the complete pending generation
instead of consuming its parameters alone. Keep all audio-side operations
bounded and free of locks/allocation. Test full retirement queues and rapid
project loads, including an export immediately after the last load.

### MS-R03 [P2] Reset's press can disappear before reaching the processor

**Proven with the SDK's own parameter queue and no window.**

Evidence: `source/editor.cpp:483` through the two `performEdit` calls at
lines 489 and 491; the processor requires the rising edge in
`source/processor.cpp:177`.

`Editor::PressReset()` reports 1 and 0 immediately in the same call. A host
putting these edits at the same process sample retains only the last value.
The SDK's `ParameterValueQueue::addPoint()` explicitly replaces a point at an
existing sample offset (`external/vst3sdk/public.sdk/source/vst/hosting/parameterchanges.cpp:75`).
Such a host therefore sends only 0, and the processor never sees a press.

Reproduction: connect an `IComponentHandler` that puts `performEdit` values
into the SDK queue at offset 0. Call the unopened editor's real `PressReset()`
and pass the resulting queue to `Processor::process()`. The playhead advances
instead of resetting:

```text
RESET: points=1 final_value=0.0 resets_before=0 after=0
       position_before=4096.0 after=4160.0
RESET_POSITIVE_CONTROL: resets_after_explicit_press=1
```

The positive control sends a lone 1 through the same handler and processor.
This proves a supported host path loses Reset; it does not assert that every
DAW coalesces edits identically.

**Minimal correction:** use a trigger delivery that remains observable across
host queue coalescing and works for repeated presses, while retaining the
documented automation semantics of parameter 121. Do not report the press and
release back-to-back at the same host time. Test the actual editor callback
through a coalescing handler, including two separate presses.

### MS-R04 [P2] Changing the sample does not mark the project dirty

**Proven with an `IComponentHandler2` observer.**

Evidence: `source/controller.cpp:181` and `source/controller.cpp:216`.
The existing `setDirty(true)` at `source/controller.cpp:231` is only the
window-settings callback.

A successful file choice changes the saved sound path but neither `LoadFile()`
nor the status response tells the host that a non-parameter state changed.
Thus a host relying on `IComponentHandler2::setDirty()` can keep the project
marked clean and omit the sample change from its close/save workflow.

Reproduction: connect the real controller and processor, attach a handler that
implements `IComponentHandler2`, load another valid WAV with `LoadFile()`, wait
for the worker, request status, and render the swap. The path changes and the
file is ready, but the handler sees no dirty notification. Calling the existing
controller method directly is a positive control for that same handler:

```text
FILE_LOAD: ready=1 path_changed=1 dirty_callbacks=0
DIRTY_POSITIVE_CONTROL: callbacks=1
```

**Minimal correction:** notify the host once when a user-requested file change
successfully changes the saved sound state. Distinguish that completion from
project restoration and repeated status polls; these should not continuously
dirty the project. Test successful and failed file requests with a host handler.

### MS-R05 [P2, scope] The agreed Atlas/Orion GUI foundation is not implemented

**Proven by source inspection; no visual acceptance attempted.**

Evidence: `source/editor.cpp:75` (the fixed knob table, Blend at line 82),
`source/editor.cpp:208` (construction of that knob grid),
`source/editor.h:32` (306-pixel upper section), and the committed
`docs/PLANUNG.md` Messier table declaring no waveform/position display.

The current implementation is the earlier nine-column knob/file-line editor.
It has no sample overview, grain map, playhead/spray visualization, or
corresponding display-data handover. Blend is an ordinary rotary knob beside
the other primaries.

The later work-order section 8 and this chat select an Atlas-based nebula map,
Orion as a visual theme, and a prominent broad Blend control. Those functional
elements are missing. **Exact layout, proportions, colors, typography and glow
are still open** and are not rejected for failing to match a finished design.

**Minimal correction:** record the current editor as provisional and keep this
GUI work explicitly outstanding. Implement the agreed functional foundation
after the pending layout/color work, or obtain an explicit scope change before
calling the original GUI scope complete. Update the committed planning/status
documents to reflect that distinction. This finding is separate from the four
functional defects above.

## Checks without an additional finding

### Origin, references, FFT and licensing artifacts

- Compared all 26 manifest entries directly against Git objects from Daisy-Seed
  `8226a31cf48a25236970dc55b96b4046b24ddffa`, using `git --git-dir` rather than
  its unrelated working-tree branch. All origin and local LF-normalized hashes
  agree. The four changed entries are the three documented production patches
  and the reference's copy of F42. The remaining entries match upstream.
- Inspected P1 at its call in `LooperEngine::Render()`: it is used after gate
  acknowledgment and before publication/unmute. P2 retains the bridge/info
  contract; P3 is the documented 0.90 grain amplitude. The separate reference
  driver uses the unpatched fixed-buffer file replacement mechanism.
- Both the Qu-Bit origin and the committed Messier license resolve to Git blob
  `262208a3f36b8c7a3277a8817bbb43dae440bf15`. The plugin's `.gitattributes`
  preserves the intentional whitespace exception. Notices, the five license
  texts, the LGPL headers, rebuild instructions and stage script are present.
  This checks the requested artifacts, not a legal determination of licensing.
- The shared FFT tables are immutable after C++ static initialization; engine
  construction calls initialization before processing. Work buffers, windows,
  random generator and vocoder state belong to each engine. No additional
  mutable cross-instance DSP state was found with the selected build macros.
- The independently executed firmware vocoder test checks this backend against
  its radix-2 oracle. Engine comparisons use distinct compiled core symbols
  and the separate firmware-style driver, including file replacement. Their
  matching wrapper-independent buffers make the F42/P1 comparison useful.
  Using the same desktop FFT on both wrapper sides is offset by the separate
  FFT/vocoder numerical tests; it is not evidence of bit identity to ARM CMSIS.

### Timing, state, files and audio-side ownership

- Existing tests independently passed the five engine reference scenarios,
  plugin reference comparison, two-instance checks, state parser corruption
  cases, supported WAV conversions, file swaps and allocation counters.
  Reset in those comparisons is driven by separated parameter events/direct
  actions, which does not cover MS-R03's real editor callback.
- The adapter tests cover fixed/varying host blocks, five rates, latency and
  two-minute clock drift. The reported latencies are 49/0/90/97/195 host samples
  at 44.1/48/88.2/96/192 kHz. Double buffers carry the core's float values.
- Reviewed the mono/stereo/absent sidechain arrangements, its inactive aux
  default and ignored input, common vocoder schedule, infinite tail, parameter
  IDs/defaults/CC mapping, bend, parameter laws and typed-value conversions.
- Processor/controller use the same saved-state parser: finite normalized
  values, version/length checks, and UTF-8 without embedded NUL. Missing-file
  states retain the path and supply silence in the existing tests.
- Normal process paths, engine reconstruction, pointer swaps and retirement
  were inspected for allocation/free/locks/file access. Existing process
  allocation counters pass. Backpressure retains file ownership instead of
  deleting on the audio side; the state-consistency defect is MS-R02. No
  additional double free or use-after-free was established by this review.
- Loader tickets discard overtaken results. The frame's file-dialog callback
  guards object lifetime, and status polling is present. These observations do
  not establish all thread interleavings; no thread sanitizer was available.

### Staged result and commit identity

All 78 entries of `out/messier-0.1.0/SOURCE_SHA256SUMS.txt` match the corresponding
worktree source bytes. All 44 entries of its `SHA256SUMS.txt` also match.
The staged Release bundle independently passes the validator, 47/0.

`BUILD_INFO.txt` still names parent `27a00b3` with uncommitted changes, as the
user explicitly reported. Its source manifest matches the now-committed
plugin, and this review pins the complete commit above. No source mismatch
was found; the pre-commit metadata is not treated as a new defect here. The
bundle was not restaged or edited by this review.

## Independent builds and test results

Own temporary build root: `.work/messier-review-26b8509-codex/build`.
Windows x64, Visual Studio 18 2026 Build Tools, MSVC 19.51.36256.0,
Release, static runtime; SDK from the main checkout.

```text
cmake -S .work/messier/wt -B .work/messier-review-26b8509-codex/build
  -G "Visual Studio 18 2026" -A x64
  -DVST3_SDK_ROOT="<main checkout>/external/vst3sdk"
cmake --build .work/messier-review-26b8509-codex/build
  --config Release --parallel 4
ctest --test-dir .work/messier-review-26b8509-codex/build
  -C Release -R messier --output-on-failure
ctest --test-dir .work/messier-review-26b8509-codex/build
  -C Release -E "^messier\." --output-on-failure
validator.exe <main checkout>/out/messier-0.1.0/Messier.vst3
```

| Independent check | Result |
| --- | --- |
| Full Release build | Passed; all four plugin validator runs 47 passed, 0 failed |
| Six Messier CTests | 6/6 passed, 32.91 s |
| Ten other-plugin CTests | 10/10 passed, 244.87 s |
| Validator on staged Messier | 47 passed, 0 failed |
| Direct Git/manifest comparison | 26 entries matched |
| Source/result raw-byte hash lists | 78/78 and 44/44 matched |
| Coalescing Reset host probe | Lost press; explicit-press positive control works |
| File-change dirty-handler probe | No notification; direct-call positive control works |
| Sequential project-load backpressure probe | 83/200 file/parameter mismatches |
| Allocation-failure probe | Control succeeds; injected worker allocation terminates process |

The first sandboxed MSBuild attempt stalled at its build-system check and was
stopped. The subsequent build/tests ran with approved access outside that
sandbox. No product change was needed for the build. The additional probes
were compiled against the unchanged Release libraries in this own build root;
they were not added to the product or its existing test suite.

## Missing checks and remaining limits

- No editor opening, visual inspection, real DAW/save workflow, playback or
  listening. In particular, this is not a final GUI design review.
- Debug, VS 2022 and AddressSanitizer results in the build report were read,
  not independently repeated. No UBSan/TSan run and no exhaustive concurrency
  proof. The allocation counter does not cover every allocator or DLL.
- No new hardware comparison with Nebulae/GR-1, and no ARM CMSIS numerical
  equivalence measurement. The upstream unsigned-negation test caveat remains
  documented; it is not introduced by this plugin.
- No independent CPU/deadline benchmark. The documented 192 kHz / 64-sample
  deadline risk, possible output overshoot after resampling, missing low-pass
  for high-rate source files and held-note-only bend are acknowledged scope
  decisions, not new findings.
- Permanent regression tests for MS-R01 through MS-R04 are missing from the
  current suite. The passing 16 tests do not cover those demonstrated cases.

Temporary build products, probe sources/executables and audit logs are removed
after their results are recorded here. The retained result is this report.
