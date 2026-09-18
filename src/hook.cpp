#include "hook.h"

#include <windows.h>
#include <cstring>

#include "autocast.h"
#include "game.h"
#include "log.h"

static void* g_origTickCallee = nullptr;

// Runs our pass, then continues into the function the game meant to call. Every register and flag is preserved
// because the exe is built with link-time codegen and nothing guarantees a standard clobber set at this site.
static __declspec(naked) void TickHookStub() {
    __asm {
        pushad
        pushfd
        cld
        call autocast::OnTick
        popfd
        popad
        jmp dword ptr [g_origTickCallee]
    }
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

    auto* site = reinterpret_cast<uint8_t*>(base + game::kRvaTickCallSite);
    const uintptr_t expectedCallee = base + game::kRvaTickCallee;
    int32_t rel;
    memcpy(&rel, site + 1, sizeof(rel));
    if (site[0] != 0xE8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != expectedCallee) {
        logx::Write("tick call site does not match (opcode %02X), staying inert", site[0]);
        return false;
    }
    g_origTickCallee = reinterpret_cast<void*>(expectedCallee);

    DWORD oldProtect;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        logx::Write("VirtualProtect failed (%lu), staying inert", GetLastError());
        return false;
    }
    const int32_t newRel =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(&TickHookStub) - (reinterpret_cast<uintptr_t>(site) + 5));
    memcpy(site + 1, &newRel, sizeof(newRel));
    VirtualProtect(site, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

}  // namespace hook
