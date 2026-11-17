# Writes components.json and SHA256SUMS.upstream for Messier: where every copied file
# comes from, and the hashes that tie the copies to their origin.
#
# The origin is read from a commit of the neighbouring Daisy-Seed checkout with
# `git show`, not from its working tree (which may sit on another branch):
#
#   pwsh instruments/messier/tools/update_provenance.ps1 -DaisySeed ..\Daisy-Seed
#
# The build does not need this script or the Daisy-Seed checkout. The CTest
# "messier.provenance" verifies the files this script wrote.
#
# Hashes are SHA-256 of the file content with CRLF replaced by LF: what Git stores,
# whatever the line endings of a checkout.
param(
    [string] $DaisySeed = (Join-Path $PSScriptRoot '..\..\..\..\Daisy-Seed'),
    [string] $Commit = '8226a31cf48a25236970dc55b96b4046b24ddffa'
)
$ErrorActionPreference = 'Stop'
$plugin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$daisy = (Resolve-Path $DaisySeed).Path
$originRoot = 'instruments/granular'
$latin1 = [Text.Encoding]::GetEncoding(28591)

function Get-Hash([byte[]] $bytes) {
    $text = $latin1.GetString($bytes).Replace("`r`n", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($sha.ComputeHash($latin1.GetBytes($text))) -replace '-', '').ToLower()
}

function Get-OriginBytes([string] $path) {
    $temp = [IO.Path]::GetTempFileName()
    try {
        # Through cmd so that the bytes reach the file unchanged.
        & cmd /c "git -C `"$daisy`" show $($Commit):$path > `"$temp`""
        if ($LASTEXITCODE -ne 0) { throw "git show $($Commit):$path failed" }
        return [IO.File]::ReadAllBytes($temp)
    } finally {
        Remove-Item $temp -ErrorAction SilentlyContinue
    }
}

# local path (below instruments/messier) <- origin path (below instruments/granular),
# and how the two relate:
#   verbatim  identical content
#   patched   the origin with the changes listed in UPSTREAM.md
$core = @('reference_dsp.h', 'reference_dsp.cpp', 'pcm.h', 'engine.h', 'engine.cpp',
    'control.h', 'control.cpp', 'fft.h', 'profile.h', 'wav.h')
$map = @()
foreach ($f in $core) { $map += , @("core/$f", "src/$f", $null) }
foreach ($f in $core) {
    $relation = if ($f -eq 'reference_dsp.h') { 'patched' } else { 'verbatim' }
    $map += , @("tests/reference/$f", "src/$f", $relation)
}
$map += , @('tests/core/test_vocoder.cpp', 'tests/test_vocoder.cpp', 'verbatim')
foreach ($f in @('Qu-Bit-MIT.txt', 'De-Pattern-MIT.txt', 'Csound-LGPL-2.1.txt',
        'Csound-FGENS-NOTICE.txt', 'Project-MIT.txt')) {
    $map += , @("licenses/$f", "LICENSES/$f", 'verbatim')
}

$files = @()
foreach ($entry in $map) {
    $local, $origin, $relation = $entry
    $originHash = Get-Hash (Get-OriginBytes "$originRoot/$origin")
    $localHash = Get-Hash ([IO.File]::ReadAllBytes((Join-Path $plugin $local)))
    if (-not $relation) { $relation = if ($originHash -eq $localHash) { 'verbatim' } else { 'patched' } }
    if ($relation -eq 'verbatim' -and $originHash -ne $localHash) {
        throw "$local is listed as verbatim but differs from its origin"
    }
    $blob = (& git -C $daisy rev-parse "$($Commit):$originRoot/$origin")
    $files += [ordered]@{
        local = $local; origin = "$originRoot/$origin"; relation = $relation
        origin_sha256 = $originHash; origin_git_blob = $blob; local_sha256 = $localHash
    }
}

$daisyPathCommit = (& git -C $daisy log -1 --format=%H $Commit -- $originRoot)
# The submodule's gitlink, which is right also in a worktree without the submodule.
$sdkCommit = ((& git -C (Join-Path $plugin '..\..') ls-tree HEAD external/vst3sdk) -split '\s+')[2]
$version = ([regex]'FULL_VERSION_STR "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'source/version.h'))).Groups[1].Value
$engineVersion = ([regex]'kEngineVersion = "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'engine/looper_engine.h'))).Groups[1].Value
$adapterVersion = ([regex]'kAdapterVersion = "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'adapter/stream_adapter.h'))).Groups[1].Value
$short = $Commit.Substring(0, 7)

$manifest = [ordered]@{
    schema = 1
    product = [ordered]@{
        id = 'messier'; name = 'Messier'; version = $version; vendor = 'TAUREON'
        format = 'VST3'; platform = 'Windows x64'
        license = 'MIT, except the vocoder in core/reference_dsp.* (LGPL-2.1-or-later)'
    }
    components = @(
        [ordered]@{
            id = 'messier.core'; path = 'core/'; layer = 1
            version = "nebulae-on-daisy 0.2.2 via daisy-seed@$short"
            description = 'Hardware-free firmware DSP: phase vocoder (Mincer), grains, windows, limiter, control laws, engine; vendored with the F42 grain level and two patches without effect on the sound; fft_desktop.cpp is this repository''s FFT backend for it'
            license = 'LGPL-2.1-or-later (reference_dsp.*, from Csound pvlock.c and fgens.c); MIT (Qu-Bit Electronix, De-Pattern, Daisy-Seed contributors, TAUREON) for the rest'
        },
        [ordered]@{
            id = 'messier.engine'; path = 'engine/'; layer = 2; version = $engineVersion
            description = 'Looper engine: drives neb::Engine through its public interface; file hand-over by the FileBridge; Volume'
            interface = 'engine/looper_engine.h'; depends_on = @('messier.core')
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'messier.adapter'; path = 'adapter/'; layer = 3; version = $adapterVersion
            description = 'Streaming adapter: host sample rate, 64-sample quanta, event time, latency; from Kekule 0.3.1'
            interface = 'adapter/stream_adapter.h'; depends_on = @()
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'messier.shell'; path = 'source/'; layer = 4; version = $version
            description = 'VST3 processor, controller, state, editor, WAV reader and file loader'
            depends_on = @('messier.engine', 'messier.adapter', 'vst3sdk', 'vstgui')
            license = 'MIT (TAUREON)'
        },
        [ordered]@{
            id = 'messier.reference'; path = 'tests/reference/'; test_only = $true
            version = "daisy-seed@$short with the F42 line only"
            description = 'The pinned firmware core for the bit-exact reference comparison; differs from its origin only by the F42 line; not part of the plug-in'
            license = 'LGPL-2.1-or-later (reference_dsp.*); MIT for the rest'
        }
    )
    origins = [ordered]@{
        'daisy-seed' = [ordered]@{
            repository = 'https://github.com/sscheidl/Daisy-Seed'
            commit = $Commit
            path = $originRoot
            last_commit_touching_path = $daisyPathCommit
            read_with = 'git show <commit>:<path> (the local checkout was on another branch)'
            product = 'Nebulae on Daisy 0.2.2'
        }
        csound = [ordered]@{
            repository = 'https://github.com/csound/csound'
            commit = 'efbcabb4d56e6237e2cabc9949cfadadda946cf9'
            path = 'Opcodes/pvlock.c, Engine/fgens.c'
            note = 'as recorded by Daisy-Seed instruments/granular/LICENSES/README.md; taken through Daisy-Seed'
        }
        'de-pattern' = [ordered]@{
            repository = 'https://github.com/De-Pattern/Nebulae-Granular_Looper_Pedal'
            commit = '073a1ae4f4ce41bda54f8c78819f3b2a3319df4a'
            note = 'as recorded by Daisy-Seed; taken through Daisy-Seed'
        }
        'qu-bit' = [ordered]@{
            repository = 'https://github.com/Qu-Bit-Electronix/QB_Nebulae_V2'
            commit = '4be42b5159ae5c63466d84f0a8ab3b2fb4afd5a6'
            note = 'the original instrument (Csound); its control laws, as recorded by Daisy-Seed'
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
    "# Messier: files taken from Daisy-Seed $Commit ($originRoot).",
    '# SHA-256 of the content with CRLF replaced by LF. Columns: hash of the local',
    '# file, local path, relation, hash of the origin file, origin path.',
    '# Written by tools/update_provenance.ps1; checked by the test messier.provenance.')
foreach ($f in $files) {
    $lines += '{0}  {1}  {2}  {3}  {4}' -f $f.local_sha256, $f.local, $f.relation, $f.origin_sha256, $f.origin
}
[IO.File]::WriteAllText((Join-Path $plugin 'SHA256SUMS.upstream'), ($lines -join "`n") + "`n")
'{0} files: {1} verbatim, {2} patched' -f $files.Count,
    @($files | Where-Object relation -eq 'verbatim').Count,
    @($files | Where-Object relation -eq 'patched').Count
