// Offline smoke test: maps the real game exe as an image (no code of the game runs), installs the hook against it,
// then drives the autocast pass over a fake world built inside the image's own globals. IssueOrder is swapped for a
// recorder so nothing of the game executes. Usage: selftest.exe "<path to Warcraft II.exe>"
#include <windows.h>
#include <cstdio>
#include <cstring>

#include "../src/autocast.h"
#include "../src/config.h"
#include "../src/game.h"
#include "../src/hook.h"
#include "../src/log.h"

using namespace game;

static uintptr_t g_base;
static int g_failures = 0;
static uint8_t g_units[64 * kUnitSize];
static Unit* g_grid[64 * 64];
constexpr int kMap = 64;
static int g_unitCount = 0;

template <typename T>
static T* At(uint32_t rva) { return reinterpret_cast<T*>(g_base + rva); }

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            ++g_failures;                \
            printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);         \
            printf("\n");                \
        }                                \
    } while (0)

static void __cdecl FakeIssueOrder(Unit* caster, int16_t x, int16_t y, Unit* target, void*) {
    Field<uint8_t>(caster, kOffOrder) = static_cast<uint8_t>(*At<uint16_t>(kRvaPendingSpellOrder));
    Field<Unit*>(caster, kOffOrderTarget) = target;
    if (!target) {  // the real IssueOrder stores the destination tile only for positional orders
        Field<int16_t>(caster, kOffOrderX) = x;
        Field<int16_t>(caster, kOffOrderY) = y;
    }
}

static void PatchJump(uint32_t rva, void* dest) {
    auto* p = reinterpret_cast<uint8_t*>(g_base + rva);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(dest) - (reinterpret_cast<uintptr_t>(p) + 5));
    memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
}

static void ResetWorld() {
    memset(g_units, 0, sizeof(g_units));
    memset(g_grid, 0, sizeof(g_grid));
    g_unitCount = 0;
    *At<uint32_t>(kRvaUnitCount) = 0;
    *At<uint32_t>(kRvaNetGame) = 0;
}

static Unit* AddUnit(uint8_t type, uint8_t owner, int x, int y, int hp, int mana, uint8_t order) {
    Unit* u = reinterpret_cast<Unit*>(g_units + g_unitCount * kUnitSize);
    ++g_unitCount;
    Field<int16_t>(u, kOffX) = static_cast<int16_t>(x);
    Field<int16_t>(u, kOffY) = static_cast<int16_t>(y);
    Field<uint16_t>(u, kOffHp) = static_cast<uint16_t>(hp);
    Field<uint8_t>(u, kOffMana) = static_cast<uint8_t>(mana);
    Field<uint8_t>(u, kOffType) = type;
    Field<uint8_t>(u, kOffOwner) = owner;
    Field<uint8_t>(u, kOffOrder) = order;
    g_grid[y * kMap + x] = u;
    *At<uint32_t>(kRvaUnitCount) = g_unitCount;
    return u;
}

static uint8_t OrderOf(Unit* u) { return Field<uint8_t>(u, kOffOrder); }
static Unit* TargetOf(Unit* u) { return Field<Unit*>(u, kOffOrderTarget); }

// Fake unit types used by the scenarios.
constexpr uint8_t kFootman = 0, kGrunt = 1, kOgre = 7, kSkeleton = 0x37, kPeon = 3, kDragon = 0x2B, kDaemon = 0x38;

int wmain(int argc, wchar_t** argv) {
    const wchar_t* exe = argc > 1 ? argv[1] : L"C:\\Program Files (x86)\\Warcraft II Remastered\\x86\\Warcraft II.exe";
    const HMODULE img = LoadLibraryExW(exe, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!img) {
        printf("cannot map %ls (%lu)\n", exe, GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uintptr_t>(img);
    printf("mapped game image at %p\n", img);

    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wcscat_s(dir, L"war2r_autocast_selftest");
    CreateDirectoryW(dir, nullptr);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\autocast.ini", dir);
    DeleteFileW(ini);  // always start from the shipped defaults
    logx::Open(dir);
    autocast::SetModuleBase(g_base, dir);

    // 1. Hook install against the real bytes.
    CHECK(hook::Install(g_base), "hook::Install rejected the supported exe");
    const auto* site = reinterpret_cast<const uint8_t*>(g_base + kRvaTickCallSite);
    int32_t rel;
    memcpy(&rel, site + 1, 4);
    const uintptr_t dest = reinterpret_cast<uintptr_t>(site) + 5 + rel;
    CHECK(site[0] == 0xE8 && dest != g_base + kRvaTickCallee, "call site was not redirected");
    CHECK(!hook::Install(g_base), "second Install must refuse an already patched site");

    // 2. Static data the mod reads straight from the exe.
    const uint16_t* mana = At<uint16_t>(kRvaManaCostByOrder);
    printf("mana costs from exe: heal %u exorcism %u slow %u polymorph %u bloodlust %u coil %u haste %u armor %u\n",
           mana[0x27], mana[0x29], mana[0x2C], mana[0x2E], mana[0x31], mana[0x33], mana[0x35], mana[0x36]);
    // Remastered 1.0.2 rebalanced heal 6 -> 5 and bloodlust 50 -> 60 versus the 1999 BNE table.
    CHECK(mana[0x27] == 5 && mana[0x29] == 4 && mana[0x2C] == 50 && mana[0x2E] == 200, "human mana costs");
    CHECK(mana[0x33] == 100 && mana[0x35] == 50 && mana[0x36] == 100, "orc mana costs");

    // 3. Fake world in the image's globals.
    PatchJump(kRvaIssueOrder, &FakeIssueOrder);
    *At<Unit*>(kRvaUnitArray) = reinterpret_cast<Unit*>(g_units);
    *At<Unit**>(kRvaUnitGrid) = g_grid;
    *At<uint16_t>(kRvaMapSize) = kMap;
    *At<uint8_t>(kRvaLocalPlayer) = 0;
    uint8_t* controller = At<uint8_t>(kRvaController);
    memset(controller, 1, kMaxPlayers);
    controller[0] = 0;
    uint8_t* alliance = At<uint8_t>(kRvaAlliance);
    memset(alliance, 0, kMaxPlayers * kMaxPlayers);
    alliance[0 * 16 + 0] = alliance[1 * 16 + 1] = alliance[2 * 16 + 2] = 1;
    alliance[0 * 16 + 2] = alliance[2 * 16 + 0] = 1;  // player 2 is our ally, player 1 the enemy
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    uint16_t* maxHp = At<uint16_t>(kRvaMaxHpByType);
    auto defType = [&](uint8_t t, uint32_t flags, uint16_t hp) { tf[t] = flags; maxHp[t] = hp; };
    defType(kFootman, kTfFleshy | kTfAttacker, 60);
    defType(kGrunt, kTfFleshy | kTfAttacker, 60);
    defType(kOgre, kTfFleshy | kTfAttacker, 90);
    defType(kPeon, kTfFleshy, 30);
    defType(kSkeleton, kTfUndead | kTfAttacker, 40);
    defType(kDragon, kTfFleshy | kTfAttacker | kTfFlyer, 100);
    defType(kDaemon, kTfFleshy | kTfAttacker | kTfFlyer, 60);
    defType(kTypePaladin, kTfFleshy | kTfAttacker | kTfCaster, 90);
    defType(kTypeOgreMage, kTfFleshy | kTfAttacker | kTfCaster, 90);
    defType(kTypeMage, kTfFleshy | kTfCaster, 60);
    defType(kTypeDeathKnight, kTfCaster, 60);
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF;

    autocast::OnTick();  // first tick loads the default config
    CHECK(config::g.enabled && config::g.spell[kSpellHeal] && !config::g.spell[kSpellUnholyArmor], "default config");

    // Heal: most hurt own unit wins; healthy, allied-player and out-of-threshold units are skipped.
    ResetWorld();
    Unit* pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    AddUnit(kFootman, 0, 31, 30, 60, 0, kOrderStand);             // full HP
    AddUnit(kFootman, 0, 28, 30, 55, 0, kOrderStand);             // 91%, above heal_below_pct
    Unit* hurt = AddUnit(kFootman, 0, 33, 31, 20, 0, kOrderStand);
    AddUnit(kFootman, 0, 32, 32, 40, 0, kOrderStand);
    AddUnit(kFootman, 2, 30, 31, 5, 0, kOrderStand);              // ally's unit, own_units_only=1
    autocast::RunPass();
    CHECK(OrderOf(pal) == 0x27 && TargetOf(pal) == hurt, "heal should pick the 20 HP footman (order %u)", OrderOf(pal));

    // Claims: a second paladin must take the next most hurt unit, not the same one.
    Unit* pal2 = AddUnit(kTypePaladin, 0, 31, 33, 90, 255, kOrderStand);
    autocast::RunPass();
    CHECK(OrderOf(pal2) == 0x27 && TargetOf(pal2) != hurt && TargetOf(pal2) != nullptr, "second paladin must not double-heal");

    // A move order is never interrupted; no mana means no cast; unresearched means no cast.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, 3 /* ORDER_MOVE */);
    AddUnit(kFootman, 0, 31, 30, 10, 0, kOrderStand);
    autocast::RunPass();
    CHECK(OrderOf(pal) == 3, "moving caster was interrupted");
    Field<uint8_t>(pal, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal, kOffMana) = 3;
    autocast::RunPass();
    CHECK(OrderOf(pal) == kOrderStand, "cast without mana");
    Field<uint8_t>(pal, kOffMana) = 255;
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF & ~0x2u;
    autocast::RunPass();
    CHECK(OrderOf(pal) == kOrderStand, "cast an unresearched spell");
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF;

    // Exorcism on enemy undead, never on invisible ones.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    Unit* skel = AddUnit(kSkeleton, 1, 34, 30, 40, 0, kOrderAttack);
    Field<uint16_t>(skel, kOffInvisTimer) = 100;
    autocast::RunPass();
    CHECK(OrderOf(pal) == kOrderStand, "targeted an invisible enemy");
    Field<uint16_t>(skel, kOffInvisTimer) = 0;
    autocast::RunPass();
    CHECK(OrderOf(pal) == 0x29 && TargetOf(pal) == skel, "exorcism on skeleton");

    // Bloodlust only on fighting units without bloodlust.
    ResetWorld();
    Unit* om = AddUnit(kTypeOgreMage, 0, 20, 20, 90, 255, kOrderStand);
    Unit* idle = AddUnit(kGrunt, 0, 21, 20, 60, 0, kOrderStand);
    autocast::RunPass();
    CHECK(OrderOf(om) == kOrderStand, "bloodlust on an idle grunt");
    Field<uint8_t>(idle, kOffOrder) = kOrderAttackTarget;  // attacking but no enemy anywhere near
    autocast::RunPass();
    CHECK(OrderOf(om) == kOrderStand, "bloodlust with no enemy near");
    AddUnit(kFootman, 1, 23, 21, 60, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(om) == 0x31 && TargetOf(om) == idle, "bloodlust on the fighting grunt");
    Field<uint8_t>(om, kOffOrder) = kOrderStand;
    Field<Unit*>(om, kOffOrderTarget) = nullptr;
    Field<uint16_t>(idle, kOffBloodTimer) = 500;
    autocast::RunPass();
    CHECK(OrderOf(om) == kOrderStand, "re-bloodlusted a lusted grunt");

    // Mage: polymorph the ogre, not the grunt; with polymorph off, slow instead.
    ResetWorld();
    Unit* mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kGrunt, 1, 42, 40, 60, 0, kOrderAttack);
    Unit* ogre = AddUnit(kOgre, 1, 45, 41, 90, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == ogre, "polymorph should pick the ogre");
    Field<uint8_t>(mage, kOffOrder) = kOrderStand;
    Field<Unit*>(mage, kOffOrderTarget) = nullptr;
    config::g.spell[kSpellPolymorph] = false;
    autocast::RunPass();
    CHECK(OrderOf(mage) == 0x2C, "slow when polymorph is disabled (order %u)", OrderOf(mage));
    config::g.spell[kSpellPolymorph] = true;

    // Flying attackers outrank everything; a daemon qualifies despite 60 max HP (real unitdata.dat value).
    ResetWorld();
    mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kOgre, 1, 41, 40, 90, 0, kOrderAttack);
    AddUnit(kTypeMage, 1, 42, 40, 60, 0, kOrderStand);
    Unit* daemon = AddUnit(kDaemon, 1, 46, 44, 60, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == daemon, "polymorph should pick the daemon first");
    ResetWorld();
    mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kDaemon, 1, 41, 40, 60, 0, kOrderAttack);
    Unit* dragon = AddUnit(kDragon, 1, 45, 45, 100, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == dragon, "dragon (100 HP) outranks daemon (60 HP)");

    // Death knight: coil an enemy; peons and buildings are not slow targets but are coil targets if fleshy.
    ResetWorld();
    Unit* dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* enemy = AddUnit(kFootman, 1, 13, 12, 60, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == enemy, "death coil on footman");

    // Haste (flyers only by default): never a ground unit, never an idle flyer, yes a flyer sent to attack.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* fighter = AddUnit(kGrunt, 0, 11, 10, 60, 0, kOrderDefend);
    Unit* ownDragon = AddUnit(kDragon, 0, 12, 12, 100, 0, kOrderStand);
    autocast::RunPass();
    CHECK(OrderOf(dk) == kOrderStand, "haste went on a ground unit or an idle dragon (order %u)", OrderOf(dk));
    Field<uint8_t>(ownDragon, kOffOrder) = kOrderAttackArea;
    autocast::RunPass();
    CHECK(OrderOf(dk) == 0x35 && TargetOf(dk) == ownDragon, "haste on the attacking dragon");
    Field<uint8_t>(dk, kOffOrder) = kOrderStand;
    Field<Unit*>(dk, kOffOrderTarget) = nullptr;
    Field<int16_t>(ownDragon, kOffHasteTimer) = 300;
    config::g.hasteFlyersOnly = false;
    autocast::RunPass();
    CHECK(OrderOf(dk) == 0x35 && TargetOf(dk) == fighter, "haste_flyers_only=0 should haste the defending grunt");
    config::g.hasteFlyersOnly = true;

    // Raise Dead: at the nearest corpse tile, only with an enemy around, one death knight per corpse.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* corpse = AddUnit(kTypeCorpse, 1, 12, 11, 0, 0, 0);
    Field<uint8_t>(corpse, kOffStateFlags) = kStateDying;
    autocast::RunPass();
    CHECK(OrderOf(dk) == kOrderStand, "raised dead with no enemy around");
    AddUnit(kFootman, 1, 16, 10, 60, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(dk) == 0x32 && TargetOf(dk) == nullptr && Field<int16_t>(dk, kOffOrderX) == 12 &&
              Field<int16_t>(dk, kOffOrderY) == 11,
          "raise dead at the corpse tile (order %u)", OrderOf(dk));
    Unit* dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    autocast::RunPass();
    CHECK(OrderOf(dk2) == 0x33, "second death knight should coil, the corpse is taken (order %u)", OrderOf(dk2));

    // Neutral units are never enemies. Targets outside search_radius are ignored.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, kNeutralPlayer, 11, 10, 60, 0, kOrderStand);
    AddUnit(kFootman, 1, 10 + config::g.searchRadius + 1, 10, 60, 0, kOrderAttack);
    autocast::RunPass();
    CHECK(OrderOf(dk) == kOrderStand, "cast on neutral or out-of-range unit");

    // Multiplayer and the master switch gate OnTick.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, 1, 12, 10, 60, 0, kOrderAttack);
    *At<uint32_t>(kRvaNetGame) = 1;
    for (int i = 0; i < 40; ++i) autocast::OnTick();
    CHECK(OrderOf(dk) == kOrderStand, "autocast ran in a network game");
    *At<uint32_t>(kRvaNetGame) = 0;
    config::g.enabled = false;
    for (int i = 0; i < 40; ++i) autocast::OnTick();
    CHECK(OrderOf(dk) == kOrderStand, "autocast ran while disabled");
    config::g.enabled = true;
    for (int i = 0; i < 40; ++i) autocast::OnTick();
    CHECK(OrderOf(dk) == 0x33, "OnTick never reached the pass");

    // A computer-controlled "local player" (attract mode / observer) must do nothing.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, 1, 12, 10, 60, 0, kOrderAttack);
    controller[0] = 1;
    autocast::RunPass();
    CHECK(OrderOf(dk) == kOrderStand, "cast for a non-human local player");
    controller[0] = 0;

    printf(g_failures ? "%d FAILURE(S)\n" : "ALL CHECKS PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
