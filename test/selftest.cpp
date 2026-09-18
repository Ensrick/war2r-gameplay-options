// Offline smoke test: maps the real game exe as an image (no code of the game runs), installs the hook against it,
// then drives the autocast pass over a fake world built inside the image's own globals. IssueOrder is swapped for a
// recorder so nothing of the game executes. Usage: selftest.exe "<path to Warcraft II.exe>"
#include <windows.h>
#include <cstdio>
#include <cstring>

#include "../src/mod.h"
#include "../src/world.h"
#include "../src/config.h"
#include "../src/datatweaks.h"
#include "../src/game.h"
#include "../src/hook.h"
#include "../src/log.h"

using namespace game;

static int g_failures = 0;
static uint8_t g_units[64 * kUnitSize];
static Unit* g_grid[64 * 64];
constexpr int kMap = 64;
static int g_unitCount = 0;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            ++g_failures;                \
            printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);         \
            printf("\n");                \
        }                                \
    } while (0)

static void __cdecl FakeIssueOrder(Unit* caster, int16_t x, int16_t y, Unit* target, void* handler) {
    // Like the real SetOrder (FUN_004ef080): the new order lands in the next-order slot, the current one stays.
    const uint32_t rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handler) - g_base);
    const uint8_t order = rva == kRvaMoveHandler      ? kOrderMove
                          : rva == kRvaHarvestHandler ? kOrderHarvest
                          : rva == kRvaReturnHandler  ? kOrderReturnGoods
                          : rva == kRvaRepairHandler  ? kOrderRepair
                                                      : static_cast<uint8_t>(*At<uint16_t>(kRvaPendingSpellOrder));
    Field<uint8_t>(caster, kOffNextOrder) = order;
    Field<Unit*>(caster, kOffOrderTarget) = target;
    if (!target) {  // the real IssueOrder stores the destination tile only for positional orders
        Field<int16_t>(caster, kOffOrderX) = x;
        Field<int16_t>(caster, kOffOrderY) = y;
    }
}

static int g_messages = 0;
static void __cdecl FakeShowMessage(const char*, int, int, int) { ++g_messages; }

static void WriteFileText(const wchar_t* path, const char* text) {
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"wb");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
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
    Field<uint8_t>(u, kOffNextOrder) = kOrderNone;
    g_grid[y * kMap + x] = u;
    *At<uint32_t>(kRvaUnitCount) = g_unitCount;
    return u;
}

static void Idle(Unit* u) {  // back to standing with nothing pending
    Field<uint8_t>(u, kOffOrder) = kOrderStand;
    Field<uint8_t>(u, kOffNextOrder) = kOrderNone;
    Field<Unit*>(u, kOffOrderTarget) = nullptr;
}
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
    swprintf_s(ini, L"%s\\autocast.toml", dir);
    DeleteFileW(ini);  // always start from the shipped defaults
    logx::Open(dir);
    mod::SetModuleBase(g_base, dir);

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

    // The order handlers the mod passes to IssueOrder must be the entries of the game's own handler table
    // (VA 0x8C1498, indexed by order id), and the gold decrement the refill relies on must be where we found it.
    {
        const uint32_t* handlers = At<uint32_t>(0x4C1498);
        CHECK(handlers[kOrderMove] == g_base + kRvaMoveHandler, "move handler is not table entry 3");
        CHECK(handlers[kOrderHarvest] == g_base + kRvaHarvestHandler, "harvest handler is not table entry 23");
        CHECK(handlers[kOrderReturnGoods] == g_base + kRvaReturnHandler, "return handler is not table entry 24");
        CHECK(handlers[kOrderRepair] == g_base + kRvaRepairHandler, "repair handler is not table entry 27");
        CHECK(handlers[kOrderSpellEye] == g_base + kRvaSpellOrderHandler, "spell handler is not table entry 0x30");
        const uint8_t decrement[] = {0x66, 0x01, 0x88, 0x82, 0x00, 0x00, 0x00};  // add word [eax+0x82], cx
        CHECK(memcmp(At<uint8_t>(0xC99C8), decrement, sizeof(decrement)) == 0, "gold decrement not at 0x4C99C8: +0x82 may be wrong");
    }

    // 3. Fake world in the image's globals.
    PatchJump(kRvaIssueOrder, &FakeIssueOrder);
    PatchJump(kRvaShowMessage, &FakeShowMessage);
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

    mod::OnTick();  // first tick writes the embedded default config and loads it
    CHECK(config::g.enabled && config::g.spell[kSpellHeal] && !config::g.spell[kSpellUnholyArmor], "default config");
    CHECK(GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES, "default autocast.toml was not written");
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        GetFileAttributesExW(ini, GetFileExInfoStandard, &fad);
        CHECK(fad.nFileSizeLow > 1000, "default autocast.toml is empty: the embedded resource was not found");
    }
    CHECK(config::g.polymorphRank[kDragon] && config::g.polymorphRank[kDaemon] && !config::g.polymorphRank[kGrunt],
          "default polymorph list");

    // Heal: most hurt own unit wins; healthy, allied-player and out-of-threshold units are skipped.
    ResetWorld();
    Unit* pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    AddUnit(kFootman, 0, 31, 30, 60, 0, kOrderStand);             // full HP
    AddUnit(kFootman, 0, 28, 30, 51, 0, kOrderStand);             // missing 9, under heal_min_missing_hp
    Unit* hurt = AddUnit(kFootman, 0, 33, 31, 20, 0, kOrderStand);
    AddUnit(kFootman, 0, 32, 32, 40, 0, kOrderStand);
    AddUnit(kFootman, 2, 30, 31, 5, 0, kOrderStand);              // ally's unit, own_units_only=1
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27 && TargetOf(pal) == hurt, "heal should pick the 20 HP footman (order %u)", OrderOf(pal));

    // Claims: a second paladin must take the next most hurt unit, not the same one.
    Unit* pal2 = AddUnit(kTypePaladin, 0, 31, 33, 90, 255, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal2) == 0x27 && TargetOf(pal2) != hurt && TargetOf(pal2) != nullptr, "second paladin must not double-heal");

    // The 10 HP floor is exact: missing 9 is ignored, missing 10 is healed.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    Unit* scratched = AddUnit(kFootman, 0, 31, 30, 51, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "healed a unit missing only 9 HP");
    Field<uint16_t>(scratched, kOffHp) = 50;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27 && TargetOf(pal) == scratched, "unit missing 10 HP should be healed");

    // Regression (0.1.5): the real IssueOrder only fills the NEXT-order slot. A heal that is pending must count as
    // cast, otherwise the same pass falls through to exorcism and overwrites it, and later passes re-issue it.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    Unit* wounded = AddUnit(kFootman, 0, 31, 30, 20, 0, kOrderStand);
    AddUnit(kSkeleton, 1, 33, 30, 40, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(Field<uint8_t>(pal, kOffOrder) == kOrderStand && Field<uint8_t>(pal, kOffNextOrder) == 0x27 &&
              TargetOf(pal) == wounded,
          "pending heal was overwritten (next order %u)", Field<uint8_t>(pal, kOffNextOrder));
    Field<Unit*>(pal, kOffOrderTarget) = nullptr;  // would be re-filled if a second pass re-issued anything
    mod::RunAutocastPass();
    CHECK(TargetOf(pal) == nullptr, "caster with a pending spell was given another order");

    // A move the player just queued (still in the next-order slot) is as untouchable as one under way.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderAttackTarget);
    Field<uint8_t>(pal, kOffNextOrder) = 3;  // ORDER_MOVE
    AddUnit(kFootman, 0, 31, 30, 10, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(Field<uint8_t>(pal, kOffNextOrder) == 3, "queued move was overwritten");

    // A move order is never interrupted; no mana means no cast; unresearched means no cast.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, 3 /* ORDER_MOVE */);
    AddUnit(kFootman, 0, 31, 30, 10, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 3, "moving caster was interrupted");
    Field<uint8_t>(pal, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal, kOffMana) = 3;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "cast without mana");
    Field<uint8_t>(pal, kOffMana) = 255;
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF & ~0x2u;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "cast an unresearched spell");
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF;

    // Exorcism on enemy undead, never on invisible ones.
    ResetWorld();
    pal = AddUnit(kTypePaladin, 0, 30, 30, 90, 255, kOrderStand);
    Unit* skel = AddUnit(kSkeleton, 1, 34, 30, 40, 0, kOrderAttack);
    Field<uint16_t>(skel, kOffInvisTimer) = 100;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "targeted an invisible enemy");
    Field<uint16_t>(skel, kOffInvisTimer) = 0;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x29 && TargetOf(pal) == skel, "exorcism on skeleton");

    // Bloodlust only on fighting units without bloodlust.
    ResetWorld();
    Unit* om = AddUnit(kTypeOgreMage, 0, 20, 20, 90, 200, kOrderStand);  // not full mana: no Eye of Kilrogg
    Unit* idle = AddUnit(kGrunt, 0, 21, 20, 60, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderStand, "bloodlust on an idle grunt");
    Field<uint8_t>(idle, kOffOrder) = kOrderAttackTarget;  // attacking but no enemy anywhere near
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderStand, "bloodlust with no enemy near");
    AddUnit(kFootman, 1, 23, 21, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == 0x31 && TargetOf(om) == idle, "bloodlust on the fighting grunt");
    Idle(om);
    Field<uint16_t>(idle, kOffBloodTimer) = 500;
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderStand, "re-bloodlusted a lusted grunt");

    // Mage: polymorph the ogre, not the grunt; with polymorph off, slow instead.
    ResetWorld();
    Unit* mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kGrunt, 1, 42, 40, 60, 0, kOrderAttack);
    Unit* ogre = AddUnit(kOgre, 1, 45, 41, 90, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == ogre, "polymorph should pick the ogre");
    Idle(mage);
    config::g.spell[kSpellPolymorph] = false;
    mod::RunAutocastPass();
    CHECK(OrderOf(mage) == 0x2C, "slow when polymorph is disabled (order %u)", OrderOf(mage));
    config::g.spell[kSpellPolymorph] = true;

    // Flying attackers outrank everything; a daemon qualifies despite 60 max HP (real unitdata.dat value).
    ResetWorld();
    mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kOgre, 1, 41, 40, 90, 0, kOrderAttack);
    AddUnit(kTypeMage, 1, 42, 40, 60, 0, kOrderStand);
    Unit* daemon = AddUnit(kDaemon, 1, 46, 44, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == daemon, "polymorph should pick the daemon first");
    ResetWorld();
    mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kDaemon, 1, 41, 40, 60, 0, kOrderAttack);
    Unit* dragon = AddUnit(kDragon, 1, 45, 45, 100, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == dragon, "dragon (100 HP) outranks daemon (60 HP)");

    // Death knight: coil an enemy; peons and buildings are not slow targets but are coil targets if fleshy.
    ResetWorld();
    Unit* dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* enemy = AddUnit(kFootman, 1, 13, 12, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == enemy, "death coil on footman");

    // Haste (flyers only by default): never a ground unit, never an idle flyer, yes a flyer sent to attack.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* fighter = AddUnit(kGrunt, 0, 11, 10, 60, 0, kOrderDefend);
    Unit* ownDragon = AddUnit(kDragon, 0, 12, 12, 100, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "haste went on a ground unit or an idle dragon (order %u)", OrderOf(dk));
    Field<uint8_t>(ownDragon, kOffOrder) = kOrderAttackArea;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x35 && TargetOf(dk) == ownDragon, "haste on the attacking dragon");
    Idle(dk);
    Field<int16_t>(ownDragon, kOffHasteTimer) = 300;
    config::g.hasteFlyersOnly = false;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x35 && TargetOf(dk) == fighter, "haste_flyers_only=0 should haste the defending grunt");
    config::g.hasteFlyersOnly = true;

    // Raise Dead: at the nearest corpse tile, only with an enemy around, one death knight per corpse.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* corpse = AddUnit(kTypeCorpse, 1, 12, 11, 0, 0, 0);
    Field<uint8_t>(corpse, kOffStateFlags) = kStateDying;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "raised dead with no enemy around");
    AddUnit(kFootman, 1, 16, 10, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x32 && TargetOf(dk) == nullptr && Field<int16_t>(dk, kOffOrderX) == 12 &&
              Field<int16_t>(dk, kOffOrderY) == 11,
          "raise dead at the corpse tile (order %u)", OrderOf(dk));
    Unit* dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk2) == 0x33, "second death knight should coil, the corpse is taken (order %u)", OrderOf(dk2));

    // Neutral units are never enemies. Targets outside search_radius are ignored.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, kNeutralPlayer, 11, 10, 60, 0, kOrderStand);
    AddUnit(kFootman, 1, 10 + config::g.searchRadius + 1, 10, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "cast on neutral or out-of-range unit");

    // Multiplayer and the master switch gate OnTick.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, 1, 12, 10, 60, 0, kOrderAttack);
    *At<uint32_t>(kRvaNetGame) = 1;
    for (int i = 0; i < 40; ++i) mod::OnTick();
    CHECK(OrderOf(dk) == kOrderStand, "autocast ran in a network game");
    *At<uint32_t>(kRvaNetGame) = 0;
    config::g.enabled = false;
    for (int i = 0; i < 40; ++i) mod::OnTick();
    CHECK(OrderOf(dk) == kOrderStand, "autocast ran while disabled");
    config::g.enabled = true;
    for (int i = 0; i < 40; ++i) mod::OnTick();
    CHECK(OrderOf(dk) == 0x33, "OnTick never reached the pass");

    // A computer-controlled "local player" (attract mode / observer) must do nothing.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kFootman, 1, 12, 10, 60, 0, kOrderAttack);
    controller[0] = 1;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "cast for a non-human local player");
    controller[0] = 0;

    // Eye of Kilrogg: an idle ogre-mage at full mana casts it, never a second one while an eye is out.
    static uint8_t exploredMap[kMap * kMap], visibleMap[kMap * kMap];
    *At<uint8_t*>(kRvaExploredMap) = exploredMap;
    *At<uint8_t*>(kRvaVisibleMap) = visibleMap;
    defType(kTypeEye, kTfFlyer, 100);
    ResetWorld();
    om = AddUnit(kTypeOgreMage, 0, 20, 20, 90, 254, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderStand, "eye cast below cast_at_mana");
    Field<uint8_t>(om, kOffMana) = 255;
    Field<uint8_t>(om, kOffOrder) = kOrderAttackTarget;
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderAttackTarget, "eye cast by a busy ogre-mage");
    Idle(om);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == kOrderSpellEye && TargetOf(om) == nullptr, "idle full-mana ogre-mage should cast the eye (order %u)", OrderOf(om));
    Unit* om2 = AddUnit(kTypeOgreMage, 0, 22, 20, 90, 255, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(om2) == kOrderStand, "second eye cast while one is pending (max_active = 1)");

    // Auto-scout: toward unexplored ground, again after arriving, hands off once the player flies it elsewhere.
    ResetWorld();
    memset(exploredMap, 0, sizeof(exploredMap));
    for (int y = 0; y < kMap; ++y)
        for (int x = 40; x < kMap; ++x) exploredMap[y * kMap + x] = kTileUnexplored;
    Unit* eyeUnit = AddUnit(kTypeEye, 0, 10, 10, 100, 255, kOrderStop);
    Field<uint32_t>(eyeUnit, kOffSerial) = 777;
    mod::RunAutocastPass();
    int destX = Field<int16_t>(eyeUnit, kOffOrderX), destY = Field<int16_t>(eyeUnit, kOffOrderY);
    CHECK(OrderOf(eyeUnit) == kOrderMove && destX >= 37 && destX < kMap && destY >= 0 && destY < kMap,
          "eye should head for the unexplored east (order %u dest %d,%d)", OrderOf(eyeUnit), destX, destY);
    Field<int16_t>(eyeUnit, kOffX) = static_cast<int16_t>(destX);  // arrived
    Field<int16_t>(eyeUnit, kOffY) = static_cast<int16_t>(destY);
    Field<uint8_t>(eyeUnit, kOffOrder) = kOrderStop;
    Field<uint8_t>(eyeUnit, kOffNextOrder) = kOrderNone;
    for (int y = 0; y < kMap; ++y)  // what it has seen so far is explored now
        for (int x = 40; x < 52; ++x) exploredMap[y * kMap + x] = 0;
    mod::RunAutocastPass();
    CHECK(OrderOf(eyeUnit) == kOrderMove && Field<int16_t>(eyeUnit, kOffOrderX) >= 49, "eye should keep scouting after arriving (dest x %d)",
          Field<int16_t>(eyeUnit, kOffOrderX));
    Field<int16_t>(eyeUnit, kOffX) = 5;  // the player flew it somewhere else and it stopped there
    Field<int16_t>(eyeUnit, kOffY) = 60;
    Field<uint8_t>(eyeUnit, kOffOrder) = kOrderStop;
    Field<uint8_t>(eyeUnit, kOffNextOrder) = kOrderNone;
    mod::RunAutocastPass();
    CHECK(OrderOf(eyeUnit) == kOrderStop, "an eye the player took over must be left alone");
    memset(exploredMap, 0, sizeof(exploredMap));

    // Unlimited gold mines: off by default; when on, a mine is put back to the most it held, never below 5000 gold.
    ResetWorld();
    defType(kTypeGoldMine, kTfBuilding | 0x400000, 25500);
    Unit* richMine = AddUnit(kTypeGoldMine, kNeutralPlayer, 50, 50, 25500, 0, 0);
    Unit* poorMine = AddUnit(kTypeGoldMine, kNeutralPlayer, 56, 50, 25500, 0, 0);
    Field<uint16_t>(richMine, kOffResources) = 400;
    Field<uint16_t>(poorMine, kOffResources) = 3;
    mod::OnTick();
    Field<uint16_t>(richMine, kOffResources) = 399;  // a worker took 100 gold
    mod::OnTick();
    CHECK(Field<uint16_t>(richMine, kOffResources) == 399 && Field<uint16_t>(poorMine, kOffResources) == 3, "mines refilled while the option is off");
    config::g.goldMinesUnlimited = true;
    mod::OnTick();
    Field<uint16_t>(richMine, kOffResources) = 398;
    mod::OnTick();
    CHECK(Field<uint16_t>(richMine, kOffResources) == 399, "mine should return to the amount seen when the option came on (%u)", Field<uint16_t>(richMine, kOffResources));
    CHECK(Field<uint16_t>(poorMine, kOffResources) == 50, "a nearly empty mine should be lifted to the 5000 gold floor (%u)", Field<uint16_t>(poorMine, kOffResources));
    config::g.goldMinesUnlimited = false;

    // Idle workers. Play time is fed in 100 ms steps; repair needs 1 s idle, harvest 10 s.
    auto step = [&](int ms) { for (int t = 0; t < ms; t += 100) { Sleep(100); mod::OnTick(); } };
    static uint16_t regionMap[kMap * kMap];
    for (auto& r : regionMap) r = 1;
    *At<uint16_t*>(kRvaRegionMap) = regionMap;
    struct Sz { uint16_t w, h; };
    At<Sz>(kRvaUnitSizeByType)[kTypeGoldMine] = {3, 3};
    constexpr uint8_t kFarm = 0x3A;
    At<Sz>(kRvaUnitSizeByType)[kFarm] = {2, 2};
    defType(kFarm, kTfBuilding, 400);
    defType(kPeon, kTfFleshy | kTfWorker, 30);
    At<int32_t>(kRvaPlayerGold)[0] = 500;
    At<int32_t>(kRvaPlayerLumber)[0] = 500;
    config::g.workerHarvestIdleSeconds = 2;  // keep the test short; the 10 s default is checked above in "default config"

    ResetWorld();
    Unit* peon = AddUnit(kPeon, 0, 20, 20, 30, 0, kOrderStop);
    Field<uint32_t>(peon, kOffSerial) = 1001;
    Unit* farm = AddUnit(kFarm, 0, 25, 20, 200, 0, 0);  // damaged, 5 tiles away, complete
    Field<uint16_t>(farm, kOffStateFlags) = kStateComplete;
    AddUnit(kFarm, 0, 22, 20, 50, 0, 0);                 // closer but still under construction: must be skipped
    mod::OnTick();
    step(600);
    CHECK(OrderOf(peon) == kOrderStop, "worker repaired before repair_idle_seconds");
    step(900);
    CHECK(OrderOf(peon) == kOrderRepair && TargetOf(peon) == farm, "idle worker should repair the finished farm, not the site (order %u)", OrderOf(peon));

    ResetWorld();
    peon = AddUnit(kPeon, 0, 20, 20, 30, 0, kOrderStop);
    Field<uint32_t>(peon, kOffSerial) = 1002;
    Unit* mine = AddUnit(kTypeGoldMine, kNeutralPlayer, 24, 19, 25500, 0, 0);  // footprint 24..26: 4 tiles away
    Field<uint16_t>(mine, kOffResources) = 100;
    regionMap[20 * kMap + 14] = kRegionTree;                                    // a tree 6 tiles west: out of radius 5
    mod::OnTick();
    step(1500);
    CHECK(OrderOf(peon) == kOrderStop, "worker sent to harvest before harvest_idle_seconds");
    step(1000);
    CHECK(OrderOf(peon) == kOrderHarvest && TargetOf(peon) == mine, "idle worker should go to the gold mine (order %u)", OrderOf(peon));

    ResetWorld();
    peon = AddUnit(kPeon, 0, 20, 20, 30, 0, kOrderStop);
    Field<uint32_t>(peon, kOffSerial) = 1003;
    regionMap[20 * kMap + 14] = 1;
    regionMap[22 * kMap + 23] = kRegionTree;  // 3 tiles away
    regionMap[22 * kMap + 24] = kRegionTree;
    mod::OnTick();
    step(2600);
    CHECK(OrderOf(peon) == kOrderHarvest && TargetOf(peon) == nullptr && Field<int16_t>(peon, kOffOrderX) == 23 && Field<int16_t>(peon, kOffOrderY) == 22,
          "idle worker should chop the nearest reachable tree (order %u at %d,%d)", OrderOf(peon), Field<int16_t>(peon, kOffOrderX), Field<int16_t>(peon, kOffOrderY));

    ResetWorld();
    peon = AddUnit(kPeon, 0, 20, 20, 30, 0, kOrderStop);
    Field<uint32_t>(peon, kOffSerial) = 1004;
    Field<uint8_t>(peon, kOffWorkerFlags) = 0x80 | kWorkerCarrying;
    Unit* standing = AddUnit(kPeon, 0, 21, 20, 30, 0, kOrderStand);  // Stand Ground: must be left alone
    Field<uint32_t>(standing, kOffSerial) = 1005;
    mod::OnTick();
    step(2600);
    CHECK(OrderOf(peon) == kOrderReturnGoods, "a loaded idle worker must return its cargo, never get a harvest order (order %u)", OrderOf(peon));
    CHECK(OrderOf(standing) == kOrderStand, "a worker on Stand Ground was taken over");
    regionMap[22 * kMap + 23] = regionMap[22 * kMap + 24] = 1;
    config::g.workerAutoHarvest = config::g.workerAutoRepair = false;

    // Map-start data tweaks, run against real default values (Data\Rez\unitdata.dat / upgrades.dat, typed in here).
    {
        uint16_t* hpT = At<uint16_t>(kRvaMaxHpByType);
        uint32_t* sightT = At<uint32_t>(kRvaSightByType);
        uint8_t* goldT = At<uint8_t>(kRvaGoldCostByType);
        uint8_t* lumberT = At<uint8_t>(kRvaLumberCostByType);
        uint8_t* oilT = At<uint8_t>(kRvaOilCostByType);
        uint16_t* upGold = At<uint16_t>(kRvaUpgradeGold);
        uint16_t* upLumber = At<uint16_t>(kRvaUpgradeLumber);
        constexpr uint8_t kFarmT = 0x3A, kFoundry = 0x4E, kKeep = 0x58, kGuardTower = 0x60, kDestroyer = 0x1E;
        auto resetTables = [&] {
            hpT[kFootman] = 60; hpT[0x19] = 240; hpT[0x23] = 800; hpT[kFarmT] = 400; hpT[kTypeGoldMine] = 25500; hpT[0x4A] = 1200;
            sightT[kDragon] = 6; sightT[0x2A] = 6; sightT[kFootman] = 4; sightT[0x28] = 9;
            goldT[kFootman] = 60; lumberT[kFootman] = 0; goldT[8] = 50; lumberT[8] = 5; goldT[kDragon] = 225;
            goldT[kDestroyer] = 70; lumberT[kDestroyer] = 35; oilT[kDestroyer] = 70;
            goldT[kFarmT] = 50; lumberT[kFarmT] = 25; goldT[kFoundry] = 70; lumberT[kFoundry] = 40; oilT[kFoundry] = 40;
            goldT[kKeep] = 200; lumberT[kKeep] = 100; oilT[kKeep] = 20; goldT[kGuardTower] = 50; lumberT[kGuardTower] = 15;
            upGold[0] = 800; upGold[4] = 200; upLumber[4] = 200; upGold[8] = 300; upGold[12] = 700; upGold[20] = 1500;
            upGold[31] = 1000; upGold[33] = 1000; upGold[35] = 1000; upGold[41] = 2000; upGold[44] = 1000;
            upGold[34] = 0;  // holy vision is free and must stay free
        };
        auto resetCosts = [&] { for (double& k : config::g.cost) k = 1.0; };
        resetTables();
        CHECK(*At<uint8_t>(kRvaMapLoadCallSite) == 0xE8, "map load call site is not a call");

        *At<uint8_t>(kRvaNetGameAtLoad) = 1;
        config::g.hpUnits = 2.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60, "data tables were edited for a multiplayer map");
        config::g.hpUnits = 1.0;
        *At<uint8_t>(kRvaNetGameAtLoad) = 0;

        bool allOne = config::g.hpUnits == 1.0 && config::g.hpHeroes == 1.0;
        for (double k : config::g.cost) allOne = allOne && k == 1.0;
        CHECK(allOne, "every health and price multiplier must default to 1.0");
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60 && hpT[0x19] == 240 && goldT[kFootman] == 60 && goldT[kFarmT] == 50 && goldT[kKeep] == 200 &&
                  upGold[0] == 800 && upGold[4] == 200 && upGold[33] == 1000,
              "defaults must leave health and every price alone");
        CHECK(sightT[kDragon] == 8 && sightT[0x2A] == 8 && sightT[kFootman] == 4, "sight +2 for dragon and gryphon only");

        // Health: units and heroes only.
        resetTables();
        config::g.hpUnits = 2.0; config::g.hpHeroes = 4.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 120 && hpT[0x19] == 960 && hpT[0x23] == 3200, "health: units x2, heroes x4 (footman %u grom %u deathwing %u)", hpT[kFootman], hpT[0x19], hpT[0x23]);
        CHECK(hpT[kFarmT] == 400 && hpT[0x4A] == 1200 && hpT[kTypeGoldMine] == 25500, "structures and the gold mine must keep their health");
        config::g.hpUnits = config::g.hpHeroes = 1.0;

        // Each price group moves its own rows and nothing else.
        resetTables();
        config::g.cost[kCostUnits] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFootman] == 30 && goldT[8] == 25 && lumberT[8] == 3 && goldT[kDragon] == 113 && lumberT[kFootman] == 0,
              "unit prices halved, free stays free (archer lumber %u dragon %u)", lumberT[8], goldT[kDragon]);
        CHECK(goldT[kDestroyer] == 35 && oilT[kDestroyer] == 70, "unit oil price must not move");
        CHECK(goldT[kFarmT] == 50 && goldT[kKeep] == 200 && upGold[0] == 800, "units multiplier leaked into structures or research");

        resetTables(); resetCosts();
        config::g.cost[kCostBuildings] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFarmT] == 25 && lumberT[kFarmT] == 13 && goldT[kFoundry] == 35 && oilT[kFoundry] == 20, "building prices halved incl. oil (farm lumber %u)", lumberT[kFarmT]);
        CHECK(goldT[kKeep] == 200 && goldT[kGuardTower] == 50 && goldT[kFootman] == 60, "buildings multiplier leaked into building upgrades or units");

        resetTables(); resetCosts();
        config::g.cost[kCostBuildingUpgrades] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kKeep] == 100 && lumberT[kKeep] == 50 && oilT[kKeep] == 10 && goldT[kGuardTower] == 25 && lumberT[kGuardTower] == 8,
              "building upgrade prices halved (keep %u tower lumber %u)", goldT[kKeep], lumberT[kGuardTower]);
        CHECK(goldT[kFarmT] == 50 && goldT[kFoundry] == 70, "building upgrades multiplier leaked into plain buildings");

        resetTables(); resetCosts();
        config::g.cost[kCostMeleeUpgrades] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(upGold[0] == 400 && upGold[8] == 150, "melee research halved (swords %u shields %u)", upGold[0], upGold[8]);
        CHECK(upGold[4] == 200 && upGold[20] == 1500 && upGold[33] == 1000 && upGold[12] == 700 && upGold[41] == 2000, "melee multiplier leaked into another group");

        resetTables(); resetCosts();
        config::g.cost[kCostRangedUpgrades] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(upGold[4] == 100 && upLumber[4] == 100 && upGold[31] == 500 && upGold[0] == 800, "elf / troll research halved only");

        resetTables(); resetCosts();
        config::g.cost[kCostPaladinOgreMage] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(upGold[33] == 500 && upGold[35] == 500 && upGold[44] == 500 && upGold[34] == 0, "paladin / ogre-mage group halved, free stays free");
        CHECK(upGold[41] == 2000 && upGold[0] == 800, "paladin / ogre-mage multiplier leaked into another group");

        resetTables(); resetCosts();
        config::g.cost[kCostSiegeUpgrades] = 0.5; config::g.cost[kCostNavalUpgrades] = 0.5; config::g.cost[kCostMageDeathKnightSpells] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(upGold[20] == 750 && upGold[12] == 350 && upGold[41] == 1000 && upGold[33] == 1000, "siege / naval / mage spell groups");

        // Engine caps: 16-bit health, byte-of-tens type price, 16-bit research price.
        resetTables(); resetCosts();
        config::g.hpUnits = 1000.0; config::g.hpHeroes = 1000.0;
        config::g.cost[kCostUnits] = 2.0; config::g.cost[kCostBuildingUpgrades] = 2.0; config::g.cost[kCostSiegeUpgrades] = 1000.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60000 && hpT[0x23] == 65535, "health cap is 65535 (footman %u deathwing %u)", hpT[kFootman], hpT[0x23]);
        CHECK(goldT[kDragon] == 255 && goldT[kKeep] == 255 && upGold[20] == 65535, "price caps: 2550 for a type, 65535 for research (%u, %u, %u)", goldT[kDragon], goldT[kKeep], upGold[20]);
        CHECK(hpT[kFarmT] == 400, "structures must never have their health scaled");

        config::g.sightBonus[0x28] = 5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(sightT[0x28] == 9, "sight must clamp at 9 (%u)", sightT[0x28]);
        config::g.sightBonus[0x28] = 0;
        config::g.hpUnits = config::g.hpHeroes = 1.0;
        resetCosts();
        resetTables();  // what the later scenarios expect
    }

    // Hero regeneration: 1 HP per second of stepping time, heroes only, never past max, dead heroes stay dead.
    ResetWorld();
    defType(0x19 /* grom_hellscream */, kTfFleshy | kTfAttacker, 240);
    Unit* grom = AddUnit(0x19, 1, 5, 5, 100, 0, kOrderStand);
    Unit* nearFull = AddUnit(0x19, 0, 6, 5, 239, 0, kOrderStand);
    Unit* plainGrunt = AddUnit(kGrunt, 0, 7, 5, 10, 0, kOrderStand);
    Unit* deadHero = AddUnit(0x19, 0, 8, 5, 50, 0, kOrderStand);
    Field<uint8_t>(deadHero, kOffStateFlags) = kStateDying;
    CHECK(config::g.heroRegenPerSecond == 1 && config::g.isHero[0x19] && !config::g.isHero[kGrunt], "default [heroes]");
    mod::OnTick();  // establishes the time base
    for (int i = 0; i < 25; ++i) {  // ~2.5 s of 100 ms steps
        Sleep(100);
        mod::OnTick();
    }
    const int gromHp = Field<uint16_t>(grom, kOffHp);
    CHECK(gromHp >= 101 && gromHp <= 103, "hero regen should be ~2 HP after ~2.5 s (hp %d)", gromHp);
    CHECK(Field<uint16_t>(nearFull, kOffHp) == 240, "regen must stop at max HP (%u)", Field<uint16_t>(nearFull, kOffHp));
    CHECK(Field<uint16_t>(plainGrunt, kOffHp) == 10 && Field<uint16_t>(deadHero, kOffHp) == 50, "regen touched a non-hero or a dying hero");
    Sleep(700);  // a pause: the long gap must not be credited as play time
    mod::OnTick();
    CHECK(Field<uint16_t>(grom, kOffHp) <= gromHp + 1, "a pause was credited as regeneration time");

    // TOML config: a custom file is honoured, typos and bad values are survivable, a syntax error keeps old settings.
    WriteFileText(ini,
                  "[general]\ntoggle_key = \"F7\"\ninterval_ticks = 3\nbogus_key = 1\n"
                  "[spells]\nheal = false\nunholy_armor = true\n"
                  "[heal]\nmin_missing_hp = 25\n"
                  "[polymorph]\ntargets = [\"grunt\", \"not_a_unit\", \"dragon\"]\n"
                  "[haste]\nflyers_only = false\n");
    CHECK(config::Init(dir), "valid custom toml rejected");
    CHECK(config::g.toggleKey == VK_F7 && config::g.intervalTicks == 3, "general section not applied");
    CHECK(!config::g.spell[kSpellHeal] && config::g.spell[kSpellUnholyArmor] && config::g.spell[kSpellSlow], "spells section");
    CHECK(config::g.healMinMissingHp == 25 && config::g.healBelowPct == 100 && !config::g.hasteFlyersOnly, "tuning values");
    CHECK(config::g.polymorphRank[kGrunt] == 1 && config::g.polymorphRank[kDragon] == 2 && config::g.polymorphRank[kOgre] == 0,
          "polymorph target list (grunt %u dragon %u ogre %u)", config::g.polymorphRank[kGrunt],
          config::g.polymorphRank[kDragon], config::g.polymorphRank[kOgre]);
    ResetWorld();
    mage = AddUnit(kTypeMage, 0, 40, 40, 60, 255, kOrderStand);
    AddUnit(kOgre, 1, 41, 40, 90, 0, kOrderAttack);
    Unit* listedGrunt = AddUnit(kGrunt, 1, 44, 40, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(mage) == 0x2E && TargetOf(mage) == listedGrunt, "custom polymorph list should sheep the grunt, not the ogre");
    WriteFileText(ini, "[general\nenabled = maybe\n");
    CHECK(!config::Init(dir), "syntax error must be reported");
    CHECK(config::g.healMinMissingHp == 25, "syntax error must keep the previous settings");
    DeleteFileW(ini);

    printf(g_failures ? "%d FAILURE(S)\n" : "ALL CHECKS PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
