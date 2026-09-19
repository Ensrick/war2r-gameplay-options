// A stand-in for the game: a 32-bit exe that is NAMED "Warcraft II.exe" and imports version.dll the way the game does.
// With the proxy next to it, DllMain must take the game path: open gameplay_options.log, say it was loaded, then go
// inert because this exe's PE timestamp is not the game's. test\activation_test.ps1 runs it and reads the log.
#include <windows.h>
#include <cstdio>

int wmain() {
    DWORD handle = 0;
    wchar_t sysdir[MAX_PATH];
    GetSystemDirectoryW(sysdir, MAX_PATH);
    wchar_t target[MAX_PATH];
    swprintf_s(target, L"%s\\kernel32.dll", sysdir);
    const DWORD size = GetFileVersionInfoSizeW(target, &handle);  // forwarded through the proxy

    wchar_t loaded[MAX_PATH] = L"?";
    GetModuleFileNameW(GetModuleHandleW(L"version.dll"), loaded, MAX_PATH);
    wprintf(L"version.dll in use: %s\nGetFileVersionInfoSizeW = %lu\n", loaded, size);
    return size ? 0 : 1;
}
