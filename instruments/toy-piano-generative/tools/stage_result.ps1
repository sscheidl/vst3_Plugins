# Puts a built Toy Piano Generative bundle, the license texts that must
# accompany it and the documents for the DAW test into
# out/toy-piano-generative-<version>/, and writes SHA256SUMS.txt over
# everything there. Installs nothing. After the script of the same name in
# Mendelejew.
#
#   pwsh instruments/toy-piano-generative/tools/stage_result.ps1 [-Preset windows-vs2026-x64]
#
# Build the Release configuration of that preset first. Files already in the
# target folder (logs/) are kept and included in the hash list. In a Git
# worktree, which has no SDK and whose out/ nobody looks into, point -SdkRoot
# and -OutRoot at the main checkout.
param(
    [string] $Preset = 'windows-vs2026-x64',
    [string] $SdkRoot = '',
    [string] $OutRoot = ''
)
$ErrorActionPreference = 'Stop'
$plugin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $plugin '..\..')).Path
if (-not $SdkRoot) { $SdkRoot = Join-Path $repo 'external\vst3sdk' }
if (-not $OutRoot) { $OutRoot = Join-Path $repo 'out' }
$version = ([regex]'FULL_VERSION_STR "([^"]+)"').Match(
    [IO.File]::ReadAllText((Join-Path $plugin 'source\version.h'))).Groups[1].Value
$name = 'Toy Piano Generative.vst3'
$bundle = Join-Path $repo "build\$Preset\VST3\Release\$name"
if (-not (Test-Path (Join-Path $bundle "Contents\x86_64-win\$name"))) {
    throw "No Release bundle at $bundle. Build the preset first."
}
$target = Join-Path $OutRoot "toy-piano-generative-$version"
New-Item -ItemType Directory -Force $target, (Join-Path $target 'licenses') | Out-Null
$target = (Resolve-Path $target).Path

# The bundle, with its hidden desktop.ini and icon; an older copy goes first.
$staged = Join-Path $target $name
if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }
Copy-Item $bundle $target -Recurse -Force

$licenses = @(
    @((Join-Path $repo 'LICENSE'), 'vst3_Plugins-LICENSE.txt'),
    @((Join-Path $repo 'THIRD_PARTY_NOTICES.md'), 'THIRD_PARTY_NOTICES.md'),
    @((Join-Path $SdkRoot 'LICENSE.txt'), 'VST3-SDK-LICENSE.txt'),
    @((Join-Path $SdkRoot 'vstgui4\LICENSE'), 'VSTGUI-LICENSE.txt'))
foreach ($entry in $licenses) {
    Copy-Item $entry[0] (Join-Path $target "licenses\$($entry[1])") -Force
}
foreach ($document in 'README.md', 'CHANGELOG.md', 'docs\DAW_TEST_0.1.0.md',
    'docs\CODEX_PRUEFAUFTRAG_0.1.0.md', 'docs\CODEX_PRUEFAUFTRAG_0.2.0.md') {
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
"Staged Toy Piano Generative $version from $Preset in $target ($($lines.Count) files)."
