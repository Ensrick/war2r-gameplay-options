#include "hook.h"

#include <windows.h>
#include <cstring>

#include "config.h"
#include "datatweaks.h"
#include "game.h"
#include "log.h"
#include "mod.h"

static void* g_origTickCallee = nullptr;
static void* g_origFinalizeTables = nullptr;

// "mod" is an operator in MSVC inline assembly, so the stubs cannot name mod::OnTick directly.
static void __cdecl TickThunk() { mod::OnTick(); }
static void __cdecl MapLoadThunk() { datatweaks::OnNewMapTablesLoaded(); }

// Both stubs run our code first, then continue into the function the game meant to call. Every register and flag
// is preserved because the exe is built with link-time codegen and nothing guarantees a standard clobber set.
static __declspec(naked) void TickHookStub() {
    __asm {
        pushad
        pushfd
        cld
        call TickThunk
        popfd
        popad
        jmp dword ptr [g_origTickCallee]
    }
}

static __declspec(naked) void MapLoadHookStub() {
    __asm {
        pushad
        pushfd
        cld
        call MapLoadThunk
        popfd
        popad
        jmp dword ptr [g_origFinalizeTables]
    }
}

// Redirects one `call rel32` after checking that it still calls what we expect.
static bool RedirectCall(uintptr_t base, uint32_t siteRva, uint32_t calleeRva, void* stub, void** origOut, const char* what) {
    auto* site = reinterpret_cast<uint8_t*>(base + siteRva);
    const uintptr_t expectedCallee = base + calleeRva;
    int32_t rel;
    memcpy(&rel, site + 1, sizeof(rel));
    if (site[0] != 0xE8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != expectedCallee) {
        logx::Write("%s call site does not match (opcode %02X), not hooked", what, site[0]);
        return false;
    }
    *origOut = reinterpret_cast<void*>(expectedCallee);

    DWORD oldProtect;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        logx::Write("%s: VirtualProtect failed (%lu), not hooked", what, GetLastError());
        return false;
    }
    const int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(site) + 5));
    memcpy(site + 1, &newRel, sizeof(newRel));
    VirtualProtect(site, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

namespace hook {

bool Install(uintptr_t base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != game::kPeTimestamp) {
        logx::Write("host exe timestamp %u is not the supported build %u, staying inert", nt->FileHeader.TimeDateStamp,
                    game::kPeTimestamp);
        return false;
    }
    if (!RedirectCall(base, game::kRvaTickCallSite, game::kRvaTickCallee, &TickHookStub, &g_origTickCallee, "tick")) return false;
    // Optional second hook: without it only the map-start data tweaks (health, costs, vision) are lost.
    if (RedirectCall(base, game::kRvaMapLoadCallSite, game::kRvaFinalizeTables, &MapLoadHookStub, &g_origFinalizeTables, "map load"))
        logx::Write("map load hook installed");
    return true;
}

}  // namespace hook
