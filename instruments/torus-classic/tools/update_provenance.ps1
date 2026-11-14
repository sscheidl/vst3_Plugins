# Writes components.json and SHA256SUMS.upstream for Torus Classic: where every
# copied file comes from, and the hashes that tie the copies to their origin.
#
# Run it again after taking over a newer state of the Daisy sources or after
# changing a vendored file, with the neighbouring Daisy-Seed checkout present:
#
#   pwsh instruments/torus-classic/tools/update_provenance.ps1 -DaisySeed ..\Daisy-Seed
#
# The build does not need this script or the Daisy-Seed checkout. The CTest
# "torus-classic.provenance" verifies the files this script wrote.
#
# Hashes are SHA-256 of the file content with CRLF replaced by LF: what Git
# stores, whatever the line endings of a checkout.
param(
    [string] $DaisySeed = (Join-Path $PSScriptRoot '..\..\..\..\Daisy-Seed')
)
$ErrorActionPreference = 'Stop'
$plugin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$daisy = (Resolve-Path $DaisySeed).Path
$originRoot = 'instruments/physical-modeling'

function Get-NormalizedHash([string] $path) {
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $text = $latin1.GetString([IO.File]::ReadAllBytes($path)).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($sha.ComputeHash($latin1.GetBytes($text))) -replace '-', '').ToLower()
}

# local path (below instruments/torus-classic) <- origin path (below
# instruments/physical-modeling), and how the two relate:
#   verbatim  identical content
#   patched   the origin with the changes listed in UPSTREAM.md
#   adapted   rewritten from the origin for this product, see UPSTREAM.md
#   derived   a new file that takes its algorithm from the origin
$core = @(
    'resources.h', 'resources.cpp', 'dsp/dsp.h', 'dsp/fm_voice.h', 'dsp/fm_voice.cpp',
    'dsp/follower.h', 'dsp/limiter.h', 'dsp/note_filter.h', 'dsp/optimization.h', 'dsp/part.h',
    'dsp/part.cpp', 'dsp/patch.h', 'dsp/performance_state.h', 'dsp/plucker.h', 'dsp/resonator.h',
    'dsp/resonator.cpp', 'dsp/string.h', 'dsp/string.cpp')
$stmlib = @(
    'stmlib.h', 'LICENSE', 'dsp/cosine_oscillator.h', 'dsp/delay_line.h', 'dsp/dsp.h',
    'dsp/filter.h', 'dsp/parameter_interpolator.h', 'dsp/units.h', 'dsp/units.cc')
$map = @()
foreach ($f in $core) { $map += , @("core/torus/$f", "torus/$f", $null) }
foreach ($f in $stmlib) { $map += , @("core/stmlib/$f", "third_party/stmlib/$f", $null) }
$map += , @('core/torus/dsp/rng.h', 'third_party/stmlib/utils/random.h', 'derived')
$map += , @('engine/config.h', 'src/config.h', 'adapted')
$map += , @('engine/parameter_manager.h', 'src/parameter_manager.h', 'adapted')
$map += , @('engine/audio_output.h', 'src/audio_output.h', 'adapted')
$map += , @('engine/synth.h', 'src/physical_modeling.h', 'adapted')
$map += , @('engine/synth.cpp', 'src/physical_modeling.cpp', 'adapted')
$map += , @('source/spsc_queue.h', 'src/spsc_queue.h', 'patched')
# The pinned reference of the tests keeps the origin's layout and content.
$reference = @('src/physical_modeling.h', 'src/physical_modeling.cpp', 'src/parameter_manager.h',
    'src/audio_output.h', 'src/config.h', 'src/dsp_memory.h', 'torus/UPSTREAM.md',
    'torus/dsp/profile.h', 'torus/dsp/fx/fx_engine.h', 'torus/dsp/fx/reverb.h',
    'third_party/stmlib/utils/random.h', 'third_party/stmlib/utils/random.cc')
$reference += $core | ForEach-Object { "torus/$_" }
$reference += $stmlib | ForEach-Object { "third_party/stmlib/$_" }
foreach ($f in ($reference | Sort-Object)) { $map += , @("tests/reference/$f", $f, 'verbatim') }

$files = @()
foreach ($entry in $map) {
    $local, $origin, $relation = $entry
    $originPath = Join-Path $daisy "$originRoot/$origin"
    $originHash = Get-NormalizedHash $originPath
    $localHash = Get-NormalizedHash (Join-Path $plugin $local)
    if (-not $relation) { $relation = if ($originHash -eq $localHash) { 'verbatim' } else { 'patched' } }
    if ($relation -eq 'verbatim' -and $originHash -ne $localHash) {
        throw "$local is listed as verbatim but differs from its origin"
    }
    $blob = (& git -C $daisy rev-parse "HEAD:$originRoot/$origin" 2>$null)
    $files += [ordered]@{
        local = $local; origin = "$originRoot/$origin"; relation = $relation
        origin_sha256 = $originHash; origin_git_blob = $blob; local_sha256 = $localHash
    }
}

$daisyCommit = (& git -C $daisy rev-parse HEAD)
$daisyPathCommit = (& git -C $daisy log -1 --format=%H -- $originRoot)
$daisyDirty = [bool](& git -C $daisy status --porcelain -- $originRoot)
$sdkCommit = (& git -C (Join-Path $plugin '..\..\external\vst3sdk') rev-parse HEAD 2>$null)
$version = ([regex]'FULL_VERSION_STR "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'source/version.h'))).Groups[1].Value

$manifest = [ordered]@{
    schema = 1
    product = [ordered]@{
        id = 'torus-classic'; name = 'Torus Classic'; version = $version; vendor = 'TAUREON'
        format = 'VST3'; platform = 'Windows x64'; license = 'MIT'
    }
    components = @(
        [ordered]@{
            id = 'torus-classic.core'; path = 'core/'; layer = 1
            version = 'torus@259ed82 stmlib@5dc8b89 via daisy-seed@' + $daisyCommit.Substring(0, 7)
            description = 'Hardware-free Torus DSP (six Part models) and the stmlib parts it uses; vendored, patched for the desktop'
            license = 'MIT (Emilie Gillet; DaisyExamples contributors; TAUREON)'
        },
        [ordered]@{
            id = 'torus-classic.engine'; path = 'engine/'; layer = 2; version = '0.1.0'
            description = 'Dry four-voice instrument: note FIFO, triggers, parameter smoothing, model changes, output stage; no reverb'
            interface = 'engine/dry_engine.h'; depends_on = @('torus-classic.core')
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'torus-classic.adapter'; path = 'adapter/'; layer = 3; version = '0.1.0'
            description = 'Streaming adapter: host sample rate, 12-sample quanta, event time, latency'
            interface = 'adapter/stream_adapter.h'; depends_on = @()
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'torus-classic.shell'; path = 'source/'; layer = 4; version = $version
            description = 'VST3 processor, controller, state and editor'
            depends_on = @('torus-classic.engine', 'torus-classic.adapter', 'vst3sdk', 'vstgui')
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'torus-classic.reference'; path = 'tests/reference/'; test_only = $true
            version = 'daisy-seed@' + $daisyCommit.Substring(0, 7)
            description = 'Pinned firmware engine with its reverb, verbatim, for the dry reference comparison; not part of the plug-in'
            license = 'MIT (Emilie Gillet; DaisyExamples contributors; TAUREON)'
        }
    )
    origins = [ordered]@{
        'daisy-seed' = [ordered]@{
            repository = 'https://github.com/sscheidl/Daisy-Seed'
            commit = $daisyCommit
            path = $originRoot
            last_commit_touching_path = $daisyPathCommit
            working_tree_modified_at_copy = $daisyDirty
            product = 'Daisy Physical Modeling 0.1.1'
        }
        torus = [ordered]@{
            repository = 'https://github.com/daisyaudio/DaisyExamples'
            commit = '259ed82c7d4c0d7699f695945e6e111b7dc7cb27'; path = 'patch/Torus'
            note = 'as recorded by Daisy-Seed torus/UPSTREAM.md; taken through Daisy-Seed, with its corrections'
        }
        stmlib = [ordered]@{
            repository = 'https://github.com/pichenettes/stmlib'
            commit = '5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86'
            note = 'as recorded by Daisy-Seed torus/UPSTREAM.md; taken through Daisy-Seed'
        }
        vst3sdk = [ordered]@{
            repository = 'https://github.com/steinbergmedia/vst3sdk'
            tag = 'v3.8.0_build_66'; commit = $sdkCommit; submodule = 'external/vst3sdk'
        }
    }
    hash_method = 'SHA-256 of the file content with CRLF replaced by LF (the content Git stores)'
    files = $files
}
$json = ($manifest | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
[IO.File]::WriteAllText((Join-Path $plugin 'components.json'), $json + "`n")

$lines = @(
    "# Torus Classic: files taken from Daisy-Seed $daisyCommit ($originRoot).",
    '# SHA-256 of the content with CRLF replaced by LF. Columns: hash of the local',
    '# file, local path, relation, hash of the origin file, origin path.',
    '# Written by tools/update_provenance.ps1; checked by the test torus-classic.provenance.')
foreach ($f in $files) {
    $lines += '{0}  {1}  {2}  {3}  {4}' -f $f.local_sha256, $f.local, $f.relation, $f.origin_sha256, $f.origin
}
[IO.File]::WriteAllText((Join-Path $plugin 'SHA256SUMS.upstream'), ($lines -join "`n") + "`n")
'{0} files: {1} verbatim, {2} patched, {3} adapted, {4} derived' -f $files.Count,
    @($files | Where-Object relation -eq 'verbatim').Count,
    @($files | Where-Object relation -eq 'patched').Count,
    @($files | Where-Object relation -eq 'adapted').Count,
    @($files | Where-Object relation -eq 'derived').Count
