# Builds the drag-and-drop release zip: dist\<name>-<version>.zip
# Zip layout mirrors the game folder, so the user extracts it straight into "Warcraft II Remastered":
#   x86\version.dll, x86\gameplay_options.toml, x86\gameplay_options_readme.txt, x86\gameplay_options_tutorial.txt
param([string] $Name = 'War2R-Gameplay-Options')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$cmakeLists = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(war2r_gameplay_options VERSION ([0-9.]+)') { throw 'version not found in CMakeLists.txt' }
$version = $Matches[1]
if ($cmakeLists -match 'set\(MOD_PRERELEASE "([^"]*)"\)') { $version += $Matches[1] }

& (Join-Path $root 'build.ps1') | Out-Null
$selftest = Join-Path $root 'build\Release\selftest.exe'
$gameExe = 'C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe'
if (Test-Path $gameExe) {
    & $selftest $gameExe | Out-Null
    if ($LASTEXITCODE) { throw 'selftest failed: not packaging a broken build' }
} else { Write-Warning 'game exe not found, selftest skipped' }

$stage = Join-Path $root "dist\stage-$version"
if (Test-Path $stage) { Rename-Item $stage "stage-$version.bak.$(Get-Date -Format yyyyMMddHHmmss)" }
$x86 = New-Item -ItemType Directory -Force (Join-Path $stage 'x86')
Copy-Item (Join-Path $root 'build\Release\version.dll') $x86
Copy-Item (Join-Path $root 'config\gameplay_options.default.toml') (Join-Path $x86 'gameplay_options.toml')
Copy-Item (Join-Path $root 'docs\USER_README.txt') (Join-Path $x86 'gameplay_options_readme.txt')
Copy-Item (Join-Path $root 'docs\CONFIG_TUTORIAL.md') (Join-Path $x86 'gameplay_options_tutorial.txt')

$zip = Join-Path $root "dist\$Name-$version.zip"
if (Test-Path $zip) { Rename-Item $zip "$Name-$version.zip.bak.$(Get-Date -Format yyyyMMddHHmmss)" }
Compress-Archive -Path (Join-Path $stage 'x86') -DestinationPath $zip
Get-Item $zip | Select-Object FullName, Length
"sha256 $((Get-FileHash $zip -Algorithm SHA256).Hash)"
