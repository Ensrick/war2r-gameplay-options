#include "damagetypes.h"

#include <windows.h>
#include <cstring>

#include "game.h"
#include "log.h"

using namespace game;

namespace damagetypes {

namespace {

// The game's own damage functions, resolved from each call site's rel32 so a thunk always returns to the callee
// that site really had (three different ones, see docs/research/damage.md).
using ApplyFn = void(__cdecl*)(Unit*, Unit*, uint8_t);
RollFn g_origRoll = nullptr;    // FUN_004BD770(attacker): roll, armor of attacker+0x88 taken off the basic half
TowerFn g_origTower = nullptr;  // FUN_004BDC20(attacker, target): the same with the target passed in
void* g_origApply = nullptr;    // FUN_004BD8F0(source, victim, damage): where the damage is finally applied
bool g_installed = false;

// A pointer is a unit when it sits on a slot boundary inside the live unit array. Nothing else is dereferenced.
bool IsUnit(Unit* u) {
    if (!u) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(*At<Unit*>(kRvaUnitArray));
    const uintptr_t p = reinterpret_cast<uintptr_t>(u);
    const uint32_t count = *At<uint32_t>(kRvaUnitCount) & 0xFFFF;
    if (!base || p < base) return false;
    return (p - base) < static_cast<uintptr_t>(count) * kUnitSize && (p - base) % kUnitSize == 0;
}

}  // namespace

int Scale(int damage, Unit* attacker, Unit* target) {
    const DamageTypes& t = config::g.damageTypes;
    if (!t.any || damage <= 0) return damage;              // nothing configured, or nothing to scale
    if (*At<uint32_t>(kRvaNetGame) != 0) return damage;    // single player only, like every other data change
    if (!IsUnit(attacker) || !IsUnit(target)) return damage;
    const uint8_t w = t.weaponOf[TypeOf(attacker)], a = t.armorOf[TypeOf(target)];
    if (w == kNoDamageType || a == kNoDamageType) return damage;
    const uint32_t m = t.bonus[w][a];
    if (m == kDamageBonusOne) return damage;
    int scaled = static_cast<int>((static_cast<uint32_t>(damage) * m + 128) >> 8);  // x256 fixed point, to nearest
    if (scaled < 1) scaled = 1;                            // a bonus below 1 never makes a unit immune
    if (scaled > 255) scaled = 255;                        // the game clamps there too
    return scaled;
}

// ---- the thunks ----

int __cdecl RollThunk(Unit* attacker) {
    const int damage = g_origRoll(attacker);
    return Scale(damage, attacker, Field<Unit*>(attacker, kOffOrderTarget));
}

int __cdecl TowerThunk(Unit* attacker, Unit* target) { return Scale(g_origTower(attacker, target), attacker, target); }

// The splash hit, once per victim: the missile is in EBX at the call site, so the victim, the source and the
// missile type are all in hand. Only missile types the game itself marks as splashing weapons are scaled
// (0x8C090C, set for 7, 13, 14 and 24); every spell missile has a type outside that set.
int __cdecl SplashScale(uint8_t* missile, Unit* source, Unit* victim, int damage) {
    if (!missile || !At<uint8_t>(kRvaMissileSplashes)[missile[kMisOffType]]) return damage;
    return Scale(damage, source, victim);
}

__declspec(naked) void SplashThunk() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0x14]  // damage
        push dword ptr [esp + 0x14]  // victim
        push dword ptr [esp + 0x14]  // source unit (the missile's +0x30)
        push ebx                     // the missile itself
        call SplashScale
        add esp, 16
        mov dword ptr [esp + 0x14], eax  // the damage argument the game is about to pass on
        pop edx
        pop ecx
        jmp dword ptr [g_origApply]
    }
}

namespace {

struct Site {
    uint32_t rva;
    uint32_t callee;
    void* stub;
    const char* what;
};

// Every site is a plain `call rel32`; the callee is checked before anything is written.
bool Verify(uintptr_t base, const Site& s) {
    const auto* at = reinterpret_cast<const uint8_t*>(base + s.rva);
    int32_t rel;
    memcpy(&rel, at + 1, sizeof(rel));
    if (at[0] == 0xE8 && reinterpret_cast<uintptr_t>(at) + 5 + rel == base + s.callee) return true;
    logx::Write("damage types: the %s call site at 0x%06X is not the one this mod knows (%02X), no hook installed",
                s.what, 0x400000 + s.rva, at[0]);
    return false;
}

bool Redirect(uintptr_t base, const Site& s) {
    auto* at = reinterpret_cast<uint8_t*>(base + s.rva);
    DWORD old;
    if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        logx::Write("damage types: VirtualProtect failed at 0x%06X (%lu)", 0x400000 + s.rva, GetLastError());
        return false;
    }
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(s.stub) - (reinterpret_cast<uintptr_t>(at) + 5));
    memcpy(at + 1, &rel, sizeof(rel));
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
    return true;
}

}  // namespace

bool InstallHooks(uintptr_t base) {
    if (g_installed) return false;
    const Site sites[] = {
        {kRvaMeleeRollSite, kRvaDamageRoll, &RollThunk, "melee"},
        {kRvaMissileRollSite, kRvaDamageRoll, &RollThunk, "missile"},
        {kRvaTowerRollSite, kRvaDamageRollTarget, &TowerThunk, "tower"},
        {kRvaSplashApplySite, kRvaApplyDamage, &SplashThunk, "splash"},
    };
    for (const Site& s : sites)
        if (!Verify(base, s)) return false;  // all four or none
    g_origRoll = reinterpret_cast<RollFn>(base + kRvaDamageRoll);
    g_origTower = reinterpret_cast<TowerFn>(base + kRvaDamageRollTarget);
    g_origApply = reinterpret_cast<void*>(base + kRvaApplyDamage);
    for (const Site& s : sites)
        if (!Redirect(base, s)) return false;
    g_installed = true;
    return true;
}

bool Installed() { return g_installed; }

void SetOriginalsForTest(RollFn roll, TowerFn tower) {
    g_origRoll = roll;
    g_origTower = tower;
}
int RollThunkForTest(Unit* attacker) { return RollThunk(attacker); }
int TowerThunkForTest(Unit* attacker, Unit* target) { return TowerThunk(attacker, target); }
int SplashScaleForTest(uint8_t* missile, Unit* source, Unit* victim, int damage) {
    return SplashScale(missile, source, victim, damage);
}

}  // namespace damagetypes
