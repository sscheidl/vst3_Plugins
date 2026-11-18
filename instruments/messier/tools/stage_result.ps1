# Puts a built Messier bundle, the license texts that must accompany it, the build
# information that lets the LGPL part be rebuilt, and the documents for the DAW test
# into one folder, and writes SHA256SUMS.txt over everything there. Installs nothing.
#
#   pwsh instruments/messier/tools/stage_result.ps1 [-BuildDir build\windows-vs2026-x64]
#        [-Target out\messier-<version>] [-SdkRoot external\vst3sdk]
#
# Build the Release configuration first. Files already in the target folder (logs/)
# are kept and included in the hash list. After Kekule's tools/stage_result.ps1.
param(
    [string] $BuildDir = '',
    [string] $Target = '',
    [string] $SdkRoot = ''
)
$ErrorActionPreference = 'Stop'
$plugin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $plugin '..\..')).Path
$version = ([regex]'FULL_VERSION_STR "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'source\version.h'))).Groups[1].Value
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\windows-vs2026-x64' }
if (-not $Target) { $Target = Join-Path $repo "out\messier-$version" }
if (-not $SdkRoot) { $SdkRoot = Join-Path $repo 'external\vst3sdk' }
$bundle = Join-Path $BuildDir 'VST3\Release\Messier.vst3'
if (-not (Test-Path (Join-Path $bundle 'Contents\x86_64-win\Messier.vst3'))) {
    throw "No Release bundle at $bundle. Build it first."
}
New-Item -ItemType Directory -Force $Target, (Join-Path $Target 'licenses') | Out-Null
Copy-Item $bundle $Target -Recurse -Force

$licenses = @(
    @((Join-Path $repo 'LICENSE'), 'vst3_Plugins-LICENSE.txt'),
    @((Join-Path $repo 'THIRD_PARTY_NOTICES.md'), 'THIRD_PARTY_NOTICES.md'),
    @((Join-Path $plugin 'licenses\Qu-Bit-MIT.txt'), 'Qu-Bit-MIT.txt'),
    @((Join-Path $plugin 'licenses\De-Pattern-MIT.txt'), 'De-Pattern-MIT.txt'),
    @((Join-Path $plugin 'licenses\Csound-LGPL-2.1.txt'), 'Csound-LGPL-2.1.txt'),
    @((Join-Path $plugin 'licenses\Csound-FGENS-NOTICE.txt'), 'Csound-FGENS-NOTICE.txt'),
    @((Join-Path $plugin 'licenses\Project-MIT.txt'), 'Daisy-Seed-Project-MIT.txt'),
    @((Join-Path $SdkRoot 'LICENSE.txt'), 'VST3-SDK-LICENSE.txt'),
    @((Join-Path $SdkRoot 'vstgui4\LICENSE'), 'VSTGUI-LICENSE.txt'))
foreach ($entry in $licenses) {
    Copy-Item $entry[0] (Join-Path $Target "licenses\$($entry[1])") -Force
}
foreach ($document in 'README.md', 'CHANGELOG.md', 'UPSTREAM.md', 'components.json',
    'docs\DAW_TEST_0.1.0.md', 'docs\BUILD_REPORT_0.1.0.md', 'docs\CODEX_PRUEFAUFTRAG_0.1.0.md',
    'docs\CODEX_REVIEW_0.1.0.md', 'docs\CODEX_REVIEW_0.1.0_UMSETZUNG.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.1.md', 'docs\CODEX_REVIEW_0.1.1.md',
    'docs\CODEX_REVIEW_0.1.1_UMSETZUNG.md', 'docs\CODEX_PRUEFAUFTRAG_0.1.2.md',
    'docs\CODEX_REVIEW_0.1.2.md', 'docs\CODEX_REVIEW_0.1.2_UMSETZUNG.md', 'docs\CODEX_PRUEFAUFTRAG_0.1.3.md',
    'docs\BUILD_REPORT_0.1.4.md', 'docs\CODEX_REVIEW_0.1.3.md', 'docs\CODEX_REVIEW_0.1.3_UMSETZUNG.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.5.md', 'docs\CODEX_REVIEW_0.1.5.md', 'docs\BUILD_REPORT_0.1.6.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.6.md', 'docs\CODEX_REVIEW_0.1.6.md', 'docs\BUILD_REPORT_0.1.7.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.7.md', 'docs\CODEX_REVIEW_0.1.7.md', 'docs\BUILD_REPORT_0.1.8.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.8.md', 'docs\GRAFIKAUFTRAG_GUI.md', 'docs\BUILD_REPORT_0.1.9.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.9.md', 'docs\CODEX_REVIEW_0.1.8.md', 'docs\CODEX_REVIEW_0.1.9.md',
    'docs\BUILD_REPORT_0.1.10.md', 'docs\CODEX_PRUEFAUFTRAG_0.1.10.md', 'docs\CODEX_REVIEW_0.1.10.md',
    'docs\BUILD_REPORT_0.1.11.md', 'docs\BUILD_REPORT_0.2.0.md', 'docs\CODEX_PRUEFAUFTRAG_0.2.0.md') {
    $source = Join-Path $plugin $document
    if (Test-Path $source) { Copy-Item $source (Join-Path $Target (Split-Path $document -Leaf)) -Force }
}

# Which source the bundle was built from, for the LGPL part (README, License).
$head = (& git -C $repo rev-parse HEAD)
$branch = (& git -C $repo rev-parse --abbrev-ref HEAD)
$dirty = @(& git -C $repo status --porcelain -- instruments/messier CMakeLists.txt)
$sourceSums = Get-ChildItem $plugin -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($repo.Length + 1).Replace('\', '/')
        '{0} *{1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $relative
    }
[IO.File]::WriteAllText((Join-Path $Target 'SOURCE_SHA256SUMS.txt'), ($sourceSums -join "`n") + "`n")
$state = if ($dirty.Count -gt 0) {
    "with uncommitted changes in instruments/messier (the working tree; its files are listed with their hashes in SOURCE_SHA256SUMS.txt)"
} else {
    'clean'
}
$info = @(
    "Messier $version, VST3, Windows x64",
    "Built $(Get-Date -Format 'yyyy-MM-dd HH:mm') from https://github.com/sscheidl/vst3_Plugins",
    "  branch $branch, commit $head, $state",
    "  build folder $BuildDir, configuration Release",
    '',
    'The phase vocoder (core/reference_dsp.h, core/reference_dsp.cpp) is LGPL-2.1-or-later,',
    'adapted from Csound Opcodes/pvlock.c; see licenses/. To replace it: check out the source',
    'above, change those two files, then from the repository root',
    '  git submodule update --init external/vst3sdk',
    '  git -C external/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk vstgui4',
    '  cmake --preset windows-vs2026-x64',
    '  cmake --build --preset windows-vs2026-x64-release --target Messier',
    'and use build/windows-vs2026-x64/VST3/Release/Messier.vst3. Nothing in the bundle prevents',
    'the replacement.')
[IO.File]::WriteAllText((Join-Path $Target 'BUILD_INFO.txt'), ($info -join "`n") + "`n")

# A short guide for the test.
$guide = @(
    "Messier $version - VST3 instrument, Windows x64 (TAUREON)",
    '',
    'Install: copy the folder Messier.vst3 (the whole folder) into',
    '  C:\Program Files\Common Files\VST3\',
    'and let the host rescan its plug-ins. No Visual C++ runtime is needed.',
    '',
    'Try: insert Messier on an instrument track, press LOAD... and choose a WAV file',
    '(16 or 24 bit PCM or 32 bit float, mono or stereo, 8 to 192 kHz, up to 256 MiB as',
    '16 bit). The loop plays at once, without a note. Notes transpose from C4 and start',
    'from Start; releasing all notes returns to the base pitch. Blend 0 % is the phase',
    'vocoder, 100 % the grains.',
    '',
    'What to check: DAW_TEST_0.1.0.md. What was tested automatically and what is known:',
    'BUILD_REPORT_0.1.0.md. Origin and licenses: UPSTREAM.md, licenses/, BUILD_INFO.txt',
    '(the phase vocoder is LGPL-2.1-or-later). SHA256SUMS.txt lists every file here.')
[IO.File]::WriteAllText((Join-Path $Target 'README.txt'), ($guide -join "`r`n") + "`r`n")

# Hashes of the raw bytes of every file, the list itself excepted.
$sums = Join-Path $Target 'SHA256SUMS.txt'
$lines = Get-ChildItem $Target -Recurse -File -Force |
    Where-Object { $_.FullName -ne $sums } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring((Resolve-Path $Target).Path.Length + 1).Replace('\', '/')
        '{0} *{1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $relative
    }
[IO.File]::WriteAllText($sums, ($lines -join "`n") + "`n")
"Staged Messier $version from $BuildDir in $Target ($($lines.Count) files)."
