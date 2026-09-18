# Installs (or disables) the autocast proxy DLL in the Warcraft II Remastered x86 folder. Never launches the game.
param(
    [string] $GameDir = 'C:\Program Files (x86)\Warcraft II Remastered\x86',
    [switch] $Disable   # renames the installed version.dll to version.dll.disabled instead of installing
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path (Join-Path $GameDir 'Warcraft II.exe'))) { throw "Warcraft II.exe not found in $GameDir" }
if (Get-Process -Name 'Warcraft II' -ErrorAction SilentlyContinue) { throw 'Warcraft II is running: close it first.' }

$target = Join-Path $GameDir 'version.dll'
if ($Disable) {
    if (Test-Path $target) {
        Move-Item $target "$target.disabled" -Force
        'autocast disabled (version.dll -> version.dll.disabled)'
    } else { 'nothing installed' }
    return
}

$built = Join-Path $PSScriptRoot 'build\Release\version.dll'
if (-not (Test-Path $built)) { throw "build first: $built is missing" }
Copy-Item $built $target -Force
$hash = (Get-FileHash $target -Algorithm SHA256).Hash
if ($hash -ne (Get-FileHash $built -Algorithm SHA256).Hash) { throw 'deployed file does not match the build' }

# Pre-TOML builds used autocast.ini; it is no longer read.
$legacyIni = Join-Path $GameDir 'autocast.ini'
if (Test-Path $legacyIni) {
    Move-Item $legacyIni "$legacyIni.bak" -Force
    'legacy autocast.ini renamed to autocast.ini.bak (settings now live in autocast.toml)'
}

"installed $target"
"sha256 $hash"
"config  $(Join-Path $GameDir 'autocast.toml') (created on first game start if missing)"
"log     $(Join-Path $GameDir 'autocast.log')"
