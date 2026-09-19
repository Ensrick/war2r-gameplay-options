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
# Left-overs of earlier swaps (see below); they can go once nothing has them loaded any more.
Get-ChildItem $GameDir -Filter 'version.dll.inuse.*' -ErrorAction SilentlyContinue | ForEach-Object {
    try { [IO.File]::Delete($_.FullName) } catch { }
}
try {
    Copy-Item $built $target -Force
} catch [System.IO.IOException] {
    # The map editor and the Blizzard helper exes in this folder load the proxy too (it stays inert in them).
    # Windows cannot overwrite a loaded DLL but it can rename one, so move the in-use copy aside.
    $aside = "$target.inuse.$(Get-Date -Format yyyyMMddHHmmss)"
    Move-Item $target $aside -Force
    Copy-Item $built $target -Force
    "version.dll was in use by another program from this folder; the old copy was renamed to $(Split-Path $aside -Leaf)"
}
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
