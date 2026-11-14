# Puts a built Mendelejew bundle, the license texts that must accompany it and
# the documents for the DAW test into out/elements-<version>/, and writes
# SHA256SUMS.txt over everything there. Installs nothing. After the script of
# the same name in Torus Classic.
#
#   pwsh instruments/elements/tools/stage_result.ps1 [-Preset windows-vs2026-x64]
#
# Build the Release configuration of that preset first. Files already in the
# target folder (logs/) are kept and included in the hash list.
param(
    [string] $Preset = 'windows-vs2026-x64'
)
$ErrorActionPreference = 'Stop'
$plugin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $plugin '..\..')).Path
$version = ([regex]'FULL_VERSION_STR "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'source\version.h'))).Groups[1].Value
$bundle = Join-Path $repo "build\$Preset\VST3\Release\Mendelejew.vst3"
if (-not (Test-Path (Join-Path $bundle 'Contents\x86_64-win\Mendelejew.vst3'))) {
    throw "No Release bundle at $bundle. Build the preset first."
}
$target = Join-Path $repo "out\elements-$version"
New-Item -ItemType Directory -Force $target, (Join-Path $target 'licenses') | Out-Null

# The bundle, with its hidden desktop.ini and icon. A bundle staged under the
# former working title would otherwise stay beside it.
$former = Join-Path $target 'Elements.vst3'
if (Test-Path $former) { Remove-Item $former -Recurse -Force }
Copy-Item $bundle $target -Recurse -Force

$licenses = @(
    @((Join-Path $repo 'LICENSE'), 'vst3_Plugins-LICENSE.txt'),
    @((Join-Path $repo 'THIRD_PARTY_NOTICES.md'), 'THIRD_PARTY_NOTICES.md'),
    @((Join-Path $plugin 'licenses\Elements-MIT.txt'), 'Elements-MIT.txt'),
    @((Join-Path $plugin 'licenses\stmlib-LICENSE.txt'), 'stmlib-LICENSE.txt'),
    @((Join-Path $plugin 'licenses\Daisy-Seed-LICENSE.txt'), 'Daisy-Seed-LICENSE.txt'),
    @((Join-Path $repo 'external\vst3sdk\LICENSE.txt'), 'VST3-SDK-LICENSE.txt'),
    @((Join-Path $repo 'external\vst3sdk\vstgui4\LICENSE'), 'VSTGUI-LICENSE.txt'))
foreach ($entry in $licenses) {
    Copy-Item $entry[0] (Join-Path $target "licenses\$($entry[1])") -Force
}
foreach ($document in 'README.md', 'CHANGELOG.md', 'UPSTREAM.md', 'SHA256SUMS.upstream',
    'docs\BUILD_REPORT_0.1.0.md', 'docs\DSP_MEASUREMENTS_0.1.1.md', 'docs\DAW_TEST_0.1.0.md',
    'docs\CODEX_REVIEW_0.1.0.md', 'docs\CODEX_REVIEW_0.1.0_UMSETZUNG.md') {
    $source = Join-Path $plugin $document
    if (Test-Path $source) { Copy-Item $source (Join-Path $target (Split-Path $document -Leaf)) -Force }
}

# Hashes of the raw bytes of every file, the list itself excepted.
$sums = Join-Path $target 'SHA256SUMS.txt'
$lines = Get-ChildItem $target -Recurse -File -Force |
    Where-Object { $_.FullName -ne $sums } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($target.Length + 1).Replace('\', '/')
        '{0} *{1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $relative
    }
[IO.File]::WriteAllText($sums, ($lines -join "`n") + "`n")
"Staged Mendelejew $version from $Preset in $target ($($lines.Count) files)."
