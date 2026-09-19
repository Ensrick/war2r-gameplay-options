// version.dll proxy: the game imports VERSION.dll, which is not a KnownDLL, so Windows loads this copy from the
// game folder first. We forward every export to the real system DLL and patch one call site in the game.
#include <windows.h>
#include <cstdint>

#include "mod.h"
#include "game.h"
#include "hook.h"
#include "log.h"

constexpr int kExportCount = 17;
static const char* const kExportNames[kExportCount] = {
    "GetFileVersionInfoA",     "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",     "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",  "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",     "VerFindFileA",               "VerFindFileW",              "VerInstallFileA",
    "VerInstallFileW",         "VerLanguageNameA",           "VerLanguageNameW",          "VerQueryValueA",
    "VerQueryValueW",
};

extern "C" FARPROC g_realExports[kExportCount] = {};

#define PROXY_STUB(index, name) \
    extern "C" __declspec(naked) void proxy_##name() { __asm { jmp dword ptr [g_realExports + index * 4] } }

PROXY_STUB(0, GetFileVersionInfoA)
PROXY_STUB(1, GetFileVersionInfoByHandle)
PROXY_STUB(2, GetFileVersionInfoExA)
PROXY_STUB(3, GetFileVersionInfoExW)
PROXY_STUB(4, GetFileVersionInfoSizeA)
PROXY_STUB(5, GetFileVersionInfoSizeExA)
PROXY_STUB(6, GetFileVersionInfoSizeExW)
PROXY_STUB(7, GetFileVersionInfoSizeW)
PROXY_STUB(8, GetFileVersionInfoW)
PROXY_STUB(9, VerFindFileA)
PROXY_STUB(10, VerFindFileW)
PROXY_STUB(11, VerInstallFileA)
PROXY_STUB(12, VerInstallFileW)
PROXY_STUB(13, VerLanguageNameA)
PROXY_STUB(14, VerLanguageNameW)
PROXY_STUB(15, VerQueryValueA)
PROXY_STUB(16, VerQueryValueW)

static bool LoadRealVersionDll() {
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for this 32-bit process
    if (!n || n > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\version.dll");
    const HMODULE real = LoadLibraryW(path);
    if (!real) return false;
    for (int i = 0; i < kExportCount; ++i) g_realExports[i] = GetProcAddress(real, kExportNames[i]);
    return true;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    if (!LoadRealVersionDll()) return FALSE;

    // The folder also holds BlizzardBrowser/BlizzardError/the map editor, which load this DLL too: stay a pure
    // proxy in anything that is not the game.
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const wchar_t* exeName = wcsrchr(exePath, L'\\');
    exeName = exeName ? exeName + 1 : exePath;
    if (_wcsicmp(exeName, L"Warcraft II.exe") != 0) return TRUE;

    wchar_t dllDir[MAX_PATH];
    GetModuleFileNameW(self, dllDir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dllDir, L'\\')) *slash = 0;

    logx::Open(dllDir);
    logx::Write("gameplay_options " MOD_VERSION " loaded into %ls", exeName);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    mod::SetModuleBase(base, dllDir);
    if (hook::Install(base)) logx::Write("tick hook installed at %p", reinterpret_cast<void*>(base + game::kRvaTickCallSite));
    return TRUE;
}
