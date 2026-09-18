# Loads build\Release\version.dll inside a 32-bit PowerShell and calls a forwarded export through it.
# Proves the proxy resolves the real system version.dll and stays inert in a host that is not the game.
param([string] $Dll = (Join-Path $PSScriptRoot '..\build\Release\version.dll'))
$Dll = (Resolve-Path $Dll).Path
$ps32 = "$env:WINDIR\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
$script = @"
`$sig = @'
[DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryW(string p);
[DllImport("kernel32", CharSet=CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr h, string n);
[UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet=CharSet.Unicode)] public delegate uint SizeFn(string file, out uint handle);
'@
`$k = Add-Type -MemberDefinition `$sig -Name K -Namespace T -PassThru | Where-Object Name -eq 'K'
`$h = `$k::LoadLibraryW('$Dll')
if (`$h -eq [IntPtr]::Zero) { 'LOAD FAILED'; exit 1 }
`$p = `$k::GetProcAddress(`$h, 'GetFileVersionInfoSizeW')
`$fn = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(`$p, [T.K+SizeFn])
`$dummy = 0
`$size = `$fn.Invoke("`$env:WINDIR\SysWOW64\kernel32.dll", [ref]`$dummy)
"proxy loaded at 0x{0:X}, GetFileVersionInfoSizeW(kernel32.dll) = {1}" -f `$h.ToInt64(), `$size
if (`$size -gt 0) { 'PROXY OK' } else { 'PROXY FAILED'; exit 1 }
"@
& $ps32 -NoProfile -Command $script
exit $LASTEXITCODE
