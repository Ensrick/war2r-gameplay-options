# Runs the stand-in "Warcraft II.exe" (test\activation_host.cpp, NOT the game) with a version.dll next to it and checks
# that the proxy took the game path: gameplay_options.log is created and says it was loaded.
# -Dll lets you test the copy installed in the game folder instead of the fresh build.
param([string] $Dll = (Join-Path $PSScriptRoot '..\build\Release\version.dll'))
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\build\activation'
$hostExe = Join-Path $dir 'Warcraft II.exe'
if (-not (Test-Path $hostExe)) { throw "build first: $hostExe is missing" }
Copy-Item (Resolve-Path $Dll).Path (Join-Path $dir 'version.dll') -Force
$log = Join-Path $dir 'gameplay_options.log'
if (Test-Path $log) { [IO.File]::Delete($log) }

$out = & $hostExe 2>&1
$code = $LASTEXITCODE
$out
"host exit code: $code"
if (-not (Test-Path $log)) { 'ACTIVATION FAILED: no gameplay_options.log was written'; exit 1 }
'--- gameplay_options.log'
Get-Content $log
if ((Get-Content $log -Raw) -notmatch 'loaded into Warcraft II\.exe') { 'ACTIVATION FAILED: the load line is missing'; exit 1 }
if ($code -ne 0) { 'ACTIVATION FAILED: the forwarded export did not work'; exit 1 }
'ACTIVATION OK'
