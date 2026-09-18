# Configures and builds the 32-bit version.dll proxy. Output: build\Release\version.dll
param([ValidateSet('Release', 'Debug')] [string] $Config = 'Release')
$ErrorActionPreference = 'Stop'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
$build = Join-Path $PSScriptRoot 'build'
& $cmake -S $PSScriptRoot -B $build -G 'Visual Studio 17 2022' -A Win32 | Out-Host
if ($LASTEXITCODE) { throw "cmake configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config $Config | Out-Host
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
Get-Item (Join-Path $build "$Config\version.dll") | Select-Object FullName, Length, LastWriteTime
