// Offline smoke test: maps the real game exe as an image (no code of the game runs), installs the hook against it,
// then drives the autocast pass over a fake world built inside the image's own globals. IssueOrder is swapped for a
// recorder so nothing of the game executes. Usage: selftest.exe "<path to Warcraft II.exe>"
#include <windows.h>
#include <share.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "../src/mod.h"
#include "../src/autocast.h"
#include "../src/world.h"
#include "../src/config.h"
#include "../src/damagetypes.h"
#include "../src/datatweaks.h"
#include "../src/game.h"
#include "../src/hook.h"
#include "../src/log.h"
#include "../src/production.h"
#include "../src/resume.h"
#include "../src/dodge.h"
#include "../src/scouts.h"
#include "../src/spells.h"
#include "../src/upgrades.h"
#include "../src/aijobs.h"
#include "../src/aiwatch.h"
#include "../src/farms.h"
#include "../src/trees.h"

using namespace game;

static int g_failures = 0;
static uint8_t g_units[64 * kUnitSize];
static Unit* g_grid[64 * 64];
static Unit* g_airGrid[64 * 64];  // the game files flyers here and ONLY here (FUN_004b4a00)
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
                          : rva == kRvaStopHandler    ? kOrderStop
                          : rva == kRvaBuildHandler   ? kOrderBuild
                          : rva == kRvaAttackMoveHandler ? kOrderAttackArea
                          : rva == kRvaPatrolHandler     ? kOrderPatrol
                          : rva == kRvaPatrolCommandHandler ? kOrderPatrol
                          : rva == kRvaStandHandler      ? kOrderStand
                          : rva == kRvaAttackHandler     ? (target ? kOrderAttackTarget : kOrderAttack)
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
    resume::OnNewMap();  // no attack-move waiting to be handed back into the next scenario
    memset(g_units, 0, sizeof(g_units));
    memset(g_grid, 0, sizeof(g_grid));
    memset(g_airGrid, 0, sizeof(g_airGrid));
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
    Field<uint8_t>(u, kOffSeenMask) = 0xFF;  // what FUN_004f11c0 writes for every visible unit; fog mask 0 = not fogged
    const bool flyer = (At<uint32_t>(kRvaTypeFlags)[type] & kTfFlyer) != 0;  // defType() the type before adding it
    (flyer ? g_airGrid : g_grid)[y * kMap + x] = u;
    *At<uint32_t>(kRvaUnitCount) = g_unitCount;
    return u;
}

static void Idle(Unit* u) {  // back to standing with nothing pending
    Field<uint8_t>(u, kOffOrder) = kOrderStand;
    Field<uint8_t>(u, kOffNextOrder) = kOrderNone;
    Field<Unit*>(u, kOffOrderTarget) = nullptr;
}
static Unit* TargetOf(Unit* u) { return Field<Unit*>(u, kOffOrderTarget); }

// The shipped defaults are conservative (4 spells + auto-repair). The behaviour scenarios below exercise every
// feature, so they switch everything on; unholy armor stays off because several scenarios rely on that. The spells of
// the "every spell" round (holy vision .. runes) have their own block and switch themselves on one at a time.
static void SetLegacyAreaRules() {
    for (uint16_t& v : config::g.areaValues.pct) v = 100;
    config::g.areaLookaheadTiles = 0;
    config::g.areaReserveValue = 0.0;
}

static void EnableEverythingForTests() {
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i <= kSpellRaiseDead && i != kSpellUnholyArmor;
    config::g.eyeCast = config::g.eyeAutoScout = true;
    config::g.workerAutoHarvest = config::g.workerAutoRepair = true;
    config::g.heroRegen = true;
    config::g.heroRegenPerSecond = 1;
    // The area-spell scenarios written before 1.32 measure coverage alone: no per-type values, no lookahead hold, no
    // mana reserve. AreaValueTests switches those on.
    SetLegacyAreaRules();
}

// ---- Tree regrowth fixtures (src/trees.cpp, docs/research/tree_regrowth.md) ----
// Art\bgs\Forest\nf_tree.bin without its two header words: 41 rows of 10. Rows 0..25 are the same in all four tilesets,
// rows 26..40 are this tileset's art variants (each repeats the nine transitions of one of the rows 1..24).
static const uint16_t kForestTreeTable[41 * 10] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009,  // 0
    0x0001, 0x0001, 0x0001, 0x0001, 0x0016, 0x1019, 0x0001, 0x0016, 0x1019, 0x0001,  // 1
    0x0002, 0x0010, 0x0001, 0x0002, 0x0015, 0x1019, 0x0002, 0x000E, 0x0003, 0x0002,  // 2
    0x0003, 0x0014, 0x1019, 0x0003, 0x0014, 0x1019, 0x0003, 0x0003, 0x0003, 0x0003,  // 3
    0x0004, 0x000D, 0x0001, 0x0002, 0x000F, 0x1019, 0x0002, 0x0012, 0x0005, 0x0004,  // 4
    0x0005, 0x0006, 0x1019, 0x0003, 0x0006, 0x1019, 0x0003, 0x0005, 0x0005, 0x0005,  // 5
    0x0006, 0x0006, 0x1019, 0x0014, 0x0006, 0x1019, 0x0014, 0x0006, 0x0006, 0x0006,  // 6
    0x0007, 0x0008, 0x0009, 0x000C, 0x0008, 0x1019, 0x000E, 0x0007, 0x0005, 0x0012,  // 7
    0x0008, 0x0008, 0x0009, 0x0011, 0x0008, 0x1019, 0x0015, 0x0008, 0x0006, 0x000F,  // 8
    0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x1019, 0x0016, 0x0009, 0x1019, 0x0016,  // 9
    0x000A, 0x000A, 0x0017, 0x0013, 0x0008, 0x1019, 0x0010, 0x0008, 0x0006, 0x000D,  // 10
    0x000B, 0x0013, 0x0017, 0x000B, 0x0011, 0x1019, 0x0002, 0x000C, 0x0003, 0x0002,  // 11
    0x000C, 0x0011, 0x0009, 0x000C, 0x0011, 0x1019, 0x000E, 0x000C, 0x0003, 0x000E,  // 12
    0x000D, 0x000D, 0x0001, 0x0010, 0x000F, 0x1019, 0x0010, 0x000F, 0x0006, 0x000D,  // 13
    0x000E, 0x0015, 0x0016, 0x000E, 0x0015, 0x1019, 0x000E, 0x000E, 0x0003, 0x000E,  // 14
    0x000F, 0x000F, 0x0016, 0x0015, 0x000F, 0x1019, 0x0015, 0x000F, 0x0006, 0x000F,  // 15
    0x0010, 0x0010, 0x0001, 0x0010, 0x0015, 0x1019, 0x0010, 0x0015, 0x0014, 0x0010,  // 16
    0x0011, 0x0011, 0x0009, 0x0011, 0x0011, 0x1019, 0x0015, 0x0011, 0x0014, 0x0015,  // 17
    0x0012, 0x000F, 0x0016, 0x000E, 0x000F, 0x1019, 0x000E, 0x0012, 0x0005, 0x0012,  // 18
    0x0013, 0x0013, 0x0017, 0x0013, 0x0011, 0x1019, 0x0010, 0x0011, 0x0014, 0x0010,  // 19
    0x0014, 0x0014, 0x1019, 0x0014, 0x0014, 0x1019, 0x0014, 0x0014, 0x0014, 0x0014,  // 20
    0x0015, 0x0015, 0x0016, 0x0015, 0x0015, 0x1019, 0x0015, 0x0015, 0x0014, 0x0015,  // 21
    0x0016, 0x0016, 0x0016, 0x0016, 0x0016, 0x1019, 0x0016, 0x0016, 0x1019, 0x0016,  // 22
    0x0017, 0x0017, 0x0017, 0x0017, 0x0009, 0x1019, 0x0001, 0x0009, 0x1019, 0x0001,  // 23
    0x0018, 0x000A, 0x0017, 0x000B, 0x0008, 0x1019, 0x0002, 0x0007, 0x0005, 0x0004,  // 24
    0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019,  // 25
    0x001A, 0x000A, 0x0017, 0x000B, 0x0008, 0x1019, 0x0002, 0x0007, 0x0005, 0x0004,  // 26
    0x001B, 0x000A, 0x0017, 0x000B, 0x0008, 0x1019, 0x0002, 0x0007, 0x0005, 0x0004,  // 27
    0x001C, 0x0009, 0x0009, 0x0009, 0x0009, 0x1019, 0x0016, 0x0009, 0x1019, 0x0016,  // 28
    0x001D, 0x0001, 0x0001, 0x0001, 0x0016, 0x1019, 0x0001, 0x0016, 0x1019, 0x0001,  // 29
    0x001E, 0x0017, 0x0017, 0x0017, 0x0009, 0x1019, 0x0001, 0x0009, 0x1019, 0x0001,  // 30
    0x001F, 0x0006, 0x1019, 0x0014, 0x0006, 0x1019, 0x0014, 0x0006, 0x0006, 0x0006,  // 31
    0x0020, 0x0008, 0x0009, 0x0011, 0x0008, 0x1019, 0x0015, 0x0008, 0x0006, 0x000F,  // 32
    0x0021, 0x0006, 0x1019, 0x0003, 0x0006, 0x1019, 0x0003, 0x0005, 0x0005, 0x0005,  // 33
    0x0022, 0x0010, 0x0001, 0x0002, 0x0015, 0x1019, 0x0002, 0x000E, 0x0003, 0x0002,  // 34
    0x0023, 0x0014, 0x1019, 0x0003, 0x0014, 0x1019, 0x0003, 0x0003, 0x0003, 0x0003,  // 35
    0x0024, 0x0008, 0x0009, 0x000C, 0x0008, 0x1019, 0x000E, 0x0007, 0x0005, 0x0012,  // 36
    0x0025, 0x000D, 0x0001, 0x0010, 0x000F, 0x1019, 0x0010, 0x000F, 0x0006, 0x000D,  // 37
    0x0026, 0x000D, 0x0001, 0x0010, 0x000F, 0x1019, 0x0010, 0x000F, 0x0006, 0x000D,  // 38
    0x0027, 0x0011, 0x0009, 0x000C, 0x0011, 0x1019, 0x000E, 0x000C, 0x0003, 0x000E,  // 39
    0x0028, 0x0011, 0x0009, 0x000C, 0x0011, 0x1019, 0x000E, 0x000C, 0x0003, 0x000E,  // 40
};
constexpr int kForestTreeTiles = 40;   // what the game stores at kRvaTreeTileCount for this table
constexpr uint16_t kTreeBaseT = 0x66;  // tile id of state 1
constexpr uint16_t kSolidT = 0x7D;     // state 24, solid forest
constexpr uint16_t kStumpT = 0x7E;     // state 25, the cleared tile
constexpr uint16_t kGrassT = 0x0150;   // any tile id outside the wall / tree / rock ranges
constexpr uint16_t kLandT = 0x4001;    // a land region id
static uint16_t g_tileMap[kMap * kMap], g_sqMap[kMap * kMap];
static uint16_t* g_regionT = nullptr;  // the region map the worker tests already own
static uint8_t g_vertex[(kMap + 1) * (kMap + 1)];  // 1 = this corner point is forest

struct TerrainSnap {
    uint16_t tile[kMap * kMap], sq[kMap * kMap], region[kMap * kMap];
};
static void TakeSnap(TerrainSnap& s) {
    memcpy(s.tile, g_tileMap, sizeof(s.tile));
    memcpy(s.sq, g_sqMap, sizeof(s.sq));
    memcpy(s.region, g_regionT, sizeof(s.region));
}
static bool SameAsSnap(const TerrainSnap& s) {
    return !memcmp(s.tile, g_tileMap, sizeof(s.tile)) && !memcmp(s.sq, g_sqMap, sizeof(s.sq)) && !memcmp(s.region, g_regionT, sizeof(s.region));
}

static void ForestVertices(int x0, int y0, int x1, int y1) {  // corner points x0..x1 / y0..y1: tree tiles x0-1..x1, y0-1..y1
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) g_vertex[y * (kMap + 1) + x] = 1;
}

// All grass, then a tree tile wherever a tile touches a forest corner point: the state the map editor would use.
static void BuildTerrain() {
    static const uint8_t kStateOfMaskT[16] = {0, 9, 1, 23, 6, 8, 13, 10, 3, 12, 2, 11, 5, 7, 4, 24};
    for (int y = 0; y < kMap; ++y)
        for (int x = 0; x < kMap; ++x) {
            const int i = y * kMap + x, v = y * (kMap + 1) + x;
            const int mask = g_vertex[v] | g_vertex[v + 1] << 1 | g_vertex[v + kMap + 1] << 2 | g_vertex[v + kMap + 2] << 3;
            g_tileMap[i] = mask ? static_cast<uint16_t>(kTreeBaseT + kStateOfMaskT[mask] - 1) : kGrassT;
            g_sqMap[i] = mask ? 0x0081 : 0x0001;
            g_regionT[i] = mask ? kRegionTree : kLandT;
        }
}

// The game's own tree removal (FUN_004eb040 -> FUN_004e9240 -> FUN_004eb400) over the fake maps, minus the region
// merge. The game indexes the table with no bounds check: a tile id outside the tree range under a forest region word
// would read past it, which is the one thing the mod must never set up, so that is a failure here.
static bool FakeFell(int fx, int fy) {
    int cell = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            ++cell;
            const int x = fx + dx, y = fy + dy;
            if (x < 0 || y < 0 || x >= kMap || y >= kMap) continue;
            const int i = y * kMap + x;
            if (g_regionT[i] != kRegionTree && g_regionT[i] != kRegionChopping) continue;
            const int state = (g_tileMap[i] & 0x7FF) - kTreeBaseT + 1;
            if (state < 1 || state > kForestTreeTiles) {
                CHECK(false, "felling at %d,%d: tile id %04X at %d,%d is outside the tree table", fx, fy, g_tileMap[i], x, y);
                return false;
            }
            if (kForestTreeTable[state * 10] & 0xF000) continue;
            const uint16_t entry = kForestTreeTable[state * 10 + cell];
            g_tileMap[i] = static_cast<uint16_t>((entry & 0xFFF) - 1 + kTreeBaseT);
            if (entry & 0xF000) {
                g_sqMap[i] &= 0xFF7F;
                g_regionT[i] = kLandT;
            }
        }
    return true;
}

// Components of the land a ground unit can use (units ignored). `eight` = diagonal steps count as connected.
static void LabelOpenLand(int* label, bool eight) {
    static int stack[kMap * kMap];
    for (int i = 0; i < kMap * kMap; ++i) label[i] = (g_sqMap[i] & 0x08CE) ? -1 : 0;
    int next = 0;
    for (int start = 0; start < kMap * kMap; ++start) {
        if (label[start] != 0) continue;
        int top = 0;
        stack[top++] = start;
        label[start] = ++next;
        while (top) {
            const int i = stack[--top], x = i % kMap, y = i / kMap;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((!dx && !dy) || (!eight && dx && dy)) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= kMap || ny >= kMap || label[ny * kMap + nx] != 0) continue;
                    label[ny * kMap + nx] = next;
                    stack[top++] = ny * kMap + nx;
                }
        }
    }
}

// Tiles that were connected before must still be connected after: no tile of one old component may end up in two.
static bool StillConnected(const int* before, const int* after) {
    static int seenAs[kMap * kMap + 1];
    memset(seenAs, 0, sizeof(seenAs));
    for (int i = 0; i < kMap * kMap; ++i) {
        if (after[i] <= 0) continue;   // closed now
        if (before[i] <= 0) return false;  // a closed tile opened: regrowth never does that
        if (!seenAs[before[i]]) seenAs[before[i]] = after[i];
        else if (seenAs[before[i]] != after[i]) return false;
    }
    return true;
}

// What must hold for every tile after regrowth, given the terrain `was` before it. Returns the number of new trees.
static int CheckRegrowth(const TerrainSnap& was, const char* what) {
    int grown = 0, bad = 0;
    for (int i = 0; i < kMap * kMap; ++i) {
        const uint16_t id = g_tileMap[i], sq = g_sqMap[i], region = g_regionT[i];
        const bool wasTree = was.region[i] == kRegionTree || was.region[i] == kRegionChopping;
        const bool isTree = region == kRegionTree || region == kRegionChopping;
        if (was.tile[i] == kStumpT && id != kStumpT) {  // a stump grew: exactly the three writes, id in the editor range
            ++grown;
            bad += !(id >= kTreeBaseT && id <= kSolidT && sq == (was.sq[i] | 0x0080) && region == kRegionTree);
        } else if (wasTree) {  // an existing tree may only get another tree id
            bad += !(isTree && sq == was.sq[i] && region == was.region[i] && id >= kTreeBaseT && id < kTreeBaseT + kForestTreeTiles &&
                     id != kStumpT && (id == was.tile[i] || id <= kSolidT));
        } else {  // everything else, never-forest land included, is untouched
            bad += !(id == was.tile[i] && sq == was.sq[i] && region == was.region[i]);
        }
        if (isTree) bad += !(sq & 0x0080);
    }
    CHECK(bad == 0, "%s: %d tile(s) break the regrowth rules", what, bad);
    return grown;
}

// Fake unit types used by the scenarios.
constexpr uint8_t kFootman = 0, kGrunt = 1, kOgre = 7, kSkeleton = 0x37, kPeon = 3, kDragon = 0x2B, kDaemon = 0x38;

// ---- [spell_cost] / [spell_damage] / [mana] regen (src/spells.cpp, docs/research/spells.md) ----
// The bytes below are typed in from the research disassembly, not taken from spells.cpp, so a wrong RVA or a wrong
// original in either place fails here.
struct SpellSite {
    uint32_t rva;
    int len;
    uint8_t bytes[16];
};
enum { kSiteFireball, kSiteMarker, kSiteFlame, kSiteBlizzard, kSiteDecay, kSiteWhirlwind, kSiteCoil0, kSiteCoil1, kSiteCoil2,
       kSiteCoil3, kSiteCoil4, kSiteRunesKill, kSiteRunesSub, kSiteHealCap, kSiteRegen0, kSiteRegen1, kSiteRegen2, kSpellSiteCount };
static const SpellSite kSpellSites[kSpellSiteCount] = {
    {kRvaFireballDamageInsn, 2, {0xB0, 0x28}},                                                     // mov al, 0x28
    {kRvaFireballMarker, 16, {0xB8, 0x28, 0x00, 0x00, 0x00, 0x38, 0x47, 0x37, 0xB9, 0x19, 0x00, 0x00, 0x00, 0x0F, 0x44, 0xC8}},
    {kRvaFlameShieldDamageInsn, 4, {0xC6, 0x40, 0x37, 0x04}},                                      // mov byte [eax+0x37], 4
    {kRvaBlizzardDamageInsn, 4, {0xC6, 0x47, 0x37, 0x0A}},                                         // mov byte [edi+0x37], 10
    {kRvaDeathAndDecayDamageInsn, 4, {0xC6, 0x46, 0x37, 0x0A}},                                    // mov byte [esi+0x37], 10
    {kRvaWhirlwindDamageInsn, 4, {0xC6, 0x46, 0x37, 0x04}},                                        // mov byte [esi+0x37], 4
    {kRvaDeathCoilBudgetInsns[0], 3, {0x83, 0xF8, 0x32}},                                          // cmp eax, 50
    {kRvaDeathCoilBudgetInsns[1], 3, {0x83, 0xFB, 0x32}},                                          // cmp ebx, 50
    {kRvaDeathCoilBudgetInsns[2], 3, {0x83, 0xF8, 0x32}},                                          // cmp eax, 50
    {kRvaDeathCoilBudgetInsns[3], 5, {0xB8, 0x32, 0x00, 0x00, 0x00}},                              // mov eax, 50
    {kRvaDeathCoilBudgetInsns[4], 5, {0xBB, 0x32, 0x00, 0x00, 0x00}},                              // mov ebx, 50
    {kRvaRunesDamageInsn, 5, {0xB9, 0x32, 0x00, 0x00, 0x00}},                                      // mov ecx, 50
    {kRvaRunesSubtractInsn, 3, {0x83, 0xC0, 0xCE}},                                                // add eax, -50
    {kRvaHealCapInsn, 5, {0xB8, 0x28, 0x00, 0x00, 0x00}},                                          // mov eax, 40
    {kRvaManaRegenReloadInsn, 4, {0xC6, 0x46, 0x74, 0x28}},                                        // mov byte [esi+0x74], 40
    {kRvaManaRegenCreateInsn, 4, {0xC6, 0x46, 0x74, 0x28}},
    {kRvaManaRegenConvertInsn, 4, {0xC6, 0x42, 0x74, 0x28}},
};
static const uint16_t kGameSpellCosts[19] = {70, 5, 5, 4, 80, 100, 50, 200, 200, 25, 70, 60, 50, 100, 100, 50, 100, 200, 30};  // orders 0x26..0x38

static bool SiteIs(int site, const uint8_t* bytes) {
    return memcmp(At<uint8_t>(kSpellSites[site].rva), bytes, kSpellSites[site].len) == 0;
}
static bool SiteIsGame(int site) { return SiteIs(site, kSpellSites[site].bytes); }
static bool AllSitesAreGame() {
    for (int i = 0; i < kSpellSiteCount; ++i)
        if (!SiteIsGame(i)) return false;
    return true;
}
static bool CostsAreGame() { return memcmp(At<uint16_t>(kRvaManaCostByOrder) + 0x26, kGameSpellCosts, sizeof(kGameSpellCosts)) == 0; }
static void ResetSpellConfig() {
    config::g.spellCostAll = config::g.spellDamageAll = config::g.manaRegen = 1.0;
    for (int& v : config::g.spellCost) v = -1;
    for (int& v : config::g.spellDamage) v = -1;
}

// The expected bytes of every damage site for one set of numbers (fireball, flame, blizzard, decay, whirlwind, coil,
// runes, heal cap). A number equal to the game's must give the game's bytes.
static bool DamageSitesAre(int fireball, int flame, int blizzard, int decay, int whirlwind, int coil, int runes, int healCap) {
    auto one = [](int site, int at, int value) {
        uint8_t b[16];
        memcpy(b, kSpellSites[site].bytes, sizeof(b));
        b[at] = static_cast<uint8_t>(value);
        return SiteIs(site, b);
    };
    bool ok = one(kSiteFireball, 1, fireball);
    if (fireball == 40) {
        ok = ok && SiteIsGame(kSiteMarker);
    } else {  // mov al, D / cmp [edi+0x37], al / push 0x19 / pop ecx / jne +3 / push 0x28 / pop ecx / nop x3
        const uint8_t marker[16] = {0xB0, static_cast<uint8_t>(fireball), 0x38, 0x47, 0x37, 0x6A, 0x19, 0x59, 0x75, 0x03, 0x6A, 0x28, 0x59, 0x90, 0x90, 0x90};
        ok = ok && SiteIs(kSiteMarker, marker);
    }
    ok = ok && one(kSiteFlame, 3, flame) && one(kSiteBlizzard, 3, blizzard) && one(kSiteDecay, 3, decay) && one(kSiteWhirlwind, 3, whirlwind);
    ok = ok && one(kSiteCoil0, 2, coil) && one(kSiteCoil1, 2, coil) && one(kSiteCoil2, 2, coil) && one(kSiteCoil3, 1, coil) && one(kSiteCoil4, 1, coil);
    ok = ok && one(kSiteRunesKill, 1, runes) && one(kSiteRunesSub, 2, -runes);
    return ok && one(kSiteHealCap, 1, healCap);
}
static bool RegenSitesAre(int interval) {
    for (int site = kSiteRegen0; site <= kSiteRegen2; ++site) {
        uint8_t b[16];
        memcpy(b, kSpellSites[site].bytes, sizeof(b));
        b[3] = static_cast<uint8_t>(interval);
        if (!SiteIs(site, b)) return false;
    }
    return true;
}
// Costs by order id 0x26..0x38 (0x28 is the unused slot, always 5).
static bool CostsAre(const uint16_t (&want)[19]) { return memcmp(At<uint16_t>(kRvaManaCostByOrder) + 0x26, want, sizeof(want)) == 0; }
static void PrintCosts(const char* what) {
    const uint16_t* t = At<uint16_t>(kRvaManaCostByOrder) + 0x26;
    printf("  %s costs 0x26..0x38:", what);
    for (int i = 0; i < 19; ++i) printf(" %u", t[i]);
    printf("\n");
}
static void PokeCode(uint32_t rva, uint8_t value) {
    DWORD old;
    VirtualProtect(At<uint8_t>(rva), 1, PAGE_EXECUTE_READWRITE, &old);
    *At<uint8_t>(rva) = value;
    VirtualProtect(At<uint8_t>(rva), 1, old, &old);
}
static bool LogContains(const wchar_t* dir, const char* text) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\gameplay_options.log", dir);
    FILE* f = _wfsopen(path, L"rb", _SH_DENYNO);  // the log is still open for writing; _wfopen_s would not share it
    if (!f) return false;
    static char buf[1 << 20];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, text) != nullptr;
}
// How many lines of the log hold `text` (the throttled production diagnostic is counted with this).
static int LogCount(const wchar_t* dir, const char* text) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\gameplay_options.log", dir);
    FILE* f = _wfsopen(path, L"rb", _SH_DENYNO);
    if (!f) return 0;
    static char buf[1 << 20];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    int hits = 0;
    for (const char* p = strstr(buf, text); p; p = strstr(p + 1, text)) ++hits;
    return hits;
}

// ---------------------------------------------------------------------------------------------------------------
// [general] log_ai: the read-only computer-player diagnostic (src/aiwatch.cpp, docs/research/ai_stall.md).
// A fake ai.bin blob and fake AI state blocks are written into the mapped image's own globals, so the decoding, the
// range check and the two timers are exercised without any of the game running.
// ---------------------------------------------------------------------------------------------------------------

static char g_aiLines[8][512];
static int g_aiLineCount = 0;
static int g_aiStalls = 0;

static void AiSink(const char* line) {
    if (g_aiLineCount < 8) strcpy_s(g_aiLines[g_aiLineCount], sizeof g_aiLines[0], line);
    ++g_aiLineCount;
    if (strstr(line, "has been on")) {
        strcpy_s(g_aiLines[7], sizeof g_aiLines[0], line);  // the newest stall line, whatever the report count
        ++g_aiStalls;
    }
}

static uint8_t g_aiBlob[256];

// Points player `p`'s script at blob offset `off` and fills the fields the line prints.
static void AiScript(int p, uint32_t off, uint8_t peasantTarget, uint8_t landSize, uint8_t landCount) {
    uint8_t* st = At<uint8_t>(kRvaAiState) + p * kAiStateStride;
    memset(st, 0, kAiStateStride);
    *reinterpret_cast<const uint8_t**>(st + kAiOffPc) = g_aiBlob + off;
    st[kAiOffPeasantTarget] = peasantTarget;
    st[kAiOffLandWaveSize] = landSize;
    st[kAiOffLandWaveCount] = landCount;
    st[kAiOffFootTarget] = 6;
    st[kAiOffArcherTarget] = 3;
    st[kAiOffSiegeTarget] = 0;
    st[kAiOffKnightTarget] = 4;
    st[kAiOffBuildListLen] = 13;
}

static void AiReset() {
    g_aiLineCount = 0;
    g_aiStalls = 0;
    aiwatch::ResetForTests();
}

// One tick of `ms` milliseconds of play through the real entry point.
static void AiTick(unsigned ms) {
    World w;
    if (!BuildWorld(w)) {
        CHECK(false, "aiwatch test: BuildWorld failed");
        return;
    }
    aiwatch::OnTick(w, ms);
}

static void AiWatchTests() {
    const bool savedLogAi = config::g.logAi;
    aiwatch::SetSinkForTests(&AiSink);

    memset(g_aiBlob, 0, sizeof g_aiBlob);
    g_aiBlob[0x10] = kAiOpWaitFor;  g_aiBlob[0x11] = 2;      // WAITFOR have_castle
    g_aiBlob[0x20] = kAiOpWaitFor;  g_aiBlob[0x21] = 4;      // WAITFOR landForce >= count * size
    g_aiBlob[0x30] = kAiOpSleep;    *reinterpret_cast<uint32_t*>(g_aiBlob + 0x31) = 8000;
    g_aiBlob[0x40] = kAiOpSet;      g_aiBlob[0x41] = 0x0D; g_aiBlob[0x42] = 6;
    g_aiBlob[0x50] = kAiOpJump;     *reinterpret_cast<uint16_t*>(g_aiBlob + 0x51) = 0x0020;
    g_aiBlob[0x60] = kAiOpWaitFor;  g_aiBlob[0x61] = 99;     // condition the report does not know
    g_aiBlob[0x70] = 77;                                     // opcode the report does not know

    *At<const uint8_t*>(kRvaAiScriptBlob) = g_aiBlob;
    *At<uint32_t>(kRvaAiScriptBlobSize) = sizeof g_aiBlob;
    *At<uint16_t>(kRvaGameFromSave) = 0;
    memset(At<uint8_t>(kRvaController), 0, kMaxPlayers);
    memset(At<uint8_t>(kRvaAiBuildDone), 0, kAiPlayerCount * kAiBuildListMax);
    memset(At<uint8_t>(kRvaAiScriptId), 0, kMaxPlayers);
    At<uint8_t>(kRvaController)[3] = 1;  // player 3 is the computer
    At<uint8_t>(kRvaAiScriptId)[3] = 41;
    At<int32_t>(kRvaPlayerGold)[3] = 3750;
    At<int32_t>(kRvaPlayerLumber)[3] = 1000;
    At<int32_t>(kRvaPlayerOil)[3] = 4700;
    At<uint16_t>(kRvaFoodSupply)[3] = 60;
    At<uint16_t>(kRvaUnitsCounted)[3] = 24;
    At<uint16_t>(kRvaFoodFreeUnits)[3] = 0;
    At<uint16_t>(kRvaLandForce)[3] = 13;
    At<uint16_t>(kRvaSeaForce)[3] = 0;
    At<uint16_t>(kRvaAirForce)[3] = 0;
    At<uint16_t>(kRvaAiFootCount)[3] = 6;
    At<uint16_t>(kRvaAiArcherCount)[3] = 3;
    At<uint16_t>(kRvaAiSiegeCount)[3] = 0;
    At<uint16_t>(kRvaAiKnightCount)[3] = 4;
    At<uint16_t>(kRvaPeasantCount)[3] = 8;
    for (int i = 0; i < 9; ++i) At<uint8_t>(kRvaAiBuildDone)[3 * kAiBuildListMax + i] = 1;  // buildlist 9/13
    At<uint16_t>(kRvaAiGoldWorkers)[3] = 4;  // the worker-job counters, one of them wrapped by a load
    At<uint16_t>(kRvaAiLumberWorkers)[3] = 0xFFFF;
    At<uint16_t>(kRvaAiRepairWorkers)[3] = 1;

    ResetWorld();
    AddUnit(kTypeMage, 0, 10, 10, 60, 255, kOrderStand);  // the human, so BuildWorld succeeds
    AddUnit(kGrunt, 3, 20, 20, 60, 0, kOrderStand);       // the computer still owns a unit
    AiScript(3, 0x10, 8, 5, 1);

    // The expected line, verbatim.
    const char* kExpected =
        "ai: player 3 script 41 pc 0x0010 WAITFOR have_castle same pc for 1m | gold 3750 lum 1000 oil 4700 | "
        "food 24/60 | force land 13 sea 0 air 0 | foot 6/6 arch 3/3 siege 0/0 knight 4/4 | workers 8/8 | "
        "buildlist 9/13 | jobs gold 4 lum 65535 rep 1";

    // 1. Nothing before a minute of play, exactly one line at the minute, with the expected text.
    config::g.logAi = true;
    AiReset();
    for (int i = 0; i < 59; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "log_ai must not write before a minute of play (%d line(s))", g_aiLineCount);
    AiTick(1000);
    CHECK(g_aiLineCount == 1, "log_ai must write one line per computer player per minute (%d)", g_aiLineCount);
    CHECK(g_aiLineCount == 1 && strcmp(g_aiLines[0], kExpected) == 0, "log_ai line text\n  want: %s\n  got:  %s",
          kExpected, g_aiLineCount ? g_aiLines[0] : "(none)");

    // 2. log_ai = false writes nothing at all.
    config::g.logAi = false;
    AiReset();
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "log_ai = false must log nothing (%d line(s))", g_aiLineCount);
    config::g.logAi = true;

    // 3. A program counter outside the blob is refused: no line, and nothing is read through it.
    AiReset();
    uint8_t* st = At<uint8_t>(kRvaAiState) + 3 * kAiStateStride;
    *reinterpret_cast<const uint8_t**>(st + kAiOffPc) = g_aiBlob - 1;
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "a pc below the blob must produce no line (%d)", g_aiLineCount);
    AiReset();
    // The last kAiMaxInstructionSize bytes cannot hold a whole instruction either.
    *reinterpret_cast<const uint8_t**>(st + kAiOffPc) = g_aiBlob + sizeof g_aiBlob - 2;
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "a pc without room for an instruction must produce no line (%d)", g_aiLineCount);
    AiReset();
    *reinterpret_cast<const uint8_t**>(st + kAiOffPc) = nullptr;
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "a null pc must produce no line (%d)", g_aiLineCount);

    // 4. A blob the game has not loaded, and an unbelievable size, are both refused.
    AiScript(3, 0x10, 8, 5, 1);
    AiReset();
    *At<const uint8_t*>(kRvaAiScriptBlob) = nullptr;
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "no script blob must produce no line (%d)", g_aiLineCount);
    *At<const uint8_t*>(kRvaAiScriptBlob) = g_aiBlob;
    AiReset();
    *At<uint32_t>(kRvaAiScriptBlobSize) = 0x7FFFFFFF;
    for (int i = 0; i < 120; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "an out-of-range blob size must produce no line (%d)", g_aiLineCount);
    *At<uint32_t>(kRvaAiScriptBlobSize) = sizeof g_aiBlob;

    // 5. The stall line: after five minutes on the same instruction, not before, and then every five minutes.
    AiScript(3, 0x10, 8, 5, 1);
    AiReset();
    for (int i = 0; i < 299; ++i) AiTick(1000);
    CHECK(g_aiStalls == 0, "no stall line before five minutes (%d)", g_aiStalls);
    AiTick(1000);
    CHECK(g_aiStalls == 1, "one stall line at five minutes (%d)", g_aiStalls);
    CHECK(strcmp(g_aiLines[7], "ai: player 3 has been on WAITFOR have_castle for 5 min") == 0,
          "stall line text: %s", g_aiLines[7]);
    for (int i = 0; i < 299; ++i) AiTick(1000);  // five minutes minus one step later
    CHECK(g_aiStalls == 1, "the stall line must not repeat before another five minutes (%d)", g_aiStalls);
    AiTick(1000);
    CHECK(g_aiStalls == 2, "the stall line repeats every five minutes (%d)", g_aiStalls);
    CHECK(strcmp(g_aiLines[7], "ai: player 3 has been on WAITFOR have_castle for 10 min") == 0,
          "second stall line text: %s", g_aiLines[7]);

    // 6. Moving the program counter clears the stall timer.
    AiReset();
    for (int i = 0; i < 299; ++i) AiTick(1000);
    AiScript(3, 0x20, 8, 5, 1);  // the script moved on
    for (int i = 0; i < 299; ++i) AiTick(1000);
    CHECK(g_aiStalls == 0, "a moving pc must not report a stall (%d)", g_aiStalls);

    // 6a. A SLEEP counting down is not a stall: the wait word is above the 1 a failed WAITFOR leaves, the program
    // counter already points at the next instruction, and the stall clock only starts once the script is awake.
    {
        AiScript(3, 0x10, 8, 5, 1);
        uint32_t steps = 9000;
        memcpy(At<uint8_t>(kRvaAiState) + 3 * kAiStateStride + kAiOffWait, &steps, sizeof steps);
        AiReset();
        for (int i = 0; i < 400; ++i) AiTick(1000);
        CHECK(g_aiStalls == 0, "a sleeping script must not be reported as stalled (%d)", g_aiStalls);
        CHECK(g_aiLineCount >= 1 && strstr(g_aiLines[0], "sleeping 9000 steps, then WAITFOR have_castle same pc for 0m"),
              "status line of a sleeping script: %s", g_aiLineCount ? g_aiLines[0] : "(no line)");
        steps = 1;  // the sleep ran out, the WAITFOR now fails every step
        memcpy(At<uint8_t>(kRvaAiState) + 3 * kAiStateStride + kAiOffWait, &steps, sizeof steps);
        for (int i = 0; i < 299; ++i) AiTick(1000);
        CHECK(g_aiStalls == 0, "the stall clock starts when the sleep ends, not before (%d)", g_aiStalls);
        AiTick(1000);
        CHECK(g_aiStalls == 1, "five minutes awake on the same WAITFOR is a stall (%d)", g_aiStalls);
    }

    // 7. Every opcode and condition decodes, and unknown ones print their number instead of a guess.
    struct { uint32_t off; uint8_t size, count; const char* want; } kOps[] = {
        {0x20, 6, 2, "WAITFOR landForce >= 12"},
        {0x30, 0, 0, "SLEEP 8000"},
        {0x40, 0, 0, "SET st[0x0D] = 6"},
        {0x50, 0, 0, "JUMP 0x0020"},
        {0x60, 0, 0, "WAITFOR cond 99"},
        {0x70, 0, 0, "op 77"},
    };
    for (const auto& k : kOps) {
        AiScript(3, k.off, 8, k.size, k.count);
        AiReset();
        for (int i = 0; i < 60; ++i) AiTick(1000);
        CHECK(g_aiLineCount == 1 && strstr(g_aiLines[0], k.want) != nullptr,
              "decoding %s from offset 0x%02X: %s", k.want, k.off, g_aiLineCount ? g_aiLines[0] : "(no line)");
    }

    // 8. A computer player with no live unit left is not reported; a human player never is.
    AiScript(3, 0x10, 8, 5, 1);
    ResetWorld();
    AddUnit(kTypeMage, 0, 10, 10, 60, 255, kOrderStand);
    AiReset();
    for (int i = 0; i < 60; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "a computer with no unit left must not be reported (%d)", g_aiLineCount);
    At<uint8_t>(kRvaController)[3] = 0;
    ResetWorld();
    AddUnit(kTypeMage, 0, 10, 10, 60, 255, kOrderStand);
    AddUnit(kGrunt, 3, 20, 20, 60, 0, kOrderStand);
    AiReset();
    for (int i = 0; i < 60; ++i) AiTick(1000);
    CHECK(g_aiLineCount == 0, "a human player must never be reported (%d)", g_aiLineCount);

    // Leave the image as the other tests expect it.
    At<uint8_t>(kRvaController)[3] = 0;
    memset(At<uint8_t>(kRvaAiState) + 3 * kAiStateStride, 0, kAiStateStride);
    *At<const uint8_t*>(kRvaAiScriptBlob) = nullptr;
    *At<uint32_t>(kRvaAiScriptBlobSize) = 0;
    aiwatch::SetSinkForTests(nullptr);
    aiwatch::ResetForTests();
    config::g.logAi = savedLogAi;
    ResetWorld();
}

// ---------------------------------------------------------------------------------------------------------------
// [general] fix_ai_after_load (src/aijobs.cpp, docs/research/ai_lumber.md): a savegame load zeroes the computer's
// worker-job counters but keeps the job bits, and the first release wraps a counter to 65535. The fix sets every
// counter back to the number of units carrying its bit.
// ---------------------------------------------------------------------------------------------------------------

static char g_jobLines[4][768];
static int g_jobLineCount = 0;

static void JobSink(const char* line) {
    if (g_jobLineCount < 4) strcpy_s(g_jobLines[g_jobLineCount], sizeof g_jobLines[0], line);
    ++g_jobLineCount;
}

static Unit* AddWorker(uint8_t owner, uint16_t job, uint16_t kind, uint16_t state) {
    Unit* u = AddUnit(kPeon, owner, 20 + g_unitCount % 40, 20 + g_unitCount / 40, 30, 0, kOrderStand);
    Field<uint16_t>(u, kOffAiJob) = job;
    Field<uint16_t>(u, kOffAiBuildKind) = kind;
    Field<uint16_t>(u, kOffStateFlags) = state;
    return u;
}

static uint16_t* JobGold() { return At<uint16_t>(kRvaAiGoldWorkers); }
static uint16_t* JobLumber() { return At<uint16_t>(kRvaAiLumberWorkers); }
static uint16_t* JobRepair() { return At<uint16_t>(kRvaAiRepairWorkers); }
static uint16_t* JobBuild(int p) { return At<uint16_t>(kRvaAiBuilders) + p * kAiBuildKinds; }

static void ClearJobCounters() {
    memset(JobGold(), 0, kMaxPlayers * 2);
    memset(JobLumber(), 0, kMaxPlayers * 2);
    memset(JobRepair(), 0, kMaxPlayers * 2);
    memset(JobBuild(0), 0, kMaxPlayers * kAiBuildKinds * 2);
}

// Player 3's counters as a savegame load leaves them after the first lumber worker delivered: everything zeroed,
// then the lumber counter and one builder entry taken below zero.
static void WrapJobCounters() {
    ClearJobCounters();
    JobLumber()[3] = 0xFFFF;
    JobBuild(3)[5] = 0xFFFF;
    JobGold()[0] = 7;  // the human's words: never read by the game, must never be touched
    JobLumber()[0] = 9;
}

static void JobTick() {
    World w;
    if (!BuildWorld(w)) {
        CHECK(false, "aijobs test: BuildWorld failed");
        return;
    }
    aijobs::OnTick(w);
}

static bool JobsAreFixed() {
    const uint16_t* b = JobBuild(3);
    int others = 0;
    for (int k = 0; k < kAiBuildKinds; ++k)
        if (k != 0 && k != 5) others += b[k];
    return JobGold()[3] == 2 && JobLumber()[3] == 3 && JobRepair()[3] == 1 && b[0] == 1 && b[5] == 1 && others == 0;
}

static void AiJobsTests() {
    const bool savedFix = config::g.fixAiAfterLoad;
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    const uint32_t savedPeon = tf[kPeon], savedGrunt = tf[kGrunt];
    tf[kPeon] = kTfFleshy | kTfWorker;
    tf[kGrunt] = kTfFleshy | kTfAttacker;
    memset(At<uint8_t>(kRvaController), 0, kMaxPlayers);
    At<uint8_t>(kRvaController)[3] = 1;  // player 3 is the computer, player 0 the human
    aijobs::SetSinkForTests(&JobSink);

    ResetWorld();
    AddUnit(kTypeMage, 0, 10, 10, 60, 255, kOrderStand);  // the human, so BuildWorld succeeds
    AddWorker(3, kAiJobGold, 0, 0);
    AddWorker(3, kAiJobGold, 0, 0x08);          // inside the mine: the removal path still releases it, so it counts
    AddWorker(3, kAiJobLumber, 0, 0);
    AddWorker(3, kAiJobLumber, 0, 0x80);        // other state bits above 7 do not matter either
    AddWorker(3, kAiJobRepair, 0, 0);
    AddWorker(3, kAiJobBuild, 5, 0);            // builder of kind 5
    AddWorker(3, kAiJobBuildFarm, 0, 0);        // farm builder, kind 0
    AddWorker(3, kAiJobLumber, 0, kStateDying); // dying: already released by FUN_004ee380, not counted
    AddWorker(3, kAiJobLumber | kAiJobBuild, kAiBuildKinds, 0);  // lumber counts, a kind past the row does not
    AddWorker(3, 0, 5, 0);                      // no job at all
    Unit* grunt = AddUnit(kGrunt, 3, 50, 50, 60, 0, kOrderStand);
    Field<uint16_t>(grunt, kOffAiJob) = kAiJobLumber;  // not a worker type: the game never releases it
    AddWorker(0, kAiJobGold | kAiJobLumber, 0, 0);     // the human's peon: not the computer's business

    // 1. The wrapped counters are put back, the human's words stay, and one line says what changed.
    config::g.fixAiAfterLoad = true;
    g_jobLineCount = 0;
    WrapJobCounters();
    JobTick();
    CHECK(JobsAreFixed(), "recount after load: gold %u lumber %u repair %u build0 %u build5 %u", JobGold()[3],
          JobLumber()[3], JobRepair()[3], JobBuild(3)[0], JobBuild(3)[5]);
    CHECK(JobGold()[0] == 7 && JobLumber()[0] == 9, "the human player's counters must not be touched (%u %u)",
          JobGold()[0], JobLumber()[0]);
    const char* kWant =
        "ai: recounted workers after load: player 3 gold 0 lumber 65535 repair 0 build 65535 -> gold 2 lumber 3 "
        "repair 1 build 2";
    CHECK(g_jobLineCount == 1 && strcmp(g_jobLines[0], kWant) == 0, "recount line\n  want: %s\n  got:  %s", kWant,
          g_jobLineCount ? g_jobLines[0] : "(none)");

    // 2. Idempotent: the next steps find nothing to do and say nothing.
    for (int i = 0; i < 10; ++i) JobTick();
    CHECK(JobsAreFixed() && g_jobLineCount == 1, "a second pass must change nothing (%d lines)", g_jobLineCount);

    // 3. A fresh map, where the counters already match the units: not one write, not one line.
    g_jobLineCount = 0;
    for (int i = 0; i < 10; ++i) JobTick();
    CHECK(JobsAreFixed() && g_jobLineCount == 0, "matching counters must be left alone (%d lines)", g_jobLineCount);

    // 4. The off switch leaves the wrapped counters as the game has them.
    config::g.fixAiAfterLoad = false;
    WrapJobCounters();
    for (int i = 0; i < 10; ++i) JobTick();
    CHECK(JobLumber()[3] == 0xFFFF && JobBuild(3)[5] == 0xFFFF && JobGold()[3] == 0 && g_jobLineCount == 0,
          "fix_ai_after_load = false must not write (lumber %u, %d lines)", JobLumber()[3], g_jobLineCount);
    config::g.fixAiAfterLoad = true;

    // 5. Multiplayer: the real entry point never gets as far as the recount.
    WrapJobCounters();
    *At<uint32_t>(kRvaNetGame) = 1;
    for (int i = 0; i < 10; ++i) mod::OnTick();
    *At<uint32_t>(kRvaNetGame) = 0;
    CHECK(JobLumber()[3] == 0xFFFF && JobGold()[3] == 0 && g_jobLineCount == 0,
          "a network game must not be touched (lumber %u, %d lines)", JobLumber()[3], g_jobLineCount);

    // 6. ...and in single player the same entry point does run it.
    mod::OnTick();
    CHECK(JobsAreFixed() && g_jobLineCount == 1, "mod::OnTick must run the recount in single player (lumber %u)",
          JobLumber()[3]);

    // 7. A human player's slot is never recounted, even with job bits on its units.
    At<uint8_t>(kRvaController)[3] = 0;
    WrapJobCounters();
    g_jobLineCount = 0;
    JobTick();
    CHECK(JobLumber()[3] == 0xFFFF && g_jobLineCount == 0, "a human slot must not be recounted (lumber %u)",
          JobLumber()[3]);

    // 8. A drift that comes back on every step is logged 20 times, then once more to say it stops logging.
    At<uint8_t>(kRvaController)[3] = 1;
    g_jobLineCount = 0;
    for (int i = 0; i < 30; ++i) {
        JobLumber()[3] = 0xFFFF;
        JobTick();
    }
    CHECK(g_jobLineCount == 21 && JobsAreFixed(), "a repeating drift must stop logging after 20 lines (%d)",
          g_jobLineCount);

    // The log_ai line shows the three counters.
    // (checked in AiWatchTests through the expected line)

    ClearJobCounters();
    memset(At<uint8_t>(kRvaController), 0, kMaxPlayers);
    tf[kPeon] = savedPeon;
    tf[kGrunt] = savedGrunt;
    aijobs::SetSinkForTests(nullptr);
    config::g.fixAiAfterLoad = savedFix;
    ResetWorld();
}

// ---------------------------------------------------------------------------------------------------------------
// [farms] auto_build (src/farms.cpp, docs/research/farms.md). The site comes from the game's OWN code: the computer's
// search FUN_004dbc30 and the player's placement test FUN_004dc210 run for real on the mapped image, over a small
// hand-made map. Only IssueOrder is the fake one.
// ---------------------------------------------------------------------------------------------------------------

constexpr uint32_t kRvaPlayerUnitList = 0x534848;  // Unit*[16], chained through +0x68 (FUN_004ed030); read by FUN_004dbc50
constexpr uint32_t kRvaTileSeenBits = 0x51AD64;    // uint8*: per-tile player bits read by FUN_004b4a50 for a human's site
constexpr uint8_t kPeasant = 2, kHall = 0x4A, kFarmType = 0x3A;
constexpr int kHallX = 20, kHallY = 20;

static uint16_t g_farmRegion[kMap * kMap], g_farmSq[kMap * kMap];
static uint8_t g_farmExplored[kMap * kMap], g_farmSeen[kMap * kMap];

struct FarmSaved {
    uint16_t* region; uint16_t* sq; uint8_t* explored; uint8_t* seen;
    uint32_t tfPeasant, tfHall, tfFarm, allowed;
    uint8_t mask;
};

static void FarmFood(int supply, int used) {
    At<uint16_t>(kRvaFoodSupply)[0] = static_cast<uint16_t>(supply);
    At<uint16_t>(kRvaUnitsCounted)[0] = static_cast<uint16_t>(used);
    At<uint16_t>(kRvaFoodFreeUnits)[0] = 0;
    At<uint16_t>(kRvaUnitsInTraining)[0] = 0;
}

// Links every unit into its owner's list, as FUN_004ed030 does after a load.
static void FarmLinkLists() {
    Unit** heads = At<Unit*>(kRvaPlayerUnitList);
    memset(heads, 0, 16 * sizeof(Unit*));
    for (int i = 0; i < g_unitCount; ++i) {
        Unit* u = reinterpret_cast<Unit*>(g_units + i * kUnitSize);
        Field<Unit*>(u, 0x68) = heads[OwnerOf(u)];
        heads[OwnerOf(u)] = u;
    }
}

// A hall at 20,20 (4x4, its squares marked as a building) and nothing else; every tile explored, one region.
static Unit* FarmWorld() {
    ResetWorld();
    for (auto& r : g_farmRegion) r = 1;
    memset(g_farmSq, 0, sizeof g_farmSq);
    memset(g_farmExplored, 0, sizeof g_farmExplored);
    memset(g_farmSeen, 0, sizeof g_farmSeen);
    for (int y = kHallY; y < kHallY + 4; ++y)
        for (int x = kHallX; x < kHallX + 4; ++x) g_farmSq[y * kMap + x] = 0x800;
    Unit* hall = AddUnit(kHall, 0, kHallX, kHallY, 1200, 0, 0);
    Field<uint16_t>(hall, kOffStateFlags) = kStateComplete;
    At<int32_t>(kRvaPlayerGold)[0] = 2000;
    At<int32_t>(kRvaPlayerLumber)[0] = 1000;
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    FarmFood(20, 18);
    farms::ResetForTests();
    return hall;
}

static Unit* FarmPeasant(int x, int y, uint8_t order) {
    Unit* u = AddUnit(kPeasant, 0, x, y, 30, 0, order);
    g_farmSq[y * kMap + x] |= 0x100;
    return u;
}

// One pass through the module (it runs once a second of play).
static void FarmPass() {
    FarmLinkLists();
    World w;
    if (!BuildWorld(w)) {
        CHECK(false, "farms test: BuildWorld failed");
        return;
    }
    farms::OnTick(w, 1000);
}

static int FarmOrders() {
    int n = 0;
    for (int i = 0; i < g_unitCount; ++i) {
        Unit* u = reinterpret_cast<Unit*>(g_units + i * kUnitSize);
        if (Field<uint8_t>(u, kOffNextOrder) == kOrderBuild) ++n;
    }
    return n;
}

static void FarmTests() {
    // 1. The trigger: the HIGHER of free_min and free_percent of the supply, rounded up.
    struct { int supply, used; bool want; } kCases[] = {
        {20, 16, true}, {20, 15, false},    // 10 % of 20 = 2: 4 is higher
        {30, 26, true}, {30, 25, false},    // 10 % of 30 = 3: 4 is higher
        {41, 36, true}, {41, 35, false},    // 10 % of 41 = 4.1, rounded up to 5
        {60, 54, true}, {60, 53, false},    // 10 % of 60 = 6 is higher than 4
        {150, 135, true}, {150, 134, false},
        {199, 199, true}, {200, 200, false}, {220, 219, false},  // never at 200 supply
    };
    for (const auto& k : kCases)
        CHECK(farms::ShouldBuild(k.supply, k.used, 0, 4, 10) == k.want, "farm trigger at supply %d used %d must be %d",
              k.supply, k.used, k.want);
    CHECK(farms::ShouldBuild(60, 50, 4, 4, 10) && !farms::ShouldBuild(60, 50, 3, 4, 10),
          "units in training count as used food");
    CHECK(farms::ShouldBuild(20, 18, 0, 0, 10) && !farms::ShouldBuild(20, 17, 0, 0, 10), "free_min 0: the percent alone");
    CHECK(farms::ShouldBuild(100, 96, 0, 4, 0) && !farms::ShouldBuild(100, 95, 0, 4, 0), "free_percent 0: free_min alone");

    const bool savedAuto = config::g.farmsAutoBuild, savedLog = config::g.logCasts;
    const int savedMin = config::g.farmsFreeMin, savedPercent = config::g.farmsFreePercent;
    const int savedClearance = config::g.farmsMineClearance;
    const FarmWorkers savedWorkers = config::g.farmsWorkers;
    config::g.farmsMineClearance = 3;
    config::g.farmsWorkers = FarmWorkers::IdleThenLumber;
    FarmSaved s = {*At<uint16_t*>(kRvaRegionMap), *At<uint16_t*>(kRvaSquareFlags), *At<uint8_t*>(kRvaExploredMap),
                   *At<uint8_t*>(kRvaTileSeenBits), 0, 0, 0, At<uint32_t>(kRvaUnitsAllowed)[0],
                   *At<uint8_t>(kRvaBuildPlayerMask)};
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    s.tfPeasant = tf[kPeasant]; s.tfHall = tf[kHall]; s.tfFarm = tf[kFarmType];
    tf[kPeasant] = kTfFleshy | kTfWorker;
    tf[kHall] = kTfBuilding | 0x1000;  // town hall: a depot, what FUN_004dbc50 looks for
    tf[kFarmType] = kTfBuilding;
    struct Sz { uint16_t w, h; };
    At<Sz>(kRvaUnitSizeByType)[kHall] = {4, 4};
    At<Sz>(kRvaUnitSizeByType)[kFarmType] = {2, 2};
    At<uint8_t>(kRvaGoldCostByType)[kFarmType] = 50;    // 500 gold
    At<uint8_t>(kRvaLumberCostByType)[kFarmType] = 25;  // 250 lumber
    At<uint8_t>(kRvaOilCostByType)[kFarmType] = 0;
    *At<uint16_t*>(kRvaRegionMap) = g_farmRegion;
    *At<uint16_t*>(kRvaSquareFlags) = g_farmSq;
    *At<uint8_t*>(kRvaExploredMap) = g_farmExplored;
    *At<uint8_t*>(kRvaTileSeenBits) = g_farmSeen;
    *At<uint16_t>(kRvaMapSize) = kMap;
    *At<uint8_t>(kRvaLocalPlayer) = 0;
    At<uint8_t>(kRvaController)[0] = 0;
    At<uint32_t>(kRvaUnitsAllowed)[0] = s.allowed | kAllowFarm;
    *At<uint8_t>(kRvaBuildPlayerMask) = 0xFF;
    config::g.farmsAutoBuild = true;
    config::g.farmsFreeMin = 4;
    config::g.farmsFreePercent = 10;
    config::g.logCasts = false;

    // 2. Food low, an idle peasant: it gets the build order, the site is where the game itself allows it, next to
    //    the hall, not on it, and on the map.
    FarmWorld();
    Unit* idle = FarmPeasant(30, 30, kOrderStop);
    Unit* miner = FarmPeasant(12, 12, kOrderHarvest);
    FarmPass();
    CHECK(Field<uint8_t>(idle, kOffNextOrder) == kOrderBuild && Field<uint8_t>(idle, kOffBuildType) == kFarmType,
          "an idle peasant must get the farm order (next order %u, type 0x%02X)", Field<uint8_t>(idle, kOffNextOrder),
          Field<uint8_t>(idle, kOffBuildType));
    CHECK(Field<uint8_t>(miner, kOffNextOrder) == kOrderNone, "the harvester must be left alone while one is idle");
    {
        const uint32_t site = Field<uint32_t>(idle, kOffBuildSite);
        const int sx = static_cast<int16_t>(site & 0xFFFF), sy = static_cast<int16_t>(site >> 16);
        const bool overlapsHall = sx + 2 > kHallX && sx < kHallX + 4 && sy + 2 > kHallY && sy < kHallY + 4;
        const int dx = sx < kHallX ? kHallX - (sx + 1) : (sx > kHallX + 3 ? sx - (kHallX + 3) : 0);
        const int dy = sy < kHallY ? kHallY - (sy + 1) : (sy > kHallY + 3 ? sy - (kHallY + 3) : 0);
        using CanPlaceFn = uint16_t(__cdecl*)(Unit*, uint32_t, uint32_t);
        const uint16_t verdict = reinterpret_cast<CanPlaceFn>(g_base + kRvaCanPlaceBuilding)(idle, site, kFarmType);
        CHECK(sx >= 0 && sy >= 0 && sx + 2 <= kMap && sy + 2 <= kMap && !overlapsHall && (dx > dy ? dx : dy) <= 8 &&
                  verdict == 0,
              "farm site %d,%d: on map, off the hall, within 8 tiles of it (%d), placeable (%u)", sx, sy,
              dx > dy ? dx : dy, verdict);
        CHECK(Field<int16_t>(idle, kOffOrderX) >= sx - 1 && Field<int16_t>(idle, kOffOrderX) <= sx + 2,
              "the walk-to tile comes from FUN_004c3a20 next to the site (%d vs %d)", Field<int16_t>(idle, kOffOrderX), sx);
        printf("farm site from the game's own search: %d,%d for a hall at %d,%d, walk to %d,%d\n", sx, sy, kHallX,
               kHallY, Field<int16_t>(idle, kOffOrderX), Field<int16_t>(idle, kOffOrderY));
    }

    // 3. One at a time: the peasant on its way counts, and so does a farm under construction.
    FarmPass();
    CHECK(FarmOrders() == 1, "a second farm must wait for the first (%d orders)", FarmOrders());
    FarmWorld();
    Unit* site = AddUnit(kFarmType, 0, 30, 20, 100, 0, 0);  // under construction: kStateComplete not set
    (void)site;
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmPass();
    CHECK(FarmOrders() == 0, "a farm under construction must block the next one");

    // 4. Not enough money for the live price: nothing.
    FarmWorld();
    idle = FarmPeasant(30, 30, kOrderStop);
    At<int32_t>(kRvaPlayerLumber)[0] = 249;
    FarmPass();
    CHECK(FarmOrders() == 0, "no farm without 250 lumber");
    At<int32_t>(kRvaPlayerLumber)[0] = 250;
    At<int32_t>(kRvaPlayerGold)[0] = 499;
    FarmPass();
    CHECK(FarmOrders() == 0, "no farm without 500 gold");
    At<int32_t>(kRvaPlayerGold)[0] = 500;
    FarmPass();
    CHECK(FarmOrders() == 1, "exactly the price is enough");

    // 5. Food not low, 200 supply, the mission forbids farms, switched off: nothing.
    FarmWorld();
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmFood(20, 15);
    FarmPass();
    CHECK(FarmOrders() == 0, "5 free at supply 20 is above the 4 of the trigger");
    FarmFood(200, 200);
    FarmPass();
    CHECK(FarmOrders() == 0, "never at 200 supply");
    FarmFood(20, 18);
    At<uint32_t>(kRvaUnitsAllowed)[0] &= ~kAllowFarm;
    FarmPass();
    CHECK(FarmOrders() == 0, "a mission without farms must not get one");
    At<uint32_t>(kRvaUnitsAllowed)[0] |= kAllowFarm;
    *At<uint8_t>(kRvaBuildPlayerMask) = 0xFE;
    FarmPass();
    CHECK(FarmOrders() == 0, "the build-button player mask must be honoured");
    *At<uint8_t>(kRvaBuildPlayerMask) = 0xFF;
    config::g.farmsAutoBuild = false;
    FarmPass();
    CHECK(FarmOrders() == 0, "auto_build = false must do nothing");
    config::g.farmsAutoBuild = true;

    // 6. Who may be taken: a harvester carrying nothing when nobody is idle; never one carrying goods, repairing or
    //    already building something else; the nearest of two idle peasants.
    FarmWorld();
    Unit* loaded = FarmPeasant(24, 30, kOrderHarvest);
    Field<uint8_t>(loaded, kOffWorkerFlags) = kWorkerCarrying | kWorkerLumberJob;
    Unit* repairer = FarmPeasant(25, 30, kOrderRepair);
    Unit* builder = FarmPeasant(26, 30, kOrderBuild);
    Field<uint8_t>(builder, kOffBuildType) = 0x3C;  // a barracks: not a farm, so it does not block
    Field<uint8_t>(builder, kOffNextOrder) = kOrderNone;
    miner = FarmPeasant(40, 40, kOrderHarvest);
    Field<uint8_t>(miner, kOffWorkerFlags) = kWorkerLumberJob;  // a wood cutter walking back
    FarmPass();
    CHECK(Field<uint8_t>(miner, kOffNextOrder) == kOrderBuild && Field<uint8_t>(loaded, kOffNextOrder) == kOrderNone &&
              Field<uint8_t>(repairer, kOffNextOrder) == kOrderNone,
          "only the empty-handed harvester may be taken (miner %u, loaded %u, repairer %u)",
          Field<uint8_t>(miner, kOffNextOrder), Field<uint8_t>(loaded, kOffNextOrder), Field<uint8_t>(repairer, kOffNextOrder));
    FarmWorld();
    Unit* farPeasant = FarmPeasant(60, 60, kOrderStop);
    Unit* nearPeasant = FarmPeasant(26, 26, kOrderStop);
    FarmPass();
    CHECK(Field<uint8_t>(nearPeasant, kOffNextOrder) == kOrderBuild && Field<uint8_t>(farPeasant, kOffNextOrder) == kOrderNone,
          "the idle peasant nearest the site builds it");
    FarmWorld();
    idle = FarmPeasant(30, 30, kOrderStop);
    g_farmRegion[30 * kMap + 30] = 2;  // an island: the peasant cannot reach the hall's region
    Unit* other = FarmPeasant(10, 30, kOrderStop);
    FarmPass();
    CHECK(Field<uint8_t>(idle, kOffNextOrder) == kOrderNone && Field<uint8_t>(other, kOffNextOrder) == kOrderBuild,
          "a peasant cut off from the hall must not block one that can reach it (island %u, other %u)",
          Field<uint8_t>(idle, kOffNextOrder), Field<uint8_t>(other, kOffNextOrder));

    // 7. No free site anywhere (every square blocked but the peasant's): nothing.
    FarmWorld();
    for (auto& q : g_farmSq) q |= 0x80;
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmPass();
    CHECK(FarmOrders() == 0, "no placeable site: no order");

    // 8. Through the real entry point: runs in single player, never in a network game. The idle-worker features
    //    would otherwise send the peasant to work before the one-second farm pass comes round.
    const bool savedHarvest = config::g.workerAutoHarvest, savedRepair = config::g.workerAutoRepair;
    config::g.workerAutoHarvest = config::g.workerAutoRepair = false;
    FarmWorld();
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmLinkLists();
    *At<uint32_t>(kRvaNetGame) = 1;
    for (int i = 0; i < 15; ++i) { Sleep(100); mod::OnTick(); }
    CHECK(FarmOrders() == 0, "a network game must never get a farm");
    *At<uint32_t>(kRvaNetGame) = 0;
    for (int i = 0; i < 15; ++i) { Sleep(100); mod::OnTick(); }
    CHECK(FarmOrders() == 1, "mod::OnTick must run the farm pass in single player (%d)", FarmOrders());
    config::g.workerAutoHarvest = savedHarvest;
    config::g.workerAutoRepair = savedRepair;

    // 9. Gold mines. The test's own picture of the band the peasants walk: every tile a straight line from a hall
    //    tile to a mine tile passes through (sampled), an independent check of the mod's hull.
    constexpr uint32_t kMineFlags = 0;
    const uint32_t savedMineFlags = tf[kTypeGoldMine];
    tf[kTypeGoldMine] = kMineFlags;
    At<Sz>(kRvaUnitSizeByType)[kTypeGoldMine] = {3, 3};
    static bool band[kMap * kMap];
    auto addMine = [&](int mx, int my) {
        AddUnit(kTypeGoldMine, kNeutralPlayer, mx, my, 25500, 0, 0);
        for (int y = my; y < my + 3; ++y)
            for (int x = mx; x < mx + 3; ++x) g_farmSq[y * kMap + x] |= 0x800;
        for (int hy = kHallY; hy < kHallY + 4; ++hy)
            for (int hx = kHallX; hx < kHallX + 4; ++hx)
                for (int ny = my; ny < my + 3; ++ny)
                    for (int nx = mx; nx < mx + 3; ++nx)
                        for (int k = 0; k <= 40; ++k) {
                            const double t = k / 40.0;
                            const int bx = static_cast<int>(hx + 0.5 + (nx - hx) * t), by = static_cast<int>(hy + 0.5 + (ny - hy) * t);
                            const bool inHall = bx >= kHallX && bx < kHallX + 4 && by >= kHallY && by < kHallY + 4;
                            if (!inHall) band[by * kMap + bx] = true;  // nobody walks through the hall itself
                        }
    };
    auto rectGap = [](int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
        int gx = bx - (ax + aw - 1), gy = by - (ay + ah - 1);
        if (ax - (bx + bw - 1) > gx) gx = ax - (bx + bw - 1);
        if (ay - (by + bh - 1) > gy) gy = ay - (by + bh - 1);
        gx = gx < 0 ? 0 : gx;
        gy = gy < 0 ? 0 : gy;
        return gx > gy ? gx : gy;
    };
    auto nearBand = [&](int sx, int sy) {  // within 2 tiles of the band
        for (int y = sy - 2; y <= sy + 3; ++y)
            for (int x = sx - 2; x <= sx + 3; ++x)
                if (x >= 0 && y >= 0 && x < kMap && y < kMap && band[y * kMap + x]) return true;
        return false;
    };
    auto siteOf = [](Unit* u, int& sx, int& sy) {
        const uint32_t site = Field<uint32_t>(u, kOffBuildSite);
        sx = static_cast<int16_t>(site & 0xFFFF);
        sy = static_cast<int16_t>(site >> 16);
    };
    using FindSiteFn = int(__cdecl*)(Unit*, int16_t*, uint32_t);

    // 9a. A mine up-left of the hall: the computer's own first site lies in the band; ours must not, and must lie on
    //     the side away from the mine.
    memset(band, 0, sizeof band);
    FarmWorld();
    addMine(10, 10);
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmLinkLists();
    {
        int16_t ai[2] = {-1, -1};
        const int found = reinterpret_cast<FindSiteFn>(g_base + kRvaAiFindBuildSite)(idle, ai, kFarmType);
        CHECK(found && nearBand(ai[0], ai[1]), "test setup: the computer's own first site (%d,%d) should sit in the band",
              ai[0], ai[1]);
    }
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(FarmOrders() == 1 && !nearBand(sx, sy) && rectGap(sx, sy, 2, 2, 10, 10, 3, 3) > 3,
              "the farm must keep out of the hall-mine band and 3 tiles from the mine (site %d,%d, orders %d)", sx, sy,
              FarmOrders());
        CHECK((sx + 1 - (kHallX + 2)) + (sy + 1 - (kHallY + 2)) >= 0 && rectGap(sx, sy, 2, 2, kHallX, kHallY, 4, 4) <= 2,
              "the farm goes on the far side of the hall from the mine, close to it (site %d,%d)", sx, sy);
        printf("farm site with a mine up-left: %d,%d (the computer's own search would pick 18,18)\n", sx, sy);
    }

    // 9b. Two mines, left and right, and a farm already standing below the hall: the new one keeps clear of both
    //     mines and both bands and prefers to touch the farm.
    memset(band, 0, sizeof band);
    FarmWorld();
    addMine(12, 20);
    addMine(29, 21);
    Unit* oldFarm = AddUnit(kFarmType, 0, 20, 25, 400, 0, 0);
    Field<uint16_t>(oldFarm, kOffStateFlags) = kStateComplete;
    for (int y = 25; y < 27; ++y)
        for (int x = 20; x < 22; ++x) g_farmSq[y * kMap + x] |= 0x800;
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(FarmOrders() == 1 && !nearBand(sx, sy) && rectGap(sx, sy, 2, 2, 12, 20, 3, 3) > 3 &&
                  rectGap(sx, sy, 2, 2, 29, 21, 3, 3) > 3,
              "two mines: out of both bands and 3 tiles from both (site %d,%d)", sx, sy);
        CHECK(rectGap(sx, sy, 2, 2, 20, 25, 2, 2) == 1, "the new farm touches the old one (site %d,%d)", sx, sy);
    }

    // 9c. Only sites in the band are free: the computer would build there, the mod builds nothing.
    memset(band, 0, sizeof band);
    FarmWorld();
    addMine(10, 10);
    for (int i = 0; i < kMap * kMap; ++i)
        if (!band[i]) g_farmSq[i] |= 0x80;
    idle = FarmPeasant(30, 30, kOrderStop);
    g_farmSq[30 * kMap + 30] &= ~0x80;
    FarmLinkLists();
    {
        int16_t ai[2] = {-1, -1};
        CHECK(reinterpret_cast<FindSiteFn>(g_base + kRvaAiFindBuildSite)(idle, ai, kFarmType) != 0,
              "test setup: the computer's search should still find a site in the band");
    }
    FarmPass();
    CHECK(FarmOrders() == 0, "no site outside the band: no farm at all");

    // 9d. A site whose tile belongs to another region (water, a cliff) is never taken, however close.
    FarmWorld();
    for (int y = 16; y < 20; ++y)
        for (int x = 16; x < 20; ++x) g_farmRegion[y * kMap + x] = 2;
    idle = FarmPeasant(30, 30, kOrderStop);
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(FarmOrders() == 1 && g_farmRegion[sy * kMap + sx] == 1, "the farm must stand in the peasant's region (site %d,%d)", sx, sy);
    }

    // 9e. mine_clearance counts free tiles between farm and mine: a mine 15 tiles right of the only open side.
    auto clearanceWorld = [&]() {
        memset(band, 0, sizeof band);
        FarmWorld();
        AddUnit(kTypeGoldMine, kNeutralPlayer, 40, 20, 25500, 0, 0);  // 17 tiles from the hall: no walking band
        for (int y = 20; y < 23; ++y)
            for (int x = 40; x < 43; ++x) g_farmSq[y * kMap + x] |= 0x800;
        for (int y = 0; y < kMap; ++y)
            for (int x = 0; x < kMap; ++x)
                if (x < 24 || y < 14 || y > 29) g_farmSq[y * kMap + x] |= 0x80;  // open: right of the hall, rows 14..29
        return FarmPeasant(30, 30, kOrderStop);
    };
    config::g.farmsMineClearance = 15;
    Unit* probe = clearanceWorld();
    FarmPass();
    { int px = -1, py = -1; siteOf(probe, px, py); CHECK(FarmOrders() == 0, "mine_clearance 15: the nearest open site is 15 tiles from the mine, too close (site %d,%d)", px, py); }
    config::g.farmsMineClearance = 14;
    idle = clearanceWorld();
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(FarmOrders() == 1 && rectGap(sx, sy, 2, 2, 40, 20, 3, 3) > 14, "mine_clearance 14: that site is fine (site %d,%d)", sx, sy);
    }
    config::g.farmsMineClearance = 3;

    // 9f. A mine up and to the right: the site against the hall's top-left corner, met first in the search and
    //     clear of the band, still faces the mine; a site on the side away from it wins.
    memset(band, 0, sizeof band);
    FarmWorld();
    addMine(30, 8);
    auto ownFarm = [&](int fx, int fy) {
        Unit* u = AddUnit(kFarmType, 0, fx, fy, 400, 0, 0);
        Field<uint16_t>(u, kOffStateFlags) = kStateComplete;
        for (int y = fy; y < fy + 2; ++y)
            for (int x = fx; x < fx + 2; ++x) g_farmSq[y * kMap + x] |= 0x800;
    };
    ownFarm(30, 20);  // toward the mine, clear of its band, 7 tiles from the hall
    ownFarm(6, 30);   // away from the mine, farther out
    for (int y = 0; y < kMap; ++y)  // open ground only right of x 26 and around the far farm
        for (int x = 0; x < kMap; ++x)
            if (x < 26 && !(x >= 2 && x <= 12 && y >= 26 && y <= 36)) g_farmSq[y * kMap + x] |= 0x80;
    idle = FarmPeasant(40, 40, kOrderStop);
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        const double towardMine = (sx + 1 - (kHallX + 2)) * 9.5 + (sy + 1 - (kHallY + 2)) * -12.5;
        CHECK(FarmOrders() == 1 && towardMine < 0 && !nearBand(sx, sy),
              "a mine up-right: the farm goes on the side away from it (site %d,%d, toward %.1f)", sx, sy, towardMine);
    }

    // 9g. Nothing free against the hall: the nearest free site wins (3 tiles out), not a farther one.
    FarmWorld();
    for (int y = kHallY - 2; y < kHallY + 6; ++y)
        for (int x = kHallX - 2; x < kHallX + 6; ++x)
            if (!(x >= kHallX && x < kHallX + 4 && y >= kHallY && y < kHallY + 4)) g_farmSq[y * kMap + x] |= 0x80;
    idle = FarmPeasant(40, 40, kOrderStop);
    FarmPass();
    {
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(FarmOrders() == 1 && rectGap(sx, sy, 2, 2, kHallX, kHallY, 4, 4) == 3,
              "the nearest free site is 3 tiles from the hall (site %d,%d)", sx, sy);
    }

    // 9h. Only a FINISHED hall counts: one still under construction neither allows a farm nor anchors one.
    FarmWorld();
    {
        Unit* hall0 = reinterpret_cast<Unit*>(g_units);  // FarmWorld's hall is the first unit
        Field<uint16_t>(hall0, kOffStateFlags) = 0;      // being built
        idle = FarmPeasant(30, 30, kOrderStop);
        FarmPass();
        CHECK(FarmOrders() == 0, "no farm while the only town hall is still being built");
        Field<uint16_t>(hall0, kOffStateFlags) = kStateComplete;
        FarmPass();
        CHECK(FarmOrders() == 1, "a farm once the hall is finished");
    }

    // 9i. Fill the base: farm after farm until no site is left. The hall and both mines must still be joined by open
    //     ground (a walk over tiles without building or unpassable bits), and no farm may touch the bands.
    memset(band, 0, sizeof band);
    FarmWorld();
    addMine(10, 10);
    addMine(32, 22);
    idle = FarmPeasant(40, 40, kOrderStop);
    int built = 0;
    for (; built < 60; ++built) {
        Field<uint8_t>(idle, kOffNextOrder) = kOrderNone;
        FarmPass();
        if (FarmOrders() == 0) break;
        int sx = -1, sy = -1;
        siteOf(idle, sx, sy);
        CHECK(!nearBand(sx, sy), "farm %d at %d,%d lies within 2 tiles of a hall-mine band", built, sx, sy);
        Unit* f = AddUnit(kFarmType, 0, sx, sy, 400, 0, 0);
        Field<uint16_t>(f, kOffStateFlags) = kStateComplete;
        for (int y = sy; y < sy + 2; ++y)
            for (int x = sx; x < sx + 2; ++x) g_farmSq[y * kMap + x] |= 0x800;
    }
    Field<uint8_t>(idle, kOffNextOrder) = kOrderNone;
    CHECK(built >= 8, "the base should still take a good number of farms (%d)", built);
    auto reaches = [&](int mx, int my) {  // flood fill from the tiles around the hall to the tiles around the mine
        static bool seen[kMap * kMap];
        memset(seen, 0, sizeof seen);
        int queue[kMap * kMap], head = 0, tail = 0;
        auto open = [&](int x, int y) { return x >= 0 && y >= 0 && x < kMap && y < kMap && !(g_farmSq[y * kMap + x] & 0x880); };
        for (int y = kHallY - 1; y <= kHallY + 4; ++y)
            for (int x = kHallX - 1; x <= kHallX + 4; ++x)
                if (open(x, y) && !seen[y * kMap + x]) { seen[y * kMap + x] = true; queue[tail++] = y * kMap + x; }
        while (head < tail) {
            const int t = queue[head++], x = t % kMap, y = t / kMap;
            if (x >= mx - 1 && x <= mx + 3 && y >= my - 1 && y <= my + 3) return true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (open(x + dx, y + dy) && !seen[(y + dy) * kMap + x + dx]) {
                        seen[(y + dy) * kMap + x + dx] = true;
                        queue[tail++] = (y + dy) * kMap + x + dx;
                    }
        }
        return false;
    };
    CHECK(reaches(10, 10) && reaches(32, 22), "after %d farms the hall must still reach both mines over open ground", built);
    printf("farms placed around a hall with two mines before the base was full: %d\n", built);

    // 10. Which workers: [farms] workers.
    auto lumberman = [&](int x, int y, uint8_t flags) {
        Unit* u = FarmPeasant(x, y, kOrderHarvest);
        Field<uint8_t>(u, kOffWorkerFlags) = flags;
        return u;
    };
    // idle_then_lumber (the default): a wood cutter on its way back, never a gold miner or one that is chopping.
    config::g.farmsWorkers = FarmWorkers::IdleThenLumber;
    FarmWorld();
    Unit* goldMiner = lumberman(26, 26, kWorkerGoldJob);
    Unit* chopper = lumberman(27, 26, kWorkerLumberJob | kWorkerChopping);
    Unit* walker = lumberman(40, 40, kWorkerLumberJob);
    FarmPass();
    CHECK(Field<uint8_t>(walker, kOffNextOrder) == kOrderBuild && Field<uint8_t>(goldMiner, kOffNextOrder) == kOrderNone &&
              Field<uint8_t>(chopper, kOffNextOrder) == kOrderNone,
          "idle_then_lumber: the wood cutter walking back builds, not the nearer gold miner or chopper (%u %u %u)",
          Field<uint8_t>(walker, kOffNextOrder), Field<uint8_t>(goldMiner, kOffNextOrder), Field<uint8_t>(chopper, kOffNextOrder));
    FarmWorld();
    goldMiner = lumberman(26, 26, kWorkerGoldJob);
    lumberman(27, 26, 0);                                     // harvesting, no job bit yet: not known to cut wood
    lumberman(28, 26, kWorkerGoldJob | kWorkerLumberJob);     // defensive: any gold bit rules a worker out
    FarmPass();
    CHECK(FarmOrders() == 0, "idle_then_lumber: a gold miner (or a harvester without the wood-cutting bit) is never taken");
    // idle_only
    config::g.farmsWorkers = FarmWorkers::IdleOnly;
    FarmWorld();
    walker = lumberman(40, 40, kWorkerLumberJob);
    FarmPass();
    CHECK(FarmOrders() == 0, "idle_only: no harvester at all");
    idle = FarmPeasant(45, 45, kOrderStop);
    FarmPass();
    CHECK(Field<uint8_t>(idle, kOffNextOrder) == kOrderBuild && Field<uint8_t>(walker, kOffNextOrder) == kOrderNone,
          "idle_only: the idle peasant builds");
    // any: the old behaviour, an empty-handed gold miner too
    config::g.farmsWorkers = FarmWorkers::Any;
    FarmWorld();
    goldMiner = lumberman(26, 26, kWorkerGoldJob);
    FarmPass();
    CHECK(Field<uint8_t>(goldMiner, kOffNextOrder) == kOrderBuild, "any: the empty-handed gold miner may build");
    config::g.farmsWorkers = FarmWorkers::IdleThenLumber;
    tf[kTypeGoldMine] = savedMineFlags;

    *At<uint16_t*>(kRvaRegionMap) = s.region;
    *At<uint16_t*>(kRvaSquareFlags) = s.sq;
    *At<uint8_t*>(kRvaExploredMap) = s.explored;
    *At<uint8_t*>(kRvaTileSeenBits) = s.seen;
    tf[kPeasant] = s.tfPeasant; tf[kHall] = s.tfHall; tf[kFarmType] = s.tfFarm;
    At<uint32_t>(kRvaUnitsAllowed)[0] = s.allowed;
    *At<uint8_t>(kRvaBuildPlayerMask) = s.mask;
    memset(At<Unit*>(kRvaPlayerUnitList), 0, 16 * sizeof(Unit*));
    FarmFood(0, 0);
    config::g.farmsAutoBuild = savedAuto;
    config::g.farmsFreeMin = savedMin;
    config::g.farmsFreePercent = savedPercent;
    config::g.farmsMineClearance = savedClearance;
    config::g.farmsWorkers = savedWorkers;
    config::g.logCasts = savedLog;
    ResetWorld();
}

// ---------------------------------------------------------------------------------------------------------------
// Live unit stats (datatweaks::OnConfigReloaded): a config reload mid-game puts the tables back to the copy the
// new-map pass took and applies the pass again. Never twice on top of itself, never after a savegame load.
// ---------------------------------------------------------------------------------------------------------------

struct LiveTables {
    uint16_t hp[110];
    uint8_t gold[110], lumber[110], oil[110], buildTime[110], armor[110], basic[110], piercing[110], range[110];
    uint8_t reactC[110], reactH[110];
    uint32_t sight[110];
    uint16_t rGold[52], rLumber[52], rOil[52];
    uint8_t rTime[52];
};

static void LiveRead(LiveTables& t) {
    memcpy(t.hp, At<uint16_t>(kRvaMaxHpByType), sizeof t.hp);
    memcpy(t.gold, At<uint8_t>(kRvaGoldCostByType), 110);
    memcpy(t.lumber, At<uint8_t>(kRvaLumberCostByType), 110);
    memcpy(t.oil, At<uint8_t>(kRvaOilCostByType), 110);
    memcpy(t.buildTime, At<uint8_t>(kRvaBuildTimeByType), 110);
    memcpy(t.armor, At<uint8_t>(kRvaArmorByType), 110);
    memcpy(t.basic, At<uint8_t>(kRvaBasicDamageByType), 110);
    memcpy(t.piercing, At<uint8_t>(kRvaPiercingDamageByType), 110);
    memcpy(t.range, At<uint8_t>(kRvaAttackRangeByType), 110);
    memcpy(t.reactC, At<uint8_t>(kRvaReactRangeComputer), 110);
    memcpy(t.reactH, At<uint8_t>(kRvaReactRangeHuman), 110);
    memcpy(t.sight, At<uint32_t>(kRvaSightByType), sizeof t.sight);
    memcpy(t.rGold, At<uint16_t>(kRvaUpgradeGold), sizeof t.rGold);
    memcpy(t.rLumber, At<uint16_t>(kRvaUpgradeLumber), sizeof t.rLumber);
    memcpy(t.rOil, At<uint16_t>(kRvaUpgradeOil), sizeof t.rOil);
    memcpy(t.rTime, At<uint8_t>(kRvaResearchTime), sizeof t.rTime);
}

static void LiveWrite(const LiveTables& t) {
    memcpy(At<uint16_t>(kRvaMaxHpByType), t.hp, sizeof t.hp);
    memcpy(At<uint8_t>(kRvaGoldCostByType), t.gold, 110);
    memcpy(At<uint8_t>(kRvaLumberCostByType), t.lumber, 110);
    memcpy(At<uint8_t>(kRvaOilCostByType), t.oil, 110);
    memcpy(At<uint8_t>(kRvaBuildTimeByType), t.buildTime, 110);
    memcpy(At<uint8_t>(kRvaArmorByType), t.armor, 110);
    memcpy(At<uint8_t>(kRvaBasicDamageByType), t.basic, 110);
    memcpy(At<uint8_t>(kRvaPiercingDamageByType), t.piercing, 110);
    memcpy(At<uint8_t>(kRvaAttackRangeByType), t.range, 110);
    memcpy(At<uint8_t>(kRvaReactRangeComputer), t.reactC, 110);
    memcpy(At<uint8_t>(kRvaReactRangeHuman), t.reactH, 110);
    memcpy(At<uint32_t>(kRvaSightByType), t.sight, sizeof t.sight);
    memcpy(At<uint16_t>(kRvaUpgradeGold), t.rGold, sizeof t.rGold);
    memcpy(At<uint16_t>(kRvaUpgradeLumber), t.rLumber, sizeof t.rLumber);
    memcpy(At<uint16_t>(kRvaUpgradeOil), t.rOil, sizeof t.rOil);
    memcpy(At<uint8_t>(kRvaResearchTime), t.rTime, sizeof t.rTime);
}

static bool LiveSame(const LiveTables& a, const LiveTables& b) { return memcmp(&a, &b, sizeof a) == 0; }

// What FinalizeTables (FUN_004c4ba0) does right after the new-map pass: sight 0..9 -> reveal-function pointer.
static void LiveFinalize() {
    uint32_t* sight = At<uint32_t>(kRvaSightByType);
    for (int t = 0; t < 110; ++t) sight[t] = At<uint32_t>(kRvaSightFunctions)[sight[t]];
}

static void LiveStatsTests(const wchar_t* dir) {
    static LiveTables saved, pristine, pristineFinal, once, now;
    static Config savedConfig;
    savedConfig = config::g;
    LiveRead(saved);

    // Plausible "game" tables: every cell non-zero so every multiplier shows.
    for (int t = 0; t < 110; ++t) {
        pristine.hp[t] = static_cast<uint16_t>(60 + t);
        pristine.gold[t] = static_cast<uint8_t>(10 + t % 50);
        pristine.lumber[t] = static_cast<uint8_t>(5 + t % 20);
        pristine.oil[t] = static_cast<uint8_t>(1 + t % 7);
        pristine.buildTime[t] = static_cast<uint8_t>(30 + t % 60);
        pristine.armor[t] = static_cast<uint8_t>(t % 10);
        pristine.basic[t] = static_cast<uint8_t>(3 + t % 9);
        pristine.piercing[t] = static_cast<uint8_t>(2 + t % 6);
        pristine.range[t] = static_cast<uint8_t>(1 + t % 5);
        pristine.reactC[t] = static_cast<uint8_t>(4 + t % 4);
        pristine.reactH[t] = static_cast<uint8_t>(3 + t % 4);
        pristine.sight[t] = static_cast<uint32_t>(t % 10);
    }
    for (int i = 0; i < 52; ++i) {
        pristine.rGold[i] = static_cast<uint16_t>(500 + i * 10);
        pristine.rLumber[i] = static_cast<uint16_t>(100 + i);
        pristine.rOil[i] = static_cast<uint16_t>(i);
        pristine.rTime[i] = static_cast<uint8_t>(60 + i);
    }
    pristineFinal = pristine;
    for (int t = 0; t < 110; ++t) pristineFinal.sight[t] = At<uint32_t>(kRvaSightFunctions)[pristine.sight[t]];

    auto setConfig = [](double health, double costs, int footmanHp) {
        config::g = Config();
        config::g.health.all = health;
        config::g.costs.all = costs;
        config::g.time.all = costs;
        config::g.unitStat[0][kStatHitPoints] = footmanHp;  // -1 = the game's own
    };
    auto newMap = [&]() {  // the new-map hook, then the game's FinalizeTables
        LiveWrite(pristine);
        *At<uint16_t>(kRvaGameFromSave) = 0;
        *At<uint8_t>(kRvaNetGameAtLoad) = 0;
        datatweaks::ResetForTests();
        datatweaks::OnNewMapTablesLoaded();
        LiveFinalize();
    };

    // 1. A reload with the same settings, twice, leaves exactly what the map load made: nothing is applied twice.
    setConfig(2.0, 1.5, -1);
    newMap();
    LiveRead(once);
    CHECK(once.hp[0] == 120 && once.gold[5] == 23, "live stats setup: health x2 / costs x1.5 applied at map load (%u, %u)",
          once.hp[0], once.gold[5]);
    datatweaks::OnConfigReloaded(false);
    datatweaks::OnConfigReloaded(false);
    LiveRead(now);
    CHECK(LiveSame(now, once), "live stats: two reloads with the same settings must give the map-load tables (hp %u, gold %u)",
          now.hp[0], now.gold[5]);

    // 2. Back to every multiplier 1.0 and no own numbers: the game's own tables, sight as its reveal pointers.
    setConfig(1.0, 1.0, -1);
    datatweaks::OnConfigReloaded(false);
    LiveRead(now);
    CHECK(LiveSame(now, pristineFinal), "live stats: all 1.0 restores the pristine tables (hp %u want %u)", now.hp[0],
          pristineFinal.hp[0]);

    // 3. An own sight number mid-game goes in as the matching reveal pointer, never as a plain number.
    config::g.unitStat[3][kStatSight] = 7;
    datatweaks::OnConfigReloaded(false);
    CHECK(At<uint32_t>(kRvaSightByType)[3] == At<uint32_t>(kRvaSightFunctions)[7],
          "live stats: sight 7 is written as the sight-7 reveal function");
    config::g.unitStat[3][kStatSight] = -1;

    // 4. Units alive keep their share of hit points; a building under construction is left alone.
    setConfig(1.0, 1.0, -1);
    newMap();
    ResetWorld();
    AddUnit(kTypeMage, 0, 10, 10, 30, 255, kOrderStand);  // the player, for BuildWorld
    Unit* hurt = AddUnit(0, 0, 12, 10, 30, 0, kOrderStand);      // footman 30 of 60
    Unit* full = AddUnit(0, 1, 14, 10, 60, 0, kOrderStand);      // 60 of 60, a computer's
    Unit* site = AddUnit(0x3A, 0, 20, 20, 50, 0, 0);             // a farm under construction
    Unit* over = AddUnit(0, 0, 16, 10, 70, 0, kOrderStand);      // above its maximum (a modded save): 70 of 60
    const uint32_t savedFarmFlags = At<uint32_t>(kRvaTypeFlags)[0x3A];
    At<uint32_t>(kRvaTypeFlags)[0x3A] = kTfBuilding;
    config::g.unitStat[0][kStatHitPoints] = 120;
    config::g.unitStat[0x3A][kStatHitPoints] = 400;  // the farm's maximum changes too
    datatweaks::OnConfigReloaded(false);
    CHECK(At<uint16_t>(kRvaMaxHpByType)[0] == 120 && Field<uint16_t>(hurt, kOffHp) == 60 && Field<uint16_t>(full, kOffHp) == 120,
          "live stats: max 60 -> 120 keeps the share (%u, %u)", Field<uint16_t>(hurt, kOffHp), Field<uint16_t>(full, kOffHp));
    CHECK(Field<uint16_t>(site, kOffHp) == 50, "live stats: a building under construction keeps its hit points (%u)",
          Field<uint16_t>(site, kOffHp));
    CHECK(Field<uint16_t>(over, kOffHp) == 120, "live stats: never above the new maximum (%u)", Field<uint16_t>(over, kOffHp));
    Field<uint16_t>(hurt, kOffHp) = 1;
    config::g.unitStat[0][kStatHitPoints] = 10;  // 120 -> 10: never 0, never above the new maximum
    datatweaks::OnConfigReloaded(false);
    CHECK(Field<uint16_t>(hurt, kOffHp) == 1 && Field<uint16_t>(full, kOffHp) == 10, "live stats: 120 -> 10 clamps (%u, %u)",
          Field<uint16_t>(hurt, kOffHp), Field<uint16_t>(full, kOffHp));
    At<uint32_t>(kRvaTypeFlags)[0x3A] = savedFarmFlags;
    ResetWorld();

    // 5. A game loaded from a save, or without a copy from its start, or multiplayer: the tables stay as they are.
    setConfig(2.0, 1.0, -1);
    newMap();
    LiveRead(once);
    setConfig(3.0, 1.0, -1);
    *At<uint16_t>(kRvaGameFromSave) = 1;
    datatweaks::OnConfigReloaded(false);
    LiveRead(now);
    CHECK(LiveSame(now, once), "live stats: a game loaded from a save is never reloaded");
    CHECK(LogContains(dir, "unit stats reload at the next new map (game loaded from a save)"), "live stats: says why, once");
    *At<uint16_t>(kRvaGameFromSave) = 0;
    datatweaks::OnConfigReloaded(true);
    LiveRead(now);
    CHECK(LiveSame(now, once), "live stats: never in multiplayer");
    datatweaks::ResetForTests();
    datatweaks::OnConfigReloaded(false);
    LiveRead(now);
    CHECK(LiveSame(now, once), "live stats: no copy of this game's start, no reload");
    // A multiplayer map load drops the copy too.
    setConfig(2.0, 1.0, -1);
    newMap();
    LiveRead(once);
    setConfig(3.0, 1.0, -1);
    *At<uint8_t>(kRvaNetGameAtLoad) = 1;
    datatweaks::OnNewMapTablesLoaded();
    *At<uint8_t>(kRvaNetGameAtLoad) = 0;
    datatweaks::OnConfigReloaded(false);
    LiveRead(now);
    CHECK(LiveSame(now, once), "live stats: after a multiplayer map load there is nothing to reload from");

    config::g = savedConfig;
    LiveWrite(saved);
    datatweaks::ResetForTests();
}

static void SpellNumberTests(const wchar_t* dir, const wchar_t* ini) {
    // The real exe: every patch site and the 19 cost words are what the research found.
    for (int i = 0; i < kSpellSiteCount; ++i) CHECK(SiteIsGame(i), "spell patch site %d at RVA 0x%X is not the game's instruction", i, kSpellSites[i].rva);
    CHECK(CostsAreGame(), "the mana cost table 0x26..0x38 is not 70,5,5,4,80,100,50,200,200,25,70,60,50,100,100,50,100,200,30");
    // Everything before this ran with the shipped config: not one word or byte may have been written.
    CHECK(spells::WriteCount() == 0, "the shipped config wrote %u spell table word(s) / patch(es)", spells::WriteCount());

    ResetSpellConfig();
    spells::Sync(false);
    datatweaks::OnNewMapTablesLoaded();
    spells::Sync(false);
    CHECK(spells::WriteCount() == 0 && AllSitesAreGame() && CostsAreGame(), "defaults must write nothing");

    // all = 2 in both sections. Costs round up and stop at 255; heal / exorcism get half the doubled per-HP price.
    config::g.spellCostAll = 2.0;
    config::g.spellDamageAll = 2.0;
    spells::Sync(false);
    {
        const uint16_t want[19] = {140, 5, 5, 4, 160, 200, 100, 255, 255, 50, 140, 120, 100, 200, 200, 100, 200, 255, 60};
        CHECK(CostsAre(want), "all = 2: cost table");
        if (!CostsAre(want)) PrintCosts("all = 2");
    }
    CHECK(DamageSitesAre(80, 8, 20, 20, 8, 100, 100, 80) && RegenSitesAre(40), "all = 2: damage bytes");
    const unsigned writes = spells::WriteCount();
    spells::Sync(false);
    spells::Sync(false);
    CHECK(spells::WriteCount() == writes, "a second Sync must not write again (%u -> %u)", writes, spells::WriteCount());

    // Multiplayer, through all three ways it arrives: the game's bytes and costs come back at once.
    spells::Sync(true);
    CHECK(AllSitesAreGame() && CostsAreGame(), "Sync(multiplayer) must restore every site and cost");
    spells::Sync(false);
    CHECK(DamageSitesAre(80, 8, 20, 20, 8, 100, 100, 80), "single player again: patched again");
    *At<uint8_t>(kRvaNetGameAtLoad) = 1;
    datatweaks::OnNewMapTablesLoaded();
    *At<uint8_t>(kRvaNetGameAtLoad) = 0;
    CHECK(AllSitesAreGame() && CostsAreGame(), "a multiplayer map load must restore every site and cost");
    spells::Sync(false);
    *At<uint32_t>(kRvaNetGame) = 1;  // a multiplayer game started from a savegame never passes the new-map hook
    mod::OnTick();
    *At<uint32_t>(kRvaNetGame) = 0;
    CHECK(AllSitesAreGame() && CostsAreGame(), "the first multiplayer tick must restore every site and cost");
    mod::OnTick();
    CHECK(DamageSitesAre(80, 8, 20, 20, 8, 100, 100, 80) && !CostsAreGame(), "the tick re-applies the config in single player");

    // Back to the defaults: everything is the game's again.
    ResetSpellConfig();
    spells::Sync(false);
    CHECK(AllSitesAreGame() && CostsAreGame(), "switching back to the defaults must restore every site and cost");

    // Rounding: costs round UP, damage to the nearest; per-spell values replace the base before the multiplier.
    config::g.spellCostAll = 1.5;
    config::g.spellCost[kCostFireball] = 7;  // 10.5 -> 11
    config::g.spellDamage[kDamageRunes] = 30;
    config::g.spellDamage[kDamageHeal] = 60;
    config::g.spellDamageAll = 1.5;  // runes 45, heal cap 90, fireball 60, flame 6, blizzard 15, coil 75, whirlwind 6
    spells::Sync(false);
    {
        // heal 5 x 1.5 = 7.5 -> 8, then / 1.5 = 5.33 -> 6; exorcism 4 x 1.5 = 6, / 1.5 = 4
        const uint16_t want[19] = {105, 6, 5, 4, 120, 11, 75, 255, 255, 38, 105, 90, 75, 150, 150, 75, 150, 255, 45};
        CHECK(CostsAre(want), "x1.5: costs round up, fireball's own 7 before the multiplier");
        if (!CostsAre(want)) PrintCosts("x1.5");
    }
    CHECK(DamageSitesAre(60, 6, 15, 15, 6, 75, 45, 90), "x1.5 damage: rounded to nearest, per-spell runes 30 and heal cap 60 first");

    // x1.1: 70 x 1.1 is 77.00000000000001 in floating point and must still cost 77; 4.4 rounds UP for a cost, DOWN
    // (to the nearest) for a damage number.
    ResetSpellConfig();
    config::g.spellCostAll = 1.1;
    config::g.spellDamageAll = 1.1;  // fireball 44, flame 4, blizzard 11, decay 11, whirlwind 4, coil 55, runes 55, heal cap 44
    spells::Sync(false);
    {
        // heal 5.5 -> 6, / 1.1 = 5.45 -> 6; exorcism 4.4 -> 5, / 1.1 = 4.55 -> 5
        const uint16_t want[19] = {77, 6, 5, 5, 88, 110, 55, 220, 220, 28, 77, 66, 55, 110, 110, 55, 110, 220, 33};
        CHECK(CostsAre(want), "x1.1: costs round up without floating-point noise");
        if (!CostsAre(want)) PrintCosts("x1.1");
    }
    CHECK(DamageSitesAre(44, 4, 11, 11, 4, 55, 55, 44), "x1.1 damage: rounded to the nearest");

    // A zero multiplier cannot come from the file (the reader refuses it), but the engine limits must hold anyway.
    config::g.spellCostAll = 0.0;
    config::g.spellDamageAll = 0.0;
    config::g.manaRegen = 0.0;
    spells::Sync(false);
    {
        const uint16_t want[19] = {1, 1, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        CHECK(CostsAre(want), "multiplier 0: every cost at least 1");
        if (!CostsAre(want)) PrintCosts("x0");
    }
    CHECK(DamageSitesAre(1, 1, 1, 1, 1, 1, 1, 1) && RegenSitesAre(40), "multiplier 0: damage at least 1, regen left at the game's");

    // Clamps: 1 / 255 for costs, 254 / 127 / 128 / 255 for the damage sites.
    ResetSpellConfig();
    config::g.spellCostAll = 1000.0;
    config::g.spellDamageAll = 1000.0;
    spells::Sync(false);
    {
        // heal and exorcism: 255 / 1000 = 0.255 -> 1
        const uint16_t want[19] = {255, 1, 5, 1, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
        CHECK(CostsAre(want), "x1000: costs stop at 255, heal / exorcism prices at 1");
        if (!CostsAre(want)) PrintCosts("x1000");
    }
    CHECK(DamageSitesAre(254, 254, 254, 254, 254, 127, 128, 255), "x1000 damage: 254 / 127 / 128 / 255");
    CHECK(At<uint8_t>(kRvaRunesSubtractInsn)[2] == 0x80 && At<uint8_t>(kRvaDeathCoilBudgetInsns[0])[2] == 0x7F,
          "runes subtract -128, death coil 127: both still positive as signed bytes");
    config::g.spellCostAll = 0.01;
    config::g.spellDamageAll = 0.01;
    spells::Sync(false);
    {
        // heal 0.05 -> 1, then 1 / 0.01 = 100; exorcism 0.04 -> 1 -> 100
        const uint16_t want[19] = {1, 100, 5, 100, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1};
        CHECK(CostsAre(want), "x0.01: nothing below 1 (a free heal divides by zero)");
        if (!CostsAre(want)) PrintCosts("x0.01");
    }
    CHECK(DamageSitesAre(1, 1, 1, 1, 1, 1, 1, 1), "x0.01 damage: nothing below 1");
    ResetSpellConfig();
    config::g.spellDamage[kDamageFireball] = 41;  // the marker follows any value but 40
    spells::Sync(false);
    CHECK(DamageSitesAre(41, 4, 10, 10, 4, 50, 50, 40), "fireball 41: marker rewrite");
    config::g.spellDamage[kDamageFireball] = 40;  // an explicit game value is the game's bytes
    spells::Sync(false);
    CHECK(AllSitesAreGame(), "fireball 40 must put the original 16 marker bytes back");

    // [mana] regen: 40 steps per point divided by the multiplier, rounded, 1..255, the three stores agree.
    const struct { double k; int interval; } kRegen[] = {{2.0, 20}, {3.0, 13}, {1.5, 27}, {40.0, 1}, {0.1, 255}, {1.0, 40}};
    for (const auto& r : kRegen) {
        config::g.manaRegen = r.k;
        spells::Sync(false);
        CHECK(RegenSitesAre(r.interval), "[mana] regen %.1f must reload the counter with %d", r.k, r.interval);
    }
    CHECK(AllSitesAreGame(), "regen 1.0 is the game's bytes");

    // The author's config, through the file reader.
    WriteFileText(ini,
                  "[spell_damage]\nall = 2.0\n"
                  "[spell_cost]\nall = 1.0\nfireball = 50\nflame_shield = 40\nblizzard = 13\ndeath_and_decay = 15\nwhirlwind = 50\n"
                  "death_coil = 50\nrunes = 100\nexorcism = 2\nheal = 3\n");
    CHECK(config::Init(dir), "the author's spell config was rejected");
    CHECK(config::g.spellDamageAll == 2.0 && config::g.spellCostAll == 1.0 && config::g.spellCost[kCostFireball] == 50 &&
              config::g.spellCost[kCostHeal] == 3 && config::g.spellCost[kCostExorcism] == 2 && config::g.spellCost[kCostRunes] == 100 &&
              config::g.spellCost[kCostSlow] == -1 && config::g.spellDamage[kDamageFireball] == -1 && config::g.manaRegen == 1.0,
          "the author's spell config did not load");
    datatweaks::OnNewMapTablesLoaded();
    {
        // holy vision 70, heal 3 / 2 -> 2, (0x28) 5, exorcism 2 / 2 -> 1, flame 40, fireball 50, slow 50, invisibility 200,
        // polymorph 200, blizzard 13, eye 70, bloodlust 60, raise dead 50, coil 50, whirlwind 50, haste 50, unholy 100,
        // runes 100, decay 15
        const uint16_t want[19] = {70, 2, 5, 1, 40, 50, 50, 200, 200, 13, 70, 60, 50, 50, 50, 50, 100, 100, 15};
        CHECK(CostsAre(want), "the author's config: cost table");
        PrintCosts("author's config");
    }
    CHECK(DamageSitesAre(80, 8, 20, 20, 8, 100, 100, 80) && RegenSitesAre(40), "the author's config: damage x2 and heal cap 80");
    CHECK(LogContains(dir, "spells: mana cost heal 5->2 exorcism 4->1 flame_shield 80->40 fireball 100->50") &&
              LogContains(dir, "spells: damage fireball 40->80 flame_shield 4->8 blizzard 10->20 death_and_decay 10->20 whirlwind 4->8 "
                               "death_coil 50->100 runes 50->100 heal_cap 40->80"),
          "the new-map log lines");

    // Bad values: 0 and < -1 refused, above the engine's limit clamped, a typo reported.
    WriteFileText(ini,
                  "[spell_cost]\nheal = 0\nexorcism = 300\nfirebal = 5\n"
                  "[spell_damage]\ndeath_coil = 500\nruns = 5\nrunes = -5\nfireball = 254\nall = 0\n"
                  "[mana]\nregen = 100\n");
    CHECK(config::Init(dir), "bad spell values must not be a syntax error");
    CHECK(config::g.spellCost[kCostHeal] == -1 && config::g.spellCost[kCostExorcism] == 255 && config::g.spellDamage[kDamageDeathCoil] == 127 &&
              config::g.spellDamage[kDamageRunes] == -1 && config::g.spellDamage[kDamageFireball] == 254 && config::g.spellDamageAll == 1.0 &&
              config::g.manaRegen == 1.0,
          "bad spell values (heal %d exorcism %d coil %d runes %d regen %.2f)", config::g.spellCost[kCostHeal], config::g.spellCost[kCostExorcism],
          config::g.spellDamage[kDamageDeathCoil], config::g.spellDamage[kDamageRunes], config::g.manaRegen);
    CHECK(LogContains(dir, "unknown key [spell_cost] firebal") && LogContains(dir, "unknown key [spell_damage] runs"), "spell key typos must be logged");
    DeleteFileW(ini);
    CHECK(config::Init(dir), "default config did not come back");
    EnableEverythingForTests();
    spells::Sync(false);
    CHECK(AllSitesAreGame() && CostsAreGame(), "the default file must restore everything");

    // A byte that is neither the game's nor the mod's: that group (and that cost word) is left alone for the session,
    // everything else still follows the config. Last, because a refusal is permanent.
    PokeCode(kRvaFireballMarker + 13, 0xCC);
    At<uint16_t>(kRvaManaCostByOrder)[0x2B] = 123;
    config::g.spellDamageAll = 2.0;
    config::g.spellCostAll = 2.0;
    spells::Sync(false);
    CHECK(SiteIsGame(kSiteFireball) && At<uint8_t>(kRvaFireballMarker)[13] == 0xCC && At<uint8_t>(kRvaFireballMarker)[0] == 0xB8,
          "a foreign byte in the fireball marker: both fireball sites must stay untouched");
    CHECK(At<uint16_t>(kRvaManaCostByOrder)[0x2B] == 123 && At<uint16_t>(kRvaManaCostByOrder)[0x2C] == 100,
          "a foreign cost word stays, the others follow the config");
    {
        uint8_t flame[4] = {0xC6, 0x40, 0x37, 0x08};
        CHECK(SiteIs(kSiteFlame, flame) && RegenSitesAre(40), "the other groups must still be patched");
    }
    PokeCode(kRvaFireballMarker + 13, 0x0F);
    At<uint16_t>(kRvaManaCostByOrder)[0x2B] = 100;
    spells::Sync(false);
    CHECK(SiteIsGame(kSiteFireball) && SiteIsGame(kSiteMarker) && At<uint16_t>(kRvaManaCostByOrder)[0x2B] == 100,
          "a refused group stays refused even when its bytes look right again");
    ResetSpellConfig();
    spells::Sync(false);
    // The mana cost the panels print comes from the same table [spell_cost] writes: the classic panel reads it at
    // 0x4E80BC through the button record's order byte, so the tooltip follows the config with no display copy.
    {
        const uint8_t read[] = {0x0F, 0xB6, 0x47, 0x11, 0x0F, 0xB6, 0x04, 0x45};  // the table address is relocated
        const uint8_t* at = At<uint8_t>(kRvaClassicCostDisplayRead);
        uint32_t table;
        memcpy(&table, at + sizeof(read), sizeof(table));
        CHECK(memcmp(at, read, sizeof(read)) == 0 && table == g_base + kRvaManaCostByOrder,
              "the status panel no longer reads the spell cost from the table the mod writes");
        uint16_t* costs = At<uint16_t>(kRvaManaCostByOrder);
        const uint16_t gameBlizzard = costs[kOrderBlizzard];
        config::g.spellCost[kCostBlizzard] = 13;
        spells::Sync(false);
        CHECK(costs[kOrderBlizzard] == 13 && At<uint8_t>(kRvaManaCostByOrder)[kOrderBlizzard * 2] == 13,
              "what the panel reads must be the configured cost (%u)", costs[kOrderBlizzard]);
        spells::Sync(true);
        CHECK(costs[kOrderBlizzard] == gameBlizzard, "and the game's own cost in a multiplayer game (%u)",
              costs[kOrderBlizzard]);
        config::g.spellCost[kCostBlizzard] = -1;
        spells::Sync(false);
    }
    // What the panel prints as "Range" is the same table [unit.NAME] range writes, but a unit picking its own
    // target searches with a react range the mod does not touch (docs/research/data_tables.md 5a).
    {
        uint32_t table;
        memcpy(&table, At<uint8_t>(0xA8EB8), sizeof(table));
        CHECK(table == g_base + kRvaReactRangeComputer, "the computer react-range read at 0x4A8EB8 moved");
        memcpy(&table, At<uint8_t>(0xA8ECB), sizeof(table));
        CHECK(table == g_base + kRvaReactRangeHuman, "the player react-range read at 0x4A8ECB moved");
        memcpy(&table, At<uint8_t>(0xE5FAF), sizeof(table));
        CHECK(table == g_base + kRvaAttackRangeByType, "the panel no longer prints the attack range table");
    }
    CHECK(AllSitesAreGame() && CostsAreGame(), "the test must leave the image as the game made it");
}

// ---- [heal] cooldown_for_computer: the computer's paladins keep to the same timer (src/hook.cpp) ----
static void ComputerPaladinTests(const wchar_t* dir) {
    // The hooks are three call sites inside the game's paladin AI. If a game patch moves any of them the mod must
    // notice here, not in someone's game: Install() hooks all three or none.
    const int savedCooldown = config::g.healCooldownSeconds, savedUrgent = config::g.healUrgentBelowPercent;
    const bool savedForComputer = config::g.healCooldownForComputer, savedLog = config::g.logCasts;
    ResetWorld();
    autocast::OnNewMap();
    Unit* pal = AddUnit(kTypePaladin, 1, 20, 20, 90, 255, kOrderStand);  // owner 1 is a computer player
    Unit* friendly = AddUnit(kFootman, 1, 21, 20, 55, 0, kOrderStand);   // 55 of 60: worth healing, not urgent
    Field<uint32_t>(pal, kOffSerial) = 4242;
    At<uint16_t>(kRvaMaxHpByType)[kFootman] = 60;  // the data-table tests leave their own numbers behind
    At<uint16_t>(kRvaMaxHpByType)[kTypePaladin] = 90;
    config::g.logCasts = true;
    config::g.healUrgentBelowPercent = 10;

    config::g.healCooldownSeconds = 0;
    CHECK(autocast::ComputerHealAllowed(pal), "without a cooldown the computer is never held back");
    config::g.healCooldownSeconds = 5;
    config::g.healCooldownForComputer = true;
    CHECK(autocast::ComputerHealAllowed(pal) && autocast::ComputerExorcismAllowed(pal), "the first cast is allowed");

    const unsigned blocked = autocast::ComputerBlockedCount();
    autocast::NoteComputerCast(pal);
    CHECK(!autocast::ComputerHealAllowed(pal) && !autocast::ComputerExorcismAllowed(pal),
          "heal and exorcism share one timer");
    CHECK(autocast::ComputerBlockedCount() == blocked + 2, "held-back casts are counted (%u)",
          autocast::ComputerBlockedCount());

    // A friend about to die is the player's own exception, and it is the computer's too. Exorcism has none.
    Field<uint16_t>(friendly, kOffHp) = 5;  // 5 of 60
    CHECK(autocast::ComputerHealAllowed(pal), "a nearly dead friend cannot wait for the timer");
    CHECK(!autocast::ComputerExorcismAllowed(pal), "exorcism is never urgent");
    Field<uint16_t>(friendly, kOffHp) = 55;
    CHECK(!autocast::ComputerHealAllowed(pal), "a scratch waits for the timer");

    autocast::AddPlayTime(5000);
    CHECK(autocast::ComputerHealAllowed(pal), "the timer must run out");

    // The paladin dies and its slot is reused: the new unit starts with a clean timer.
    autocast::NoteComputerCast(pal);
    CHECK(!autocast::ComputerHealAllowed(pal), "the timer is running again");
    Field<uint32_t>(pal, kOffSerial) = 4243;
    CHECK(autocast::ComputerHealAllowed(pal), "another unit in the same slot must not inherit the timer");
    Field<uint32_t>(pal, kOffSerial) = 4242;

    // Switched off, and never in a network game.
    config::g.healCooldownForComputer = false;
    CHECK(autocast::ComputerHealAllowed(pal), "cooldown_for_computer = false leaves the computer alone");
    config::g.healCooldownForComputer = true;
    CHECK(!autocast::ComputerHealAllowed(pal), "and true puts it back");
    *At<uint32_t>(kRvaNetGame) = 1;
    CHECK(autocast::ComputerHealAllowed(pal), "a network game runs the AI on every machine and must not be touched");
    const unsigned held = autocast::ComputerBlockedCount();
    autocast::NoteComputerCast(pal);
    *At<uint32_t>(kRvaNetGame) = 0;
    CHECK(autocast::ComputerBlockedCount() == held, "nothing is counted in a network game");

    // The log line is throttled to one per 30 s of play.
    const unsigned lines = autocast::ComputerBlockedLogCount();
    for (int i = 0; i < 5; ++i) autocast::ComputerHealAllowed(pal);
    CHECK(autocast::ComputerBlockedLogCount() == lines, "the log line must not repeat within 30 s (%u)",
          autocast::ComputerBlockedLogCount());
    autocast::AddPlayTime(30000);
    autocast::NoteComputerCast(pal);
    autocast::ComputerHealAllowed(pal);
    CHECK(autocast::ComputerBlockedLogCount() == lines + 1 && LogContains(dir, "computer paladins kept to the heal cooldown"),
          "one line per 30 s of play, and it says what it is");

    config::g.healCooldownSeconds = savedCooldown;
    config::g.healUrgentBelowPercent = savedUrgent;
    config::g.healCooldownForComputer = savedForComputer;
    config::g.logCasts = savedLog;
    ResetWorld();
    autocast::OnNewMap();
    EnableEverythingForTests();
}

// ---- [autocast] resume_orders: give the attack-move back after the cast (src/resume.cpp) ----
static void ResumeOrderTests(const wchar_t* dir) {
    const bool savedResume = config::g.resumeOrders, savedLog = config::g.logCasts;
    const uint32_t savedRuleset = *At<uint32_t>(kRvaRuleset);
    bool savedSpells[kSpellCount];
    memcpy(savedSpells, config::g.spell, sizeof(savedSpells));
    const int savedMissing = config::g.healMinMissingHp;
    *At<uint32_t>(kRvaRuleset) = 1;  // the Remastered ruleset is what keeps a resume order at all
    config::g.resumeOrders = true;
    config::g.logCasts = true;
    config::g.healMinMissingHp = 1;
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellHeal;
    uint32_t serial = 7000;
    auto ox = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderX)); };
    auto oy = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderY)); };
    auto unitAt = [](int i) { return reinterpret_cast<Unit*>(g_units + i * kUnitSize); };

    // A paladin on an attack-move heals: the resume byte must be clear while the spell runs (the 1.16.2 rule), and
    // the attack-move must come back, with its destination, once the paladin is idle again.
    auto setup = [&](uint8_t resumeOrder, int destOffset, int16_t dx, int16_t dy) {
        ResetWorld();
        Unit* pal = AddUnit(kTypePaladin, 0, 20, 20, 90, 255, kOrderStand);
        Field<uint32_t>(pal, kOffSerial) = ++serial;
        Field<uint8_t>(pal, kOffResumeOrder) = resumeOrder;
        Field<uint8_t>(pal, kOffResumeState) = 0x14;
        Field<int16_t>(pal, destOffset) = dx;
        Field<int16_t>(pal, destOffset + 2) = dy;
        AddUnit(kFootman, 0, 22, 20, 30, 0, kOrderStand);  // something to heal
        return pal;
    };
    // After the cast the caster has to be left with nothing to do, or it heals again and never goes idle.
    auto goIdle = [&](Unit* u) {
        Field<uint16_t>(unitAt(1), kOffHp) = 60;  // the footman is whole again
        Field<uint8_t>(u, kOffOrder) = kOrderStand;
        Field<uint8_t>(u, kOffNextOrder) = kOrderNone;
    };
    Unit* pal = setup(10, 0x90, 34, 12);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27 && Field<uint8_t>(pal, kOffResumeOrder) == kOrderNone,
          "the heal must go out with the resume byte cleared (order %u, resume %u)", OrderOf(pal),
          Field<uint8_t>(pal, kOffResumeOrder));
    CHECK(resume::Count() == 1, "the attack-move must be remembered (%u)", resume::Count());
    mod::RunAutocastPass();  // still casting: nothing given back yet
    CHECK(resume::Count() == 1 && OrderOf(pal) == 0x27, "a caster that is still casting keeps waiting");
    goIdle(pal);  // the heal is over and there is nothing left to heal
    const unsigned restored = resume::RestoreCount();
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderAttackArea && ox(pal) == 34 && oy(pal) == 12 && resume::RestoreCount() == restored + 1,
          "the attack-move must come back with its destination (order %u at %d,%d)", OrderOf(pal), ox(pal), oy(pal));
    CHECK(Field<uint8_t>(pal, kOffResumeOrder) == 10 && Field<uint8_t>(pal, kOffResumeState) == 0x14 &&
              Field<int16_t>(pal, 0x90) == 34 && Field<int16_t>(pal, 0x92) == 12,
          "and the resume bytes must be put back (resume %u state %u dest %d,%d)", Field<uint8_t>(pal, kOffResumeOrder),
          Field<uint8_t>(pal, kOffResumeState), Field<int16_t>(pal, 0x90), Field<int16_t>(pal, 0x92));
    CHECK(resume::Count() == 0 && LogContains(dir, "resumed attack-move of caster type 12"),
          "the record is forgotten once it is handed back, and logged");

    // A patrol is kept at its own pair of fields.
    pal = setup(5, 0x94, 8, 41);
    mod::RunAutocastPass();
    goIdle(pal);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderPatrol && ox(pal) == 8 && oy(pal) == 41 && Field<uint8_t>(pal, kOffResumeOrder) == 5,
          "the patrol must come back the same way (order %u at %d,%d)", OrderOf(pal), ox(pal), oy(pal));

    // The player gives the caster something else in the meantime: the mod must not fight them for it.
    pal = setup(10, 0x90, 34, 12);
    mod::RunAutocastPass();
    Field<uint16_t>(unitAt(1), kOffHp) = 60;
    Field<uint8_t>(pal, kOffOrder) = kOrderMove;  // a move order of the player's
    Field<uint8_t>(pal, kOffNextOrder) = kOrderNone;
    mod::RunAutocastPass();
    CHECK(resume::Count() == 0 && OrderOf(pal) == kOrderMove, "a new player order must drop the record (order %u)",
          OrderOf(pal));
    pal = setup(10, 0x90, 34, 12);  // or a new attack-move of their own, seen as a fresh resume byte
    mod::RunAutocastPass();
    goIdle(pal);
    Field<uint8_t>(pal, kOffResumeOrder) = 10;
    const unsigned restoredBefore = resume::RestoreCount();
    mod::RunAutocastPass();
    CHECK(resume::Count() == 0 && resume::RestoreCount() == restoredBefore,
          "a resume byte set by someone else must drop the record without re-issuing anything");

    // A caster that dies, and a record that simply grows old.
    pal = setup(10, 0x90, 34, 12);
    mod::RunAutocastPass();
    Field<uint8_t>(pal, kOffStateFlags) = kStateDying;
    mod::RunAutocastPass();
    CHECK(resume::Count() == 0, "a dead caster must drop the record");
    pal = setup(10, 0x90, 34, 12);
    mod::RunAutocastPass();
    CHECK(resume::Count() == 1, "the record is there before it expires");
    for (int i = 0; i < 605; ++i) mod::RunAutocastPass();  // 30 s of play at 50 ms a step
    CHECK(resume::Count() == 0 && OrderOf(pal) == 0x27, "the record must expire after 30 s (order %u)", OrderOf(pal));

    // Switched off: nothing is remembered at all. Checked straight after the order, with no pass in between: a pass
    // would drop the records on its own and hide a Remember that files them regardless of the setting.
    config::g.resumeOrders = false;
    pal = setup(10, 0x90, 34, 12);
    IssueOrder(pal, 21, 21, nullptr, kRvaMoveHandler);
    CHECK(resume::Count() == 0, "resume_orders = false must remember nothing (%u)", resume::Count());
    pal = setup(10, 0x90, 34, 12);
    mod::RunAutocastPass();
    CHECK(resume::Count() == 0, "and the pass must not remember anything either");
    goIdle(pal);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "and nothing is handed back (order %u)", OrderOf(pal));
    config::g.resumeOrders = true;

    // The eye of Kilrogg is the mod's own unit: never remembered, never re-ordered.
    ResetWorld();
    Unit* eye = AddUnit(kTypeEye, 0, 30, 30, 60, 0, kOrderStand);
    Field<uint32_t>(eye, kOffSerial) = ++serial;
    Field<uint8_t>(eye, kOffResumeOrder) = 10;
    IssueOrder(eye, 31, 31, nullptr, kRvaMoveHandler);
    CHECK(resume::Count() == 0, "the eye must never have a resume order remembered");

    *At<uint32_t>(kRvaRuleset) = savedRuleset;
    config::g.resumeOrders = savedResume;
    config::g.logCasts = savedLog;
    config::g.healMinMissingHp = savedMissing;
    memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
    ResetWorld();
    EnableEverythingForTests();
}

// ---- [heal] cooldown_seconds: one timer per caster, shared by Heal and Exorcism ----
static void HealCooldownTests(const wchar_t* dir, const wchar_t* ini) {
    const int savedCooldown = config::g.healCooldownSeconds, savedUrgent = config::g.healUrgentBelowPercent;
    const int savedMissing = config::g.healMinMissingHp;
    bool savedSpells[kSpellCount];
    memcpy(savedSpells, config::g.spell, sizeof(savedSpells));
    const bool savedLog = config::g.logCasts;
    config::g.logCasts = true;
    config::g.healMinMissingHp = 1;
    uint16_t* maxHp = At<uint16_t>(kRvaMaxHpByType);
    const uint16_t savedFootmanHp = maxHp[kFootman], savedSkeletonHp = maxHp[kSkeleton];
    maxHp[kFootman] = 60;
    maxHp[kSkeleton] = 40;
    uint32_t serial = 9000;
    auto paladin = [&](int mana) {
        Unit* u = AddUnit(kTypePaladin, 0, 20, 20, 90, static_cast<uint8_t>(mana), kOrderStand);
        Field<uint32_t>(u, kOffSerial) = ++serial;
        return u;
    };
    auto tick = [&](unsigned ms) { autocast::AddPlayTime(ms); };

    // A hurt footman, healed twice in a row: the cooldown must hold the second cast back.
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellHeal;
    config::g.healCooldownSeconds = 10;
    config::g.healUrgentBelowPercent = 10;
    ResetWorld();
    autocast::OnNewMap();
    Unit* pal = paladin(255);
    Unit* hurt = AddUnit(kFootman, 0, 22, 20, 30, 0, kOrderStand);  // half health: not urgent
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27, "the first heal must go out (order %u)", OrderOf(pal));
    Field<uint8_t>(pal, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal, kOffNextOrder) = kOrderNone;
    tick(5000);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "a second heal inside the cooldown must wait (order %u)", OrderOf(pal));
    tick(5000);  // 10 s of play time: the timer is up
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27, "after the cooldown the heal must come (order %u)", OrderOf(pal));

    // A target at or below urgent_below_percent breaks the cooldown.
    Field<uint8_t>(pal, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal, kOffNextOrder) = kOrderNone;
    tick(1000);
    Field<uint16_t>(hurt, kOffHp) = 5;  // 8 % of 60
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == 0x27 && LogContains(dir, "(urgent, 8 %)"),
          "a target at 8 %% must break the cooldown and say so (order %u)", OrderOf(pal));
    Field<uint8_t>(pal, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal, kOffNextOrder) = kOrderNone;
    tick(1000);
    Field<uint16_t>(hurt, kOffHp) = 30;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal) == kOrderStand, "and the timer is restarted by the urgent cast too (order %u)", OrderOf(pal));

    // Exorcism breaks the cooldown only when the mana on hand finishes the target.
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellExorcism;
    const int cost = At<uint16_t>(kRvaManaCostByOrder)[0x29];
    ResetWorld();
    autocast::OnNewMap();
    Unit* pal2 = paladin(255);
    AddUnit(kSkeleton, 1, 22, 20, 30, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal2) == 0x29, "the first exorcism must go out (order %u)", OrderOf(pal2));
    Field<uint8_t>(pal2, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal2, kOffNextOrder) = kOrderNone;
    tick(1000);
    Field<uint8_t>(pal2, kOffMana) = static_cast<uint8_t>(30 * cost - 1);  // one mana short of finishing it
    mod::RunAutocastPass();
    CHECK(OrderOf(pal2) == kOrderStand, "an exorcism that cannot finish the target must wait (order %u)", OrderOf(pal2));
    Field<uint8_t>(pal2, kOffMana) = static_cast<uint8_t>(30 * cost);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal2) == 0x29 && LogContains(dir, "(urgent, 30 hp for "),
          "an exorcism that finishes the target must break the cooldown (order %u)", OrderOf(pal2));

    // Either spell restarts the shared timer.
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellHeal || i == kSpellExorcism;
    ResetWorld();
    autocast::OnNewMap();
    Unit* pal3 = paladin(255);
    AddUnit(kSkeleton, 1, 22, 20, 30, 0, kOrderStand);
    Unit* hurt3 = AddUnit(kFootman, 0, 21, 22, 30, 0, kOrderStand);
    mod::RunAutocastPass();
    const uint8_t firstOrder = OrderOf(pal3);
    CHECK(firstOrder == 0x27 || firstOrder == 0x29, "one of the two spells must go out (order %u)", firstOrder);
    Field<uint8_t>(pal3, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal3, kOffNextOrder) = kOrderNone;
    // Not enough mana to finish the skeleton either, so neither spell has an excuse to break the timer.
    Field<uint8_t>(pal3, kOffMana) = static_cast<uint8_t>(30 * cost - 1);
    tick(2000);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal3) == kOrderStand, "the other spell must wait on the same timer (order %u)", OrderOf(pal3));
    (void)hurt3;

    // cooldown_seconds = 0 is the behaviour of every version before this one.
    config::g.healCooldownSeconds = 0;
    ResetWorld();
    autocast::OnNewMap();
    Unit* pal4 = paladin(255);
    AddUnit(kFootman, 0, 22, 20, 30, 0, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(pal4) == 0x27, "cooldown 0: the first heal (order %u)", OrderOf(pal4));
    Field<uint8_t>(pal4, kOffOrder) = kOrderStand;
    Field<uint8_t>(pal4, kOffNextOrder) = kOrderNone;
    mod::RunAutocastPass();
    CHECK(OrderOf(pal4) == 0x27, "cooldown 0 must heal again at once (order %u)", OrderOf(pal4));

    // The reader.
    WriteFileText(ini, "[heal]\ncooldown_seconds = 601\nurgent_below_percent = 25\nbogus = 1\n"
                       "cooldown_for_computer = false\n[unit.ballista]\nreact_range = 11\n");
    CHECK(config::Init(dir), "[heal] cooldown config rejected");
    CHECK(config::g.healCooldownSeconds == 600 && config::g.healUrgentBelowPercent == 25 &&
              LogContains(dir, "unknown key [heal] bogus"),
          "[heal] cooldown_seconds clamps to 600 and urgent_below_percent reads (%d %d)", config::g.healCooldownSeconds,
          config::g.healUrgentBelowPercent);
    CHECK(!config::g.healCooldownForComputer && config::g.unitStat[4][kStatReactRange] == 11 &&
              !LogContains(dir, "unknown key [heal] cooldown_for_computer") &&
              !LogContains(dir, "unknown key [unit.ballista] react_range"),
          "every key the reader accepts must be in the known-key list, or the player is told it is a typo");
    DeleteFileW(ini);
    CHECK(config::Init(dir), "the default config did not come back");
    CHECK(config::g.healCooldownSeconds == 0 && config::g.healUrgentBelowPercent == 10 && config::g.healCooldownForComputer,
          "the shipped defaults are cooldown 0, urgent 10 %% and the computer on the same timer");

    maxHp[kFootman] = savedFootmanHp;
    maxHp[kSkeleton] = savedSkeletonHp;
    config::g.healCooldownSeconds = savedCooldown;
    config::g.healUrgentBelowPercent = savedUrgent;
    config::g.healMinMissingHp = savedMissing;
    memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
    config::g.logCasts = savedLog;
    ResetWorld();
    autocast::OnNewMap();
    EnableEverythingForTests();
}

// ---- weapon / armor types and the damage hooks (src/damagetypes.cpp, docs/research/damage.md) ----
static int g_stubDamage = 0;
static Unit* g_stubAttacker = nullptr;
static int __cdecl StubRoll(Unit* attacker) {
    g_stubAttacker = attacker;
    return g_stubDamage;
}
static int __cdecl StubTower(Unit* attacker, Unit* target) {
    g_stubAttacker = attacker;
    (void)target;
    return g_stubDamage;
}

static void DamageTypeTests(const wchar_t* dir, const wchar_t* ini) {
    // The exe still has the four call sites and the three callees the mod knows.
    {
        struct Site { uint32_t rva, callee; const char* what; };
        const Site sites[] = {{kRvaMeleeRollSite, kRvaDamageRoll, "melee"},
                              {kRvaMissileRollSite, kRvaDamageRoll, "missile"},
                              {kRvaTowerRollSite, kRvaDamageRollTarget, "tower"},
                              {kRvaSplashApplySite, kRvaApplyDamage, "splash"}};
        for (const Site& s : sites) {
            const uint8_t* at = At<uint8_t>(s.rva);
            int32_t rel;
            memcpy(&rel, at + 1, sizeof(rel));
            CHECK(at[0] == 0xE8 && reinterpret_cast<uintptr_t>(at) + 5 + rel == g_base + s.callee,
                  "the %s damage call site 0x%06X no longer calls 0x%06X", s.what, 0x400000 + s.rva, 0x400000 + s.callee);
        }
        // The splash filter: the game's own "this missile type splashes" table, spell missile types are not in it.
        const uint8_t* splashes = At<uint8_t>(kRvaMissileSplashes);
        CHECK(splashes[7] && splashes[13] && splashes[14] && splashes[24], "the splashing weapon missile types changed");
        CHECK(!splashes[2] && !splashes[3] && !splashes[4] && !splashes[5] && !splashes[6] && !splashes[12],
              "a spell missile type is marked as a splashing weapon: the filter would scale spell damage");
    }

    // The install is all four sites or none, and it refuses a site whose bytes changed.
    {
        const uint32_t rvas[4] = {kRvaMeleeRollSite, kRvaMissileRollSite, kRvaTowerRollSite, kRvaSplashApplySite};
        uint8_t before[4][5];
        for (int i = 0; i < 4; ++i) memcpy(before[i], At<uint8_t>(rvas[i]), 5);
        auto poke = [&](uint32_t rva, int offset, uint8_t value) {
            uint8_t* at = At<uint8_t>(rva);
            DWORD old;
            VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
            at[offset] = value;
            VirtualProtect(at, 5, old, &old);
        };
        poke(kRvaTowerRollSite, 1, static_cast<uint8_t>(before[2][1] ^ 0xFF));  // it now calls somewhere else
        CHECK(!damagetypes::InstallHooks(g_base) && !damagetypes::Installed(),
              "a changed call site must refuse the whole install");
        bool untouched = true;
        for (int i = 0; i < 4; ++i)
            if (i != 2) untouched = untouched && memcmp(At<uint8_t>(rvas[i]), before[i], 5) == 0;
        CHECK(untouched, "a refused install must leave every site alone");
        poke(kRvaTowerRollSite, 1, before[2][1]);
        CHECK(damagetypes::InstallHooks(g_base) && damagetypes::Installed(), "the install must take on the game's own bytes");
        bool redirected = true;
        for (int i = 0; i < 4; ++i) redirected = redirected && memcmp(At<uint8_t>(rvas[i]), before[i], 5) != 0;
        CHECK(redirected, "every site must be redirected once the install took");
        for (int i = 0; i < 4; ++i)
            for (int b = 0; b < 5; ++b) poke(rvas[i], b, before[i][b]);  // the image goes back as it was
    }

    const DamageTypes savedTypes = config::g.damageTypes;
    ResetWorld();
    // The author's own example: siege weapons hit structures twice as hard and ships half again.
    WriteFileText(ini,
                  "[weapon_types]\nsiege = [\"ballista\", \"catapult\", \"human_cannon_tower\"]\n"
                  "[armor_types]\nstructure = [\"structures\"]\nship = [\"ships\"]\n"
                  "[damage_bonus.siege]\nstructure = 2.0\nship = 1.5\n");
    CHECK(config::Init(dir), "[weapon_types] config rejected");
    {
        const DamageTypes& d = config::g.damageTypes;
        CHECK(d.weaponCount == 1 && d.armorCount == 2 && d.any && d.bonusCount == 2,
              "one weapon type, two armor types, two bonuses (%d %d %d)", d.weaponCount, d.armorCount, d.bonusCount);
        CHECK(d.weaponOf[0x04] == 0 && d.weaponOf[0x05] == 0 && d.weaponOf[0x62] == 0 && d.weaponOf[0x00] == kNoDamageType,
              "ballista, catapult and the human cannon tower carry the siege weapon type");
        // Sections are read in alphabetical key order (that is how the TOML reader stores a table), so the index a
        // type gets is not the file order: look them up by name.
        auto armorIndex = [&](const char* name) {
            for (int i = 0; i < d.armorCount; ++i)
                if (strcmp(d.armorName[i], name) == 0) return static_cast<uint8_t>(i);
            return kNoDamageType;
        };
        CHECK(d.armorOf[0x4A] == armorIndex("structure") && d.armorOf[0x1E] == armorIndex("ship") &&
                  d.armorOf[0x00] == kNoDamageType && d.armorOf[0x2B] == kNoDamageType,
              "structures and ships carry their armor types, footmen and dragons carry none");
        CHECK(LogContains(dir, "damage types: weapon siege (3 units), armor ") && LogContains(dir, "; 2 bonuses"),
              "the config load must log the types it read");
    }
    // The rule itself: x2 on a town hall, x1.5 on a destroyer, x1 on a footman and a dragon.
    {
        Unit* ballista = AddUnit(0x04, 0, 10, 10, 100, 0, kOrderStand);
        Unit* hall = AddUnit(0x4A, 1, 20, 20, 1200, 0, kOrderStand);
        Unit* destroyer = AddUnit(0x1E, 1, 22, 20, 100, 0, kOrderStand);
        Unit* footman = AddUnit(0x00, 1, 24, 20, 60, 0, kOrderStand);
        Unit* dragon = AddUnit(0x2B, 1, 26, 20, 100, 0, kOrderStand);
        Unit* knight = AddUnit(0x06, 0, 12, 10, 90, 0, kOrderStand);
        CHECK(damagetypes::Scale(40, ballista, hall) == 80 && damagetypes::Scale(40, ballista, destroyer) == 60 &&
                  damagetypes::Scale(40, ballista, footman) == 40 && damagetypes::Scale(40, ballista, dragon) == 40,
              "the bonus must be x2 on a hall, x1.5 on a ship, x1 elsewhere (%d %d %d %d)",
              damagetypes::Scale(40, ballista, hall), damagetypes::Scale(40, ballista, destroyer),
              damagetypes::Scale(40, ballista, footman), damagetypes::Scale(40, ballista, dragon));
        CHECK(damagetypes::Scale(40, knight, hall) == 40, "a weapon type nobody assigned must change nothing");
        // Rounding, the clamps and the guards.
        CHECK(damagetypes::Scale(5, ballista, destroyer) == 8 && damagetypes::Scale(1, ballista, destroyer) == 2,
              "x1.5 must round to nearest (5 -> %d, 1 -> %d)", damagetypes::Scale(5, ballista, destroyer),
              damagetypes::Scale(1, ballista, destroyer));
        CHECK(damagetypes::Scale(200, ballista, hall) == 255, "the result must clamp at 255");
        CHECK(damagetypes::Scale(0, ballista, hall) == 0, "zero damage stays zero");
        CHECK(damagetypes::Scale(40, ballista, nullptr) == 40 && damagetypes::Scale(40, nullptr, hall) == 40,
              "a null unit must leave the damage alone");
        static uint8_t outside[kUnitSize] = {};  // a unit-shaped block that is not in the game's array
        outside[kOffType] = 0x4A;
        CHECK(damagetypes::Scale(40, ballista, reinterpret_cast<Unit*>(outside)) == 40,
              "a pointer outside the unit array must be refused");
        CHECK(damagetypes::Scale(40, reinterpret_cast<Unit*>(reinterpret_cast<uint8_t*>(ballista) + 3), hall) == 40,
              "a pointer that is not on a unit boundary must be refused");
        *At<uint32_t>(kRvaNetGame) = 1;
        CHECK(damagetypes::Scale(40, ballista, hall) == 40, "a multiplayer game must never be scaled");
        *At<uint32_t>(kRvaNetGame) = 0;
        // The thunks, driven through a stub instead of the engine.
        damagetypes::SetOriginalsForTest(&StubRoll, &StubTower);
        g_stubDamage = 40;
        Field<Unit*>(ballista, kOffOrderTarget) = hall;
        CHECK(damagetypes::RollThunkForTest(ballista) == 80 && g_stubAttacker == ballista,
              "the roll thunk must scale with the attacker's own target");
        Field<Unit*>(ballista, kOffOrderTarget) = nullptr;
        CHECK(damagetypes::RollThunkForTest(ballista) == 40, "no target: the damage must come through untouched");
        CHECK(damagetypes::TowerThunkForTest(ballista, destroyer) == 60, "the tower thunk must scale with its own target");
        // The splash hit is scaled per victim, and only for missile types the game marks as splashing weapons.
        static uint8_t splashMissile[kMissileSize] = {};
        splashMissile[kMisOffType] = 7;  // a catapult / ballista weapon missile
        CHECK(damagetypes::SplashScaleForTest(splashMissile, ballista, hall, 40) == 80 &&
                  damagetypes::SplashScaleForTest(splashMissile, ballista, footman, 40) == 40,
              "the splash hit must use the victim in front of it");
        splashMissile[kMisOffType] = 5;  // blizzard: a spell missile, never scaled
        CHECK(damagetypes::SplashScaleForTest(splashMissile, ballista, hall, 40) == 40 &&
                  damagetypes::SplashScaleForTest(nullptr, ballista, hall, 40) == 40,
              "spell splash and a missing missile must go through untouched");
        Field<Unit*>(ballista, kOffOrderTarget) = nullptr;
    }
    // A specific name beats a group, a second assignment warns and the first wins, unknown names warn.
    WriteFileText(ini,
                  "[weapon_types]\nsiege = [\"ballista\"]\nheavy = [\"ballista\", \"nonsense_unit\"]\n"
                  "[armor_types]\nfortified = [\"structures\"]\nkeep = [\"castle\"]\n"
                  "[damage_bonus.siege]\nfortified = 3.0\nkeep = 4.0\nmissing = 2.0\n"
                  "[damage_bonus.nosuchweapon]\nfortified = 2.0\n");
    CHECK(config::Init(dir), "the second damage-type config was rejected");
    {
        const DamageTypes& d = config::g.damageTypes;
        // "heavy" sorts before "siege", and the reader walks a table in that order: the first claim keeps the unit.
        CHECK(strcmp(d.weaponName[d.weaponOf[0x04]], "heavy") == 0 && LogContains(dir, "already has the weapon type heavy"),
              "the first claim on a unit wins and the second is logged (%s)", d.weaponName[d.weaponOf[0x04]]);
        CHECK(d.armorOf[0x5A] == 1 && d.armorOf[0x4A] == 0,
              "a named building must beat the structures group (castle %u, hall %u)", d.armorOf[0x5A], d.armorOf[0x4A]);
        CHECK(LogContains(dir, "unknown unit \"nonsense_unit\" ignored") &&
                  LogContains(dir, "[damage_bonus.siege] there is no armor type called \"missing\"") &&
                  LogContains(dir, "[damage_bonus.nosuchweapon] there is no weapon type called"),
              "an unknown unit, armor type and weapon type must each be logged");
    }
    // More than 32 types of one kind: the rest are refused with a warning, and nothing else breaks.
    {
        char toml[2048] = "[weapon_types]\n";
        for (int i = 0; i < 34; ++i) {
            char line[48];
            sprintf_s(line, "w%d = [\"ballista\"]\n", i);
            strcat_s(toml, line);
        }
        WriteFileText(ini, toml);
        CHECK(config::Init(dir), "the 34-type config was rejected");
        CHECK(config::g.damageTypes.weaponCount == kMaxDamageTypes && LogContains(dir, "more than 32 weapon types, ignored"),
              "at most %d weapon types, the rest logged (%d)", kMaxDamageTypes, config::g.damageTypes.weaponCount);
    }
    // Nothing configured: the hook passes everything through.
    DeleteFileW(ini);
    CHECK(config::Init(dir), "the default config did not come back");
    {
        Unit* ballista = AddUnit(0x04, 0, 30, 30, 100, 0, kOrderStand);
        Unit* hall = AddUnit(0x4A, 1, 34, 30, 1200, 0, kOrderStand);
        CHECK(!config::g.damageTypes.any && damagetypes::Scale(40, ballista, hall) == 40,
              "the shipped config must leave every hit alone");
    }
    config::g.damageTypes = savedTypes;
    ResetWorld();
    EnableEverythingForTests();
}

// ---- [upgrades]: the per-upgrade-group effect bytes (src/upgrades.cpp, docs/research/damage.md) ----
static void UpgradeTests(const wchar_t* dir, const wchar_t* ini) {
    uint8_t* table = At<uint8_t>(kRvaUpgradeEffects);
    const int idx[kUpgradeEffectCount] = {0, 1, 2, 3, 4, 6};
    uint8_t saved[kUpgradeEffectTableLen];
    memcpy(saved, table, sizeof(saved));
    const int savedCfg[kUpgradeEffectCount] = {config::g.upgradeEffect[0], config::g.upgradeEffect[1],
                                               config::g.upgradeEffect[2], config::g.upgradeEffect[3],
                                               config::g.upgradeEffect[4], config::g.upgradeEffect[5]};
    auto tableIs = [&](int missile, int melee, int shields, int shipDmg, int shipArm, int siege) {
        const int want[kUpgradeEffectCount] = {missile, melee, shields, shipDmg, shipArm, siege};
        for (int i = 0; i < kUpgradeEffectCount; ++i)
            if (table[idx[i]] != want[i]) return false;
        return true;
    };
    // The exe's own bytes are the ones the mod knows, and entries 5 and 7..10 are never touched.
    CHECK(tableIs(2, 2, 2, 5, 5, 15) && table[5] == 10 && table[7] == 0 && table[8] == 1 && table[9] == 0xFF &&
              table[10] == 3,
          "the upgrade effect table is not 02 02 02 05 05 0a 0f 00 01 ff 03");
    const unsigned before = upgrades::WriteCount();
    upgrades::Sync(false);
    CHECK(upgrades::WriteCount() == before && tableIs(2, 2, 2, 5, 5, 15), "the shipped config must write no effect byte");
    // The author's own case plus one of every other key.
    config::g.upgradeEffect[kUpgradeSiegeDamage] = 30;
    config::g.upgradeEffect[kUpgradeMissileDamage] = 4;
    config::g.upgradeEffect[kUpgradeShipDamage] = 0;
    upgrades::Sync(false);
    CHECK(tableIs(4, 2, 2, 0, 5, 30), "[upgrades] must write the configured bytes (%u %u %u %u %u %u)", table[0], table[1],
          table[2], table[3], table[4], table[6]);
    CHECK(table[5] == 10 && table[8] == 1, "the dead entry 5 and the range byte must be left alone");
    // Multiplayer: the game's own numbers, and back again afterwards.
    upgrades::Sync(true);
    CHECK(tableIs(2, 2, 2, 5, 5, 15), "a multiplayer game must get the game's own upgrade numbers");
    upgrades::Sync(false);
    CHECK(tableIs(4, 2, 2, 0, 5, 30), "and single player gets the configured ones back");
    // -1 puts one line back without touching the others.
    config::g.upgradeEffect[kUpgradeSiegeDamage] = -1;
    upgrades::Sync(false);
    CHECK(tableIs(4, 2, 2, 0, 5, 15), "-1 must restore the game's siege number only");
    // A byte that is neither the game's nor the mod's last write is left alone for the session.
    table[idx[kUpgradeMissileDamage]] = 99;
    config::g.upgradeEffect[kUpgradeMissileDamage] = 6;
    upgrades::Sync(false);
    CHECK(table[idx[kUpgradeMissileDamage]] == 99 && LogContains(dir, "upgrades: missile_damage is 99, neither the game's 2"),
          "a foreign effect byte must be left alone and logged (%u)", table[idx[kUpgradeMissileDamage]]);
    table[idx[kUpgradeMissileDamage]] = 2;
    for (int i = 0; i < kUpgradeEffectCount; ++i) config::g.upgradeEffect[i] = -1;
    upgrades::Sync(false);
    CHECK(tableIs(2, 2, 2, 5, 5, 15) || table[idx[kUpgradeMissileDamage]] == 2, "everything back to the game's numbers");
    // A table that is not the one this mod knows switches the whole feature off, before anything is written.
    table[idx[kUpgradeSiegeDamage]] = 7;
    upgrades::ResetForTest();
    config::g.upgradeEffect[kUpgradeSiegeDamage] = 30;
    upgrades::Sync(false);
    CHECK(table[idx[kUpgradeSiegeDamage]] == 7 &&
              LogContains(dir, "upgrades: the upgrade effect table is not the one this mod knows"),
          "a foreign effect table must switch [upgrades] off (%u)", table[idx[kUpgradeSiegeDamage]]);
    table[idx[kUpgradeSiegeDamage]] = 15;
    upgrades::ResetForTest();
    config::g.upgradeEffect[kUpgradeSiegeDamage] = -1;
    upgrades::Sync(false);

    // The reader: range, typos, and the log line of the new-map hook.
    WriteFileText(ini, "[upgrades]\nsiege_damage = 30\nmelee_damage = 101\nship_armor = -5\nbogus = 3\n");
    CHECK(config::Init(dir), "[upgrades] config rejected");
    CHECK(config::g.upgradeEffect[kUpgradeSiegeDamage] == 30 && config::g.upgradeEffect[kUpgradeMeleeDamage] == 100 &&
              config::g.upgradeEffect[kUpgradeShipArmor] == -1 && config::g.upgradeEffect[kUpgradeShields] == -1,
          "[upgrades] keys (siege %d melee %d ship_armor %d)", config::g.upgradeEffect[kUpgradeSiegeDamage],
          config::g.upgradeEffect[kUpgradeMeleeDamage], config::g.upgradeEffect[kUpgradeShipArmor]);
    CHECK(LogContains(dir, "unknown key [upgrades] bogus") && LogContains(dir, "[upgrades] melee_damage = 101 is outside -1..100"),
          "an unknown [upgrades] key and an out-of-range value must be logged");
    upgrades::OnNewMap(false);
    CHECK(LogContains(dir, "upgrades: melee_damage 2->100 siege_damage 15->30"), "the new-map line must list what changed");
    upgrades::Sync(true);
    DeleteFileW(ini);
    CHECK(config::Init(dir), "the default config did not come back");
    for (int i = 0; i < kUpgradeEffectCount; ++i) config::g.upgradeEffect[i] = savedCfg[i];
    upgrades::Sync(false);
    memcpy(table, saved, sizeof(saved));
    EnableEverythingForTests();
}

// ---- [auto_production] (src/production.cpp, docs/research/production.md) ----
// StartProduction (0x4ACE10) is swapped for this recorder: it does to the building, the bank and the training counter
// what the game's function does, so a pass over several buildings sees the same numbers it would in the game.
struct ProdStart {
    Unit* at;
    uint8_t type;
};
static ProdStart g_prodStarts[512];
static int g_prodStartCount = 0, g_prodAttempts = 0;
static bool g_prodRefuse = false;
static uint32_t g_prodSerial = 5000;
static uint16_t g_prodSquare[kMap * kMap];  // the square-flag map the production tests own (water bit 0x40)

static int __cdecl FakeStartProduction(Unit* b, uint8_t id, uint8_t kind) {
    ++g_prodAttempts;
    if (g_prodRefuse || kind != 0 || (Field<uint16_t>(b, kOffJobFlags) & 0x10)) return 0;
    const uint8_t p = OwnerOf(b);
    At<int32_t>(kRvaPlayerGold)[p] -= At<uint8_t>(kRvaGoldCostByType)[id] * 10;
    At<int32_t>(kRvaPlayerLumber)[p] -= At<uint8_t>(kRvaLumberCostByType)[id] * 10;
    At<int32_t>(kRvaPlayerOil)[p] -= At<uint8_t>(kRvaOilCostByType)[id] * 10;
    ++At<uint16_t>(kRvaUnitsInTraining)[p];
    Field<uint16_t>(b, kOffJobFlags) |= 0x10;
    Field<uint8_t>(b, kOffJobKind) = kind;
    Field<uint8_t>(b, kOffJobId) = id;
    if (g_prodStartCount < 512) g_prodStarts[g_prodStartCount++] = {b, id};
    return 1;
}

static Unit* AddProd(uint8_t type, uint8_t owner, int x, int y) {
    Unit* u = AddUnit(type, owner, x, y, 100, 0, kOrderStand);
    Field<uint32_t>(u, kOffSerial) = ++g_prodSerial;
    if (At<uint32_t>(kRvaTypeFlags)[type] & kTfBuilding) {
        Field<uint16_t>(u, kOffStateFlags) = kStateComplete;
        Field<uint8_t>(u, kOffOrder) = 0x21;  // idle building
    } else {
        ++At<uint16_t>(kRvaUnitsCounted)[owner];
    }
    return u;
}

// Training ends in every busy building of `owner`: the unit appears next to it (FUN_004acb00), or, with placed =
// false, "exit blocked": no unit, the money comes back.
static void FinishTraining(uint8_t owner, bool placed = true) {
    const int n = g_unitCount;
    for (int i = 0; i < n; ++i) {
        Unit* b = reinterpret_cast<Unit*>(g_units + i * kUnitSize);
        if (OwnerOf(b) != owner || !(Field<uint16_t>(b, kOffJobFlags) & 0x10)) continue;
        const uint8_t id = Field<uint8_t>(b, kOffJobId);
        Field<uint16_t>(b, kOffJobFlags) &= ~0x30;
        --At<uint16_t>(kRvaUnitsInTraining)[owner];
        if (placed && g_unitCount < 64) {
            AddProd(id, owner, g_unitCount % kMap, 40 + g_unitCount / kMap);
        } else {
            At<int32_t>(kRvaPlayerGold)[owner] += At<uint8_t>(kRvaGoldCostByType)[id] * 10;
            At<int32_t>(kRvaPlayerLumber)[owner] += At<uint8_t>(kRvaLumberCostByType)[id] * 10;
            At<int32_t>(kRvaPlayerOil)[owner] += At<uint8_t>(kRvaOilCostByType)[id] * 10;
        }
    }
}

static int StartsOf(uint8_t type) {
    int n = 0;
    for (int i = 0; i < g_prodStartCount; ++i) n += g_prodStarts[i].type == type;
    return n;
}
static int StartsAt(Unit* b) {
    int n = 0;
    for (int i = 0; i < g_prodStartCount; ++i) n += g_prodStarts[i].at == b;
    return n;
}

// Everything the production tests write into the image, saved once and put back at the end.
struct ProdSnapshot {
    uint32_t rva;
    uint32_t size;
    uint8_t bytes[0x200];
};
static ProdSnapshot g_prodSnaps[] = {
    {kRvaTypeFlags, 110 * 4, {}},
    {kRvaGoldCostByType, 110, {}},      {kRvaLumberCostByType, 110, {}}, {kRvaOilCostByType, 110, {}},
    {kRvaUpgradeGold, 104, {}},         {kRvaUpgradeLumber, 104, {}},    {kRvaUpgradeOil, 104, {}},
    {kRvaUnitsAllowed, 0x180, {}},      {kRvaUpgradeLevels, 0xC0, {}},   {kRvaFoodSupply, 32, {}},
    {kRvaUnitsCounted, 32, {}},         {kRvaFoodFreeUnits, 32, {}},     {kRvaUnitsInTraining, 32, {}},
    {kRvaPlayerGold, 64, {}},           {kRvaPlayerLumber, 64, {}},      {kRvaPlayerOil, 64, {}},
    {kRvaSelectedUnit, 4 + 12 * 4, {}}, {kRvaSquareFlags, 4, {}},   {kRvaAlliance, 16 * 16, {}},
};
static void ProdSave() {
    for (auto& s : g_prodSnaps) memcpy(s.bytes, At<uint8_t>(s.rva), s.size < sizeof(s.bytes) ? s.size : sizeof(s.bytes));
}
static void ProdRestore() {
    for (auto& s : g_prodSnaps) memcpy(At<uint8_t>(s.rva), s.bytes, s.size < sizeof(s.bytes) ? s.size : sizeof(s.bytes));
}

// A fresh game for player 0: rich, 200 food, dry land, everything allowed except the two hall upgrades (so the reserve
// starts at zero), no research, nothing selected. Prices are the game's (unitdata.dat, tens).
static void ProdWorld() {
    ResetWorld();
    g_prodStartCount = g_prodAttempts = 0;
    g_prodRefuse = false;
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    for (int t = 0x3A; t <= 0x63; ++t) tf[t] = kTfBuilding;
    tf[0x56] = tf[0x57] = kTfBuilding | kTfOilPlatform;
    tf[kTypeOilPatch] = 0;  // scenery, not a building: only the map profile counts it
    const struct { uint8_t type, gold, lumber, oil; } kPrices[] = {
        {0x00, 60, 0, 0},   {0x02, 40, 0, 0},     {0x04, 90, 30, 0},    {0x06, 80, 10, 0},  {0x08, 50, 5, 0},
        {0x0A, 120, 0, 0},  {0x0C, 80, 10, 0},    {0x0E, 70, 25, 0},    {0x12, 50, 5, 0},   {0x1A, 40, 20, 0},
        {0x1C, 60, 20, 50}, {0x1E, 70, 35, 70},   {0x20, 100, 50, 100}, {0x26, 80, 15, 90}, {0x28, 50, 10, 0},
        {0x2A, 225, 0, 0},  {0x58, 200, 100, 20}, {0x5A, 250, 120, 50}, {0x60, 50, 15, 0},  {0x62, 100, 30, 0},
    };
    for (const auto& pr : kPrices)
        for (int race = 0; race < 2; ++race) {
            At<uint8_t>(kRvaGoldCostByType)[pr.type + race] = pr.gold;
            At<uint8_t>(kRvaLumberCostByType)[pr.type + race] = pr.lumber;
            At<uint8_t>(kRvaOilCostByType)[pr.type + race] = pr.oil;
        }
    memset(At<uint8_t>(kRvaUnitsAllowed), 0, 0x180);  // ALOW: units, spells known / allowed / in research, upgrades
    for (int p = 0; p < 2; ++p) At<uint32_t>(kRvaUnitsAllowed)[p] = 0x07FFFFFF;  // no keep (bit 27) / castle (bit 28) upgrade
    memset(At<uint8_t>(kRvaUpgradeLevels), 0, 0xC0);
    memset(At<uint8_t>(kRvaUpgradeGold), 0, 104);
    memset(At<uint8_t>(kRvaUpgradeLumber), 0, 104);
    memset(At<uint8_t>(kRvaUpgradeOil), 0, 104);
    for (int p = 0; p < 2; ++p) {
        At<uint16_t>(kRvaFoodSupply)[p] = 200;
        At<uint16_t>(kRvaUnitsCounted)[p] = At<uint16_t>(kRvaFoodFreeUnits)[p] = At<uint16_t>(kRvaUnitsInTraining)[p] = 0;
        At<int32_t>(kRvaPlayerGold)[p] = At<int32_t>(kRvaPlayerLumber)[p] = At<int32_t>(kRvaPlayerOil)[p] = 100000;
    }
    memset(At<uint8_t>(kRvaSelectedUnit), 0, 4 + 12 * 4);
    memset(At<uint8_t>(kRvaAlliance), 0, 16 * 16);  // everyone hostile to everyone but himself
    for (int i = 0; i < 16; ++i) At<uint8_t>(kRvaAlliance)[i * 16 + i] = 1;
    memset(g_prodSquare, 0, sizeof(g_prodSquare));  // all land
    *At<uint16_t*>(kRvaSquareFlags) = g_prodSquare;
    production::OnNewMap();
    config::g.production = AutoProduction();
    config::g.production.enabled = true;
    config::g.logCasts = false;
}

// Makes `percent` of the map water and adds `oilPatches` oil patches, then has the profile counted again.
static void ProdWaterMap(int percent, int oilPatches) {
    const int tiles = kMap * kMap;
    for (int i = 0; i < tiles; ++i) g_prodSquare[i] = static_cast<uint16_t>(i < tiles * percent / 100 ? kSqWater : kSqLand);
    for (int i = 0; i < oilPatches; ++i) AddProd(kTypeOilPatch, kNeutralPlayer, 40 + i, 60);
    production::OnNewMap();
}

// An enemy shipyard somewhere on the map: that alone lifts the "the enemy has no navy" ship caps.
static Unit* AddEnemyShipyard(uint8_t owner = 1) { return AddProd(0x49, owner, 60, 60); }

static bool ProdPass(unsigned nowMs) {
    World w;
    if (!BuildWorld(w)) return false;
    production::Pass(w, nowMs);
    return true;
}

// A decision-core plan: tier, every class trainable, the game's human prices, a big bank.
static production::Plan CorePlan(int tier) {
    production::Plan p;
    p.tier = tier;
    p.bank = {{1000000, 1000000, 1000000}};
    p.supply = 200;
    const int prices[kProdClassCount][3] = {{400, 0, 0},     {600, 0, 0},       {500, 50, 0},  {800, 100, 0},
                                            {1200, 0, 0},    {2250, 0, 0},      {900, 300, 0}, {400, 200, 0},
                                            {700, 350, 700}, {1000, 500, 1000}, {800, 150, 900}};
    for (int c = 0; c < kProdClassCount; ++c) {
        p.trainable[c] = true;
        for (int r = 0; r < 3; ++r) p.cost[c].r[r] = prices[c][r];
    }
    return p;
}
static unsigned ArmyMask() {
    unsigned m = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (production::IsArmy(c)) m |= 1u << c;
    return m;
}
static unsigned GroupMask(production::Group g) {
    unsigned m = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (production::GroupOf(c) == g) m |= 1u << c;
    return m;
}
constexpr unsigned kBarracksMask = 1u << kProdInfantry | 1u << kProdArchers | 1u << kProdKnights | 1u << kProdSiege;

// Trains `rounds` units out of a plan with money for everything, and checks the counts came out in the configured
// percentages of the group the map's navy share points at (navyShare 0 = all land, 1 = all ships).
static void CheckMix(int tier, production::Group group, double navyShare, const AutoProduction& cfg,
                     const int (&mix)[kProdTiers][kProdClassCount], int rounds, const char* what) {
    production::Plan p = CorePlan(tier);
    p.navyShare = navyShare;
    for (int i = 0; i < rounds; ++i) {
        bool saving = false;
        const int c = production::PickArmyClass(p, cfg, ArmyMask(), &saving);
        if (c < 0) break;
        production::Commit(p, c);
    }
    int sum = 0;
    for (int c = 0; c < kProdClassCount; ++c)
        if (production::GroupOf(c) == group) sum += mix[tier - 1][c];
    bool ok = production::ArmySize(p) == rounds && sum > 0;
    char got[256] = "";
    for (int c = 0; c < kProdClassCount; ++c) {
        if (production::GroupOf(c) == production::kGroupNone) continue;
        const double want = production::GroupOf(c) == group ? mix[tier - 1][c] * static_cast<double>(rounds) / sum : 0;
        ok = ok && fabs(p.count[c] - want) <= 2;
        char one[32];
        sprintf_s(one, "%d/%d ", p.count[c], static_cast<int>(want + 0.5));
        strcat_s(got, one);
    }
    CHECK(ok, "%s: got/wanted per class %s(army %d)", what, got, production::ArmySize(p));
}

// The author's live case (1.10.1): a tier-3 army on a map that wants 23 % ships, destroyers sitting at their share,
// battleships far behind, and a bank that is enormous in gold and lumber but thin in oil. 2950 oil is the level the
// mod bought a destroyer at: (2950 - 120 reserve) / 700 = 4.04 prices, while a battleship wants 4 x 1000 + 120.
static production::Plan ShipyardCase(int oil) {
    production::Plan p = CorePlan(3);
    p.navyShare = 0.23;
    p.bank = {{590000, 41000, oil}};
    p.reserve = {{2600, 1020, 120}};
    const int alive[kProdClassCount] = {0, 20, 20, 40, 15, 6, 4, 0, 10, 2, 0};
    for (int c = 0; c < kProdClassCount; ++c) p.count[c] = alive[c];
    return p;
}

static bool g_fakeCtrl = false, g_fakeF10 = false;
static bool FakeKeys(int vk) { return (vk == VK_CONTROL && g_fakeCtrl) || (vk == VK_F10 && g_fakeF10); }
// [area_values], [autocast] lookahead_tiles / area_settle_percent / area_reserve_value (1.32): what a Blizzard target is
// worth by type, no cheap cast when a better spot is a few tiles further, and mana kept back for a good area target.
static void AreaValueTests(const wchar_t* dir, const wchar_t* ini) {
    const AreaValues defaults;
    CHECK(defaults.pct[0x60] == 300 && defaults.pct[0x63] == 300 && defaults.pct[0x3C] == 150 && defaults.pct[0x4A] == 150 &&
              defaults.pct[0x3A] == 30 && defaults.pct[0x40] == 30 && defaults.pct[0x56] == 30 && defaults.pct[0x67] == 30 &&
              defaults.pct[0x0A] == 150 && defaults.pct[0x05] == 150 && defaults.pct[0x01] == 100 && defaults.pct[0x5C] == 100,
          "[area_values] defaults: towers 3, production 1.5, farms / scout towers / platforms / walls 0.3, casters / siege 1.5");
    const Config shipped;
    CHECK(shipped.areaLookaheadTiles == 8 && shipped.areaSettlePercent == 50 && shipped.areaReserveValue == 4.0,
          "[autocast] lookahead_tiles 8, area_settle_percent 50, area_reserve_value 4 by default");

    constexpr uint8_t kFarm = 0x3A, kTower = 0x60;
    struct Sz { uint16_t w, h; };
    Sz* sizes = At<Sz>(kRvaUnitSizeByType);
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    uint16_t* maxHp = At<uint16_t>(kRvaMaxHpByType);
    const Sz savedSz[2] = {sizes[kFarm], sizes[kTower]};
    const uint32_t savedTf[3] = {tf[kFarm], tf[kTower], tf[kGrunt]};
    const uint16_t savedHp[2] = {maxHp[kFarm], maxHp[kTower]};
    sizes[kFarm] = sizes[kTower] = {2, 2};
    tf[kFarm] = tf[kTower] = kTfBuilding;
    tf[kGrunt] = kTfFleshy | kTfAttacker;
    maxHp[kFarm] = 400;
    maxHp[kTower] = 130;
    bool savedSpells[kSpellCount];
    memcpy(savedSpells, config::g.spell, sizeof(savedSpells));
    const Priority savedPriority = config::g.priority;
    const bool savedLog = config::g.logCasts;
    config::g.logCasts = true;
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellBlizzard || i == kSpellSlow;
    config::g.areaValues = defaults;
    config::g.areaLookaheadTiles = 8;
    config::g.areaSettlePercent = 50;
    config::g.areaReserveValue = 4.0;
    config::g.channelManaReserve = 0;
    auto list = [&](int first, int second) {
        int8_t* l = config::g.priority.list[kCasterMage];
        l[0] = static_cast<int8_t>(first);
        l[1] = static_cast<int8_t>(second);
        l[2] = -1;
    };
    auto building = [&](uint8_t type, int x, int y, int hp) {
        Unit* b = AddUnit(type, 1, x, y, hp, 0, kOrderStand);
        Field<uint16_t>(b, kOffStateFlags) = kStateComplete;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) g_grid[(y + dy) * kMap + x + dx] = b;  // filed on every tile, as the game does
        return b;
    };
    auto world = [&](int mana) {
        ResetWorld();
        Unit* m = AddUnit(kTypeMage, 0, 20, 20, 60, mana, kOrderStand);
        AddUnit(kGrunt, 1, 18, 24, 60, 0, kOrderAttack);  // a slow target, out of every blast
        return m;
    };
    auto ox = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderX)); };
    auto oy = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderY)); };

    // Two farms in reach, a guard tower 11 tiles out (its middle only reachable from aims 31 and up; reach is 8): the
    // farms are worth 0.3 x 2 against the tower's 3.0, so the mage holds for the tower instead of blizzarding farms,
    // and casts nothing else either.
    list(kSpellBlizzard, kSpellSlow);
    Unit* mg = world(255);
    building(kFarm, 25, 20, 400);
    building(kFarm, 25, 22, 400);
    building(kTower, 33, 20, 130);
    {
        const int before = LogCount(dir, "holding: mage at 20,20 mana 255 for blizzard: a better target");
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand && LogCount(dir, "holding: mage at 20,20 mana 255 for blizzard: a better target") == before + 1,
              "farms in reach, a guard tower further out: the mage must hold (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
        CHECK(LogContains(dir, "tiles away (human_guard_tower)"), "the hold must name the better target");
    }
    config::g.areaSettlePercent = 0;  // off: the farms get the blizzard, as before
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard && ox(mg) <= 28, "area_settle_percent = 0: blizzard at the farms (order %u at %d,%d)",
          OrderOf(mg), ox(mg), oy(mg));
    config::g.areaSettlePercent = 50;
    config::g.areaLookaheadTiles = 0;  // no lookahead: nothing is seen past the reach
    mg = world(255);
    building(kFarm, 25, 20, 400);
    building(kFarm, 25, 22, 400);
    building(kTower, 33, 20, 130);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard, "lookahead_tiles = 0: blizzard at the farms (order %u)", OrderOf(mg));
    config::g.areaLookaheadTiles = 8;

    // The tower in reach wins over the farms, and the cast line says what it was worth.
    mg = world(255);
    building(kFarm, 25, 20, 400);
    building(kFarm, 25, 22, 400);
    building(kTower, 26, 26, 130);
    {
        const int before = LogCount(dir, "units (human_guard_tower)");
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderBlizzard && oy(mg) >= 24 && LogCount(dir, "units (human_guard_tower)") == before + 1,
              "a guard tower in reach must win over two farms (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
    }

    // The reserve: slow comes first in the list, the tower is out of reach. With 100 mana a slow would leave 50, less
    // than the 75 of one full blizzard: no slow. With 125 it leaves 75: slow. Nothing is written by the probe.
    list(kSpellSlow, kSpellBlizzard);
    mg = world(100);
    building(kTower, 33, 20, 130);
    {
        const int before = LogCount(dir, "reserving: mage at 20,20 keeps 75 mana for blizzard");
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand && autocast::ChannelCount() == 0 &&
                  LogCount(dir, "reserving: mage at 20,20 keeps 75 mana for blizzard") == before + 1,
              "a guard tower near: slow must not eat the blizzard's 75 mana (order %u)", OrderOf(mg));
    }
    Field<uint8_t>(mg, kOffMana) = 125;
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == 0x2C, "125 mana: slow spends only what is above the reserve (order %u)", OrderOf(mg));
    config::g.areaReserveValue = 0.0;
    mg = world(100);
    building(kTower, 33, 20, 130);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == 0x2C, "area_reserve_value = 0: slow at 100 mana as before (order %u)", OrderOf(mg));
    config::g.areaReserveValue = 4.0;
    // Only farms around (0.3 x 3 = 0.9 units each): no reserve, slow as before.
    mg = world(100);
    building(kFarm, 33, 20, 400);
    building(kFarm, 33, 23, 400);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == 0x2C, "no good area target: slow at 100 mana as before (order %u)", OrderOf(mg));

    // The reader.
    WriteFileText(ini, "[autocast]\nlookahead_tiles = 12\narea_settle_percent = 70\narea_reserve_value = 2.5\n"
                       "[area_values]\nfarm = 2.5\nbogus_thing = 1\nhuman_guard_tower = 11\ngrunt = 0\n");
    CHECK(config::Init(dir), "[area_values] config rejected");
    CHECK(config::g.areaLookaheadTiles == 12 && config::g.areaSettlePercent == 70 && config::g.areaReserveValue == 2.5 &&
              config::g.areaValues.pct[kFarm] == 250 && config::g.areaValues.pct[kGrunt] == 0 && config::g.areaValues.pct[kTower] == 300,
          "[area_values] reader: farm 2.5, grunt 0, the tower's 11 refused");
    CHECK(LogContains(dir, "[area_values] bogus_thing: unknown unit or building") &&
              LogContains(dir, "[area_values] human_guard_tower must be a number from 0 to 10"),
          "[area_values] reader must name what it refused");
    WriteFileText(ini, "");
    config::Init(dir);

    config::g.priority = savedPriority;
    memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
    config::g.logCasts = savedLog;
    SetLegacyAreaRules();
    sizes[kFarm] = savedSz[0];
    sizes[kTower] = savedSz[1];
    tf[kFarm] = savedTf[0];
    tf[kTower] = savedTf[1];
    tf[kGrunt] = savedTf[2];
    maxHp[kFarm] = savedHp[0];
    maxHp[kTower] = savedHp[1];
    ResetWorld();
}

// Re-aiming a running channel (1.33): the watchdog stops a Blizzard whose tile has become worth less than
// area_settle_percent of the best spot in reach, and the same pass casts there. At most once per 5 s per caster.
static void ReaimTests(const wchar_t* dir) {
    constexpr uint8_t kFarm = 0x3A, kTower = 0x60;
    struct Sz { uint16_t w, h; };
    Sz* sizes = At<Sz>(kRvaUnitSizeByType);
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    const Sz savedSz[2] = {sizes[kFarm], sizes[kTower]};
    const uint32_t savedTf[3] = {tf[kFarm], tf[kTower], tf[kGrunt]};
    sizes[kFarm] = sizes[kTower] = {2, 2};
    tf[kFarm] = tf[kTower] = kTfBuilding;
    tf[kGrunt] = kTfFleshy | kTfAttacker;
    bool savedSpells[kSpellCount];
    memcpy(savedSpells, config::g.spell, sizeof(savedSpells));
    const Priority savedPriority = config::g.priority;
    const bool savedLog = config::g.logCasts;
    config::g.logCasts = true;
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellBlizzard;
    config::g.areaValues = AreaValues();  // farms 0.3, towers 3
    config::g.areaSettlePercent = 50;
    config::g.channelManaReserve = 0;
    int8_t* l = config::g.priority.list[kCasterMage];
    l[0] = kSpellBlizzard;
    l[1] = -1;
    auto building = [&](uint8_t type, int x, int y, int hp) {
        Unit* b = AddUnit(type, 1, x, y, hp, 0, kOrderStand);
        Field<uint16_t>(b, kOffStateFlags) = kStateComplete;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) g_grid[(y + dy) * kMap + x + dx] = b;
        return b;
    };
    auto ox = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderX)); };
    auto oy = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderY)); };

    // The nearest thing, a farm, gets the channel; then three grunts gather 6 tiles off, worth well over twice as much.
    ResetWorld();
    autocast::OnNewMap();
    Unit* mg = AddUnit(kTypeMage, 0, 20, 20, 60, 255, kOrderStand);
    Field<uint32_t>(mg, kOffSerial) = 8801;
    building(kFarm, 23, 19, 400);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard && oy(mg) <= 22 && autocast::ChannelCount() == 1,
          "re-aim setup: blizzard at the farm (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
    const int farmX = ox(mg), farmY = oy(mg);
    Unit* g1 = AddUnit(kGrunt, 1, 26, 25, 60, 0, kOrderAttack);
    AddUnit(kGrunt, 1, 27, 25, 60, 0, kOrderAttack);
    AddUnit(kGrunt, 1, 26, 26, 60, 0, kOrderAttack);
    config::g.areaSettlePercent = 0;  // off: the channel stays on the farm
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard && ox(mg) == farmX && oy(mg) == farmY,
          "area_settle_percent = 0: the channel must stay on the farm (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
    config::g.areaSettlePercent = 50;
    {
        World w;
        BuildWorld(w);
        autocast::GuardChannels(w);  // autocast switched off (Ctrl+F9): the watchdog never stops for a better spot
    }
    CHECK(OrderOf(mg) == kOrderBlizzard && ox(mg) == farmX, "with autocast off the watchdog must not re-aim (order %u)", OrderOf(mg));
    {
        const int before = LogCount(dir, "re-aiming: better spot at");
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderBlizzard && oy(mg) >= 24 && LogCount(dir, "re-aiming: better spot at") == before + 1,
              "the channel must move from the farm to the grunts (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
    }
    // Within 5 s no second re-aim, even for a far better spot: a guard tower appears and the grunts are made worthless.
    const int groupX = ox(mg), groupY = oy(mg);
    config::g.areaValues.pct[kGrunt] = 10;
    building(kTower, 17, 13, 130);
    autocast::AddPlayTime(4000);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard && ox(mg) == groupX && oy(mg) == groupY,
          "a second re-aim within 5 s (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
    autocast::AddPlayTime(1000);
    mod::RunAutocastPass();
    CHECK(OrderOf(mg) == kOrderBlizzard && oy(mg) <= 16, "after 5 s the channel goes to the tower (order %u at %d,%d)", OrderOf(mg),
          ox(mg), oy(mg));
    (void)g1;

    config::g.priority = savedPriority;
    memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
    config::g.logCasts = savedLog;
    SetLegacyAreaRules();
    config::g.areaValues = AreaValues();
    for (uint16_t& v : config::g.areaValues.pct) v = 100;
    sizes[kFarm] = savedSz[0];
    sizes[kTower] = savedSz[1];
    tf[kFarm] = savedTf[0];
    tf[kTower] = savedTf[1];
    tf[kGrunt] = savedTf[2];
    autocast::OnNewMap();
    ResetWorld();
}


// [dodge] (src/dodge.cpp): the player's units step out of a falling Blizzard / Death and Decay and hold at its edge
// instead of walking in, and get their order back once it is gone.
static void DodgeTests(const wchar_t* dir) {
    CHECK(!Dodge().enabled, "[dodge] must be off by default");
    constexpr uint8_t kArcher = 0x08, kBarracksD = 0x3C;
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    uint8_t* range = At<uint8_t>(kRvaAttackRangeByType);
    const uint32_t savedTf[5] = {tf[kFootman], tf[kGrunt], tf[kArcher], tf[kBarracksD], tf[kDragon]};
    const uint8_t savedRange[3] = {range[kFootman], range[kGrunt], range[kArcher]};
    tf[kFootman] = tf[kGrunt] = tf[kArcher] = kTfFleshy | kTfAttacker;
    tf[kBarracksD] = kTfBuilding;
    tf[kDragon] = kTfFlyer | kTfFleshy | kTfAttacker;
    range[kFootman] = range[kGrunt] = 1;
    range[kArcher] = 4;
    static uint8_t pool[16 * kMissileSize];
    uint8_t* const savedPool = *At<uint8_t*>(kRvaMissilePool);
    const uint32_t savedSlots = *At<uint32_t>(kRvaMissileSlots);
    *At<uint8_t*>(kRvaMissilePool) = pool;
    *At<uint32_t>(kRvaMissileSlots) = 16;
    const bool savedLog = config::g.logCasts;
    config::g.logCasts = true;
    config::g.dodge.enabled = true;

    auto clearPool = [&]() {
        memset(pool, 0, sizeof(pool));
        for (int i = 0; i < 16; ++i) pool[i * kMissileSize + kMisOffFlags] = 1;
    };
    auto missile = [&](int slot, uint8_t type, Unit* source, int px, int py) {
        uint8_t* m = pool + slot * kMissileSize;
        m[kMisOffFlags] = 0;
        m[kMisOffType] = type;
        *reinterpret_cast<Unit**>(m + kMisOffSource) = source;
        *reinterpret_cast<int16_t*>(m + 0x28) = static_cast<int16_t>(px);
        *reinterpret_cast<int16_t*>(m + 0x2A) = static_cast<int16_t>(py);
    };
    auto decay = [&](int slot, int ax, int ay) { missile(slot, 6, nullptr, ax * 32 + 16, ay * 32 + 16); };
    auto gone = [&](int slot) { pool[slot * kMissileSize + kMisOffFlags] = 1; };
    auto run = [&](unsigned ms) {
        for (unsigned t = 0; t < ms; t += 250) {
            World w;
            if (BuildWorld(w)) dodge::OnTick(w, 250);
        }
    };
    auto ox = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderX)); };
    auto oy = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderY)); };
    auto cheb = [](int ax, int ay, int bx, int by) { return abs(ax - bx) > abs(ay - by) ? abs(ax - bx) : abs(ay - by); };
    auto arrive = [&](Unit* u) {  // the unit gets where it was sent and stops
        Field<int16_t>(u, kOffX) = Field<int16_t>(u, kOffOrderX);
        Field<int16_t>(u, kOffY) = Field<int16_t>(u, kOffOrderY);
        Idle(u);
        Field<uint8_t>(u, kOffOrder) = kOrderStop;
    };
    auto fresh = [&]() {
        ResetWorld();
        dodge::OnNewMap();
        clearPool();
    };

    // A death and decay at 30,30: danger box 27..33. A footman standing at 30,31 steps out, away from the aim.
    fresh();
    Unit* ft = AddUnit(kFootman, 0, 30, 31, 60, 0, kOrderStand);
    Field<uint32_t>(ft, kOffSerial) = 7001;
    decay(0, 30, 30);
    run(250);
    CHECK(dodge::AreaCount() == 1, "one death and decay area (%u)", dodge::AreaCount());
    CHECK(OrderOf(ft) == kOrderMove && cheb(ox(ft), oy(ft), 30, 30) >= 5 && oy(ft) > 31 && dodge::DodgeCount() == 1,
          "the footman must step out of the area, away from the aim (order %u to %d,%d)", OrderOf(ft), ox(ft), oy(ft));
    CHECK(LogContains(dir, "dodge: unit type 0 at 30,31 out of a blizzard / death and decay"), "the dodge must be logged");
    const int destX = ox(ft), destY = oy(ft);
    run(1000);
    CHECK(dodge::DodgeCount() == 1 && ox(ft) == destX && oy(ft) == destY, "a unit on its way out must not be re-sent");
    arrive(ft);
    run(3000);  // out, and the area still falls: nothing, no ping-pong at the edge
    CHECK(OrderOf(ft) == kOrderStop && dodge::DodgeCount() == 1 && dodge::RestoreCount() == 0,
          "at the safe tile with the area still there the footman must wait (order %u)", OrderOf(ft));
    gone(0);
    run(750);
    CHECK(OrderOf(ft) == kOrderStop, "an order must not come back before the area has been gone a second");
    run(500);
    CHECK(OrderOf(ft) == kOrderMove && ox(ft) == 30 && oy(ft) == 31 && dodge::RestoreCount() == 1,
          "then the footman walks back to where it stood (order %u to %d,%d)", OrderOf(ft), ox(ft), oy(ft));
    arrive(ft);
    run(250);
    CHECK(OrderOf(ft) == kOrderStand, "and stands its ground again, as it did (order %u)", OrderOf(ft));

    // The player's own move through the area is his call.
    fresh();
    ft = AddUnit(kFootman, 0, 30, 30, 60, 0, kOrderMove);
    Field<uint32_t>(ft, kOffSerial) = 7002;
    Field<int16_t>(ft, kOffOrderX) = 45;
    Field<int16_t>(ft, kOffOrderY) = 30;
    decay(0, 30, 30);
    run(1000);
    CHECK(OrderOf(ft) == kOrderMove && ox(ft) == 45 && dodge::DodgeCount() == 0, "the player's own move must be left alone");
    // ... and a player's order in the middle of a dodge makes the unit his.
    fresh();
    ft = AddUnit(kFootman, 0, 30, 31, 60, 0, kOrderStop);
    Field<uint32_t>(ft, kOffSerial) = 7003;
    decay(0, 30, 30);
    run(250);
    Field<uint8_t>(ft, kOffOrder) = kOrderAttackTarget;  // the player sends it at something
    Field<uint8_t>(ft, kOffNextOrder) = kOrderNone;
    Field<int16_t>(ft, kOffOrderX) = 0;
    run(250);
    Field<int16_t>(ft, kOffX) = 40;  // wherever it went, it is out
    gone(0);
    run(3000);
    CHECK(OrderOf(ft) == kOrderAttackTarget && dodge::RestoreCount() == 0,
          "an order the player gave during a dodge must not be overwritten (order %u)", OrderOf(ft));

    // Exempt: the casting mage, a building, a unit under unholy armor, the computer's units. A flyer dodges too.
    fresh();
    Unit* mage = AddUnit(kTypeMage, 0, 30, 30, 60, 100, kOrderBlizzard);
    Field<int16_t>(mage, kOffOrderX) = 31;
    Field<int16_t>(mage, kOffOrderY) = 30;
    Unit* barracks = AddUnit(kBarracksD, 0, 29, 29, 800, 0, kOrderStand);
    Unit* armored = AddUnit(kFootman, 0, 32, 31, 60, 0, kOrderStand);
    Field<uint16_t>(armored, kOffArmorTimer) = 300;
    Unit* enemy = AddUnit(kGrunt, 1, 31, 31, 60, 0, kOrderStand);
    Unit* dragon = AddUnit(kDragon, 0, 31, 29, 100, 0, kOrderStand);
    Field<uint32_t>(dragon, kOffSerial) = 7004;
    missile(0, 5, mage, 32 * 32 + 16, 31 * 32 + 16);  // a shard of the mage's channel: the area is its order tile 31,30
    run(250);
    CHECK(OrderOf(mage) == kOrderBlizzard && OrderOf(barracks) == kOrderStand && OrderOf(armored) == kOrderStand &&
              OrderOf(enemy) == kOrderStand,
          "caster %u, building %u, armored %u, enemy %u must stay put", OrderOf(mage), OrderOf(barracks), OrderOf(armored),
          OrderOf(enemy));
    CHECK(OrderOf(dragon) == kOrderMove && cheb(ox(dragon), oy(dragon), 31, 30) >= 5, "an own dragon must fly out (order %u to %d,%d)",
          OrderOf(dragon), ox(dragon), oy(dragon));

    // Blizzard: while the mage channels the whole 5x5 (+1) around its order tile is the area; once it stops, only the
    // chains still falling on their own point are.
    fresh();
    mage = AddUnit(kTypeMage, 0, 20, 20, 60, 100, kOrderBlizzard);
    Field<int16_t>(mage, kOffOrderX) = 30;
    Field<int16_t>(mage, kOffOrderY) = 30;
    ft = AddUnit(kFootman, 0, 27, 30, 60, 0, kOrderStand);  // 3 tiles off the aim: in the quarter ring of the pattern
    Field<uint32_t>(ft, kOffSerial) = 7005;
    missile(0, 5, mage, 32 * 32 + 16, 32 * 32 + 16);  // falling on 32,32
    run(250);
    CHECK(OrderOf(ft) == kOrderMove, "a footman 3 tiles off a channelled blizzard's aim must step out (order %u)", OrderOf(ft));
    fresh();
    mage = AddUnit(kTypeMage, 0, 20, 20, 60, 100, kOrderStop);  // the channel is over
    ft = AddUnit(kFootman, 0, 29, 30, 60, 0, kOrderStand);  // inside the old 5x5 (+1), outside the chain's own 3x3
    Field<uint32_t>(ft, kOffSerial) = 7006;
    missile(0, 5, mage, 32 * 32 + 16, 32 * 32 + 16);
    run(250);
    CHECK(OrderOf(ft) == kOrderStand, "after the channel only 31..33 around the falling chain is dangerous (order %u)", OrderOf(ft));

    // Ground units only step onto ground they can stand on: water to the south and east of the area.
    fresh();
    static uint16_t sq[kMap * kMap];
    uint16_t* const savedSq = *At<uint16_t*>(kRvaSquareFlags);
    for (int y = 0; y < kMap; ++y)
        for (int x = 0; x < kMap; ++x) sq[y * kMap + x] = (x >= 31 || y >= 33) ? kSqWater : kSqLand;
    *At<uint16_t*>(kRvaSquareFlags) = sq;
    ft = AddUnit(kFootman, 0, 30, 32, 60, 0, kOrderStand);
    Field<uint32_t>(ft, kOffSerial) = 7007;
    decay(0, 31, 32);
    run(250);
    CHECK(OrderOf(ft) == kOrderMove && !(sq[oy(ft) * kMap + ox(ft)] & kSqWater), "the footman must step onto land (to %d,%d)",
          ox(ft), oy(ft));
    *At<uint16_t*>(kRvaSquareFlags) = savedSq;
    // The side the aim is NOT on: the only thing on that side (x <= 29, left of the unit at 30,32 with the aim at 31,32)
    // is water, then forest; the nearest exit (x 27) is there, so a footman that ignores what it can stand on steps
    // into it. It must take the land above instead.
    for (int blocked = 0; blocked < 2; ++blocked) {
        fresh();
        for (int y = 0; y < kMap; ++y)
            for (int x = 0; x < kMap; ++x)
                sq[y * kMap + x] = x <= 29 ? (blocked == 0 ? kSqWater : static_cast<uint16_t>(kSqLand | kSqUnpassable)) : kSqLand;
        *At<uint16_t*>(kRvaSquareFlags) = sq;
        ft = AddUnit(kFootman, 0, 30, 32, 60, 0, kOrderStand);
        Field<uint32_t>(ft, kOffSerial) = 7020 + blocked;
        decay(0, 31, 32);
        run(250);
        const uint16_t there = OrderOf(ft) == kOrderMove ? sq[oy(ft) * kMap + ox(ft)] : 0;
        CHECK(OrderOf(ft) == kOrderMove && !(there & (kSqWater | kSqUnpassable)),
              "the footman must not step onto %s (order %u to %d,%d)", blocked == 0 ? "water" : "forest", OrderOf(ft), ox(ft), oy(ft));
        *At<uint16_t*>(kRvaSquareFlags) = savedSq;
    }
    // Away from the aim: a footman one tile down and right of a death and decay's aim leaves down and right, not along
    // the edge toward the top of the area (the first safe tile a scan would meet is 35,27).
    fresh();
    ft = AddUnit(kFootman, 0, 31, 31, 60, 0, kOrderStand);
    Field<uint32_t>(ft, kOffSerial) = 7022;
    decay(0, 30, 30);
    run(250);
    CHECK(OrderOf(ft) == kOrderMove && ox(ft) >= 33 && oy(ft) >= 33,
          "the footman must leave on the side away from the aim (order %u to %d,%d)", OrderOf(ft), ox(ft), oy(ft));

    // Avoid: a footman sent at a grunt standing inside the area holds its ground at the edge; an archer in range of the
    // grunt keeps shooting. Once the area is gone the footman goes back to its attack.
    fresh();
    Unit* grunt = AddUnit(kGrunt, 1, 30, 30, 60, 0, kOrderStand);
    Field<uint32_t>(grunt, kOffSerial) = 7008;
    ft = AddUnit(kFootman, 0, 22, 30, 60, 0, kOrderAttackTarget);
    Field<uint32_t>(ft, kOffSerial) = 7009;
    Field<Unit*>(ft, kOffOrderTarget) = grunt;
    Unit* archer = AddUnit(kArcher, 0, 23, 30, 40, 0, kOrderAttackTarget);  // 4 tiles from the danger box, range 4 to 27
    Field<uint32_t>(archer, kOffSerial) = 7010;
    Field<Unit*>(archer, kOffOrderTarget) = grunt;
    Field<int16_t>(archer, kOffX) = 26;
    decay(0, 30, 30);
    run(250);
    CHECK(OrderOf(ft) == kOrderStand && dodge::HoldCount() == 1, "the footman must hold instead of walking in (order %u)", OrderOf(ft));
    CHECK(OrderOf(archer) == kOrderAttackTarget, "an archer in range must keep shooting (order %u)", OrderOf(archer));
    CHECK(LogContains(dir, "holds at the edge"), "the hold must be logged");
    run(2000);
    CHECK(OrderOf(ft) == kOrderStand && dodge::HoldCount() == 1, "a holding unit must keep holding while the area falls");
    gone(0);
    run(1250);
    CHECK(OrderOf(ft) == kOrderAttackTarget && Field<Unit*>(ft, kOffOrderTarget) == grunt,
          "once the area is gone the footman attacks again (order %u)", OrderOf(ft));
    // A target it picked by itself (+0x54) counts as well.
    fresh();
    grunt = AddUnit(kGrunt, 1, 30, 30, 60, 0, kOrderStand);
    ft = AddUnit(kFootman, 0, 22, 30, 60, 0, kOrderAttack);
    Field<uint32_t>(ft, kOffSerial) = 7011;
    Field<Unit*>(ft, kOffAutoTarget) = grunt;
    decay(0, 30, 30);
    run(250);
    CHECK(OrderOf(ft) == kOrderStand, "chasing its own pick into the area must be held too (order %u)", OrderOf(ft));
    // An attack-move whose next steps lie in the area holds; further off it walks on; afterwards it is given back.
    fresh();
    ft = AddUnit(kFootman, 0, 22, 30, 60, 0, kOrderAttackArea);
    Field<uint32_t>(ft, kOffSerial) = 7012;
    Field<int16_t>(ft, kOffOrderX) = 45;
    Field<int16_t>(ft, kOffOrderY) = 30;
    decay(0, 30, 30);
    run(500);
    CHECK(OrderOf(ft) == kOrderAttackArea, "an attack-move 5 tiles from the area goes on (order %u)", OrderOf(ft));
    Field<int16_t>(ft, kOffX) = 25;
    run(250);
    CHECK(OrderOf(ft) == kOrderStand, "an attack-move about to enter the area must hold (order %u)", OrderOf(ft));
    gone(0);
    run(1250);
    CHECK(OrderOf(ft) == kOrderAttackArea && ox(ft) == 45 && oy(ft) == 30,
          "the attack-move to 45,30 comes back once the area is gone (order %u to %d,%d)", OrderOf(ft), ox(ft), oy(ft));

    // Off: nothing at all.
    fresh();
    config::g.dodge.enabled = false;
    ft = AddUnit(kFootman, 0, 30, 31, 60, 0, kOrderStand);
    decay(0, 30, 30);
    run(1000);
    CHECK(OrderOf(ft) == kOrderStand && dodge::DodgeCount() == 0, "dodging while [dodge] enabled = false");

    config::g.dodge.enabled = false;
    config::g.logCasts = savedLog;
    *At<uint8_t*>(kRvaMissilePool) = savedPool;
    *At<uint32_t>(kRvaMissileSlots) = savedSlots;
    tf[kFootman] = savedTf[0];
    tf[kGrunt] = savedTf[1];
    tf[kArcher] = savedTf[2];
    tf[kBarracksD] = savedTf[3];
    tf[kDragon] = savedTf[4];
    range[kFootman] = savedRange[0];
    range[kGrunt] = savedRange[1];
    range[kArcher] = savedRange[2];
    dodge::OnNewMap();
    ResetWorld();
}

static bool g_fakeF11 = false;
static bool FakeKeysF11(int vk) { return (vk == VK_CONTROL && g_fakeCtrl) || (vk == VK_F11 && g_fakeF11); }

// [scouts] (src/scouts.cpp): idle flying machines / zeppelins of the local player fly to unexplored ground, then to the
// fog that has gone longest unseen; a player's order is left alone until the unit has been idle again for idle_seconds.
static void ScoutTests(const wchar_t* dir, const wchar_t* ini) {
    const Scouts defaults;
    CHECK(!defaults.enabled && defaults.toggleKey == VK_F11 && defaults.idleSeconds == 5 && defaults.type[0x28] &&
              defaults.type[0x29] && !defaults.type[0x2A] && !defaults.type[kTypeEye],
          "[scouts] defaults: off, Ctrl+F11, 5 s, flying machine and zeppelin only");
    // The table the anti-air test reads is the one FUN_004a9810 reads for a target in the air.
    CHECK(At<uint8_t>(0xA9825)[0] == 0x0F && At<uint8_t>(0xA9825)[1] == 0xB6 && *At<uint32_t>(0xA9828) == g_base + kRvaCanTargetByType &&
              At<uint8_t>(0xA982D)[0] == 0x83 && At<uint8_t>(0xA982D)[2] == 0x04,
          "0x4A9825: movzx eax, byte [ecx+0x918580]; and eax, 4");

    constexpr uint8_t kFlyingMachine = 0x28, kZeppelin = 0x29, kGryphon = 0x2A, kGuardTower = 0x60, kArcher = 0x08;
    uint32_t* tf = At<uint32_t>(kRvaTypeFlags);
    uint8_t* canTarget = At<uint8_t>(kRvaCanTargetByType);
    uint8_t* range = At<uint8_t>(kRvaAttackRangeByType);
    const uint32_t savedTf[5] = {tf[kFlyingMachine], tf[kZeppelin], tf[kGryphon], tf[kGuardTower], tf[kArcher]};
    const uint8_t savedCan[2] = {canTarget[kGuardTower], canTarget[kArcher]};
    const uint8_t savedRange[2] = {range[kGuardTower], range[kArcher]};
    tf[kFlyingMachine] = tf[kZeppelin] = tf[kGryphon] = kTfFlyer;
    tf[kGuardTower] = kTfBuilding | kTfAttacker;
    tf[kArcher] = kTfAttacker | kTfFleshy;
    canTarget[kGuardTower] = canTarget[kArcher] = kCanTargetAir | 3;
    range[kGuardTower] = 6;
    range[kArcher] = 4;
    static uint8_t explored[kMap * kMap], visible[kMap * kMap];
    uint8_t* const savedExplored = *At<uint8_t*>(kRvaExploredMap);
    uint8_t* const savedVisible = *At<uint8_t*>(kRvaVisibleMap);
    *At<uint8_t*>(kRvaExploredMap) = explored;
    *At<uint8_t*>(kRvaVisibleMap) = visible;
    const bool savedLog = config::g.logCasts;
    config::g.logCasts = true;
    config::g.scouts = defaults;
    config::g.scouts.enabled = true;

    auto dest = [](Unit* u) { return std::make_pair(static_cast<int>(Field<int16_t>(u, kOffOrderX)), static_cast<int>(Field<int16_t>(u, kOffOrderY))); };
    auto cheb = [](int ax, int ay, int bx, int by) { return abs(ax - bx) > abs(ay - by) ? abs(ax - bx) : abs(ay - by); };
    auto tick = [&](unsigned ms) {
        World w;
        if (BuildWorld(w)) scouts::OnTick(w, ms);
    };
    auto run = [&](unsigned ms) {  // in 250 ms steps, like the game's own clock
        for (unsigned t = 0; t < ms; t += 250) tick(250);
    };
    // Everything explored and in sight, except an unexplored block at 40..59 x 40..59.
    auto mapWith = [&](int x0, int x1) {
        memset(explored, 0, sizeof(explored));
        memset(visible, 0, sizeof(visible));
        for (int y = 40; y < 60; ++y)
            for (int x = x0; x < x1; ++x) explored[y * kMap + x] = visible[y * kMap + x] = kTileUnexplored;
    };
    auto fresh = [&]() {
        ResetWorld();
        scouts::OnNewMap();
        mapWith(40, 60);
    };

    // Idle for 5 s first, then off to the unexplored block.
    fresh();
    Unit* fm = AddUnit(kFlyingMachine, 0, 10, 10, 150, 0, kOrderStop);
    Field<uint32_t>(fm, kOffSerial) = 9001;
    run(4750);
    CHECK(OrderOf(fm) == kOrderStop, "a scout idle for 4.75 s must not be sent yet (order %u)", OrderOf(fm));
    run(250);
    auto [dx, dy] = dest(fm);
    CHECK(OrderOf(fm) == kOrderMove && dx >= 36 && dx <= 63 && dy >= 36 && dy <= 63 && scouts::OrderCount() == 1,
          "after 5 s idle the scout must fly to the unexplored block (order %u to %d,%d)", OrderOf(fm), dx, dy);
    CHECK(LogContains(dir, "(unexplored)"), "the scout's order must be logged");
    run(2000);  // on its way: nothing new
    CHECK(scouts::OrderCount() == 1 && dest(fm) == std::make_pair(dx, dy), "a scout on the way must not be re-sent");

    // Arrived: straight on to the next place, no idle wait.
    Field<int16_t>(fm, kOffX) = static_cast<int16_t>(dx);
    Field<int16_t>(fm, kOffY) = static_cast<int16_t>(dy);
    Idle(fm);
    Field<uint8_t>(fm, kOffOrder) = kOrderStop;
    run(250);
    CHECK(OrderOf(fm) == kOrderMove && scouts::OrderCount() == 2, "an arrived scout must go on at once (order %u)", OrderOf(fm));

    // The player takes over: a move of his own is left alone, and after it the scout waits idle_seconds again.
    Field<uint8_t>(fm, kOffOrder) = kOrderMove;
    Field<uint8_t>(fm, kOffNextOrder) = kOrderNone;
    Field<int16_t>(fm, kOffOrderX) = 5;
    Field<int16_t>(fm, kOffOrderY) = 5;
    run(1000);
    CHECK(dest(fm) == std::make_pair(5, 5) && scouts::OrderCount() == 2 && LogContains(dir, "player took over"),
          "the player's own move must be left alone");
    Field<int16_t>(fm, kOffX) = 5;
    Field<int16_t>(fm, kOffY) = 5;
    Field<uint8_t>(fm, kOffOrder) = kOrderStop;
    run(4750);
    CHECK(OrderOf(fm) == kOrderStop && scouts::OrderCount() == 2, "after the player's order the scout must rest 5 s (order %u)",
          OrderOf(fm));
    run(250);
    CHECK(OrderOf(fm) == kOrderMove && scouts::OrderCount() == 3, "then it scouts again (order %u)", OrderOf(fm));
    // A pending order of the player's (next-order slot) counts as busy too.
    fresh();
    fm = AddUnit(kFlyingMachine, 0, 10, 10, 150, 0, kOrderStop);
    Field<uint32_t>(fm, kOffSerial) = 9002;
    Field<uint8_t>(fm, kOffNextOrder) = kOrderPatrol;
    run(6000);
    CHECK(scouts::OrderCount() == 0, "a scout with an order pending must be left alone");

    // Only the listed types, only the local player's, only while enabled.
    fresh();
    Unit* gry = AddUnit(kGryphon, 0, 10, 10, 100, 0, kOrderStop);
    Unit* enemyFm = AddUnit(kFlyingMachine, 1, 12, 10, 150, 0, kOrderStop);
    Unit* zep = AddUnit(kZeppelin, 0, 14, 10, 150, 0, kOrderStop);
    Field<uint32_t>(gry, kOffSerial) = 9003;
    Field<uint32_t>(enemyFm, kOffSerial) = 9004;
    Field<uint32_t>(zep, kOffSerial) = 9005;
    config::g.scouts.enabled = false;
    run(6000);
    CHECK(scouts::OrderCount() == 0, "scouting while [scouts] enabled = false");
    config::g.scouts.enabled = true;
    run(6000);
    CHECK(OrderOf(gry) == kOrderStop && OrderOf(enemyFm) == kOrderStop && OrderOf(zep) == kOrderMove,
          "only the player's zeppelin may scout (gryphon %u, enemy %u, zeppelin %u)", OrderOf(gry), OrderOf(enemyFm), OrderOf(zep));

    // Two scouts never head for the same area; a known guard tower (range 6 + 2) is never a destination.
    int tooClose = 0, intoTower = 0;
    for (int round = 0; round < 20; ++round) {
        fresh();
        mapWith(30, 64);
        Unit* a = AddUnit(kFlyingMachine, 0, 10, 10, 150, 0, kOrderStop);
        Unit* b = AddUnit(kZeppelin, 0, 11, 10, 150, 0, kOrderStop);
        Unit* tower = AddUnit(kGuardTower, 1, 45, 50, 130, 0, kOrderStand);
        explored[50 * kMap + 45] = 0;  // the player has seen where it stands
        Field<uint32_t>(a, kOffSerial) = 9100 + round * 2;
        Field<uint32_t>(b, kOffSerial) = 9101 + round * 2;
        (void)tower;
        run(5000);
        const auto da = dest(a), db = dest(b);
        if (OrderOf(a) != kOrderMove || OrderOf(b) != kOrderMove || cheb(da.first, da.second, db.first, db.second) < 10) ++tooClose;
        if (cheb(da.first, da.second, 45, 50) <= 8 || cheb(db.first, db.second, 45, 50) <= 8) ++intoTower;
    }
    CHECK(tooClose == 0, "two scouts sent within 10 tiles of each other in %d of 20 rounds", tooClose);
    CHECK(intoTower == 0, "a scout sent within reach of a known guard tower in %d of 20 rounds", intoTower);
    // An archer the player sees counts too; one under his fog does not (he does not know it is there).
    int nearArcher = 0, moved = 0;
    for (int round = 0; round < 20; ++round) {
        fresh();
        memset(explored, 0, sizeof(explored));
        for (int y = 0; y < kMap; ++y)
            for (int x = 30; x < 40; ++x) explored[y * kMap + x] = kTileUnexplored;  // a strip at x 30..39
        Unit* a = AddUnit(kFlyingMachine, 0, 10, 32, 150, 0, kOrderStop);
        Field<uint32_t>(a, kOffSerial) = 9200 + round;
        for (int y = 2; y < kMap; y += 16) AddUnit(kArcher, 1, 35, y, 40, 0, kOrderStand);  // every 16 tiles: reach 6
        run(5000);
        const auto da = dest(a);
        moved += OrderOf(a) == kOrderMove;
        for (int y = 2; y < kMap; y += 16) nearArcher += OrderOf(a) == kOrderMove && cheb(da.first, da.second, 35, y) <= 6;
    }
    CHECK(nearArcher == 0 && moved >= 15, "a scout sent within reach of a seen archer %d time(s) (%d of 20 sent)", nearArcher, moved);
    // Archers under the player's fog every 12 tiles would close the whole strip if he knew of them; he does not.
    int sentAnyway = 0, nearFogged = 0;
    for (int round = 0; round < 20; ++round) {
        fresh();
        memset(explored, 0, sizeof(explored));
        for (int y = 0; y < kMap; ++y)
            for (int x = 30; x < 40; ++x) explored[y * kMap + x] = kTileUnexplored;
        Unit* a = AddUnit(kFlyingMachine, 0, 10, 32, 150, 0, kOrderStop);
        Field<uint32_t>(a, kOffSerial) = 9250 + round;
        for (int y = 2; y < kMap; y += 12) Field<uint8_t>(AddUnit(kArcher, 1, 35, y, 40, 0, kOrderStand), kOffFogMask) = 1 << 0;
        run(5000);
        sentAnyway += OrderOf(a) == kOrderMove;
        for (int y = 2; y < kMap; y += 12) nearFogged += OrderOf(a) == kOrderMove && cheb(dest(a).first, dest(a).second, 35, y) <= 6;
    }
    CHECK(sentAnyway >= 15 && nearFogged > 0, "archers under fog must not keep the scout home (%d of 20 sent, %d near one)", sentAnyway,
          nearFogged);

    // Everything explored: the scout patrols the fog that has gone longest unseen. The left half was in sight for the
    // first minute, the right half for the second; now everything is fogged, so the left half is older.
    fresh();
    memset(explored, 0, sizeof(explored));
    Unit* p = AddUnit(kFlyingMachine, 0, 50, 32, 150, 0, kOrderMove);  // busy for now; right side, far from the older fog
    Field<uint32_t>(p, kOffSerial) = 9300;
    Field<int16_t>(p, kOffOrderX) = 33;
    Field<int16_t>(p, kOffOrderY) = 33;
    auto sightHalf = [&](bool left) {
        for (int y = 0; y < kMap; ++y)
            for (int x = 0; x < kMap; ++x) visible[y * kMap + x] = (x < 32) == left ? 0 : kTileUnexplored;
    };
    sightHalf(true);
    run(60000);
    sightHalf(false);
    run(60000);
    memset(visible, kTileUnexplored, sizeof(visible));
    Field<uint8_t>(p, kOffOrder) = kOrderStop;
    run(5000);
    CHECK(OrderOf(p) == kOrderMove && dest(p).first < 32 && LogContains(dir, "(fog, unseen"),
          "all explored: the scout must fly to the fog unseen longest, the left half (order %u to %d,%d)", OrderOf(p),
          dest(p).first, dest(p).second);
    // The same the other way round.
    scouts::OnNewMap();
    Idle(p);
    Field<uint8_t>(p, kOffOrder) = kOrderMove;
    Field<int16_t>(p, kOffX) = 13;  // now on the left, far from the older (right) fog
    sightHalf(false);
    run(60000);
    sightHalf(true);
    run(60000);
    memset(visible, kTileUnexplored, sizeof(visible));
    Field<uint8_t>(p, kOffOrder) = kOrderStop;
    run(5000);
    CHECK(OrderOf(p) == kOrderMove && dest(p).first >= 32, "the right half is older now (order %u to %d,%d)", OrderOf(p),
          dest(p).first, dest(p).second);

    // Ctrl+F11 flips it with a banner.
    {
        const int messages = g_messages;
        const bool before = config::g.scouts.enabled;
        mod::SetKeyReaderForTest(&FakeKeysF11);
        g_fakeCtrl = g_fakeF11 = true;
        mod::OnTick();
        CHECK(config::g.scouts.enabled != before && g_messages == messages + 1, "Ctrl+F11 must flip auto-scouting with a banner");
        g_fakeF11 = false;
        mod::OnTick();
        g_fakeF11 = true;
        mod::OnTick();
        CHECK(config::g.scouts.enabled == before, "a second press flips it back");
        g_fakeCtrl = g_fakeF11 = false;
        mod::SetKeyReaderForTest(nullptr);
    }

    // The reader.
    WriteFileText(ini, "[scouts]\nenabled = true\nunits = [\"gryphon_rider\", \"farm\", \"bogus\"]\ntoggle_key = \"F12\"\nidle_seconds = 2\n");
    CHECK(config::Init(dir), "[scouts] config rejected");
    CHECK(config::g.scouts.enabled && config::g.scouts.type[kGryphon] && !config::g.scouts.type[kFlyingMachine] &&
              !config::g.scouts.type[0x3A] && config::g.scouts.toggleKey == VK_F12 && config::g.scouts.idleSeconds == 2,
          "[scouts] reader: gryphon only, F12, 2 s");
    CHECK(LogContains(dir, "[scouts] units: \"farm\" is not a unit that can scout") &&
              LogContains(dir, "[scouts] units: \"bogus\" is not a unit that can scout"),
          "[scouts] reader must name what it dropped");
    WriteFileText(ini, "");
    config::Init(dir);

    config::g.scouts = defaults;
    config::g.logCasts = savedLog;
    *At<uint8_t*>(kRvaExploredMap) = savedExplored;
    *At<uint8_t*>(kRvaVisibleMap) = savedVisible;
    tf[kFlyingMachine] = savedTf[0];
    tf[kZeppelin] = savedTf[1];
    tf[kGryphon] = savedTf[2];
    tf[kGuardTower] = savedTf[3];
    tf[kArcher] = savedTf[4];
    canTarget[kGuardTower] = savedCan[0];
    canTarget[kArcher] = savedCan[1];
    range[kGuardTower] = savedRange[0];
    range[kArcher] = savedRange[1];
    scouts::OnNewMap();
    ResetWorld();
}

static void ProductionTests(const wchar_t* dir, const wchar_t* ini) {
    using namespace production;
    const AutoProduction defaults;

    // The engine tables the module mirrors are what the research found in this exe.
    {
        const uint8_t* at = At<uint8_t>(kRvaTrainedAt);
        CHECK(at[0x00] == 0x3C && at[0x01] == 0x3D && at[0x02] == 0x4A && at[0x03] == 0x4B && at[0x04] == 0x3C && at[0x06] == 0x3C &&
                  at[0x08] == 0x3C && at[0x0A] == 0x50 && at[0x0B] == 0x51 && at[0x0C] == 0x3C && at[0x12] == 0x3C && at[0x1A] == 0x48 &&
                  at[0x1E] == 0x48 && at[0x20] == 0x48 && at[0x21] == 0x49 && at[0x26] == 0x48 && at[0x27] == 0x49 && at[0x2A] == 0x46 &&
                  at[0x2B] == 0x47 && at[0x14] == 'n',
              "trained-at table 0x838248");
        CHECK(At<uint8_t>(kRvaResearchAt)[0] == 0x52 && At<uint8_t>(kRvaResearchAt)[24] == 0x4C && At<uint8_t>(kRvaResearchAt)[33] == 0x3E &&
                  At<uint8_t>(kRvaResearchAt)[51] == 0x51,
              "research-at table 0x838284");
        const uint32_t* req = At<uint32_t>(kRvaTrainRequirement);
        const struct { uint8_t type; uint32_t fnRva; } kReq[] = {
            {0x00, 0xA1F70}, {0x02, 0xA1F70}, {0x0A, 0xA1F70}, {0x1A, 0xA1F70}, {0x1E, 0xA1F70}, {0x2A, 0xA1F70},
            {0x04, 0xAC6C0}, {0x06, 0xAC6F0}, {0x0C, 0xAC730}, {0x08, 0xAC770}, {0x12, 0xAC7A0}, {0x20, 0xAC7F0},
            {0x26, 0xAC7D0}};
        bool reqOk = true;
        for (const auto& r : kReq)
            for (int race = 0; race < 2; ++race) reqOk = reqOk && req[r.type + race] == g_base + r.fnRva;
        CHECK(reqOk, "requirement table 0x8C0428 is not the one production.cpp mirrors");
        const uint8_t call[] = {0x6A, 0x00, 0x8A, 0x41, 0x27, 0x24, 0x01, 0x0F, 0xB6, 0xC0, 0x50, 0x51, 0xE8};
        CHECK(memcmp(At<uint8_t>(0xAE2D6), call, sizeof(call)) == 0, "the AI's StartProduction call (0x4AE2D6) changed");
    }

    // ---- Decision core ----
    // ---- Navy food ([auto_production] reserve_navy_food) ----
    {
        AutoProduction cfg;  // defaults: reserve_navy_food on, food_free 4 / 10 %
        Plan n = CorePlan(2);
        n.navyShare = 0.5;
        n.supply = 40;
        n.count[kProdInfantry] = 10;
        n.count[kProdDestroyers] = 1;  // army 11: the navy aims for round(0.5 x 12) = 6, 5 missing
        n.ownShipyard = n.enemyNavy = true;
        n.shipFood = 1;
        int have = -1, want = -1;
        CHECK(NavyFood(n, cfg, &have, &want) == 5 && have == 1 && want == 6, "navy food: 5 of 6 ships missing (%d, %d of %d)",
              NavyFood(n, cfg), have, want);
        n.navyFood = NavyFood(n, cfg);
        n.used = 31;  // 9 free: enough for the plain rule (4 after the unit), not for 4 + 5 ships
        CHECK(FoodAllows(n, cfg) && !NavyFoodAllows(n, cfg, kProdInfantry) && !NavyFoodAllows(n, cfg, kProdSiege),
              "navy food: land is held while the navy is short");
        CHECK(NavyFoodAllows(n, cfg, kProdDestroyers) && NavyFoodAllows(n, cfg, kProdBattleships) &&
                  NavyFoodAllows(n, cfg, kProdWorkers) && NavyFoodAllows(n, cfg, kProdTankers),
              "navy food: ships, workers and tankers are never held by it");
        n.used = 30;  // 10 free: 10 - 1 - 5 = 4, exactly the keep-free amount
        CHECK(NavyFoodAllows(n, cfg, kProdInfantry), "navy food: land may start when free food covers keep-free + ships");
        n.used = 31;
        {
            SaveUp st;
            const Decision d = Decide(n, cfg, kBarracksMask, st, 0);
            CHECK(d.cls < 0 && d.heldForNavy, "navy food: Decide at a barracks holds the land unit (cls %d)", d.cls);
            SaveUp st2;
            const Decision y = Decide(n, cfg, GroupMask(kGroupNavy), st2, 0);
            CHECK(y.cls >= 0 && GroupOf(y.cls) == kGroupNavy && !y.heldForNavy, "navy food: the shipyard still builds (cls %d)", y.cls);
        }
        Plan r = n;  // released: the navy has reached its share
        r.count[kProdDestroyers] = 11;  // army 21: aims for round(0.5 x 22) = 11
        CHECK(NavyFood(r, cfg) == 0, "navy food: nothing kept once the navy is at its share");
        r = n;
        r.enemyNavy = false;
        CHECK(NavyFood(r, cfg) == 0, "navy food: nothing kept without an enemy shipyard or warship");
        r = n;
        r.ownShipyard = false;
        CHECK(NavyFood(r, cfg) == 0, "navy food: nothing kept without a finished shipyard of his own");
        r = n;
        cfg.reserveNavyFood = false;
        CHECK(NavyFood(r, cfg) == 0, "navy food: reserve_navy_food = false keeps nothing");
        cfg.reserveNavyFood = true;
        r = n;
        r.trainable[kProdDestroyers] = r.trainable[kProdBattleships] = r.trainable[kProdSubmarines] = false;
        CHECK(NavyFood(r, cfg) == 0, "navy food: nothing kept when no warship can be trained");
        r = n;
        r.shipFood = 2;
        CHECK(NavyFood(r, cfg) == 10, "navy food: a ship that eats 2 needs 2 each (%d)", NavyFood(r, cfg));
        r = n;
        r.supply = 200;
        r.used = 197;
        CHECK(NavyFood(r, cfg) == 3, "navy food: never more than is left under 200 (%d)", NavyFood(r, cfg));
    }

    // Food: 4 or 10 % of the supply, whichever is larger, must still be free AFTER the unit; units in training count.
    CHECK(FoodAllows(20, 15, 0, 4, 10) && !FoodAllows(20, 16, 0, 4, 10), "food gate: 4 free after the unit at supply 20");
    CHECK(FoodAllows(20, 14, 1, 4, 10) && !FoodAllows(20, 14, 2, 4, 10), "food gate: units in training are used food");
    CHECK(FoodAllows(200, 179, 0, 4, 10) && !FoodAllows(200, 180, 0, 4, 10), "food gate: 10 %% of 200 = 20 free");
    CHECK(FoodAllows(250, 179, 0, 4, 10) && !FoodAllows(250, 180, 0, 4, 10), "food gate: the supply counts as 200 at most");
    CHECK(FoodAllows(55, 48, 0, 4, 10) && !FoodAllows(55, 49, 0, 4, 10), "food gate: 10 %% of 55 rounds up to 6");

    // The share of the army the map asks to be ships.
    CHECK(NavyShare(0, 0, 1, 1.0, 80) == 0 && NavyShare(9, 0, 1, 1.0, 80) == 0, "no oil and under 10 %% water: a land map");
    CHECK(fabs(NavyShare(9, 2, 2, 1.0, 80) - 0.19) < 1e-9, "9 %% water and two oil patches: 19 %% ships (%.3f)", NavyShare(9, 2, 2, 1.0, 80));
    CHECK(fabs(NavyShare(50, 0, 1, 1.0, 80) - 0.60) < 1e-9 && fabs(NavyShare(50, 0, 2, 1.0, 80) - 0.50) < 1e-9 &&
              fabs(NavyShare(50, 0, 3, 1.0, 80) - 0.45) < 1e-9,
          "50 %% water: 60 / 50 / 45 %% ships by hall tier");
    CHECK(fabs(NavyShare(50, 6, 2, 1.0, 100) - 0.80) < 1e-9 && fabs(NavyShare(50, 20, 2, 1.0, 100) - 0.80) < 1e-9,
          "each oil source adds 5 %%, together at most 30 %% (%.2f)", NavyShare(50, 20, 2, 1.0, 100));
    CHECK(fabs(NavyShare(90, 6, 1, 1.0, 80) - 0.80) < 1e-9 && fabs(NavyShare(90, 6, 1, 1.0, 50) - 0.50) < 1e-9, "navy_max caps it");
    CHECK(fabs(NavyShare(50, 0, 2, 0.5, 80) - 0.25) < 1e-9 && NavyShare(50, 0, 2, 0.0, 80) == 0, "navy_weight scales it");

    // The bank threshold is a multiple of the CURRENT price, per resource the unit costs.
    {
        Plan p = CorePlan(1);
        p.bank = {{2400, 0, 0}};  // a grunt costs 600
        CHECK(CanAfford(p, defaults, kProdInfantry) && fabs(Buys(p, kProdInfantry) - 4) < 1e-9, "spare 2400 = four grunts: yes");
        p.bank = {{2399, 0, 0}};
        CHECK(!CanAfford(p, defaults, kProdInfantry), "one gold short of four grunts: no");
        p.bank = {{100000, 100000, 100000}};
        p.reserve = {{98000, 0, 0}};
        CHECK(!CanAfford(p, defaults, kProdInfantry), "the upgrade reserve is not spare money");
        p.reserve = {{0, 0, 0}};
        p.cost[kProdInfantry] = {{1200, 0, 0}};  // the price doubled ([costs] or [unit.grunt])
        CHECK(fabs(Buys(p, kProdInfantry) - 83.33) < 0.01, "Buys() follows the live price");
        p.cost[kProdInfantry] = {{600, 0, 0}};
        p.bank = {{3200, 600, 3600}};  // exactly four submarines' worth of every resource one costs
        CHECK(!CanAfford(p, defaults, kProdSubmarines) && CanAfford(p, defaults, kProdInfantry), "four submarines' worth is not enough");
        p.bank = {{6400, 1200, 7200}};
        CHECK(CanAfford(p, defaults, kProdSubmarines), "eight submarines' worth is");
        AutoProduction perClass;
        perClass.classBankMultiple[kProdInfantry] = 1.0;
        p.bank = {{1200, 0, 0}};
        CHECK(CanAfford(p, perClass, kProdInfantry) && !CanAfford(p, defaults, kProdInfantry), "[bank_multiple] per class");
    }
    // Reserve: the dearest research + 25 % of the others, per resource; more items, more reserve.
    {
        const Buy items[] = {{{{1000, 0, 0}}, true}, {{{500, 300, 0}}, true}, {{{200, 0, 100}}, true}, {{{800, 0, 0}}, true}};
        const Price r3 = Reserve(items, 3, 0.25), r4 = Reserve(items, 4, 0.25), r0 = Reserve(items, 0, 0.25), rx = Reserve(items, 4, 0);
        CHECK(r3.r[0] == 1175 && r3.r[1] == 300 && r3.r[2] == 100, "reserve of 3 items (%d/%d/%d)", r3.r[0], r3.r[1], r3.r[2]);
        CHECK(r4.r[0] == 1375 && r4.r[0] > r3.r[0], "a fourth purchasable upgrade raises the reserve (%d)", r4.r[0]);
        CHECK(r0.r[0] == 0 && r0.r[1] == 0 && rx.r[0] == 1000, "no upgrades = no reserve; extra 0 = the dearest only");
        // A building upgrade is never the item the reserve is built around, however dear it is.
        const Buy keep[] = {{{{2000, 1000, 200}}, false}, {{{800, 0, 0}}, true}};
        const Price rk = Reserve(keep, 2, 0.25);
        CHECK(rk.r[0] == 1300 && rk.r[1] == 250 && rk.r[2] == 50,
              "a 2000 gold keep next to an 800 gold research: 800 + a quarter of the rest (%d/%d/%d)", rk.r[0], rk.r[1], rk.r[2]);
        const Buy onlyKeep[] = {{{{2000, 1000, 200}}, false}};
        CHECK(Reserve(onlyKeep, 1, 0.25).r[0] == 500, "nothing but a keep to buy: a quarter of it (%d)",
              Reserve(onlyKeep, 1, 0.25).r[0]);
        const Buy anchored[] = {{{{2000, 1000, 200}}, true}, {{{800, 0, 0}}, true}};
        CHECK(Reserve(anchored, 2, 0.25).r[0] == 2200, "the same two as researches would reserve 2200");
    }
    // Workers and the single tanker.
    {
        Plan p = CorePlan(1);
        CHECK(defaults.workersTier[0] == 12 && defaults.workersTier[1] == 16 && defaults.workersTier[2] == 24,
              "the worker targets are 12 / 16 / 24 by hall tier");
        p.count[kProdWorkers] = 11;
        CHECK(WantWorker(p, defaults) && WorkerTarget(p, defaults) == 12, "11 workers at tier 1: one more");
        p.count[kProdWorkers] = 12;
        CHECK(!WantWorker(p, defaults), "12 workers at tier 1 is the target");
        p.tier = 2;
        CHECK(WantWorker(p, defaults) && WorkerTarget(p, defaults) == 16, "a keep wants 16, not 12");
        p.count[kProdWorkers] = 16;
        CHECK(!WantWorker(p, defaults), "16 at tier 2");
        p.tier = 3;
        CHECK(WantWorker(p, defaults) && WorkerTarget(p, defaults) == 24, "a castle wants 24");
        p.count[kProdWorkers] = 23;
        CHECK(WantWorker(p, defaults), "23 of 24 at tier 3");
        p.tier = 0;
        CHECK(WorkerTarget(p, defaults) == 0 && !WantWorker(p, defaults), "no hall: no worker target at all");
        p.tier = 3;
        AutoProduction noWorkers;
        noWorkers.workersTier[2] = 0;
        CHECK(!WantWorker(p, noWorkers), "workers_tier3 = 0: never train workers with a castle");
        p.count[kProdWorkers] = 20;
        p.used = 190;
        CHECK(!WantWorker(p, defaults), "the food gate holds for workers too");
        Plan t = CorePlan(1);
        CHECK(WantTanker(t, defaults, true) && !WantTanker(t, defaults, false), "a tanker only with an oil platform");
        t.count[kProdTankers] = 1;
        CHECK(!WantTanker(t, defaults, true), "one tanker is the most the mod ever builds");
    }
    // A poor start: the upgrade reserve holds the whole bank, and the economy must still grow.
    {
        Plan p = CorePlan(1);
        p.bank = {{900, 300, 0}};      // a peasant is 400 gold, a tanker 400 gold / 200 lumber
        p.reserve = {{3200, 800, 0}};  // keep upgrade, swords, a tower: more than he owns
        p.count[kProdWorkers] = 4;
        CHECK(Buys(p, kProdWorkers) == 0 && !CanAfford(p, defaults, kProdWorkers), "the reserve swallows the bank");
        CHECK(CanPay(p, kProdWorkers) && WantWorker(p, defaults), "workers must be built anyway: they ARE the economy");
        CHECK(WantTanker(p, defaults, true), "the tanker pays for itself: the reserve must not stop it either");
        AutoProduction strict;
        strict.workersIgnoreReserve = strict.tankersIgnoreReserve = false;
        CHECK(!WantWorker(p, strict) && !WantTanker(p, strict, true), "workers_ignore_reserve / tankers_ignore_reserve = false");
        p.bank = {{399, 300, 0}};
        CHECK(!CanPay(p, kProdWorkers) && !WantWorker(p, defaults), "one gold short of a peasant: no peasant");
        p.bank = {{400, 300, 0}};
        CHECK(WantWorker(p, defaults), "exactly the price is enough");
        p.count[kProdWorkers] = 12;
        CHECK(!WantWorker(p, defaults), "the count target still holds");
        p.count[kProdWorkers] = 4;
        p.used = 197;
        CHECK(!WantWorker(p, defaults), "and so does the food rule");
        p.used = 0;
        p.trainable[kProdWorkers] = false;
        CHECK(!WantWorker(p, defaults), "and the mission mask");
    }
    // The mixes: tier 1 mostly infantry, tier 2 mostly knights, tier 3 casters and flyers, ships by hall tier.
    CheckMix(1, kGroupLand, 0.0, defaults, defaults.land, 200, "tier 1 land mix");
    CheckMix(2, kGroupLand, 0.0, defaults, defaults.land, 200, "tier 2 land mix");
    CheckMix(3, kGroupLand, 0.0, defaults, defaults.land, 200, "tier 3 land mix");
    CheckMix(1, kGroupNavy, 1.0, defaults, defaults.navy, 200, "tier 1 navy mix");
    CheckMix(2, kGroupNavy, 1.0, defaults, defaults.navy, 200, "tier 2 navy mix");
    CheckMix(3, kGroupNavy, 1.0, defaults, defaults.navy, 200, "tier 3 navy mix");
    {
        Plan p = CorePlan(3);
        for (int i = 0; i < 100; ++i) {
            bool saving = false;
            const int c = PickArmyClass(p, defaults, ArmyMask(), &saving);
            if (c < 0) break;
            Commit(p, c);
        }
        CHECK(p.count[kProdInfantry] == 0, "tier 3 with money for everything: no grunts at all (%d)", p.count[kProdInfantry]);
        bool saving = false;
        CHECK(PickArmyClass(p, defaults, 1u << kProdInfantry, &saving) == -1 && !saving, "a class at 0 %% is never trained by the mix");
    }
    // Submarines never take more than a fifth of the fleet, whatever the config asks for.
    {
        AutoProduction subHeavy;
        subHeavy.navy[1][kProdSubmarines] = 90;
        Plan p = CorePlan(2);
        p.navyShare = 1.0;
        for (int i = 0; i < 200; ++i) {
            bool saving = false;
            const int c = PickArmyClass(p, subHeavy, ArmyMask(), &saving);
            if (c < 0) break;
            Commit(p, c);
        }
        CHECK(p.count[kProdSubmarines] * 5 <= ArmySize(p) + 2 && p.count[kProdSubmarines] > 30,
              "submarines capped at a fifth of the fleet (%d of %d)", p.count[kProdSubmarines], ArmySize(p));
    }
    // The no-enemy-navy ceilings: a class at its cap drops out of the mix, and with every ship capped the land army
    // takes the whole share instead of it being lost.
    {
        Plan p = CorePlan(2);
        p.navyShare = 0.7;
        for (int c = 0; c < kProdClassCount; ++c) p.cap[c] = defaults.noEnemyNavyCap[c];
        CHECK(UnderCap(p, kProdDestroyers) && UnderCap(p, kProdInfantry), "nothing owned yet: everything is under its cap");
        p.count[kProdDestroyers] = 5;
        CHECK(!UnderCap(p, kProdDestroyers) && UnderCap(p, kProdBattleships), "5 destroyers are at their ceiling, battleships are not");
        double target[kProdClassCount];
        Targets(p, defaults, target);
        CHECK(target[kProdDestroyers] == 0 && target[kProdBattleships] > 0,
              "a capped class is out of the mix, the rest of the group keeps its share");
        p.count[kProdBattleships] = 2;
        Targets(p, defaults, target);
        CHECK(target[kProdSubmarines] == 0,
              "submarines may never be more than a fifth of the fleet, so they are not a fleet of their own either");
        p.count[kProdSubmarines] = 2;
        Targets(p, defaults, target);
        double land = 0, navy = 0;
        for (int c = 0; c < kProdClassCount; ++c) (GroupOf(c) == kGroupNavy ? navy : land) += target[c];
        CHECK(navy == 0 && fabs(land - (ArmySize(p) + 1)) < 1e-9,
              "every ship capped out: the whole army is land units, nothing is lost (land %.2f navy %.2f)", land, navy);
        bool saving = false;
        CHECK(PickArmyClass(p, defaults, GroupMask(kGroupNavy), &saving) == -1 &&
                  PickFiller(p, defaults, GroupMask(kGroupNavy)) == -1,
              "neither the mix nor the filler may go past a cap");
        CHECK(PickArmyClass(p, defaults, kBarracksMask, &saving) >= 0, "while the barracks still has work");
    }
    // Land and navy together: the map's share decides how much of the army is ships.
    {
        Plan p = CorePlan(2);
        p.navyShare = 0.5;
        double target[kProdClassCount];
        Targets(p, defaults, target);
        double land = 0, navy = 0;
        for (int c = 0; c < kProdClassCount; ++c) (GroupOf(c) == kGroupNavy ? navy : land) += target[c];
        CHECK(fabs(land - navy) < 1e-6 && land > 0, "half water: half the army is ships (land %.2f navy %.2f)", land, navy);
        for (int c = 0; c < kProdClassCount; ++c)
            if (GroupOf(c) == kGroupNavy) p.trainable[c] = false;  // no shipyard
        Targets(p, defaults, target);
        land = navy = 0;
        for (int c = 0; c < kProdClassCount; ++c) (GroupOf(c) == kGroupNavy ? navy : land) += target[c];
        CHECK(navy == 0 && land > 0, "without a shipyard the whole army is land (land %.2f navy %.2f)", land, navy);
    }
    // The filler: gold piling up, no lumber, tier 3 (where infantry has no share at all) still makes grunts.
    {
        Plan p = CorePlan(3);
        p.bank = {{60000, 100, 0}};
        p.count[kProdInfantry] = 20;
        bool saving = false;
        CHECK(PickArmyClass(p, defaults, kBarracksMask, &saving) == -1 && !saving, "tier 3, no lumber: nothing of the mix");
        CHECK(PickFiller(p, defaults, kBarracksMask) == kProdInfantry, "the filler picks the class the gold buys most of");
        int infantry = 0;
        for (int i = 0; i < 20; ++i) {
            const int c = PickFiller(p, defaults, kBarracksMask);
            if (c != kProdInfantry) break;
            ++infantry;
            Commit(p, c);
        }
        CHECK(infantry == 20, "and keeps picking it while the gold lasts (%d of 20)", infantry);
        CHECK(PickFiller(p, defaults, GroupMask(kGroupNavy)) == -1, "but never a ship on a land map");
        p.bank = {{3000, 100, 0}};  // only five grunts' worth: below filler_min
        CHECK(PickFiller(p, defaults, kBarracksMask) == -1, "a thin bank is not a filler reason");
        AutoProduction eager;
        eager.fillerMin = 4;
        CHECK(PickFiller(p, eager, kBarracksMask) == kProdInfantry, "filler_min decides how thick the bank must be");
    }
    // A building saves when what it trains is well over its share, so the money goes to the other buildings.
    {
        Plan p = CorePlan(1);
        p.navyShare = 0.3;
        for (int c : {kProdArchers, kProdKnights, kProdCasters, kProdFlyers, kProdBattleships, kProdSubmarines}) p.trainable[c] = false;
        p.count[kProdInfantry] = 10;
        p.count[kProdDestroyers] = 12;
        bool saving = false;
        CHECK(PickArmyClass(p, defaults, GroupMask(kGroupNavy), &saving) == -1 && saving,
              "12 destroyers on a 30 %% water map: the shipyard saves");
        CHECK(PickArmyClass(p, defaults, kBarracksMask, &saving) == kProdInfantry && !saving, "while the barracks trains");
        p.count[kProdDestroyers] = 5;
        CHECK(PickArmyClass(p, defaults, GroupMask(kGroupNavy), &saving) == kProdDestroyers, "5 destroyers is within tolerance");
    }
    // Saving up (1.10.1, the author's fleet of destroyers): the cheap class must not eat the resource the class
    // furthest behind its share is waiting for, or that class is starved for ever.
    {
        const unsigned kYard = GroupMask(kGroupNavy);
        Plan p = ShipyardCase(2950);
        double target[kProdClassCount];
        Targets(p, defaults, target);
        bool over = false;
        CHECK(target[kProdBattleships] - p.count[kProdBattleships] > target[kProdDestroyers] - p.count[kProdDestroyers] &&
                  CanAfford(p, defaults, kProdDestroyers) && !CanAfford(p, defaults, kProdBattleships) &&
                  PickArmyClass(p, defaults, kYard, &over) == kProdDestroyers && !over,
              "the case: battleships furthest behind (%+.1f against %+.1f) and the mix alone buys a destroyer anyway",
              target[kProdBattleships] - p.count[kProdBattleships], target[kProdDestroyers] - p.count[kProdDestroyers]);
        SaveUp state;
        const Decision d = Decide(p, defaults, kYard, state, 0);
        CHECK(d.cls == -1 && d.saveResource == kOil && d.saveClass == kProdBattleships && d.have == 2950 && d.need == 4120,
              "(a) the shipyard keeps its oil instead (cls %d, resource %d, have %d, need %d)", d.cls, d.saveResource,
              d.have, d.need);
        Plan rich = ShipyardCase(4200);
        SaveUp richState;
        const Decision b = Decide(rich, defaults, kYard, richState, 0);
        CHECK(b.cls == kProdBattleships && b.saveResource == -1, "(b) 4200 oil: the battleship itself is built (cls %d)", b.cls);
        AutoProduction never;
        never.saveUpSeconds = 0;
        SaveUp offState;
        CHECK(Decide(p, never, kYard, offState, 0).cls == kProdDestroyers && offState.resource == -1,
              "(e) save_up_seconds = 0 is the old behaviour: the destroyer");
    }
    // No deadlock: a blocked resource that goes nowhere releases one unit per save_up_seconds, one that is still
    // growing is waited for however long it takes.
    {
        const unsigned kYard = GroupMask(kGroupNavy);
        Plan p = ShipyardCase(2950);  // held flat on purpose: the decision core only spends when the caller commits
        SaveUp state;
        int released = 0;
        unsigned at[2] = {0, 0};
        bool clean = true;
        for (unsigned t = 0; t <= 180000; t += 1000) {
            const Decision d = Decide(p, defaults, kYard, state, t);
            if (d.cls < 0) {
                clean = clean && d.saveResource == kOil && d.saveClass == kProdBattleships;
                continue;
            }
            clean = clean && d.cls == kProdDestroyers;
            if (released < 2) at[released] = t;
            ++released;
        }
        CHECK(released == 2 && at[0] == 60000 && at[1] == 121000 && clean,
              "(c) flat oil: one destroyer per save_up_seconds, saving in between (%d releases, at %u and %u)", released,
              at[0], at[1]);
        Plan growing = ShipyardCase(2950);
        SaveUp growState;
        bool built = false;
        for (unsigned t = 0; t <= 600000; t += 1000) {
            built = built || Decide(growing, defaults, kYard, growState, t).cls >= 0;
            ++growing.bank.r[kOil];  // the tanker is working, and it is still far from a battleship's 4120
        }
        CHECK(!built && growing.bank.r[kOil] < 4120, "(d) while the oil grows nothing is built, however long it takes (%d oil)",
              growing.bank.r[kOil]);
    }
    // A class that does not cost the blocked resource is built as before, and only MONEY ever makes a building save:
    // food, a ceiling and a missing building are other gates and none of this rule's business.
    {
        Plan p = CorePlan(2);
        p.bank = {{200000, 300, 0}};  // gold to burn, almost no lumber
        double target[kProdClassCount];
        Targets(p, defaults, target);
        CHECK(!CanAfford(p, defaults, kProdKnights) && target[kProdKnights] > target[kProdArchers] &&
                  BlockingResource(p, defaults, kProdKnights) == kLumber,
              "the case: knights are the biggest share and the lumber for them is not there");
        SaveUp gruntState;
        const Decision grunt = Decide(p, defaults, 1u << kProdInfantry | 1u << kProdKnights, gruntState, 0);
        CHECK(grunt.cls == kProdInfantry && grunt.saveResource == -1 && gruntState.resource == -1,
              "(f) grunts cost no lumber, so they keep coming (cls %d)", grunt.cls);
        SaveUp archerState;
        const Decision archer = Decide(p, defaults, 1u << kProdArchers | 1u << kProdKnights, archerState, 0);
        CHECK(archer.cls == -1 && archer.saveResource == kLumber && archer.saveClass == kProdKnights,
              "(f) archers do, so they wait with the knights (cls %d, resource %d)", archer.cls, archer.saveResource);

        const unsigned kYard = GroupMask(kGroupNavy);
        Plan yard = ShipyardCase(2950);
        yard.trainable[kProdSubmarines] = false;  // no inventor / alchemist
        Plan prereq = yard;
        prereq.trainable[kProdBattleships] = false;  // no foundry
        SaveUp prereqState;
        CHECK(Decide(prereq, defaults, kYard, prereqState, 0).cls == kProdDestroyers && prereqState.resource == -1,
              "(g) a missing foundry is not a reason to save up");
        Plan capped = yard;
        capped.cap[kProdBattleships] = 2;  // no enemy navy: two is the ceiling, and two is what he has
        SaveUp cappedState;
        CHECK(Decide(capped, defaults, kYard, cappedState, 0).cls == kProdDestroyers && cappedState.resource == -1,
              "(g) nor is a class that is already at its ceiling");
        Plan starving = yard;
        starving.used = 199;
        SaveUp foodState;
        const Decision d = Decide(starving, defaults, kYard, foodState, 0);
        CHECK(d.cls == -1 && d.saveResource == -1 && foodState.resource == -1, "(g) nor is the food rule");
    }
    // plenty_units: once the bank buys that many of every class of a group, the resource ratios play no part at all
    // and the mix is the configured weights. Two banks with nothing in common must give the very same targets.
    {
        CHECK(defaults.plentyUnits == 10, "plenty_units defaults to 10 (%d)", defaults.plentyUnits);
        Plan rich = CorePlan(3);
        rich.navyShare = 0.5;
        rich.bank = {{590000, 41000, 30000}};  // his bank: gold to burn, and oil is the thin one
        Plan lean = rich;
        lean.bank = {{30000, 30000, 30000}};  // 13 gryphons is the tightest of these, still over plenty_units
        double a[kProdClassCount], b[kProdClassCount];
        Targets(rich, defaults, a);
        Targets(lean, defaults, b);
        double landShares = 0, navyShares = 0;
        for (int c = 0; c < kProdClassCount; ++c) {
            landShares += GroupOf(c) == kGroupLand ? defaults.land[2][c] : 0;
            navyShares += GroupOf(c) == kGroupNavy ? defaults.navy[2][c] : 0;
        }
        bool same = true, pure = true;
        for (int c = 0; c < kProdClassCount; ++c) {
            same = same && fabs(a[c] - b[c]) < 1e-9;
            if (GroupOf(c) == kGroupNone) continue;
            const bool navy = GroupOf(c) == kGroupNavy;
            const double share = navy ? defaults.navy[2][c] : defaults.land[2][c];
            pure = pure && fabs(a[c] - share / (navy ? navyShares : landShares) * 0.5) < 1e-9;
            CHECK(Buys(rich, c) >= defaults.plentyUnits && Buys(lean, c) >= defaults.plentyUnits,
                  "both banks must buy plenty of every class for this test to mean anything (%s: %.1f / %.1f)",
                  config::kProductionClassKeys[c], Buys(rich, c), Buys(lean, c));
        }
        CHECK(same && pure, "plenty of everything: the mix is the configured weights, whatever the bank looks like");
    }
    // Below that line a class's share shrinks in proportion to what the bank buys of it.
    {
        Plan p = CorePlan(2);
        p.bank = {{200000, 300, 0}};  // gold to burn; the lumber buys exactly 6 archers, and grunts need none
        AutoProduction six, twenty;
        six.plentyUnits = 6;
        twenty.plentyUnits = 20;
        double a[kProdClassCount], b[kProdClassCount], c[kProdClassCount];
        Targets(p, six, a);
        Targets(p, defaults, b);
        Targets(p, twenty, c);
        CHECK(fabs(a[kProdArchers] / a[kProdInfantry] - 25.0 / 10.0) < 1e-9,
              "six archers and plenty_units 6: the archers' full 25 %% against the infantry's 10 %% (%.2f)",
              a[kProdArchers] / a[kProdInfantry]);
        CHECK(fabs(b[kProdArchers] / b[kProdInfantry] - 25.0 / 10.0 * 6 / 10) < 1e-9 &&
                  fabs(c[kProdArchers] / c[kProdInfantry] - 25.0 / 10.0 * 6 / 20) < 1e-9,
              "and 6/10 or 6/20 of it when the setting asks for more (%.2f, %.2f)", b[kProdArchers] / b[kProdInfantry],
              c[kProdArchers] / c[kProdInfantry]);
    }
    // upgrade_bias: a line with 4 upgrade levels takes a bigger share; bias 0 ignores upgrades.
    {
        Plan p = CorePlan(1);
        double plain[kProdClassCount], upgraded[kProdClassCount];
        Targets(p, defaults, plain);
        p.levels[kProdArchers] = 4;
        Targets(p, defaults, upgraded);
        CHECK(upgraded[kProdArchers] > plain[kProdArchers] * 1.2 && upgraded[kProdInfantry] < plain[kProdInfantry],
              "upgrade_bias must shift the mix to the upgraded line (%.2f -> %.2f)", plain[kProdArchers], upgraded[kProdArchers]);
        AutoProduction noBias;
        noBias.upgradeBias = 0;
        Targets(p, noBias, upgraded);
        CHECK(fabs(upgraded[kProdArchers] - plain[kProdArchers]) < 1e-9, "upgrade_bias 0 must ignore upgrades");
    }
    // [auto_production.class_upgrade_bias]: one class's upgrades can count more (the author: siege x2) without
    // touching the others; -1 = the shared upgrade_bias.
    {
        Plan p = CorePlan(1);
        p.levels[kProdSiege] = 2;
        p.levels[kProdArchers] = 2;
        AutoProduction shared, siegeDouble;
        siegeDouble.classUpgradeBias[kProdSiege] = 0.5;
        double a[kProdClassCount], b[kProdClassCount];
        Targets(p, shared, a);
        Targets(p, siegeDouble, b);
        CHECK(b[kProdSiege] > a[kProdSiege] * 1.1, "class_upgrade_bias siege 0.5 must raise the siege share (%.3f -> %.3f)",
              a[kProdSiege], b[kProdSiege]);
        CHECK(fabs(b[kProdArchers] / b[kProdInfantry] - a[kProdArchers] / a[kProdInfantry]) < 1e-9,
              "the other classes keep their ratios");
        CHECK(shared.classUpgradeBias[kProdSiege] < 0, "the default is -1: use upgrade_bias");
    }

    // ---- Engine: real passes over the fake world ----
    PatchJump(kRvaStartProduction, &FakeStartProduction);
    ProdSave();

    // Off by default: the shipped config, the tick, nothing starts.
    ProdWorld();
    config::g.production.enabled = false;
    CHECK(!defaults.enabled && defaults.toggleKey == VK_F10, "auto-production must be off by default, Ctrl+F10");
    AddProd(0x3D, 0, 10, 10);
    AddProd(0x4B, 0, 14, 10);
    {
        World w;
        CHECK(BuildWorld(w), "fake world");
        production::OnTick(w, 5000);
        CHECK(g_prodAttempts == 0, "switched off: no production");
        config::g.production.enabled = true;
        production::OnTick(w, 600);
        CHECK(g_prodAttempts == 0, "less than a second of play: no pass yet");
        production::OnTick(w, 500);
        CHECK(StartsOf(0x03) == 1 && StartsOf(0x01) == 1, "orc: a peon at the great hall and a grunt at the barracks (%d, %d)",
              StartsOf(0x03), StartsOf(0x01));
    }

    // Toggle: Ctrl+F10 flips it with a banner.
    {
        const int messages = g_messages;
        const bool before = config::g.production.enabled;
        mod::SetKeyReaderForTest(&FakeKeys);
        g_fakeCtrl = g_fakeF10 = true;
        mod::OnTick();
        CHECK(config::g.production.enabled != before && g_messages == messages + 1, "Ctrl+F10 must flip auto-production with a banner");
        mod::OnTick();
        CHECK(config::g.production.enabled != before, "holding the keys flips once");
        g_fakeF10 = false;
        mod::OnTick();
        g_fakeF10 = true;
        mod::OnTick();
        CHECK(config::g.production.enabled == before, "a second press flips it back");
        g_fakeCtrl = false;
        mod::OnTick();
        g_fakeF10 = false;
        mod::SetKeyReaderForTest(nullptr);
    }

    // Workers: every idle hall at once, up to the best hall tier's target (12 / 16 / 24).
    ProdWorld();
    Unit* hall1 = AddProd(0x4A, 0, 5, 5);
    Unit* hall2 = AddProd(0x4A, 0, 15, 5);
    for (int i = 0; i < 10; ++i) AddProd(0x02, 0, 20 + i, 20);
    ProdPass(1000);
    CHECK(StartsAt(hall1) == 1 && StartsAt(hall2) == 1 && StartsOf(0x02) == 2, "10 workers, target 12: both halls train one");
    FinishTraining(0);
    ProdPass(2000);
    CHECK(StartsOf(0x02) == 2, "12 workers at tier 1 is enough (%d)", StartsOf(0x02));
    Field<uint8_t>(hall1, kOffType) = 0x58;  // one hall became a keep: tier 2, target 16
    ProdPass(3000);
    CHECK(StartsOf(0x02) == 4 && production::LastPlan().tier == 2, "a keep wants 16: both halls train again (%d)", StartsOf(0x02));
    FinishTraining(0);
    Field<uint8_t>(hall2, kOffType) = 0x5A;  // castle: 24
    ProdPass(4000);
    CHECK(StartsOf(0x02) == 6 && production::LastPlan().tier == 3, "castle: tier 3, target 24");

    // The reported case: a poor early game where the upgrade reserve holds more gold than the player owns. The
    // peasants must still come (they are the economy), the army must still wait.
    ProdWorld();
    Unit* poorHall = AddProd(0x4A, 0, 5, 5);
    AddProd(0x3C, 0, 10, 5);  // barracks: the keep upgrade is purchasable, 2000 gold
    AddProd(0x52, 0, 15, 5);  // blacksmith: swords 1, 800 gold
    for (int i = 0; i < 4; ++i) AddProd(0x02, 0, 20 + i, 20);
    At<uint16_t>(kRvaUpgradeGold)[0] = 800;
    At<uint32_t>(kRvaUpgradesAllowed)[0] = 0x4;
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF;  // the keep upgrade is allowed
    At<int32_t>(kRvaPlayerGold)[0] = 900;
    At<int32_t>(kRvaPlayerLumber)[0] = 300;
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    ProdPass(1000);
    CHECK(production::LastPlan().reserve.r[0] == 1300 && StartsAt(poorHall) == 1 && StartsOf(0x02) == 1,
          "reserve %d of a 900 gold bank: the peasant comes anyway (%d)", production::LastPlan().reserve.r[0], StartsOf(0x02));
    CHECK(StartsOf(0x00) == 0, "while the army still waits for the reserve");
    FinishTraining(0);
    config::g.production.workersIgnoreReserve = false;
    ProdPass(2000);
    CHECK(StartsOf(0x02) == 1, "workers_ignore_reserve = false: the old behaviour, no peasant (%d)", StartsOf(0x02));
    config::g.production.workersIgnoreReserve = true;
    ProdPass(3000);
    CHECK(StartsOf(0x02) == 2, "and back on again");
    // The tanker takes the same road.
    ProdWorld();
    ProdWaterMap(50, 0);
    Unit* poorYard = AddProd(0x48, 0, 30, 30);
    AddProd(0x4A, 0, 5, 5);
    AddProd(0x3C, 0, 10, 5);
    AddProd(0x56, 0, 35, 35);  // his own oil platform
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF;
    At<int32_t>(kRvaPlayerGold)[0] = 900;
    At<int32_t>(kRvaPlayerLumber)[0] = 300;
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    ProdPass(1000);
    CHECK(production::LastPlan().reserve.r[0] == 500 && StartsAt(poorYard) == 1 && StartsOf(0x1A) == 1,
          "the tanker comes although the spare bank buys less than one (%d gold reserved)", production::LastPlan().reserve.r[0]);
    FinishTraining(0);
    ProdWorld();  // a fresh yard, otherwise the one-tanker rule answers instead of the reserve
    ProdWaterMap(50, 0);
    Unit* strictYard = AddProd(0x48, 0, 30, 30);
    AddProd(0x4A, 0, 5, 5);
    AddProd(0x3C, 0, 10, 5);
    AddProd(0x56, 0, 35, 35);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF;
    At<int32_t>(kRvaPlayerGold)[0] = 900;
    At<int32_t>(kRvaPlayerLumber)[0] = 300;
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    config::g.production.tankersIgnoreReserve = false;
    ProdPass(1000);
    CHECK(StartsAt(strictYard) == 0, "tankers_ignore_reserve = false: the tanker waits for the reserve too");

    // The diagnostic: one line per 30 s of play, with the first failing gate per class.
    ProdWorld();
    config::g.logCasts = true;
    AddProd(0x4A, 0, 5, 5);
    AddProd(0x3C, 0, 10, 5);
    AddProd(0x4C, 0, 15, 5);  // lumber mill: archers are possible
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);  // the worker target is reached
    At<int32_t>(kRvaPlayerGold)[0] = 700;   // one footman (600), but not four of them
    At<int32_t>(kRvaPlayerLumber)[0] = 20;  // an archer needs 50
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    const int lines = LogCount(dir, "production: nothing (");
    ProdPass(100000);
    CHECK(LogCount(dir, "production: nothing (") == lines + 1 && g_prodStartCount == 0, "the diagnostic must be logged");
    CHECK(LogContains(dir, "production: nothing (workers 12/12, food free 188, gold 700 lum 20 oil 0, reserve 0/0/0, "
                           "blocked: workers=enough, infantry=bank, archers=lumber, knights=prereq, siege=prereq)"),
          "the diagnostic line must name the first failing gate of every class");
    ProdPass(101000);
    ProdPass(129999);
    CHECK(LogCount(dir, "production: nothing (") == lines + 1, "at most one line per 30 s of play");
    ProdPass(130001);
    CHECK(LogCount(dir, "production: nothing (") == lines + 2, "and one again 30 s later");

    // Rich, every threshold cleared, and still nothing: the shipyard is well past its share and the barracks is
    // busy. The line must say which is which, with the mix deficit.
    ProdWorld();
    config::g.logCasts = true;
    AddProd(0x4A, 0, 5, 5);
    Unit* busyBarracks = AddProd(0x3C, 0, 10, 5);
    AddProd(0x48, 0, 30, 30);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);
    for (int i = 0; i < 12; ++i) AddProd(0x1E, 0, i, 35);  // 12 destroyers on a map that wants 30 % ships
    AddEnemyShipyard();
    ProdWaterMap(25, 0);
    ProdPass(200000);
    CHECK(StartsAt(busyBarracks) == 1 && g_prodStartCount == 1, "the barracks trains, the shipyard saves");
    ProdPass(230001);  // the barracks is still busy, so this pass can do nothing at all
    CHECK(g_prodStartCount == 1 && LogContains(dir, "destroyers=enough(-") && LogContains(dir, "infantry=busy"),
          "a pass where the mix says enough must log the deficit, not a money reason");
    config::g.logCasts = false;

    // Saving up in a real pass: a shipyard with a destroyer's worth of oil and a battleship further behind its share
    // waits, says so in the log, and gives up after save_up_seconds. A new map starts that clock again.
    auto savingYard = [&] {  // tier 2, half water, no land production building, oil for a destroyer but not a battleship
        ProdWorld();
        AddEnemyShipyard();        // an enemy navy: no ship ceilings in the way
        AddProd(0x48, 0, 30, 30);  // shipyard
        AddProd(0x58, 0, 5, 5);    // keep: tier 2, where battleships are the bigger share
        AddProd(0x4E, 0, 9, 5);    // foundry: battleships are trainable
        ProdWaterMap(50, 0);
        config::g.production.workersTier[1] = 0;  // no peasants in the way
        At<int32_t>(kRvaPlayerOil)[0] = 3000;     // four destroyers (700) but not four battleships (1000)
    };
    savingYard();
    config::g.logCasts = true;
    ProdPass(1000);
    CHECK(g_prodStartCount == 0 && LogContains(dir, "production: saving oil for battleship (have 3000, need 4000)"),
          "the shipyard keeps the oil for the battleship, and the log says so (%d starts)", g_prodStartCount);
    CHECK(LogContains(dir, "destroyers=saving") && LogContains(dir, "battleships=bank"),
          "and the diagnostic line names the class that was held back");
    config::g.logCasts = false;
    ProdPass(61000);
    CHECK(StartsOf(0x1E) == 1, "oil that goes nowhere for save_up_seconds releases one destroyer (%d)", StartsOf(0x1E));
    savingYard();
    ProdPass(1000);
    production::OnNewMap();
    ProdPass(61000);
    CHECK(g_prodStartCount == 0, "(h) a new map forgets what every building was saving for (%d starts)", g_prodStartCount);
    ProdPass(121000);
    CHECK(StartsOf(0x1E) == 1, "and the clock runs again from the new map (%d)", StartsOf(0x1E));

    // The food gate in a real pass: supply 20, 15 used: one unit, not two.
    ProdWorld();
    AddProd(0x3C, 0, 5, 5);
    AddProd(0x3C, 0, 10, 5);
    At<uint16_t>(kRvaFoodSupply)[0] = 20;
    At<uint16_t>(kRvaUnitsCounted)[0] = 15;
    ProdPass(1000);
    CHECK(g_prodStartCount == 1 && StartsOf(0x00) == 1, "food: exactly one footman from two barracks (%d)", g_prodStartCount);
    At<uint16_t>(kRvaUnitsCounted)[0] = 16;
    FinishTraining(0, false);
    ProdPass(20000);
    CHECK(g_prodStartCount == 1, "16 of 20 used: nothing, the last 4 food are the player's");

    // The map profile: a dry map builds no ships at all, a wet one does, and the log says what it counted.
    ProdWorld();
    Unit* yard = AddProd(0x48, 0, 30, 30);
    AddProd(0x4A, 0, 5, 5);
    AddProd(0x3C, 0, 8, 5);
    for (int i = 0; i < 18; ++i) AddProd(0x02, 0, i, 20);  // the workers are done
    ProdPass(1000);
    CHECK(production::LastPlan().navyShare == 0 && StartsAt(yard) == 0 && StartsOf(0x00) == 1,
          "a dry map with no oil: no ships at all, while the barracks works");
    FinishTraining(0);
    ProdWaterMap(50, 2);
    ProdPass(2000);
    CHECK(fabs(production::LastPlan().navyShare - 0.72) < 1e-9, "50 %% water, 2 oil patches, tier 1: 72 %% ships (%.2f)",
          production::LastPlan().navyShare);
    CHECK(StartsAt(yard) == 1 && StartsOf(0x1E) == 1, "and the shipyard starts a destroyer (tier 1 ships are 80 %% destroyers)");
    CHECK(LogContains(dir, "production: map is 50 % water with 2 oil source(s): ships get 72 / 60 / 54 % of the army by hall tier"),
          "the map profile must be logged");

    // Tanker: one, once an oil platform is his; never a second.
    ProdWorld();
    ProdWaterMap(50, 0);
    Unit* yard2 = AddProd(0x48, 0, 30, 30);
    AddProd(0x4A, 0, 5, 5);
    for (int i = 0; i < 18; ++i) AddProd(0x02, 0, i, 20);
    ProdPass(1000);
    CHECK(StartsOf(0x1A) == 0, "no oil platform: no tanker");
    FinishTraining(0);
    AddProd(0x56, 0, 35, 35);
    ProdPass(2000);
    CHECK(StartsOf(0x1A) == 1, "an oil platform and no tanker: one tanker");
    FinishTraining(0);
    ProdPass(3000);
    FinishTraining(0);
    ProdPass(4000);
    CHECK(StartsOf(0x1A) == 1 && StartsAt(yard2) >= 3, "never a second tanker; the shipyard goes back to warships");

    // Found in play (1.14.1): a tanker spends most of its life INSIDE the platform or the shipyard, and a peasant inside
    // the mine or the hall: hidden (state bit 3), not gone. The mod skipped hidden units, saw "no tanker" and trained a
    // second one 25 s after the first. Hidden units of the player count like any other.
    {
        Unit* tanker = nullptr;
        int hiddenWorkers = 0;
        for (int i = 0; i < g_unitCount; ++i) {
            Unit* u = reinterpret_cast<Unit*>(g_units + i * kUnitSize);
            if (OwnerOf(u) != 0) continue;
            if (TypeOf(u) == 0x1A) tanker = u;
            if (TypeOf(u) == 0x02 && hiddenWorkers < 10) {  // ten of the eighteen peasants are in the mine right now
                Field<uint8_t>(u, kOffStateFlags) |= kStateHidden;
                ++hiddenWorkers;
            }
        }
        CHECK(tanker != nullptr && hiddenWorkers == 10, "test world: one tanker and ten peasants to hide");
        if (tanker) Field<uint8_t>(tanker, kOffStateFlags) |= kStateHidden;
        const int tankersBefore = StartsOf(0x1A), workersBefore = StartsOf(0x02);
        FinishTraining(0);
        ProdPass(5000);
        FinishTraining(0);
        ProdPass(6000);
        CHECK(StartsOf(0x1A) == tankersBefore, "a tanker inside its platform is still a tanker: never a second one (%d started)",
              StartsOf(0x1A) - tankersBefore);
        CHECK(StartsOf(0x02) == workersBefore, "peasants inside the mine still count as workers (%d trained on top of 18)",
              StartsOf(0x02) - workersBefore);
        CHECK(production::LastPlan().count[kProdTankers] == 1 && production::LastPlan().count[kProdWorkers] == 18,
              "the plan must see 1 tanker and 18 workers, hidden ones included (%d / %d)",
              production::LastPlan().count[kProdTankers], production::LastPlan().count[kProdWorkers]);
        // A dead or dying unit is still not counted.
        if (tanker) Field<uint8_t>(tanker, kOffStateFlags) = static_cast<uint8_t>((Field<uint8_t>(tanker, kOffStateFlags) & 0xF0) | kStateDying);
        FinishTraining(0);
        ProdPass(7000);
        CHECK(production::LastPlan().count[kProdTankers] == 0, "a dying tanker is no tanker (%d)",
              production::LastPlan().count[kProdTankers]);
    }

    // Submarines: only on top of a real bank, and never more than a fifth of the fleet.
    ProdWorld();
    ProdWaterMap(60, 4);
    AddEnemyShipyard();  // the enemy has a navy: no ship caps here
    Unit* yard3 = AddProd(0x48, 0, 30, 30);
    AddProd(0x58, 0, 5, 5);   // keep: tier 2
    AddProd(0x4E, 0, 9, 5);   // foundry: battleships
    AddProd(0x44, 0, 12, 5);  // inventor: submarines
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 20);
    At<int32_t>(kRvaPlayerGold)[0] = 3200;  // exactly four submarines' worth of every resource one costs
    At<int32_t>(kRvaPlayerLumber)[0] = 600;
    At<int32_t>(kRvaPlayerOil)[0] = 3600;
    ProdPass(1000);
    CHECK(StartsOf(0x26) == 0, "a thin bank: no submarines");
    At<int32_t>(kRvaPlayerGold)[0] = 100000;
    At<int32_t>(kRvaPlayerLumber)[0] = 100000;
    At<int32_t>(kRvaPlayerOil)[0] = 100000;
    {
        int subs = 0, ships = 0;
        for (int round = 0; round < 20; ++round) {
            FinishTraining(0);
            ProdPass(2000 + round * 1000);
        }
        for (int i = 0; i < g_prodStartCount; ++i) {
            const uint8_t t = g_prodStarts[i].type;
            if (t == 0x26 || t == 0x27) ++subs;
            if (t >= 0x1E && t <= 0x27) ++ships;
        }
        CHECK(subs > 0 && subs * 5 <= ships + 2, "rich: submarines appear but stay a fifth of the fleet (%d of %d)", subs, ships);
        CHECK(StartsAt(yard3) > 5, "the shipyard kept working (%d)", StartsAt(yard3));
    }

    // No enemy shipyard and no enemy warship anywhere on the map: a token fleet and nothing more.
    ProdWorld();
    ProdWaterMap(70, 0);
    Unit* capYard = AddProd(0x48, 0, 30, 30);
    AddProd(0x58, 0, 5, 5);   // keep: tier 2, so battleships and submarines are in the mix
    AddProd(0x4E, 0, 9, 5);   // foundry
    AddProd(0x44, 0, 12, 5);  // inventor
    AddProd(0x56, 0, 35, 35);  // his own oil platform: one tanker is allowed
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 20);
    for (int round = 0; round < 24; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x1E) == 5 && StartsOf(0x20) == 2 && StartsOf(0x26) == 2 && StartsOf(0x1A) == 1 && StartsAt(capYard) == 10,
          "no enemy navy: 5 destroyers, 2 battleships, 2 submarines, 1 tanker (%d/%d/%d/%d)", StartsOf(0x1E), StartsOf(0x20),
          StartsOf(0x26), StartsOf(0x1A));
    CHECK(LogContains(dir, "production: no enemy shipyard: ships capped at 1 tanker / 5 destroyers / 2 battleships / 2 submarines"),
          "the caps must be logged once");
    const int capLines = LogCount(dir, "production: no enemy shipyard:");
    ProdPass(30000);
    CHECK(LogCount(dir, "production: no enemy shipyard:") == capLines, "and not once per pass");
    // A half built enemy shipyard is enough to lift them.
    Unit* halfBuilt = AddEnemyShipyard();
    Field<uint16_t>(halfBuilt, kOffStateFlags) = 0;  // still under construction
    for (int round = 0; round < 3; ++round) {
        ProdPass(31000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x1E) + StartsOf(0x20) + StartsOf(0x26) > 9 && LogContains(dir, "an enemy shipyard or warship is on the map"),
          "an enemy shipyard under construction lifts the caps (%d ships)", StartsOf(0x1E) + StartsOf(0x20) + StartsOf(0x26));

    // Front line ([auto_production] land_units_where_enemies): two landmasses split by a strait (x 30..33 water),
    // home on the left, a second base on the right. Land army units come from the landmass with a known enemy.
    {
        static uint8_t explored[kMap * kMap];
        uint8_t* const savedExplored = *At<uint8_t*>(kRvaExploredMap);
        *At<uint8_t*>(kRvaExploredMap) = explored;
        struct FrontBase { Unit* hall; Unit* home; Unit* front; };
        auto frontWorld = [&](bool frontBarracks) {
            ProdWorld();
            for (int y = 0; y < kMap; ++y)
                for (int x = 30; x < 34; ++x) g_prodSquare[y * kMap + x] = kSqWater;
            memset(explored, 0, sizeof explored);  // all explored
            production::OnNewMap();
            FrontBase b{};
            b.hall = AddProd(0x4A, 0, 5, 5);
            b.home = AddProd(0x3C, 0, 10, 5);
            if (frontBarracks) b.front = AddProd(0x3C, 0, 40, 5);
            for (int i = 0; i < 5; ++i) AddProd(0x02, 0, i, 20);  // below the worker target: the hall trains
            return b;
        };

        // An enemy base on explored ground on the right: only the right barracks trains the army, the hall its peasant.
        FrontBase b = frontWorld(true);
        config::g.logCasts = false;
        AddProd(0x3D, 1, 50, 20);  // an enemy barracks
        ProdPass(1000);
        CHECK(production::LandmassAt(10, 5) == 1 && production::LandmassAt(40, 5) == 2 && production::LandmassAt(31, 5) == 0,
              "landmasses: left 1, right 2, the strait none (%d %d %d)", production::LandmassAt(10, 5),
              production::LandmassAt(40, 5), production::LandmassAt(31, 5));
        CHECK(StartsAt(b.front) == 1 && StartsAt(b.home) == 0 && StartsAt(b.hall) == 1,
              "front: the barracks facing the enemy trains, the home one waits, the hall still trains (%d %d %d)",
              StartsAt(b.front), StartsAt(b.home), StartsAt(b.hall));
        CHECK(LogContains(dir, "production: land units only on landmass 2 (enemy base seen)"), "front: the landmass is logged");
        const int frontLines = LogCount(dir, "production: land units only on landmass 2");
        FinishTraining(0);
        ProdPass(2000);
        CHECK(LogCount(dir, "production: land units only on landmass 2") == frontLines && StartsAt(b.home) == 0,
              "front: logged once, and the home barracks keeps waiting");

        // The enemy base on ground the player has never explored: nothing known, both barracks train.
        b = frontWorld(true);
        AddProd(0x3D, 1, 50, 20);
        explored[20 * kMap + 50] = kTileUnexplored;
        ProdPass(1000);
        CHECK(StartsAt(b.front) == 1 && StartsAt(b.home) == 1, "front: an enemy the player cannot know about changes nothing");

        // An enemy army seen on the right counts for a minute after it is out of sight, then the rule lets go.
        b = frontWorld(true);
        Unit* grunt = AddProd(0x01, 1, 45, 10);
        ProdPass(1000);
        CHECK(StartsAt(b.front) == 1 && StartsAt(b.home) == 0, "front: a visible enemy army marks its landmass");
        CHECK(LogContains(dir, "production: land units only on landmass 2 (enemy units seen)"), "front: units seen is logged");
        FinishTraining(0);
        Field<uint8_t>(grunt, kOffFogMask) = 1;  // now under the player's fog
        ProdPass(50000);
        CHECK(StartsAt(b.home) == 0, "front: still remembered 49 s later");
        FinishTraining(0);
        ProdPass(62000);
        CHECK(StartsAt(b.home) == 1 && LogContains(dir, "production: land units anywhere again"),
              "front: forgotten after a minute out of sight, both barracks train again");

        // No barracks of the player's on the enemy's landmass: production never stops, the home barracks trains.
        b = frontWorld(false);
        AddProd(0x3D, 1, 50, 20);
        ProdPass(1000);
        CHECK(StartsAt(b.home) == 1, "front: with no barracks facing the enemy, home trains as before");

        // Switched off: both train.
        b = frontWorld(true);
        AddProd(0x3D, 1, 50, 20);
        config::g.production.landUnitsWhereEnemies = false;
        ProdPass(1000);
        CHECK(StartsAt(b.front) == 1 && StartsAt(b.home) == 1, "front: land_units_where_enemies = false changes nothing");

        // Flyers cross water: an aviary at home keeps training while the front rule holds the home barracks.
        b = frontWorld(true);
        AddProd(0x5A, 0, 5, 10);  // a castle: tier 3, flyers are 15 % of the land army
        AddProd(0x42, 0, 14, 5);  // stables
        Unit* aviary = AddProd(0x46, 0, 18, 5);
        AddProd(0x3D, 1, 50, 20);
        ProdPass(1000);
        CHECK(StartsAt(aviary) == 1 && StartsAt(b.home) == 0, "front: the home aviary still trains flyers (%d), the home barracks waits",
              StartsAt(aviary));
        *At<uint8_t*>(kRvaExploredMap) = savedExplored;
    }

    // Navy food: an own shipyard, an enemy one, no oil yet. The barracks may not eat the food the ships still need.
    {
        auto navyWorld = [&](bool enemy) {
            ProdWorld();
            ProdWaterMap(50, 0);  // tier 1: ships are 60 % of the army
            AddProd(0x48, 0, 30, 30);
            AddProd(0x4A, 0, 5, 5);
            AddProd(0x3C, 0, 8, 5);
            for (int i = 0; i < 18; ++i) AddProd(0x02, 0, i, 20);
            if (enemy) AddEnemyShipyard();
            At<int32_t>(kRvaPlayerOil)[0] = 0;  // the ships wait for oil
            At<uint16_t>(kRvaFoodSupply)[0] = 40;
            At<uint16_t>(kRvaUnitsCounted)[0] = 35;  // 5 free: the plain rule lets one more land unit in
        };
        navyWorld(true);
        config::g.logCasts = true;  // the "production: nothing" line names the reason
        ProdPass(1000);
        CHECK(StartsOf(0x00) == 0 && production::LastPlan().navyFood == 1 && production::LastPlan().shipFood == 1,
              "navy food: the barracks waits (%d starts, keeping %d, ship food %d)", StartsOf(0x00),
              production::LastPlan().navyFood, production::LastPlan().shipFood);
        CHECK(LogContains(dir, "production: keeping 1 food for ships (navy 0 of 1)"), "navy food: the reserve is logged");
        CHECK(LogContains(dir, "infantry=navy food"), "navy food: the nothing line names navy food as the reason");
        config::g.logCasts = false;
        const int navyLines = LogCount(dir, "production: keeping 1 food for ships");
        ProdPass(2000);
        CHECK(LogCount(dir, "production: keeping 1 food for ships") == navyLines, "navy food: logged once, not every pass");
        At<int32_t>(kRvaPlayerOil)[0] = 100000;  // the oil is in: the shipyard is not held by the reserve
        ProdPass(3000);
        CHECK(StartsOf(0x1E) == 1 && StartsOf(0x00) == 0, "navy food: the destroyer starts, the footman still waits");
        navyWorld(false);
        ProdPass(1000);
        CHECK(StartsOf(0x00) == 1 && production::LastPlan().navyFood == 0, "navy food: no enemy navy, no reserve: the footman starts");
        // The table the food per ship comes from: a destroyer eats, a skeleton does not.
        CHECK(At<uint32_t>(kRvaCounterByType)[0x1E] != static_cast<uint32_t>(g_base + kRvaFoodFreeUnits) &&
                  At<uint32_t>(kRvaCounterByType)[0x37] == static_cast<uint32_t>(g_base + kRvaFoodFreeUnits),
              "the per-type counter table: skeletons are food-free, destroyers are not");
    }

    // A hostile warship with no shipyard at all lifts them too (a mission that hands the enemy a fleet).
    ProdWorld();
    ProdWaterMap(70, 0);
    AddProd(0x48, 0, 30, 30);
    AddProd(0x58, 0, 5, 5);
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 20);
    AddProd(0x1F, 1, 50, 50);  // one enemy troll destroyer, no shipyard
    for (int round = 0; round < 10; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x1E) > 5, "an enemy warship lifts the caps as well (%d destroyers)", StartsOf(0x1E));

    // An ALLIED shipyard is not an enemy navy.
    ProdWorld();
    ProdWaterMap(70, 0);
    AddProd(0x48, 0, 30, 30);
    AddProd(0x58, 0, 5, 5);
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 20);
    AddEnemyShipyard(2);
    At<uint8_t>(kRvaAlliance)[0 * 16 + 2] = 1;  // player 2 is an ally, his shipyard proves nothing
    At<uint8_t>(kRvaAlliance)[2 * 16 + 0] = 1;
    for (int round = 0; round < 12; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x1E) == 5, "an allied shipyard does not lift the caps (%d destroyers)", StartsOf(0x1E));

    // Ships in training count: the fifth destroyer is under way, so there is no sixth.
    ProdWorld();
    ProdWaterMap(70, 0);
    Unit* trainYard = AddProd(0x48, 0, 30, 30);
    AddProd(0x4A, 0, 5, 5);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);
    for (int i = 0; i < 4; ++i) AddProd(0x1E, 0, i, 35);
    ProdPass(1000);
    CHECK(StartsAt(trainYard) == 1 && StartsOf(0x1E) == 1, "4 destroyers owned: the fifth is started");
    ProdPass(2000);  // it is still in training
    CHECK(StartsOf(0x1E) == 1, "and no sixth while the fifth is in training");
    FinishTraining(0);
    ProdPass(3000);
    CHECK(StartsOf(0x1E) == 1, "nor when it is finished");

    // Already over the cap (a mission that starts you with a fleet): no ships, but the land army goes on.
    ProdWorld();
    ProdWaterMap(70, 0);
    Unit* overYard = AddProd(0x48, 0, 30, 30);
    Unit* landBarracks = AddProd(0x3C, 0, 10, 5);
    AddProd(0x4A, 0, 5, 5);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 20);
    for (int i = 0; i < 8; ++i) AddProd(0x1E, 0, i, 35);  // eight destroyers already
    for (int round = 0; round < 4; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsAt(overYard) == 0 && StartsOf(0x00) == 4 && StartsAt(landBarracks) == 4,
          "over the cap: no ships at all, and the barracks keeps working every pass (%d ships, %d footmen)",
          StartsAt(overYard), StartsOf(0x00));
    config::g.logCasts = true;
    ProdPass(100000);   // the barracks starts a footman here, so this pass did produce something
    ProdPass(101000);   // with it still busy, nothing can be produced at all: the diagnostic fires
    CHECK(LogContains(dir, "destroyers=cap"), "the diagnostic must name the cap as the reason");
    config::g.logCasts = false;

    // Excluded units never come out, whatever the player owns; the inventor / alchemist is never used.
    ProdWorld();
    ProdWaterMap(40, 2);
    AddEnemyShipyard();
    Unit* inventor = AddProd(0x44, 0, 50, 50);
    {
        int x = 2;
        for (int t : {0x3C, 0x48, 0x4E, 0x46, 0x50, 0x42, 0x52, 0x4C, 0x5A, 0x56}) AddProd(static_cast<uint8_t>(t), 0, x += 3, 5);
    }
    for (int i = 0; i < 24; ++i) AddProd(0x02, 0, i, 25);
    for (int round = 0; round < 5; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    {
        bool excluded = false;
        for (int i = 0; i < g_prodStartCount; ++i) {
            const uint8_t t = g_prodStarts[i].type & ~1;
            excluded = excluded || t == 0x0E || t == 0x10 || t == 0x1C || t == 0x28 || (t >= 0x14 && t <= 0x18) ||
                       g_prodStarts[i].type > 0x2B;
        }
        CHECK(!excluded && StartsAt(inventor) == 0 && g_prodStartCount > 10,
              "never transports, flying machines / zeppelins, dwarves or heroes (%d starts)", g_prodStartCount);
        CHECK(StartsOf(0x06) > 0 && StartsOf(0x2A) > 0 && StartsOf(0x0A) > 0,
              "tier 3 with every building: knights, gryphons and mages appear (%d %d %d)", StartsOf(0x06), StartsOf(0x2A), StartsOf(0x0A));
    }

    // ALOW: a unit the map forbids is never trained.
    ProdWorld();
    AddProd(0x3C, 0, 5, 5);
    AddProd(0x4C, 0, 10, 5);  // lumber mill: archers would be possible
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0x07FFFFFF & ~0x10u;
    for (int round = 0; round < 6; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x08) == 0 && StartsOf(0x12) == 0 && StartsOf(0x00) > 0, "archers forbidden by the map: never trained");
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0x07FFFFFF;
    for (int round = 0; round < 6; ++round) {
        ProdPass(10000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x08) > 0, "archers allowed again: they come");

    // A campaign mission that forbids knights / ogres (0x8) and battleships / juggernaughts (0x200): those two are
    // never trained, and their shares go to what is left instead of being lost.
    ProdWorld();
    ProdWaterMap(50, 0);
    AddProd(0x58, 0, 5, 5);  // keep: tier 2, where knights are 60 % of the land army
    {
        int x = 8;
        for (int t : {0x3C, 0x42, 0x52, 0x4C, 0x48, 0x4E}) AddProd(static_cast<uint8_t>(t), 0, x += 3, 5);
    }
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 25);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0x07FFFFFF & ~(0x8u | 0x200u);
    for (int round = 0; round < 12; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x06) == 0 && StartsOf(0x0C) == 0 && StartsOf(0x20) == 0,
          "mission mask: no knights (%d), no paladins (%d), no battleships (%d)", StartsOf(0x06), StartsOf(0x0C), StartsOf(0x20));
    CHECK(StartsOf(0x08) > StartsOf(0x00) && StartsOf(0x00) > 0 && StartsOf(0x1E) > 0,
          "the knights' 60 %% goes to the rest of the mix: archers %d > footmen %d > 0, destroyers %d", StartsOf(0x08),
          StartsOf(0x00), StartsOf(0x1E));

    // A mask that forbids everything a building could make: it stays idle and the mod never even asks.
    ProdWorld();
    Unit* onlyBarracks = AddProd(0x3C, 0, 5, 5);
    AddProd(0x4C, 0, 10, 5);
    AddProd(0x52, 0, 15, 5);
    AddProd(0x42, 0, 20, 5);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 25);
    AddProd(0x4A, 0, 30, 5);  // a hall, so the workers are done and only the barracks is left to do anything
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0x07FFFFFF & ~(0x1u | 0x4u | 0x8u | 0x10u);  // no footmen, siege, knights, archers
    for (int round = 0; round < 4; ++round) ProdPass(1000 + round * 1000);
    CHECK(g_prodAttempts == 0 && g_prodStartCount == 0 && !(Field<uint16_t>(onlyBarracks, kOffJobFlags) & 0x10),
          "every barracks unit forbidden: the building stays idle and StartProduction is never called (%d attempts)",
          g_prodAttempts);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0x07FFFFFF;
    ProdPass(20000);
    CHECK(StartsAt(onlyBarracks) == 1, "the same barracks works again once the mask allows something");

    // The computer's buildings are never touched, and a selected building is left to the player.
    ProdWorld();
    Unit* enemyBarracks = AddProd(0x3D, 1, 40, 40);
    Unit* mine = AddProd(0x3C, 0, 5, 5);
    *At<Unit*>(kRvaSelectedUnit) = mine;
    ProdPass(1000);
    CHECK(StartsAt(enemyBarracks) == 0 && StartsAt(mine) == 0, "a computer barracks or a selected barracks: nothing");
    *At<Unit*>(kRvaSelectedUnit) = nullptr;
    At<Unit*>(kRvaSelection)[3] = mine;
    ProdPass(2000);
    CHECK(StartsAt(mine) == 0, "a building in the selection list is left alone too");
    At<Unit*>(kRvaSelection)[3] = nullptr;
    ProdPass(3000);
    CHECK(StartsAt(mine) == 1 && StartsAt(enemyBarracks) == 0, "unselected: it trains; the computer's still does not");

    // Back-off: a refused start or a unit that could not be placed waits 10 s.
    ProdWorld();
    Unit* br = AddProd(0x3C, 0, 5, 5);
    g_prodRefuse = true;
    ProdPass(1000);
    CHECK(g_prodAttempts == 1, "one attempt");
    ProdPass(6000);
    CHECK(g_prodAttempts == 1, "refused: no retry within 10 s");
    ProdPass(11001);
    CHECK(g_prodAttempts == 2, "retry after 10 s");
    g_prodRefuse = false;
    ProdPass(22000);
    CHECK(StartsAt(br) == 1, "started");
    FinishTraining(0, false);  // exit blocked: refunded, no unit
    ProdPass(23000);
    CHECK(StartsAt(br) == 1, "no unit came out: wait");
    ProdPass(33001);
    CHECK(StartsAt(br) == 2, "10 s later it tries again");
    FinishTraining(0);
    ProdPass(34000);
    CHECK(StartsAt(br) == 3, "a unit that came out: no wait");

    // The upgrade reserve from the real tables: purchasable only, done and in research do not count.
    ProdWorld();
    AddProd(0x3C, 0, 5, 5);
    Unit* smith = AddProd(0x52, 0, 10, 5);  // blacksmith: swords 0 / 1
    At<uint16_t>(kRvaUpgradeGold)[0] = 800;
    At<uint16_t>(kRvaUpgradeGold)[1] = 2400;
    At<uint32_t>(kRvaUpgradesAllowed)[0] = 0x4 | 0x8;
    ProdPass(1000);
    CHECK(production::LastPlan().reserve.r[0] == 800, "swords 1 purchasable: reserve 800 (%d)", production::LastPlan().reserve.r[0]);
    At<uint32_t>(kRvaUpgradesAllowed)[0] = 0;  // a mission that forbids the upgrade
    ProdPass(1100);
    CHECK(production::LastPlan().reserve.r[0] == 0, "an upgrade the mission forbids must not raise the reserve (%d)",
          production::LastPlan().reserve.r[0]);
    At<uint32_t>(kRvaUpgradesAllowed)[0] = 0x4 | 0x8;
    Field<uint16_t>(smith, kOffJobFlags) |= 0x10;  // the only blacksmith is already paying for a research
    Field<uint8_t>(smith, kOffJobKind) = 2;
    ProdPass(1200);
    CHECK(production::LastPlan().reserve.r[0] == 0, "the only blacksmith is already researching: nothing to reserve for (%d)",
          production::LastPlan().reserve.r[0]);
    Field<uint16_t>(smith, kOffJobFlags) &= ~0x10;
    Field<uint8_t>(smith, kOffJobKind) = 0;
    At<uint32_t>(kRvaUpgradesInResearch)[0] = 0x4;
    ProdPass(2000);
    CHECK(production::LastPlan().reserve.r[0] == 0, "in research: not in the reserve");
    At<uint32_t>(kRvaUpgradesInResearch)[0] = 0;
    At<uint8_t>(kRvaUpgradeLevels + 0x10)[0] = 1;
    ProdPass(3000);
    CHECK(production::LastPlan().reserve.r[0] == 2400, "swords 1 done: swords 2 is next (%d)", production::LastPlan().reserve.r[0]);
    At<uint8_t>(kRvaUpgradeLevels + 0x10)[0] = 2;
    ProdPass(4000);
    CHECK(production::LastPlan().reserve.r[0] == 0, "both done: nothing");
    AddProd(0x3E, 0, 15, 5);  // church: the paladin upgrade (33, 1000 gold), then healing (35)
    At<uint16_t>(kRvaUpgradeGold)[33] = 1000;
    At<uint16_t>(kRvaUpgradeGold)[35] = 1000;
    At<uint32_t>(kRvaSpellsAllowed)[0] = 0x100000 | 0x2;
    ProdPass(5000);
    CHECK(production::LastPlan().reserve.r[0] == 1000, "church: the paladin upgrade, not yet healing (%d)", production::LastPlan().reserve.r[0]);
    At<uint32_t>(kRvaSpellsAllowed)[0] = 0;  // a mission that forbids the paladin upgrade and healing
    ProdPass(5100);
    CHECK(production::LastPlan().reserve.r[0] == 0, "a research the spell mask forbids must not raise the reserve (%d)",
          production::LastPlan().reserve.r[0]);
    At<uint32_t>(kRvaSpellsAllowed)[0] = 0x100000 | 0x2;
    At<uint32_t>(kRvaSpellsResearched)[0] = 0x100000;
    ProdPass(6000);
    CHECK(production::LastPlan().reserve.r[0] == 1000, "paladins known: healing is what is next");
    AddProd(0x40, 0, 20, 5);  // two scout towers with a blacksmith: two cannon towers
    AddProd(0x40, 0, 25, 5);
    AddProd(0x4A, 0, 30, 5);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF;  // the keep upgrade is allowed now, and a barracks is there
    ProdPass(7000);
    // healing 1000, two cannon towers 1000 each, the keep 2000 gold / 1000 lumber: dearest + a quarter of the rest.
    CHECK(production::LastPlan().reserve.r[0] == 2000 && production::LastPlan().reserve.r[1] == 400,
          "the towers and the keep raise the reserve, but never anchor it (%d gold, %d lumber)",
          production::LastPlan().reserve.r[0], production::LastPlan().reserve.r[1]);
    // The keep is a building upgrade, and it obeys the same mask a unit does (bit 0x8000000, button 0x4E36F0).
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF & ~0x8000000u;
    ProdPass(7100);
    CHECK(production::LastPlan().reserve.r[0] == 1500 && production::LastPlan().reserve.r[1] == 150,
          "a mission that forbids the keep: only healing and the two cannon towers are left (%d gold, %d lumber)",
          production::LastPlan().reserve.r[0], production::LastPlan().reserve.r[1]);
    At<uint32_t>(kRvaUnitsAllowed)[0] = 0xFFFFFFFF;
    ProdPass(7200);
    // The gate uses it: a footman needs four prices of spare bank on top of the reserve.
    FinishTraining(0);
    for (int i = 0; i < 12; ++i) AddProd(0x02, 0, i, 30);  // no worker business
    At<int32_t>(kRvaPlayerGold)[0] = 2000 + 4 * 600 - 1;
    At<int32_t>(kRvaPlayerLumber)[0] = At<int32_t>(kRvaPlayerOil)[0] = 5000;
    const int footmen = StartsOf(0x00);
    ProdPass(8000);
    CHECK(StartsOf(0x00) == footmen && production::LastPlan().reserve.r[0] == 2000, "one gold short of reserve + four prices: nothing");
    At<int32_t>(kRvaPlayerGold)[0] += 1;
    ProdPass(9000);
    CHECK(StartsOf(0x00) == footmen + 1, "exactly enough: a footman");
    // The threshold follows the live cost table ([costs] / [unit.<name>] / a map's own UDTA raise it).
    FinishTraining(0);
    At<uint8_t>(kRvaGoldCostByType)[0x00] = 120;  // the footman now costs 1200
    At<int32_t>(kRvaPlayerGold)[0] = 2000 + 4 * 1200 - 1;
    ProdPass(10000);
    CHECK(StartsOf(0x00) == footmen + 1, "the price doubled: the threshold doubles with it");
    At<int32_t>(kRvaPlayerGold)[0] += 1;
    ProdPass(11000);
    CHECK(StartsOf(0x00) == footmen + 2, "and one more gold is enough again");

    // Knights turn into paladins after the upgrade, archers into rangers.
    ProdWorld();
    AddProd(0x3C, 0, 5, 5);
    AddProd(0x58, 0, 10, 5);  // keep: tier 2
    {
        int x = 20;
        for (int t : {0x42, 0x52, 0x4C}) AddProd(static_cast<uint8_t>(t), 0, x += 3, 5);  // stables, blacksmith, lumber mill
    }
    for (int i = 0; i < 16; ++i) AddProd(0x02, 0, i, 30);
    At<uint32_t>(kRvaSpellsResearched)[0] = 0x100000;  // paladins
    At<uint8_t>(kRvaUpgradeLevels + 0x80)[0] = 1;      // rangers
    for (int round = 0; round < 10; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x0C) > 0 && StartsOf(0x06) == 0 && StartsOf(0x12) > 0 && StartsOf(0x08) == 0,
          "upgraded lines only: paladins %d (knights %d), rangers %d (archers %d)", StartsOf(0x0C), StartsOf(0x06), StartsOf(0x12),
          StartsOf(0x08));

    // The filler in a real pass: tier 3, a mountain of gold, no lumber, and only a barracks.
    ProdWorld();
    AddProd(0x3C, 0, 5, 5);
    AddProd(0x5A, 0, 10, 5);  // castle: tier 3, where infantry has no share at all
    for (int i = 0; i < 24; ++i) AddProd(0x02, 0, i, 30);
    At<int32_t>(kRvaPlayerGold)[0] = 60000;
    At<int32_t>(kRvaPlayerLumber)[0] = 100;
    At<int32_t>(kRvaPlayerOil)[0] = 0;
    for (int round = 0; round < 6; ++round) {
        ProdPass(1000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x00) == 6, "gold-rich, lumber-poor tier 3: the filler keeps the barracks on footmen (%d)", StartsOf(0x00));
    AddProd(0x4C, 0, 15, 5);  // a lumber mill, so archers are possible at all
    At<int32_t>(kRvaPlayerLumber)[0] = 100000;
    for (int round = 0; round < 4; ++round) {
        ProdPass(10000 + round * 1000);
        FinishTraining(0);
    }
    CHECK(StartsOf(0x08) > 0, "with lumber back the mix takes over again (%d archers)", StartsOf(0x08));

    // Config: every key reads, typos and bad values are reported, nothing else changes.
    WriteFileText(ini,
                  "[auto_production]\nenabled = true\ntoggle_key = \"F11\"\nworkers_tier1 = 5\nworkers_tier2 = 7\nworkers_tier3 = 201\nfood_free_min = 6\n"
                  "food_free_percent = 15\nreserve_extra = 0.5\nupgrade_bias = 0\nfiller_min = 20\nsave_up_seconds = 120\n"
                  "plenty_units = 101\nnavy_weight = 0.5\nnavy_max = 40\n"
                  "bogus = 1\n"
                  "[auto_production.units]\nsiege = false\nsubmarines = false\nsappers = true\n"
                  "[auto_production.bank_multiple]\nall = 2.5\nknights = 8\nflyers = 0\n"
                  "[auto_production.no_enemy_navy_cap]\ndestroyers = 8\nbattleships = 201\nknights = 3\ngalleys = 2\n"
                  "[auto_production.land_tier2]\nknights = 50\ndestroyers = 10\nsiege = 101\n"
                  "[auto_production.navy_tier1]\nsubmarines = 30\n");
    CHECK(config::Init(dir), "auto_production config rejected");
    {
        const AutoProduction& c = config::g.production;
        CHECK(c.enabled && c.toggleKey == VK_F11 && c.workersTier[0] == 5 && c.workersTier[1] == 7 && c.foodFreeMin == 6 &&
                  c.foodFreePercent == 15 && c.bankMultiple == 2.5 && c.reserveExtra == 0.5 && c.upgradeBias == 0 &&
                  c.fillerMin == 20 && c.saveUpSeconds == 120 && c.navyWeight == 0.5 && c.navyMax == 40,
              "[auto_production] keys");
        CHECK(c.workersTier[2] == 200 && LogContains(dir, "[auto_production] workers_tier3 = 201 is outside 0..200, using 200"),
              "a worker target above 200 is clamped and logged (%d)", c.workersTier[2]);
        CHECK(c.plentyUnits == 100 && LogContains(dir, "[auto_production] plenty_units = 101 is outside 1..100, using 100"),
              "plenty_units is a whole number from 1 to 100 (%d)", c.plentyUnits);
        CHECK(!c.unitClass[kProdSiege] && !c.unitClass[kProdSubmarines] && c.unitClass[kProdInfantry], "[auto_production.units]");
        CHECK(c.classBankMultiple[kProdKnights] == 8 && c.classBankMultiple[kProdFlyers] == 0 &&
                  LogContains(dir, "[auto_production.bank_multiple] flyers must be a number"),
              "[auto_production.bank_multiple]: per class, and 0 is refused");
        CHECK(c.land[1][kProdKnights] == 50 && c.land[1][kProdSiege] == 5 && c.land[1][kProdDestroyers] == 0 &&
                  c.navy[0][kProdSubmarines] == 30 && c.land[0][kProdInfantry] == 75,
              "[auto_production.land_tier2] / [.navy_tier1]: 101 refused, a ship class is not a land key");
        CHECK(LogContains(dir, "unknown key [auto_production] bogus") && LogContains(dir, "unknown key [auto_production.units] sappers") &&
                  LogContains(dir, "unknown key [auto_production.land_tier2] destroyers") &&
                  !LogContains(dir, "unknown key [auto_production] save_up_seconds") &&
                  !LogContains(dir, "unknown key [auto_production] plenty_units"),
              "auto_production typos must be logged, and a real key must never be one");
        CHECK(c.noEnemyNavyCap[kProdDestroyers] == 8 && c.noEnemyNavyCap[kProdBattleships] == 2 &&
                  c.noEnemyNavyCap[kProdKnights] == 3 && c.noEnemyNavyCap[kProdTankers] == 1 &&
                  c.noEnemyNavyCap[kProdInfantry] == -1 &&
                  LogContains(dir, "[auto_production.no_enemy_navy_cap] battleships must be a whole number from 0 to 200") &&
                  LogContains(dir, "unknown key [auto_production.no_enemy_navy_cap] galleys"),
              "[auto_production.no_enemy_navy_cap]: read, 201 refused, a class left out stays uncapped");
    }
    WriteFileText(ini, "[auto_production]\nplenty_units = 25\nsave_up_seconds = 0\n");
    CHECK(config::Init(dir) && config::g.production.plentyUnits == 25 && config::g.production.saveUpSeconds == 0,
          "a value in range reads through, and save_up_seconds = 0 is a value, not a missing key (%d, %d)",
          config::g.production.plentyUnits, config::g.production.saveUpSeconds);
    DeleteFileW(ini);
    CHECK(config::Init(dir), "default config did not come back");
    CHECK(!config::g.production.enabled, "the shipped file keeps auto-production off");
    EnableEverythingForTests();

    ProdRestore();
    ResetWorld();
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* exe = argc > 1 ? argv[1] : L"C:\\Program Files (x86)\\Warcraft II Remastered\\x86\\Warcraft II.exe";
    const HMODULE img = LoadLibraryExW(exe, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!img) {
        printf("cannot map %ls (%lu)\n", exe, GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uintptr_t>(img);
    printf("mapped game image at %p\n", img);

    // One folder per run: the config reload re-reads this file every 64 ticks, so two checkouts testing at the same
    // time used to rewrite each other's settings and fail at random.
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wchar_t leaf[64];
    swprintf_s(leaf, L"war2r_gameplay_options_selftest_%lu", GetCurrentProcessId());
    wcscat_s(dir, leaf);
    CreateDirectoryW(dir, nullptr);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\gameplay_options.toml", dir);
    DeleteFileW(ini);  // always start from the shipped defaults

    // 0. The log of the last two sessions survives a restart (.prev.log, .prev2.log) instead of being truncated.
    {
        wchar_t prev[MAX_PATH], prev2[MAX_PATH];
        swprintf_s(prev, L"%s\\gameplay_options.prev.log", dir);
        swprintf_s(prev2, L"%s\\gameplay_options.prev2.log", dir);
        auto contains = [](const wchar_t* file, const char* text) {
            char buf[512] = {};
            const HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD got = 0;
            ReadFile(h, buf, sizeof buf - 1, &got, nullptr);
            CloseHandle(h);
            return strstr(buf, text) != nullptr;
        };
        logx::Open(dir);
        logx::Write("session one");
        logx::Open(dir);
        logx::Write("session two");
        logx::Open(dir);
        CHECK(contains(prev, "session two"), "the previous session's log must survive as gameplay_options.prev.log");
        CHECK(contains(prev2, "session one"), "the session before that must survive as gameplay_options.prev2.log");
        CHECK(!contains(prev, "session one"), "gameplay_options.prev.log must hold one session only");
    }
    mod::SetModuleBase(g_base, dir);

    // 1. Hook install against the real bytes.
    for (int i = 0; i < 3; ++i)
        CHECK(hook::AiPaladinSiteMatches(g_base, i), "paladin AI call site %d moved (FUN_004cb2f0, see game.h)", i);
    CHECK(hook::Install(g_base), "hook::Install rejected the supported exe");
    for (int i = 0; i < 3; ++i)
        CHECK(!hook::AiPaladinSiteMatches(g_base, i), "Install left paladin AI call site %d unhooked", i);
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
    // The eight spells of docs/research/autocast_all_spells.md: costs, cast ranges (0x8C1744) and the tables they use.
    {
        CHECK(mana[kOrderHolyVision] == 70 && mana[kOrderFlameShield] == 80 && mana[kOrderFireball] == 100 &&
                  mana[kOrderInvisibility] == 200 && mana[kOrderBlizzard] == 25 && mana[kOrderWhirlwind] == 100 &&
                  mana[kOrderRunes] == 200 && mana[kOrderDeathAndDecay] == 30,
              "mana costs of the eight later spells");
        const uint8_t* range = At<uint8_t>(kRvaOrderRange);
        CHECK(range[kOrderHolyVision] == 0xFF && range[kOrderFlameShield] == 6 && range[kOrderFireball] == 10 &&
                  range[kOrderInvisibility] == 6 && range[kOrderBlizzard] == 10 && range[kOrderWhirlwind] == 12 &&
                  range[kOrderRunes] == 10 && range[kOrderDeathAndDecay] == 12 && range[0x27] == 6,
              "order range table 0x8C1744");
        auto abs32 = [&](uint32_t rva) { uint32_t v; memcpy(&v, At<uint8_t>(rva), 4); return v; };
        auto bytes = [&](uint32_t rva, const char* expect, size_t n) { return memcmp(At<uint8_t>(rva), expect, n) == 0; };
        // Whirlwind spawn FUN_004af5c0: slot count, pool, free bit, source unit at +0x30, type 0x0C at +0x34.
        CHECK(bytes(0xAF5DC, "\xA1", 1) && abs32(0xAF5DD) == g_base + kRvaMissileSlots && bytes(0xAF5E2, "\x8B\x35", 2) &&
                  abs32(0xAF5E4) == g_base + kRvaMissilePool,
              "missile pool / slot count are not read at 0x4AF5DC..0x4AF5E7");
        CHECK(bytes(0xAF5F1, "\xF6\x46\x35\x01", 4) && bytes(0xAF61C, "\x89\x7E\x30", 3) && bytes(0xAF625, "\xC6\x46\x34\x0C", 4),
              "whirlwind missile layout (free bit +0x35, source +0x30, type 0x0C at +0x34)");
        // Rune tick FUN_004e2cd0: timer u16[], x u8[], y u8[].
        CHECK(bytes(0xE2CE0, "\x0F\xB7\x04\x75", 4) && abs32(0xE2CE4) == g_base + kRvaRuneTimers && bytes(0xE2CF1, "\x0F\xB6\x96", 3) &&
                  abs32(0xE2CF4) == g_base + kRvaRuneX && bytes(0xE2CF9, "\x0F\xB6\xBE", 3) && abs32(0xE2CFC) == g_base + kRvaRuneY,
              "rune table is not read at 0x4E2CE0..0x4E2CFF");
        // Blizzard / Death and Decay hit-frame actions only stop on "mana < cost": the channel the watchdog exists for.
        const uint32_t* actions = At<uint32_t>(0x4C1590);
        CHECK(actions[kOrderBlizzard] == g_base + 0xE19A0 && actions[kOrderDeathAndDecay] == g_base + 0xE2530 &&
                  actions[kOrderWhirlwind] == g_base + 0xE27F0 && actions[kOrderRunes] == g_base + 0xE25A0,
              "spell action table 0x8C1590 entries");
    }

    // The order handlers the mod passes to IssueOrder must be the entries of the game's own handler table
    // (VA 0x8C1498, indexed by order id), and the gold decrement the refill relies on must be where we found it.
    {
        const uint32_t* handlers = At<uint32_t>(0x4C1498);
        CHECK(handlers[kOrderMove] == g_base + kRvaMoveHandler, "move handler is not table entry 3");
        CHECK(handlers[kOrderHarvest] == g_base + kRvaHarvestHandler, "harvest handler is not table entry 23");
        CHECK(handlers[kOrderReturnGoods] == g_base + kRvaReturnHandler, "return handler is not table entry 24");
        CHECK(handlers[kOrderRepair] == g_base + kRvaRepairHandler, "repair handler is not table entry 27");
        CHECK(handlers[kOrderSpellEye] == g_base + kRvaSpellOrderHandler, "spell handler is not table entry 0x30");
        CHECK(handlers[kOrderStop] == g_base + kRvaStopHandler, "stop handler is not table entry 2");
        CHECK(handlers[kOrderAttackArea] == g_base + kRvaAttackMoveHandler, "attack-move handler is not table entry 10");
        // The patrol RESUME handler is not a table entry: SetOrder pushes it as an immediate when it resumes a patrol.
        {
            const uint8_t* push = reinterpret_cast<const uint8_t*>(g_base + 0xEF10A);
            uint32_t imm;
            memcpy(&imm, push + 1, sizeof imm);
            CHECK(push[0] == 0x68 && imm == g_base + kRvaPatrolHandler, "SetOrder's patrol resume does not push kRvaPatrolHandler (%02X %08X)", push[0], imm);
        }
        const uint8_t later[] = {kOrderHolyVision, kOrderFlameShield, kOrderFireball, kOrderInvisibility,
                                 kOrderBlizzard,   kOrderWhirlwind,   kOrderRunes,    kOrderDeathAndDecay};
        for (uint8_t o : later) CHECK(handlers[o] == g_base + kRvaSpellOrderHandler, "order 0x%02X does not use the spell handler", o);
        const uint8_t decrement[] = {0x66, 0x01, 0x88, 0x82, 0x00, 0x00, 0x00};  // add word [eax+0x82], cx
        CHECK(memcmp(At<uint8_t>(0xC99C8), decrement, sizeof(decrement)) == 0, "gold decrement not at 0x4C99C8: +0x82 may be wrong");
    }

    // 3. Fake world in the image's globals.
    PatchJump(kRvaIssueOrder, &FakeIssueOrder);
    PatchJump(kRvaShowMessage, &FakeShowMessage);
    *At<Unit*>(kRvaUnitArray) = reinterpret_cast<Unit*>(g_units);
    *At<Unit**>(kRvaUnitGrid) = g_grid;
    *At<Unit**>(kRvaAirUnitGrid) = g_airGrid;
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
    // Shipped defaults: only Heal, Slow, Bloodlust, Raise Dead and worker auto-repair are on.
    {
        const bool expectSpell[kSpellCount] = {true, false, true, false, true, false, false, false, true};
        bool spellsOk = true;
        for (int i = 0; i < kSpellCount; ++i) spellsOk = spellsOk && config::g.spell[i] == expectSpell[i];
        CHECK(config::g.enabled && spellsOk, "default spells must be heal, slow, bloodlust, raise_dead only");
        bool laterOff = true;
        for (int i = kSpellHolyVision; i < kSpellCount; ++i) laterOff = laterOff && !config::g.spell[i];
        CHECK(laterOff && config::g.channelManaReserve == 0 && config::g.areaMinEnemies == 3 && config::g.fireballMinEnemies == 2,
              "holy_vision .. runes must be off by default; channel_mana_reserve 0, area_min_enemies 3, fireball_min_enemies 2");
        CHECK(!config::g.eyeCast && !config::g.eyeAutoScout && !config::g.workerAutoHarvest && config::g.workerAutoRepair &&
                  config::g.eyeMaxActive == 3 && !config::g.heroRegen && config::g.heroRegenPerSecond == 2 && !config::g.unitRegen && config::g.unitRegenPerSecond == 1 && !config::g.goldMinesUnlimited && !config::g.oilPlatformsUnlimited && config::g.rangeUpgradeBonus == 1,
              "default options: everything off except worker auto-repair");
        bool noStats = true;
        for (const auto& row : config::g.unitStat)
            for (int32_t v : row) noStats = noStats && v == -1;
        CHECK(noStats && config::g.health.all == 1.0 && config::g.costs.all == 1.0 && config::g.time.all == 1.0,
              "the shipped config must not change any unit, building or multiplier (examples are comments)");
    }
    EnableEverythingForTests();
    CHECK(GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES, "default gameplay_options.toml was not written");
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        GetFileAttributesExW(ini, GetFileExInfoStandard, &fad);
        CHECK(fad.nFileSizeLow > 1000, "default gameplay_options.toml is empty: the embedded resource was not found");
    }
    CHECK(config::g.polymorphRank[kDragon] && config::g.polymorphRank[kDaemon] && !config::g.polymorphRank[kGrunt],
          "default polymorph list");

    // The shipped [heroes] list holds all 15 campaign heroes (comments inside the list must not break it), and the
    // short / in-game spellings resolve to the same unit.
    {
        const uint8_t heroes[] = {0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x23, 0x2C, 0x2E, 0x2F, 0x31, 0x32, 0x33, 0x34, 0x35};
        int listed = 0;
        for (int t = 0; t < 256; ++t) listed += config::g.isHero[t];
        bool all = listed == 15;
        for (uint8_t h : heroes) all = all && config::g.isHero[h];
        CHECK(all, "shipped [heroes] units must hold exactly the 15 heroes (%d listed, uther %d)", listed, config::g.isHero[0x34]);
        const units::Entry* uther = units::FindByName("uther");
        const units::Entry* grom = units::FindByName("Grommash_Hellscream");
        CHECK(uther && uther->id == 0x34 && grom && grom->id == 0x19 && units::FindByName("uther_lightbringer") == uther &&
                  !units::FindByName("destroyer"),
              "unit name aliases");
        const units::Building* watch = units::FindBuildingByName("Watch_Tower");
        CHECK(watch && watch->id == 0x41 && units::FindBuildingByName("orc_watch_tower") == watch &&
                  units::FindBuildingByName("orc_scout_tower") == watch && units::FindBuildingByName("scout_tower") &&
                  units::FindBuildingByName("scout_tower")->id == 0x40 && !units::FindBuildingByName("guard_tower"),
              "building name aliases (the orc scout tower is the Watch Tower in game)");
    }

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

    // What the player cannot see is no target (docs/research/autocast_all_spells.md 2.10): a submarine with no detector
    // of the player's near it has only its owner's bit in the seen mask (FUN_004f0200), a unit under fog has the
    // player's bit in the fog mask (FUN_004f0600). The sub type is flagged fleshy here only so death coil wants it.
    constexpr uint8_t kSub = 0x26;
    const uint32_t savedSubFlags = tf[kSub];
    const uint16_t savedSubHp = maxHp[kSub];
    defType(kSub, kTfSubmarine | kTfFleshy | kTfAttacker, 60);
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* sub = AddUnit(kSub, 1, 13, 12, 60, 0, kOrderAttack);
    Field<uint8_t>(sub, kOffSeenMask) = 1 << 1;  // submerged: only its owner sees it
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "death coil at a submarine nobody of the player's detects (order %u)", OrderOf(dk));
    Field<uint8_t>(sub, kOffSeenMask) = (1 << 1) | (1 << 0);  // a flying machine of the player's came within 6 tiles
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == sub, "death coil at a detected submarine (order %u)", OrderOf(dk));
    Idle(dk);  // the flying machine flies off: the next step's seen mask has only the owner again, so no second coil
    Field<uint8_t>(sub, kOffSeenMask) = 1 << 1;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "death coil at a submarine that was detected a step ago but no longer is (order %u)",
          OrderOf(dk));
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    enemy = AddUnit(kFootman, 1, 13, 12, 60, 0, kOrderAttack);
    Field<uint8_t>(enemy, kOffFogMask) = 1 << 0;  // under the player's fog
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "death coil at a footman under fog (order %u)", OrderOf(dk));
    Field<uint8_t>(enemy, kOffFogMask) = 1 << 2;  // fogged for some other player only
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == enemy, "another player's fog must not hide the footman (order %u)",
          OrderOf(dk));

    // Flyers live in the game's AIR grid only (player report: no Bloodlust / Death Coil on air units). AddUnit files
    // them there, so every flyer test in this file now goes through the second layer.
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    Unit* enemyDragon = AddUnit(kDragon, 1, 13, 12, 100, 0, kOrderAttack);
    CHECK(g_airGrid[12 * kMap + 13] == enemyDragon && g_grid[12 * kMap + 13] == nullptr, "test world: a dragon must sit in the air grid only");
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == enemyDragon, "death coil on an enemy dragon (air layer)");
    ResetWorld();
    om = AddUnit(kTypeOgreMage, 0, 20, 20, 90, 200, kOrderStand);
    Unit* myDragon = AddUnit(kDragon, 0, 22, 20, 100, 0, kOrderAttackTarget);
    AddUnit(kFootman, 1, 24, 21, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == 0x31 && TargetOf(om) == myDragon, "bloodlust on my fighting dragon (air layer)");
    ResetWorld();  // an enemy flyer alone must count as "enemy near" for the fight test
    om = AddUnit(kTypeOgreMage, 0, 20, 20, 90, 200, kOrderStand);
    Unit* lustGrunt = AddUnit(kGrunt, 0, 21, 20, 60, 0, kOrderAttackTarget);
    AddUnit(kDragon, 1, 23, 21, 100, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(om) == 0x31 && TargetOf(om) == lustGrunt, "an enemy flyer nearby makes my grunt a bloodlust target");
    ResetWorld();  // a flyer hovering over a ground unit: both tiles' occupants are candidates
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    defType(0x3A, kTfBuilding, 400);  // farm
    AddUnit(0x3A, 1, 12, 10, 400, 0, kOrderStand);
    Unit* above = AddUnit(kDragon, 1, 12, 10, 100, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == 0x33 && TargetOf(dk) == above, "death coil on the dragon above a farm, not blocked by the ground unit");

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

    // Raise Dead, the computer's rule (FUN_004cac80): research, mana, a corpse within 15 tiles (31 x 31 box, not
    // search_radius), cast at its tile, no enemy needed; one death knight per corpse. A corpse is the dead unit's own slot
    // retyped to 0x69 (FUN_004bdfc0) and is in NEITHER grid (FUN_004ee380 -> FUN_004b5000 took it off), so the test puts
    // corpses into the unit array only, the way the game keeps them.
    auto corpseAt = [&](int x, int y) {
        Unit* below = g_grid[y * kMap + x];
        Unit* c = AddUnit(kTypeCorpse, 1, x, y, 0, 0, 0);
        g_grid[y * kMap + x] = below;
        Field<uint8_t>(c, kOffStateFlags) = kStateDying;
        return c;
    };
    auto raisedAt = [&](Unit* k, int x, int y) {
        return OrderOf(k) == 0x32 && TargetOf(k) == nullptr && Field<int16_t>(k, kOffOrderX) == x && Field<int16_t>(k, kOffOrderY) == y;
    };
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    corpseAt(12, 11);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 12, 11), "raise dead with no enemy anywhere, at the corpse tile (order %u at %d,%d)", OrderOf(dk),
          Field<int16_t>(dk, kOffOrderX), Field<int16_t>(dk, kOffOrderY));
    Unit* dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    AddUnit(kFootman, 1, 16, 10, 60, 0, kOrderAttack);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk2) == 0x33, "second death knight should coil, the corpse is taken (order %u)", OrderOf(dk2));
    ResetWorld();  // 15 tiles away (beyond search_radius 8): yes; 16: no
    dk = AddUnit(kTypeDeathKnight, 0, 20, 20, 60, 255, kOrderStand);
    corpseAt(36, 20);
    corpseAt(20, 36);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "raised a corpse 16 tiles away (order %u at %d,%d)", OrderOf(dk), Field<int16_t>(dk, kOffOrderX),
          Field<int16_t>(dk, kOffOrderY));
    corpseAt(35, 35);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 35, 35), "a corpse 15 tiles away (diagonal) must be raised (order %u at %d,%d)", OrderOf(dk),
          Field<int16_t>(dk, kOffOrderX), Field<int16_t>(dk, kOffOrderY));
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, 20, 20, 60, 255, kOrderStand);
    Unit* hiddenCorpse = corpseAt(22, 20);
    // Decay states: only state 2 is raisable (FUN_004e2420 checks (state & 0x0F) == 2); +8 = raised already / gone.
    const struct {
        uint8_t type, state;
        bool raisable;
        const char* what;
    } corpseStates[] = {{kTypeCorpse, kStateDying | 0x08, false, "a hidden / already raised corpse (state 0x0A)"},
                        {kTypeCorpse, 0, false, "a type-0x69 slot in state 0"},
                        {kTypeCorpse, 3, false, "a type-0x69 slot in state 3"},
                        {0x6A, kStateDying, false, "the 1x1 remains a corpse turns into at the end (type 0x6A)"},
                        {kTypeCorpse, kStateDying, true, "a fresh corpse (type 0x69, state 2)"}};
    for (const auto& cs : corpseStates) {
        Idle(dk);
        Field<uint8_t>(hiddenCorpse, kOffType) = cs.type;
        Field<uint8_t>(hiddenCorpse, kOffStateFlags) = cs.state;
        mod::RunAutocastPass();
        CHECK((OrderOf(dk) == 0x32) == cs.raisable, "%s: order %u", cs.what, OrderOf(dk));
    }
    Idle(dk);
    CHECK(g_grid[20 * kMap + 22] == nullptr && g_airGrid[20 * kMap + 22] == nullptr, "test world: the corpse must be in no grid");
    Unit* onCorpse = AddUnit(kFootman, 0, 22, 20, 60, 0, kOrderStand);  // a living unit on the corpse's tile owns the grid entry
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 22, 20) && g_grid[20 * kMap + 22] == onCorpse, "a corpse under a living footman must still be found (order %u)",
          OrderOf(dk));
    Idle(dk);
    uint16_t* const savedSqRd = *At<uint16_t*>(kRvaSquareFlags);
    static uint16_t sqRd[kMap * kMap];
    memset(sqRd, 0, sizeof(sqRd));
    sqRd[20 * kMap + 22] = kSqWater;  // a sunk ship is type 0x69 too: never aim at a wreck
    *At<uint16_t*>(kRvaSquareFlags) = sqRd;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "raised a wreck on a water tile");
    *At<uint16_t*>(kRvaSquareFlags) = savedSqRd;
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF & ~0x2000u;
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "raise dead without the research");
    At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF;
    Field<uint8_t>(dk, kOffMana) = static_cast<uint8_t>(At<uint16_t>(kRvaManaCostByOrder)[0x32] - 1);
    mod::RunAutocastPass();
    CHECK(OrderOf(dk) == kOrderStand, "raise dead with %u mana (cost %u)", Field<uint8_t>(dk, kOffMana), At<uint16_t>(kRvaManaCostByOrder)[0x32]);
    Field<uint8_t>(dk, kOffMana) = static_cast<uint8_t>(At<uint16_t>(kRvaManaCostByOrder)[0x32]);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 22, 20), "raise dead at exactly the mana cost (order %u)", OrderOf(dk));
    ResetWorld();  // two death knights in one pass, two corpses: never the same one
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    corpseAt(12, 11);
    corpseAt(20, 20);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 12, 11) && raisedAt(dk2, 20, 20), "two death knights, two corpses: %d,%d and %d,%d", Field<int16_t>(dk, kOffOrderX),
          Field<int16_t>(dk, kOffOrderY), Field<int16_t>(dk2, kOffOrderX), Field<int16_t>(dk2, kOffOrderY));
    ResetWorld();  // ... and one corpse: the second one does not raise it again
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    corpseAt(12, 11);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 12, 11) && OrderOf(dk2) != 0x32, "two death knights raised the same corpse (second order %u)", OrderOf(dk2));
    ResetWorld();  // a raise takes every corpse within dx*dx + dy*dy < 0x25 of its tile (FUN_004e2420): the second one looks further
    dk = AddUnit(kTypeDeathKnight, 0, 10, 10, 60, 255, kOrderStand);
    dk2 = AddUnit(kTypeDeathKnight, 0, 11, 12, 60, 255, kOrderStand);
    corpseAt(12, 11);
    corpseAt(16, 13);  // 4*4 + 2*2 = 20: raised by the first cast anyway
    corpseAt(18, 15);  // 6*6 + 4*4 = 52: out of its reach
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 12, 11) && raisedAt(dk2, 18, 15), "second death knight should skip the corpse the first raise takes (%d,%d)",
          Field<int16_t>(dk2, kOffOrderX), Field<int16_t>(dk2, kOffOrderY));
    ResetWorld();  // a corpse on the map edge and in the corner is a tile on the map
    dk = AddUnit(kTypeDeathKnight, 0, 2, 2, 60, 255, kOrderStand);
    corpseAt(0, 0);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, 0, 0), "raise dead at a corpse in the corner (order %u)", OrderOf(dk));
    ResetWorld();
    dk = AddUnit(kTypeDeathKnight, 0, kMap - 1, 40, 60, 255, kOrderStand);
    corpseAt(kMap - 1, 30);
    mod::RunAutocastPass();
    CHECK(raisedAt(dk, kMap - 1, 30), "raise dead at a corpse on the east edge (order %u)", OrderOf(dk));

    // The diagnostic behind [general] log_casts: why a death knight did not raise the dead, at most once per death knight
    // per 30 s of play.
    {
        const bool savedLog = config::g.logCasts;
        auto notes = [] { return autocast::RaiseDeadNoteCount(); };
        auto lastIs = [](const char* text) { return strstr(autocast::LastRaiseDeadNote(), text) != nullptr; };
        config::g.logCasts = false;
        ResetWorld();
        dk = AddUnit(kTypeDeathKnight, 0, 20, 20, 60, 255, kOrderStand);
        Field<uint32_t>(dk, kOffSerial) = 9001;
        At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF & ~0x2000u;
        const unsigned before = notes();
        mod::RunAutocastPass();
        CHECK(notes() == before, "raise-dead diagnostic written with log_casts off");
        config::g.logCasts = true;
        mod::RunAutocastPass();
        CHECK(notes() == before + 1 && lastIs("not researched"), "diagnostic: not researched (%u lines, \"%s\")", notes() - before,
              autocast::LastRaiseDeadNote());
        mod::RunAutocastPass();
        autocast::AddPlayTime(29999);
        mod::RunAutocastPass();
        CHECK(notes() == before + 1, "diagnostic: more than one line within 30 s of play (%u)", notes() - before);
        autocast::AddPlayTime(1);
        mod::RunAutocastPass();
        CHECK(notes() == before + 2, "diagnostic: no new line after 30 s of play (%u)", notes() - before);
        At<uint32_t>(kRvaSpellsResearched)[0] = 0xFFFFFFFF;
        Field<uint8_t>(dk, kOffMana) = 49;
        autocast::AddPlayTime(30000);
        mod::RunAutocastPass();
        CHECK(notes() == before + 3 && lastIs("mana below"), "diagnostic: mana (\"%s\")", autocast::LastRaiseDeadNote());
        Field<uint8_t>(dk, kOffMana) = 255;
        corpseAt(20, 36);  // 16 tiles away
        autocast::AddPlayTime(30000);
        mod::RunAutocastPass();
        CHECK(notes() == before + 4 && lastIs("no corpse within 15 tiles (1 on the map"), "diagnostic: no corpse in the box (\"%s\")",
              autocast::LastRaiseDeadNote());
        config::g.spell[kSpellRaiseDead] = false;
        autocast::AddPlayTime(30000);
        mod::RunAutocastPass();
        CHECK(notes() == before + 5 && lastIs("switched off"), "diagnostic: switched off (\"%s\")", autocast::LastRaiseDeadNote());
        config::g.spell[kSpellRaiseDead] = true;
        ResetWorld();  // a second death knight whose only corpse a raise under way already takes: its own first line
        dk = AddUnit(kTypeDeathKnight, 0, 20, 20, 60, 255, kOrderStand);
        dk2 = AddUnit(kTypeDeathKnight, 0, 21, 21, 60, 255, kOrderStand);
        Field<uint32_t>(dk, kOffSerial) = 9003;
        Field<uint32_t>(dk2, kOffSerial) = 9004;
        corpseAt(22, 20);
        mod::RunAutocastPass();
        CHECK(raisedAt(dk, 22, 20) && notes() == before + 6 && lastIs("claimed"), "diagnostic: all claimed (%u, \"%s\")", notes() - before,
              autocast::LastRaiseDeadNote());
        config::g.logCasts = savedLog;
    }

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
    config::g.eyeMaxActive = 1;
    Unit* om2 = AddUnit(kTypeOgreMage, 0, 22, 20, 90, 255, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(om2) == kOrderStand, "second eye cast while one is pending (max_active = 1)");
    config::g.eyeMaxActive = 3;  // the default since 1.6.2: three eyes, the fourth ogre-mage waits
    Unit* om3 = AddUnit(kTypeOgreMage, 0, 24, 20, 90, 255, kOrderStand);
    Unit* om4 = AddUnit(kTypeOgreMage, 0, 26, 20, 90, 255, kOrderStand);
    mod::RunAutocastPass();
    CHECK(OrderOf(om2) == kOrderSpellEye && OrderOf(om3) == kOrderSpellEye && OrderOf(om4) == kOrderStand,
          "max_active = 3: two more eyes, never a fourth (orders %u %u %u)", OrderOf(om2), OrderOf(om3), OrderOf(om4));

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

    // ---- The eight later spells: holy vision, flame shield, fireball, invisibility, blizzard, death and decay,
    // whirlwind, runes (docs/research/autocast_all_spells.md). Every area spell hurts own and allied units, flyers and
    // buildings alike (FUN_004afb50 has no owner test), so most scenarios put one friendly into the danger area.
    {
        bool savedSpells[kSpellCount];
        memcpy(savedSpells, config::g.spell, sizeof(savedSpells));
        const bool savedEye = config::g.eyeCast, savedLog = config::g.logCasts;
        config::g.eyeCast = false;  // an idle full-mana ogre-mage would cast the eye instead of standing still
        config::g.logCasts = true;  // every cast and watchdog log line runs at least once
        auto only = [&](int s) {
            for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == s;
        };
        struct Sz { uint16_t w, h; };
        Sz* sizes = At<Sz>(kRvaUnitSizeByType);
        uint8_t* rangeT = At<uint8_t>(kRvaAttackRangeByType);
        Sz savedSizes[110];
        uint8_t savedRanges[110];
        memcpy(savedSizes, sizes, sizeof(savedSizes));
        memcpy(savedRanges, rangeT, sizeof(savedRanges));
        constexpr uint8_t kBarracks = 0x3C, kCastle = 0x5A, kAxe = 9;
        constexpr int kChannelWallsForTest = 3;  // src/autocast.cpp kChannelWalls
        defType(kBarracks, kTfBuilding, 800);
        sizes[kBarracks] = {3, 3};
        defType(kCastle, kTfBuilding, 1600);
        sizes[kCastle] = {4, 4};
        defType(kAxe, kTfFleshy | kTfAttacker, 40);
        rangeT[kFootman] = rangeT[kGrunt] = rangeT[kOgre] = 1;
        rangeT[kAxe] = 4;
        rangeT[kTypeMage] = 2;
        rangeT[kDragon] = 4;
        static uint8_t missiles[16 * kMissileSize];
        memset(missiles, 0, sizeof(missiles));
        for (int i = 0; i < 16; ++i) missiles[i * kMissileSize + kMisOffFlags] = 1;  // all free
        *At<uint8_t*>(kRvaMissilePool) = missiles;
        *At<uint32_t>(kRvaMissileSlots) = 16;
        uint16_t* runeTimers = At<uint16_t>(kRvaRuneTimers);
        memset(runeTimers, 0, kMaxRunes * sizeof(uint16_t));
        static uint16_t sqTest[kMap * kMap];
        memset(sqTest, 0, sizeof(sqTest));
        uint16_t* const savedSq = *At<uint16_t*>(kRvaSquareFlags);
        const int refusedBefore = game::RefusedOrderCount();
        auto ox = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderX)); };
        auto oy = [](Unit* u) { return static_cast<int>(Field<int16_t>(u, kOffOrderY)); };
        auto castAt = [&](Unit* c, uint8_t order, int x, int y) {
            return OrderOf(c) == order && TargetOf(c) == nullptr && ox(c) == x && oy(c) == y;
        };
        auto tileOnMap = [&](Unit* c) { return ox(c) >= 0 && oy(c) >= 0 && ox(c) < kMap && oy(c) < kMap; };
        uint32_t serial = 5000;
        auto caster = [&](uint8_t type, int x, int y) {
            Unit* u = AddUnit(type, 0, x, y, 60, 255, kOrderStand);
            Field<uint32_t>(u, kOffSerial) = ++serial;
            return u;
        };
        auto slot = [](int i) { return reinterpret_cast<Unit*>(g_units + i * kUnitSize); };
        struct Blocker {
            uint8_t type, owner;
            int x, y;
            const char* what;
        };

        // Fireball: splashes on the aimed tile and on the ~6 tiles past it along the flight line (FUN_004ae7c0).
        auto fireballWorld = [&](int enemies) {
            ResetWorld();
            Unit* m = caster(kTypeMage, 20, 30);
            AddUnit(kGrunt, 1, 26, 30, 60, 0, kOrderAttack);
            if (enemies > 1) AddUnit(kGrunt, 1, 27, 30, 60, 0, kOrderAttack);
            return m;
        };
        Unit* mg = fireballWorld(2);
        only(-1);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "fireball cast while its switch is off");
        only(kSpellFireball);
        Field<uint8_t>(mg, kOffMana) = 99;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "fireball cast with 99 mana");
        Field<uint8_t>(mg, kOffMana) = 255;
        mod::RunAutocastPass();
        CHECK(castAt(mg, kOrderFireball, 26, 30), "fireball at the nearer grunt's tile (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
        const Blocker fireballBlockers[] = {
            {kFootman, 0, 31, 31, "an own footman beside the splash line, 5 tiles past the target"},
            {kFootman, 2, 33, 32, "an allied footman at the end of the splash line"},
            {kDragon, 0, 32, 29, "an own dragon over the splash line"},
            {kBarracks, 0, 34, 26, "an own barracks whose footprint (not its top-left tile) is 2 tiles from the line"},
        };
        for (const Blocker& b : fireballBlockers) {
            mg = fireballWorld(2);
            AddUnit(b.type, b.owner, b.x, b.y, 100, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(mg) == kOrderStand, "fireball cast with %s", b.what);
        }
        mg = fireballWorld(2);
        AddUnit(kFootman, 0, 22, 30, 60, 0, kOrderStand);  // on the way in: the fireball splashes only from the aimed tile on
        mod::RunAutocastPass();
        CHECK(castAt(mg, kOrderFireball, 26, 30), "a friendly between the mage and the aimed tile must not stop the fireball");
        mg = fireballWorld(2);
        *At<uint16_t*>(kRvaSquareFlags) = sqTest;
        sqTest[31 * kMap + 30] = kSqWalls;  // walls have no owner: any wall next to a splash point counts as ours
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "fireball cast next to a wall");
        sqTest[31 * kMap + 30] = 0;
        *At<uint16_t*>(kRvaSquareFlags) = savedSq;
        mg = fireballWorld(1);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "fireball at a single grunt with fireball_min_enemies = 2");
        config::g.fireballMinEnemies = 1;
        mod::RunAutocastPass();
        CHECK(castAt(mg, kOrderFireball, 26, 30), "fireball_min_enemies = 1 fires at a single grunt");
        config::g.fireballMinEnemies = 2;
        ResetWorld();  // the splash line runs off the map: the points outside are skipped, the aimed tile is on it
        mg = caster(kTypeMage, 50, 30);
        AddUnit(kGrunt, 1, 57, 30, 60, 0, kOrderAttack);
        AddUnit(kGrunt, 1, 58, 30, 60, 0, kOrderAttack);
        mod::RunAutocastPass();
        CHECK(castAt(mg, kOrderFireball, 57, 30), "fireball toward the map edge (order %u at %d,%d)", OrderOf(mg), ox(mg), oy(mg));
        ResetWorld();  // two mages, one group: the second may not fire at the same tile in the same pass
        mg = caster(kTypeMage, 20, 30);
        Unit* mg2 = caster(kTypeMage, 20, 32);
        AddUnit(kGrunt, 1, 26, 30, 60, 0, kOrderAttack);
        AddUnit(kGrunt, 1, 26, 31, 60, 0, kOrderAttack);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderFireball && OrderOf(mg2) == kOrderStand, "two mages fireballed the same group (%u / %u)", OrderOf(mg), OrderOf(mg2));
        // Submarines nobody of the player's detects are no target of fireball or the area spells either, not even as
        // one more enemy in the blast (autocast_all_spells.md 2.10).
        {
            ResetWorld();
            mg = caster(kTypeMage, 20, 30);
            Unit* s1 = AddUnit(kSub, 1, 26, 30, 60, 0, kOrderAttack);
            Unit* s2 = AddUnit(kSub, 1, 27, 30, 60, 0, kOrderAttack);
            Field<uint8_t>(s1, kOffSeenMask) = Field<uint8_t>(s2, kOffSeenMask) = 1 << 1;
            mod::RunAutocastPass();
            CHECK(OrderOf(mg) == kOrderStand, "fireball at two undetected submarines (order %u at %d,%d)", OrderOf(mg), ox(mg),
                  oy(mg));
            Field<uint8_t>(s1, kOffSeenMask) = Field<uint8_t>(s2, kOffSeenMask) = 0xFF;
            mod::RunAutocastPass();
            CHECK(castAt(mg, kOrderFireball, 26, 30), "fireball at two detected submarines (order %u at %d,%d)", OrderOf(mg),
                  ox(mg), oy(mg));
            for (int spell : {kSpellBlizzard, kSpellDeathAndDecay}) {
                only(spell);
                const uint8_t order = spell == kSpellBlizzard ? kOrderBlizzard : kOrderDeathAndDecay;
                ResetWorld();
                Unit* cc = caster(spell == kSpellBlizzard ? kTypeMage : kTypeDeathKnight, 20, 20);
                AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);
                AddUnit(kGrunt, 1, 28, 21, 60, 0, kOrderAttack);
                Unit* s3 = AddUnit(kSub, 1, 27, 22, 60, 0, kOrderAttack);
                Field<uint8_t>(s3, kOffSeenMask) = 1 << 1;
                mod::RunAutocastPass();
                CHECK(OrderOf(cc) == kOrderStand, "%s counted an undetected submarine as the third enemy (order %u at %d,%d)",
                      config::kSpellKeys[spell], OrderOf(cc), ox(cc), oy(cc));
                Field<uint8_t>(s3, kOffSeenMask) = 0xFF;
                mod::RunAutocastPass();
                CHECK(OrderOf(cc) == order, "%s at two grunts and a detected submarine (order %u)", config::kSpellKeys[spell],
                      OrderOf(cc));
            }
            only(kSpellFireball);
            defType(kSub, savedSubFlags, savedSubHp);
        }

        // Blizzard and Death and Decay: 5 impacts a wave on the 5x5 tiles around the target, channelled.
        struct AreaCase {
            int spell;
            uint8_t casterType, order;
            int threeWaves;
        };
        const AreaCase areaCases[] = {{kSpellBlizzard, kTypeMage, kOrderBlizzard, 75}, {kSpellDeathAndDecay, kTypeDeathKnight, kOrderDeathAndDecay, 90}};
        for (const AreaCase& ac : areaCases) {
            const char* name = config::kSpellKeys[ac.spell];
            auto areaWorld = [&](int cx, int cy) {
                ResetWorld();
                Unit* c = caster(ac.casterType, cx, cy);
                AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);  // slots 1..3
                AddUnit(kGrunt, 1, 28, 21, 60, 0, kOrderAttack);
                AddUnit(kGrunt, 1, 27, 22, 60, 0, kOrderAttack);
                return c;
            };
            only(-1);
            Unit* c = areaWorld(20, 20);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s cast while its switch is off", name);
            only(ac.spell);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(ac.threeWaves - 1);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s started without mana for three waves", name);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(ac.threeWaves);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s at the group (order %u at %d,%d)", name, OrderOf(c), ox(c), oy(c));
            // The waves scatter over the tiles around the aim tile, so one friendly on one side does not end the
            // cast: the aim walks off the group until nothing of the player's is within area_friendly_clearance.
            const Blocker areaBlockers[] = {
                {kFootman, 2, 29, 23, "an allied footman"},
                {kDragon, 0, 25, 18, "an own dragon overhead"},
                {kBarracks, 0, 22, 16, "an own barracks whose footprint (not its top-left tile) is 4 tiles away"},
            };
            auto aimClear = [&](Unit* cc, int fx, int fy) {
                const int dx = abs(ox(cc) - fx), dy = abs(oy(cc) - fy);
                return (dx > dy ? dx : dy) > config::g.areaFriendlyClearance;
            };
            auto hitsGroup = [&](Unit* cc) {  // at least one of the three grunts is still inside the 5x5 pattern
                const int gx[3] = {27, 28, 27}, gy[3] = {20, 21, 22};
                for (int i = 0; i < 3; ++i)
                    if (abs(ox(cc) - gx[i]) <= 2 && abs(oy(cc) - gy[i]) <= 2) return true;
                return false;
            };
            c = areaWorld(20, 20);  // one footman on one side: the aim walks to the far side of the group
            AddUnit(kFootman, 0, 31, 21, 60, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && aimClear(c, 31, 21) && hitsGroup(c),
                  "%s with an own footman 4 tiles from the group: the aim must walk clear and still cover it (order %u at %d,%d)",
                  name, OrderOf(c), ox(c), oy(c));
            // These leave no clean aim that still covers all three grunts AND is inside the spell range, so they
            // block the cast outright, as they always did.
            for (const Blocker& b : areaBlockers) {
                c = areaWorld(20, 20);
                AddUnit(b.type, b.owner, b.x, b.y, 100, 0, kOrderStand);
                mod::RunAutocastPass();
                CHECK(OrderOf(c) == kOrderStand, "%s cast with %s (order %u at %d,%d)", name, b.what, OrderOf(c), ox(c), oy(c));
            }
            c = areaWorld(20, 20);  // a friendly on every side: there is no clean aim tile left, so nothing is cast
            for (int i = 0; i < 4; ++i) {
                const int fx[4] = {23, 31, 27, 27}, fy[4] = {21, 21, 17, 25};
                AddUnit(kFootman, 0, fx[i], fy[i], 60, 0, kOrderStand);
            }
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s cast with the group ringed by friendlies (order %u at %d,%d)", name, OrderOf(c),
                  ox(c), oy(c));
            CHECK(LogContains(dir, "every spot worth casting on has your own type"),
                  "%s: the diagnostic must name the friendly that blocks every spot", name);
            {  // and it is throttled like the raise_dead one: one line per caster per 30 s of play
                char pattern[64];
                sprintf_s(pattern, "%s not cast: caster type", name);
                const int lines = LogCount(dir, pattern);
                mod::RunAutocastPass();
                mod::RunAutocastPass();
                CHECK(LogCount(dir, pattern) == lines, "%s: the not-cast diagnostic must be throttled", name);
            }
            {  // the watchdog stops on the same clearance the cast used: with 0 nothing of the player stops it
                c = areaWorld(20, 20);
                mod::RunAutocastPass();
                CHECK(OrderOf(c) == ac.order, "%s watchdog clearance setup (order %u)", name, OrderOf(c));
                // Below three waves: the caster cannot start a new channel, so only the watchdog can change the order.
                Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(ac.threeWaves - 1);
                config::g.areaFriendlyClearance = 0;
                AddUnit(kFootman, 0, 29, 20, 60, 0, kOrderMove);
                mod::RunAutocastPass();
                CHECK(OrderOf(c) == ac.order, "%s: with clearance 0 a friendly must not stop the channel (order %u)", name,
                      OrderOf(c));
                config::g.areaFriendlyClearance = 4;
                mod::RunAutocastPass();
                CHECK(OrderOf(c) == kOrderStop, "%s: clearance 4 again must stop it (order %u)", name, OrderOf(c));
            }
            c = areaWorld(20, 20);  // 5 tiles from every group tile: the straight aim is clean and stays
            AddUnit(kFootman, 0, 27, 15, 60, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s moved the aim although the nearest friendly is 5 tiles away", name);
            c = areaWorld(24, 20);  // the caster is the missile's source and never hurt; a unit next to it is
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s: the caster itself must not count as a friendly in the area", name);
            c = areaWorld(24, 20);
            AddUnit(kFootman, 0, 24, 22, 60, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && aimClear(c, 24, 22) && hitsGroup(c),
                  "%s with a footman beside the caster: the aim must walk clear (order %u at %d,%d)", name, OrderOf(c), ox(c),
                  oy(c));
            c = areaWorld(20, 20);  // area_friendly_clearance = 0: the player's own units are ignored, centre aim
            AddUnit(kFootman, 0, 28, 22, 60, 0, kOrderStand);  // inside the pattern, but not on the aim tile itself
            config::g.areaFriendlyClearance = 0;
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s with clearance 0 must take the straight aim (order %u at %d,%d)", name,
                  OrderOf(c), ox(c), oy(c));
            config::g.areaFriendlyClearance = 4;
            c = areaWorld(20, 20);
            *At<uint16_t*>(kRvaSquareFlags) = sqTest;
            sqTest[20 * kMap + 30] = kSqWalls;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && hitsGroup(c) && abs(ox(c) - 30) > kChannelWallsForTest,
                  "%s with a wall 3 tiles from the group: the aim must clear the wall (order %u at %d,%d)", name, OrderOf(c),
                  ox(c), oy(c));
            sqTest[20 * kMap + 30] = 0;
            *At<uint16_t*>(kRvaSquareFlags) = savedSq;
            {  // the group is against the top-left corner: no aim may fall off the map
                ResetWorld();
                Unit* edge = caster(ac.casterType, 3, 3);
                for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 0, i, 60, 0, kOrderAttack);
                AddUnit(kFootman, 0, 2, 2, 60, 0, kOrderStand);  // forces the search
                mod::RunAutocastPass();
                CHECK(OrderOf(edge) != ac.order || (ox(edge) >= 0 && oy(edge) >= 0 && ox(edge) < kMap && oy(edge) < kMap),
                      "%s put an aim tile off the map at %d,%d", name, ox(edge), oy(edge));
            }
            c = areaWorld(20, 20);
            Field<uint8_t>(slot(3), kOffStateFlags) = kStateDying;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s at 2 enemies with area_min_enemies = 3", name);
            c = areaWorld(20, 20);  // claims: the second caster must not stack a spell on the same group
            Unit* c2 = caster(ac.casterType, 20, 22);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && OrderOf(c2) == kOrderStand, "%s: two casters on one group (%u / %u)", name, OrderOf(c), OrderOf(c2));

            // Watchdog: stops a channel the mod started once it turns unsafe or useless, never one the player gave.
            c = areaWorld(20, 20);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s watchdog setup", name);
            AddUnit(ac.spell == kSpellBlizzard ? kFootman : kDragon, 0, 29, 20, 60, 0, kOrderMove);  // walks / flies in
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop, "%s: the watchdog did not stop the channel when a friendly entered (order %u)", name, OrderOf(c));
            c = areaWorld(20, 20);
            mod::RunAutocastPass();
            for (int i = 1; i <= 3; ++i) Field<uint8_t>(slot(i), kOffStateFlags) = kStateDying;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop, "%s: the watchdog did not stop the channel once the enemies were gone (order %u)", name, OrderOf(c));
            config::g.channelManaReserve = 100;
            const int reserveStart = 100 + ac.threeWaves / 3;  // the reserve plus one wave
            c = areaWorld(20, 20);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(reserveStart - 1);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s started with less than channel_mana_reserve + one wave", name);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(reserveStart);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s with channel_mana_reserve = 100 and mana %d", name, reserveStart);
            Field<uint8_t>(c, kOffMana) = 100;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s stopped at exactly the reserve", name);
            Field<uint8_t>(c, kOffMana) = 99;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop, "%s: the watchdog did not stop the channel below the reserve (order %u)", name, OrderOf(c));
            config::g.channelManaReserve = 0;
            c = areaWorld(20, 20);  // the player's own channel (same order, same tile) is never touched
            Field<uint8_t>(c, kOffOrder) = ac.order;
            Field<int16_t>(c, kOffOrderX) = 27;
            Field<int16_t>(c, kOffOrderY) = 20;
            AddUnit(kFootman, 0, 29, 20, 60, 0, kOrderMove);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: the watchdog stopped a channel the player ordered", name);
            c = areaWorld(20, 20);  // the mod's caster dies and a new unit in the same slot gets the same channel from the player
            mod::RunAutocastPass();
            Field<uint32_t>(c, kOffSerial) = ++serial;
            AddUnit(kFootman, 0, 29, 20, 60, 0, kOrderMove);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: the watchdog stopped the channel of a new unit in a reused slot", name);
            c = areaWorld(20, 20);  // a mod channel the player sends elsewhere is the player's from then on
            mod::RunAutocastPass();
            Field<int16_t>(c, kOffOrderX) = 28;
            Field<int16_t>(c, kOffOrderY) = 21;
            AddUnit(kFootman, 0, 30, 21, 60, 0, kOrderMove);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: the watchdog stopped a channel the player re-aimed", name);
            c = areaWorld(20, 20);  // with autocast switched off the watchdog still guards what the mod started
            mod::RunAutocastPass();
            config::g.enabled = false;
            AddUnit(kFootman, 0, 29, 20, 60, 0, kOrderMove);
            for (int i = 0; i < 40; ++i) mod::OnTick();
            CHECK(OrderOf(c) == kOrderStop, "%s: the watchdog did not run while autocast was off (order %u)", name, OrderOf(c));
            config::g.enabled = true;

            // ---- Buildings: what the blast is worth, and no overkill ----
            // One wave on a structure at the aim point: 5 x the live damage byte (src/autocast.cpp WaveDamage).
            const uint32_t dmgRva = ac.order == kOrderBlizzard ? kRvaBlizzardDamageInsn : kRvaDeathAndDecayDamageInsn;
            const int wave = 5 * At<uint8_t>(dmgRva)[3];
            auto barracks = [&](int x, int y, int hp) {
                Unit* b = AddUnit(kBarracks, 1, x, y, hp, 0, kOrderStand);
                Field<uint16_t>(b, kOffStateFlags) = kStateComplete;
                return b;
            };
            // Two buildings and two units beat four units, even when the four are nearer.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            for (int i = 0; i < 4; ++i) AddUnit(kGrunt, 1, 22 + i % 2, 19 + i / 2, 60, 0, kOrderAttack);  // nearer, value 4
            barracks(27, 19, 800);
            barracks(27, 21, 800);
            AddUnit(kGrunt, 1, 28, 20, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 28, 21, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && ox(c) >= 27, "%s must prefer 2 buildings + 2 units over 4 nearer units (order %u at %d,%d)",
                  name, OrderOf(c), ox(c), oy(c));
            // area_building_value = 1 makes a building worth one unit: five near units then beat two buildings and two
            // units. (Four against four is an exact tie, which now goes to the aim with its targets nearest its middle.)
            config::g.areaBuildingValue = 1;
            AddUnit(kGrunt, 1, 22, 21, 60, 0, kOrderAttack);
            Field<uint8_t>(c, kOffOrder) = kOrderStand;
            Field<uint8_t>(c, kOffNextOrder) = kOrderNone;
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && ox(c) <= 23, "%s with area_building_value = 1 must take the five nearer units (at %d,%d)",
                  name, ox(c), oy(c));
            config::g.areaBuildingValue = 3;
            // A lone building is a target; two units alone are not.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, 800);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 28, 21), "%s must aim at the centre tile of a 3x3 building (order %u at %d,%d)", name,
                  OrderOf(c), ox(c), oy(c));
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 27, 21, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s cast at two units with no building", name);
            // A building and a unit together are a valid target.
            AddUnit(kGrunt, 1, 27, 22, 60, 0, kOrderAttack);
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, 800);
            AddUnit(kGrunt, 1, 28, 21, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s must cast at a building plus a unit", name);
            // A 4x4 castle is aimed at the middle of its footprint, not at the corner tile it is filed under, and the
            // friendly-fire check looks at that same tile: a footman past the far side of the footprint still blocks.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            AddUnit(kCastle, 1, 26, 19, 1600, 0, kOrderStand);  // tiles 26..29 x 19..22, centre tile 27,20
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 20), "%s must aim at the centre tile of a 4x4 building (order %u at %d,%d)", name,
                  OrderOf(c), ox(c), oy(c));
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            AddUnit(kCastle, 1, 26, 19, 1600, 0, kOrderStand);
            AddUnit(kFootman, 0, 31, 20, 60, 0, kOrderStand);  // 2 tiles past the far edge, 4 from the centre tile
            mod::RunAutocastPass();
            {
                const int dx = abs(ox(c) - 31), dy = abs(oy(c) - 20);
                const int away = dx > dy ? dx : dy;
                // The castle's centre is between tiles 27 and 28: only impacts on 27..28 x 20..21 reach it, so the aim
                // must keep at least one of those inside its pattern.
                CHECK(OrderOf(c) == ac.order && away > 4 && ox(c) >= 25 && ox(c) <= 30 && oy(c) >= 18 && oy(c) <= 23,
                      "%s with a footman beside a 4x4 building: the aim must walk clear and still cover its centre "
                      "(order %u at %d,%d)",
                      name, OrderOf(c), ox(c), oy(c));
            }
            // ---- Coverage: the aim is the tile whose impact pattern does the most (docs/research/autocast_all_spells.md
            // 2.6a). Impacts land on the centres of the 5x5 tiles around the aim, and a building is only hit by impacts
            // near its centre: a 4x4 by its middle 2x2 tiles, a 3x3 fully by its middle tile. These worlds file every
            // building on every footprint tile, as the game does (FUN_004b4910), so a building met on many tiles must
            // still count once.
            auto fileFootprint = [&](Unit* b) {
                const Sz sz = sizes[TypeOf(b)];
                for (int y = 0; y < sz.h; ++y)
                    for (int x = 0; x < sz.w; ++x) g_grid[(Field<int16_t>(b, kOffY) + y) * kMap + Field<int16_t>(b, kOffX) + x] = b;
                Field<uint16_t>(b, kOffStateFlags) = kStateComplete;
                return b;
            };
            auto logDelta = [&](const char* text, auto&& run) {
                const int before = LogCount(dir, text);
                run();
                return LogCount(dir, text) - before;
            };
            // The cast line: (score) and "about N damage a wave", both the expected damage of one wave in hit points:
            // 5 points x 11 (blizzard) or 10 (death and decay) impacts x dmg x (0.75 x full + 0.1875 x quarter shares)
            // / 25; the score counts a building area_building_value (3) times. A lone barracks: 1 full + 8 quarter
            // shares, (3 x 1 + 9) = 12 "shares" of 3/400.
            const int impacts = ac.order == kOrderBlizzard ? 11 : 10;
            auto waveTenths = [&](int shares) { return 5 * impacts * At<uint8_t>(dmgRva)[3] * 3 * shares * 10 / 400; };
            auto castLine = [&](char* out, size_t len, int x, int y, int tenths, int weight, int tiles, int units) {
                sprintf_s(out, len, "-> tile %d,%d (%d), covers %d building tiles, %d units, about %d damage a wave", x, y,
                          (weight * tenths + 5) / 10, tiles, units, (tenths + 5) / 10);
            };
            char line[160];
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            fileFootprint(AddUnit(kBarracks, 1, 27, 20, 800, 0, kOrderStand));
            castLine(line, sizeof(line), 28, 21, waveTenths(12), 3, 9, 0);
            {
                const int logged = logDelta(line, [&] { mod::RunAutocastPass(); });
                CHECK(castAt(c, ac.order, 28, 21) && logged == 1, "%s at a lone barracks: centre tile, log '%s' (order %u at %d,%d, log %d)",
                      name, line, OrderOf(c), ox(c), oy(c), logged);
            }
            // A lone castle, with the caster south-east of it, so that the nearest of the aims that reach its middle
            // would be its corner tile 29,22: the aim still covers the whole footprint. Only impacts on its middle 2x2
            // tiles reach its centre, all four full hits: 16, times 3.
            ResetWorld();
            c = caster(ac.casterType, 33, 26);
            fileFootprint(AddUnit(kCastle, 1, 26, 19, 1600, 0, kOrderStand));  // tiles 26..29 x 19..22
            {
                castLine(line, sizeof(line), 28, 21, waveTenths(16), 3, 16, 0);
                const int covered = logDelta(line, [&] { mod::RunAutocastPass(); });
                CHECK(OrderOf(c) == ac.order && ox(c) >= 27 && ox(c) <= 28 && oy(c) >= 20 && oy(c) <= 21 && covered == 1,
                      "%s at a lone 4x4 castle must cover all 16 footprint tiles, never aim at a corner (order %u at %d,%d, "
                      "log %d)",
                      name, OrderOf(c), ox(c), oy(c), covered);
            }
            // Two barracks side by side (centres 25,21 and 28,21): an aim between them reaches both, which is worth more
            // than the centre of either one (63 against 45 quarter hits at area_building_value 3).
            ResetWorld();
            c = caster(ac.casterType, 20, 21);
            fileFootprint(AddUnit(kBarracks, 1, 24, 20, 800, 0, kOrderStand));
            fileFootprint(AddUnit(kBarracks, 1, 27, 20, 800, 0, kOrderStand));
            {
                const int covered = logDelta("covers 15 building tiles, 0 units", [&] { mod::RunAutocastPass(); });
                CHECK(OrderOf(c) == ac.order && (ox(c) == 26 || ox(c) == 27) && oy(c) == 21 && covered == 1,
                      "%s at two adjacent barracks must aim between them (order %u at %d,%d, log %d)", name, OrderOf(c),
                      ox(c), oy(c), covered);
            }
            // A castle whose middle is out of reach: an aim in range only grazes its corner tiles, which the splash never
            // reaches (48 px from its centre). A grunt beside that corner used to make the spot "a building plus a unit";
            // three grunts together are worth more than that.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);  // reach 8: aims up to x 28, pattern up to x 30
            fileFootprint(AddUnit(kCastle, 1, 30, 18, 1600, 0, kOrderStand));  // middle tiles 31..32 x 19..20
            AddUnit(kGrunt, 1, 28, 20, 60, 0, kOrderAttack);
            for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 20 + i % 2, 25 + i / 2, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && oy(c) >= 24 && ox(c) <= 22,
                  "%s must prefer three grunts over a building corner it cannot hurt (order %u at %d,%d)", name, OrderOf(c),
                  ox(c), oy(c));
            // The corner alone is no target at all: the castle's middle is out of reach and one grunt is not enough.
            Field<uint8_t>(slot(3), kOffStateFlags) = kStateDying;
            Field<uint8_t>(slot(4), kOffStateFlags) = kStateDying;
            Field<uint8_t>(slot(5), kOffStateFlags) = kStateDying;
            Idle(c);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s cast at a building corner the splash cannot reach (order %u at %d,%d)", name,
                  OrderOf(c), ox(c), oy(c));
            // A group in the map's corner: the aim that has them nearest its middle (0,0) would throw most of its
            // impacts off the map, so the aim is the nearest tile whose pattern is mostly on it.
            ResetWorld();
            c = caster(ac.casterType, 4, 4);
            AddUnit(kGrunt, 1, 0, 0, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 1, 0, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 0, 1, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 1, 1), "%s at a group in the map corner (order %u at %d,%d)", name, OrderOf(c), ox(c),
                  oy(c));
            // No overkill in the score: a target counts only for the hit points it has left. Three grunts one wave
            // finishes (2 hp each) near the caster lose to three healthy ones further off; with full hit points the
            // nearer group would win the tie.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 23 + i % 2, 20 + i / 2, 2, 0, kOrderAttack);
            for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 26 + i % 2, 26 + i / 2, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order && oy(c) >= 25, "%s must prefer three healthy grunts over three nearly dead ones (at %d,%d)",
                  name, ox(c), oy(c));
            // The watchdog stops any channel, one on units too, once every enemy in the blast would die to the wave
            // already falling: each one's hit points <= the expected damage of one wave on it (~49.5 blizzard, 45 d&d).
            c = areaWorld(20, 20);
            Field<uint8_t>(c, kOffMana) = 255;
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s overkill watchdog setup (order %u at %d,%d)", name, OrderOf(c), ox(c), oy(c));
            Field<uint16_t>(slot(1), kOffHp) = Field<uint16_t>(slot(3), kOffHp) = 10;  // two nearly dead, one healthy
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: the watchdog stopped although a 60 hp grunt outlasts one wave's ~%d (order %u)",
                  name, waveTenths(12) / 10, OrderOf(c));
            Field<uint16_t>(slot(2), kOffHp) = static_cast<uint16_t>(waveTenths(12) / 10);  // now every one dies to the wave
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop, "%s: the watchdog kept a channel on units one wave finishes (order %u)", name,
                  OrderOf(c));
            // A friendly in the blast still refuses, building or no building.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, 800);
            AddUnit(kFootman, 0, 28, 22, 60, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s cast at a building with an own footman in the area", name);
            // No overkill at cast time: what one wave would flatten is not worth a channel.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, wave);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStand, "%s started a channel on a building one wave already covers (%d hp)", name, wave);
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, wave + 1);
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 28, 21), "%s refused a building one hit point above one wave", name);
            ResetWorld();  // ... unless the units alone are reason enough
            c = caster(ac.casterType, 20, 20);
            barracks(27, 20, wave);
            for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 26, 19 + i, 60, 0, kOrderAttack);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: three units in the blast justify the cast whatever the building has left", name);
            // The wave budget: the channel runs until the waves paid for cover the hit points it started on.
            const int cost = At<uint16_t>(kRvaManaCostByOrder)[ac.order];
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            Unit* target = barracks(27, 20, 4 * wave);
            Field<uint8_t>(c, kOffMana) = 255;
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 28, 21), "%s wave budget setup (order %u)", name, OrderOf(c));
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(255 - 3 * cost);  // three waves: 3 x wave < 4 x wave
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s stopped before the waves it paid for covered the building", name);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(255 - 4 * cost);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop,
                  "%s: the watchdog did not stop the channel once the budget was spent (order %u, wave %d, cost %d, mana %u, hp %u)",
                  name, OrderOf(c), wave, cost, Field<uint8_t>(c, kOffMana), Field<uint16_t>(target, kOffHp));
            // The same channel keeps going while a crowd of units is still in the blast.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            target = barracks(27, 20, 4 * wave);
            Field<uint8_t>(c, kOffMana) = 255;
            mod::RunAutocastPass();
            for (int i = 0; i < 3; ++i) AddUnit(kGrunt, 1, 26, 19 + i, 60, 0, kOrderAttack);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(255 - 8 * cost);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: units in the blast must keep the channel alive past the budget", name);
            // A building that lost its hit points elsewhere ends the channel too.
            ResetWorld();
            c = caster(ac.casterType, 20, 20);
            target = barracks(27, 20, 4 * wave);
            Field<uint8_t>(c, kOffMana) = 255;
            mod::RunAutocastPass();
            Field<uint16_t>(target, kOffHp) = static_cast<uint16_t>(wave - 1);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == kOrderStop, "%s kept channelling at a building the next wave would finish (order %u)", name, OrderOf(c));
            // A channel cast at units only is never stopped by the budget.
            c = areaWorld(20, 20);
            Field<uint8_t>(c, kOffMana) = 255;
            mod::RunAutocastPass();
            CHECK(castAt(c, ac.order, 27, 21), "%s unit-channel setup", name);
            Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(255 - 9 * cost);
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: a channel on units must not be stopped by the wave budget (order %u)", name, OrderOf(c));
            Field<uint8_t>(slot(3), kOffStateFlags) = kStateDying;  // one of the three falls: the other two keep it alive
            mod::RunAutocastPass();
            CHECK(OrderOf(c) == ac.order, "%s: a unit channel must live on while enemies are left in the blast (order %u)", name,
                  OrderOf(c));
        }

        // Whirlwind: wanders at random (FUN_004aeb70), so nobody friendly within 6 tiles; one per death knight.
        auto wwWorld = [&](int cx, int cy) {
            ResetWorld();
            Unit* c = caster(kTypeDeathKnight, cx, cy);
            AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 28, 21, 60, 0, kOrderAttack);
            AddUnit(kGrunt, 1, 27, 22, 60, 0, kOrderAttack);
            return c;
        };
        only(-1);
        Unit* dkn = wwWorld(20, 20);
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderStand, "whirlwind cast while its switch is off");
        only(kSpellWhirlwind);
        Field<uint8_t>(dkn, kOffMana) = 99;
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderStand, "whirlwind cast with 99 mana");
        Field<uint8_t>(dkn, kOffMana) = 255;
        mod::RunAutocastPass();
        CHECK(castAt(dkn, kOrderWhirlwind, 27, 20), "whirlwind at the group (order %u at %d,%d)", OrderOf(dkn), ox(dkn), oy(dkn));
        const Blocker wwBlockers[] = {
            {kFootman, 0, 33, 21, "an own footman 6 tiles from the group"},
            {kFootman, 2, 22, 24, "an allied footman 5 tiles from the group"},
            {kDragon, 0, 30, 25, "an own dragon 5 tiles from the group"},
            {kCastle, 0, 19, 13, "an own castle whose footprint (not its top-left tile) is 6 tiles away"},
        };
        for (const Blocker& b : wwBlockers) {
            dkn = wwWorld(20, 20);
            AddUnit(b.type, b.owner, b.x, b.y, 100, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(dkn) == kOrderStand, "whirlwind cast with %s", b.what);
        }
        dkn = wwWorld(20, 20);
        AddUnit(kFootman, 0, 35, 21, 60, 0, kOrderStand);  // 7 tiles from the group
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderWhirlwind, "whirlwind refused with the nearest friendly 7 tiles away");
        dkn = wwWorld(20, 20);
        uint8_t* ww = missiles + 3 * kMissileSize;  // a whirlwind of this death knight is still in flight
        ww[kMisOffFlags] = 2;
        ww[kMisOffType] = kMissileWhirlwind;
        *reinterpret_cast<Unit**>(ww + kMisOffSource) = dkn;
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderStand, "second whirlwind while the first one is in flight");
        *reinterpret_cast<Unit**>(ww + kMisOffSource) = slot(1);  // someone else's
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderWhirlwind, "another unit's whirlwind must not block this death knight");
        ww[kMisOffFlags] = 1;
        dkn = wwWorld(20, 20);
        *At<uint8_t*>(kRvaMissilePool) = nullptr;
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderStand, "whirlwind cast although the missile pool could not be read");
        *At<uint8_t*>(kRvaMissilePool) = missiles;
        dkn = wwWorld(20, 20);
        Unit* dkn2 = caster(kTypeDeathKnight, 20, 22);
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == kOrderWhirlwind && OrderOf(dkn2) == kOrderStand, "two whirlwinds on one group (%u / %u)", OrderOf(dkn), OrderOf(dkn2));

        // Runes: no owner, no spared source (FUN_004e2cd0): 6 tiles of room around the plus, the ogre-mage included.
        auto runesWorld = [&](int cx, int cy, bool enemyFlyer) {
            ResetWorld();
            Unit* c = caster(kTypeOgreMage, cx, cy);
            AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);
            AddUnit(enemyFlyer ? kDragon : kGrunt, 1, 28, 20, 60, 0, kOrderAttack);
            return c;
        };
        only(-1);
        Unit* ogm = runesWorld(20, 20, false);
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderStand, "runes cast while its switch is off");
        only(kSpellRunes);
        Field<uint8_t>(ogm, kOffMana) = 199;
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderStand, "runes cast with 199 mana");
        Field<uint8_t>(ogm, kOffMana) = 255;
        mod::RunAutocastPass();
        CHECK(castAt(ogm, kOrderRunes, 27, 20), "runes around the grunts (order %u at %d,%d)", OrderOf(ogm), ox(ogm), oy(ogm));
        ogm = runesWorld(22, 20, false);
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderStand, "runes laid 5 tiles from the casting ogre-mage");
        const Blocker runeBlockers[] = {
            {kFootman, 0, 33, 20, "an own footman 6 tiles from the runes"},
            {kFootman, 2, 24, 17, "an allied footman"},
            {kDragon, 0, 30, 24, "an own dragon"},
            {kCastle, 0, 29, 12, "an own castle whose footprint (not its top-left tile) is 5 tiles away"},
        };
        for (const Blocker& b : runeBlockers) {
            ogm = runesWorld(20, 20, false);
            AddUnit(b.type, b.owner, b.x, b.y, 100, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(ogm) == kOrderStand, "runes cast with %s", b.what);
        }
        ogm = runesWorld(20, 20, false);
        AddUnit(kFootman, 0, 35, 20, 60, 0, kOrderStand);  // 7 tiles away
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderRunes, "runes refused with the nearest friendly 7 tiles away");
        ogm = runesWorld(20, 20, true);  // an enemy flyer never triggers a rune
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderStand, "runes cast for one grunt and one dragon");
        ogm = runesWorld(20, 20, false);
        runeTimers[7] = 100;
        At<uint8_t>(kRvaRuneX)[7] = 29;
        At<uint8_t>(kRvaRuneY)[7] = 21;
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderStand, "runes laid 2 tiles from a live rune");
        runeTimers[7] = 0;
        ogm = runesWorld(20, 20, false);
        Unit* ogm2 = caster(kTypeOgreMage, 20, 22);
        mod::RunAutocastPass();
        CHECK(OrderOf(ogm) == kOrderRunes && OrderOf(ogm2) == kOrderStand, "two ogre-magi laid runes on one group (%u / %u)", OrderOf(ogm),
              OrderOf(ogm2));

        // Flame shield: an own melee fighter with nobody friendly within 3 tiles, the mage included (the shield only
        // spares the shielded unit).
        Unit* fsTarget = nullptr;
        auto fsWorld = [&](int mx, int my, uint8_t targetType, uint8_t targetOrder, int enemies) {
            ResetWorld();
            Unit* m = caster(kTypeMage, mx, my);
            fsTarget = AddUnit(targetType, 0, 25, 20, 60, 0, targetOrder);
            AddUnit(kGrunt, 1, 26, 20, 60, 0, kOrderAttack);
            if (enemies > 1) AddUnit(kGrunt, 1, 27, 21, 60, 0, kOrderAttack);
            return m;
        };
        only(-1);
        mg = fsWorld(20, 20, kGrunt, kOrderAttackTarget, 2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield cast while its switch is off");
        only(kSpellFlameShield);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderFlameShield && TargetOf(mg) == fsTarget, "flame shield on the fighting grunt (order %u)", OrderOf(mg));
        mg = fsWorld(23, 20, kGrunt, kOrderAttackTarget, 2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield cast by a mage 2 tiles from the shielded unit");
        const Blocker fsBlockers[] = {
            {kFootman, 0, 22, 22, "an own footman 3 tiles from the shielded unit"},
            {kFootman, 2, 28, 18, "an allied footman 3 tiles from the shielded unit"},
            {kDragon, 0, 25, 22, "an own dragon next to the shielded unit"},
            {kBarracks, 0, 26, 15, "an own barracks whose footprint (not its top-left tile) is 3 tiles away"},
        };
        for (const Blocker& b : fsBlockers) {
            mg = fsWorld(20, 20, kGrunt, kOrderAttackTarget, 2);
            AddUnit(b.type, b.owner, b.x, b.y, 100, 0, kOrderStand);
            mod::RunAutocastPass();
            CHECK(OrderOf(mg) == kOrderStand, "flame shield cast with %s", b.what);
        }
        mg = fsWorld(20, 20, kDragon, kOrderAttackTarget, 2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield on a flyer (the hit-frame action refuses those)");
        mg = fsWorld(20, 20, kAxe, kOrderAttackTarget, 2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield on a ranged unit");
        mg = fsWorld(20, 20, kGrunt, kOrderAttackTarget, 1);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield with a single enemy near");
        mg = fsWorld(20, 20, kGrunt, kOrderStand, 2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield on a grunt that is not fighting");
        mg = fsWorld(20, 20, kGrunt, kOrderAttackTarget, 2);
        Field<uint16_t>(fsTarget, kOffFlameTimer) = 300;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "flame shield on a unit that already has one");
        mg = fsWorld(20, 20, kGrunt, kOrderAttackTarget, 2);
        mg2 = caster(kTypeMage, 20, 24);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderFlameShield && OrderOf(mg2) == kOrderStand, "two flame shields on one grunt (%u / %u)", OrderOf(mg),
              OrderOf(mg2));

        // Invisibility: a hurt own caster / ranged unit the player is pulling back, never one that is invisible already.
        Unit* invTarget = nullptr;
        auto invWorld = [&](uint8_t type, int hp, uint8_t order) {
            ResetWorld();
            Unit* m = caster(kTypeMage, 20, 20);
            invTarget = AddUnit(type, 0, 23, 20, hp, 0, order);
            AddUnit(kGrunt, 1, 27, 20, 60, 0, kOrderAttack);
            return m;
        };
        only(-1);
        mg = invWorld(kTypeMage, 20, kOrderMove);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility cast while its switch is off");
        only(kSpellInvisibility);
        Field<uint8_t>(mg, kOffMana) = 199;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility cast with 199 mana");
        Field<uint8_t>(mg, kOffMana) = 255;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderInvisibility && TargetOf(mg) == invTarget, "invisibility on the retreating hurt mage (order %u)", OrderOf(mg));
        mg = invWorld(kTypeMage, 20, kOrderMove);
        Field<uint16_t>(invTarget, kOffInvisTimer) = 100;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility recast on a unit that is invisible already");
        mg = invWorld(kTypeMage, 40, kOrderMove);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility on a unit above half health");
        mg = invWorld(kTypeMage, 20, kOrderStand);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility on a unit that is not being pulled back");
        mg = invWorld(kFootman, 20, kOrderMove);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility on a melee unit");
        mg = invWorld(kAxe, 15, kOrderMove);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderInvisibility && TargetOf(mg) == invTarget, "invisibility on the retreating hurt axethrower");
        mg = invWorld(kTypeMage, 20, kOrderMove);
        Field<int16_t>(slot(2), kOffX) = 30;  // the grunt, now 7 tiles from the target (grid entry moved along)
        g_grid[20 * kMap + 27] = nullptr;
        g_grid[20 * kMap + 30] = slot(2);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderStand, "invisibility with no enemy within combat_radius");
        mg = invWorld(kTypeMage, 20, kOrderMove);
        mg2 = caster(kTypeMage, 20, 22);
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == kOrderInvisibility && OrderOf(mg2) == kOrderStand, "two invisibilities on one unit (%u / %u)", OrderOf(mg),
              OrderOf(mg2));

        // Holy vision: idle paladin at full mana, at the tile whose 31x35 window holds the most unexplored ground.
        auto exploreAllBut = [&](int x0, int y0, int x1, int y1) {
            for (int y = 0; y < kMap; ++y)
                for (int x = 0; x < kMap; ++x)
                    exploredMap[y * kMap + x] = (x >= x0 && x <= x1 && y >= y0 && y <= y1) ? kTileUnexplored : 0;
        };
        exploreAllBut(40, 0, kMap - 1, kMap - 1);
        only(-1);
        ResetWorld();
        Unit* pl = caster(kTypePaladin, 10, 10);
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderStand, "holy vision cast while its switch is off");
        only(kSpellHolyVision);
        Field<uint8_t>(pl, kOffMana) = 254;
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderStand, "holy vision below full mana");
        Field<uint8_t>(pl, kOffMana) = 255;
        Field<uint8_t>(pl, kOffOrder) = kOrderAttackTarget;
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderAttackTarget, "holy vision by a busy paladin");
        Idle(pl);
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderHolyVision && TargetOf(pl) == nullptr && ox(pl) >= 25 && tileOnMap(pl),
              "holy vision toward the unexplored east (order %u at %d,%d)", OrderOf(pl), ox(pl), oy(pl));
        const int hvX = ox(pl), hvY = oy(pl);
        Unit* pl2 = caster(kTypePaladin, 12, 10);
        mod::RunAutocastPass();
        CHECK(OrderOf(pl2) == kOrderStand || (OrderOf(pl2) == kOrderHolyVision && (abs(ox(pl2) - hvX) > 15 || abs(oy(pl2) - hvY) > 15)),
              "second holy vision on the same area (%d,%d vs %d,%d)", ox(pl2), oy(pl2), hvX, hvY);
        exploreAllBut(-1, -1, -1, -1);
        ResetWorld();
        pl = caster(kTypePaladin, 10, 10);
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderStand, "holy vision with nothing left to explore");
        exploreAllBut(40, 0, kMap - 1, kMap - 1);
        *At<uint8_t*>(kRvaExploredMap) = nullptr;
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == kOrderStand, "holy vision without an explored map");
        *At<uint8_t*>(kRvaExploredMap) = exploredMap;

        // Priorities: the spells that existed first go first.
        ResetWorld();
        pl = caster(kTypePaladin, 10, 10);
        Unit* hurtFoot = AddUnit(kFootman, 0, 11, 10, 20, 0, kOrderStand);
        config::g.spell[kSpellHeal] = true;
        mod::RunAutocastPass();
        CHECK(OrderOf(pl) == 0x27 && TargetOf(pl) == hurtFoot, "heal must come before holy vision (order %u)", OrderOf(pl));
        mg = fireballWorld(2);
        only(kSpellFireball);
        config::g.spell[kSpellSlow] = true;
        mod::RunAutocastPass();
        CHECK(OrderOf(mg) == 0x2C, "slow must come before fireball (order %u)", OrderOf(mg));
        dkn = wwWorld(20, 20);
        only(kSpellDeathAndDecay);
        config::g.spell[kSpellDeathCoil] = true;
        mod::RunAutocastPass();
        CHECK(OrderOf(dkn) == 0x33, "death coil must come before death and decay (order %u)", OrderOf(dkn));

        // Every positional cast lands on the map, with the caster on each edge and in each corner.
        const struct { int x, y; } spots[] = {{0, 0}, {kMap - 1, 0}, {0, kMap - 1}, {kMap - 1, kMap - 1},
                                              {32, 0}, {0, 32}, {kMap - 1, 32}, {32, kMap - 1}};
        for (const auto& s : spots) {
            const int dx = s.x == 0 ? 1 : (s.x == kMap - 1 ? -1 : 0), dy = s.y == 0 ? 1 : (s.y == kMap - 1 ? -1 : 0);
            auto group = [&](int k) {  // three grunts around the tile k steps inward
                const int px = s.x + k * dx, py = s.y + k * dy;
                AddUnit(kGrunt, 1, px, py, 60, 0, kOrderAttack);
                AddUnit(kGrunt, 1, px + (px < kMap / 2 ? 1 : -1), py, 60, 0, kOrderAttack);
                AddUnit(kGrunt, 1, px, py + (py < kMap / 2 ? 1 : -1), 60, 0, kOrderAttack);
            };
            const struct {
                int spell;
                uint8_t type, order;
                int steps;
            } edgeCases[] = {{kSpellFireball, kTypeMage, kOrderFireball, 5},     {kSpellBlizzard, kTypeMage, kOrderBlizzard, 6},
                             {kSpellDeathAndDecay, kTypeDeathKnight, kOrderDeathAndDecay, 6}, {kSpellWhirlwind, kTypeDeathKnight, kOrderWhirlwind, 6},
                             {kSpellRunes, kTypeOgreMage, kOrderRunes, 7}};
            for (const auto& e : edgeCases) {
                ResetWorld();
                Unit* c = caster(e.type, s.x, s.y);
                group(e.steps);
                only(e.spell);
                mod::RunAutocastPass();
                CHECK(OrderOf(c) == e.order && TargetOf(c) == nullptr && tileOnMap(c), "%s with the caster at %d,%d: order %u at %d,%d",
                      config::kSpellKeys[e.spell], s.x, s.y, OrderOf(c), ox(c), oy(c));
            }
            ResetWorld();
            exploreAllBut(kMap - 1 - s.x - 5, kMap - 1 - s.y - 5, kMap - 1 - s.x + 5, kMap - 1 - s.y + 5);
            pl = caster(kTypePaladin, s.x, s.y);
            only(kSpellHolyVision);
            mod::RunAutocastPass();
            CHECK(OrderOf(pl) == kOrderHolyVision && tileOnMap(pl), "holy vision with the paladin at %d,%d: order %u at %d,%d", s.x, s.y,
                  OrderOf(pl), ox(pl), oy(pl));
        }
        CHECK(game::RefusedOrderCount() == refusedBefore, "the new spells produced %d order(s) outside the map",
              game::RefusedOrderCount() - refusedBefore);

        // ---- [priority]: the order a caster tries its spells in, and saving mana for a better one ----
        {
            const Priority defaults;
            const Priority saved = config::g.priority;
            // The author's own prices: death coil 50 (the game asks 100), so a knight at 60 mana can coil but cannot
            // pay for three waves of death and decay (30 each). The mod reads the live table, so this is his case.
            uint16_t* manaCost = At<uint16_t>(kRvaManaCostByOrder);
            const uint16_t savedCoilCost = manaCost[0x33];
            manaCost[0x33] = 50;
            auto enemyBuilding = [&](int x, int y, int hp) {
                Unit* b = AddUnit(kBarracks, 1, x, y, hp, 0, kOrderStand);
                Field<uint16_t>(b, kOffStateFlags) = kStateComplete;
                return b;
            };
            // (a) the shipped lists are exactly the order CasterThink used before there was a [priority] section.
            const int8_t wantPaladin[] = {kSpellHeal, kSpellExorcism, kSpellHolyVision, -1};
            const int8_t wantMage[] = {kSpellPolymorph, kSpellSlow,     kSpellFireball,
                                       kSpellInvisibility, kSpellBlizzard, kSpellFlameShield, -1};
            const int8_t wantOgre[] = {kSpellBloodlust, kSpellRunes, -1};
            const int8_t wantKnight[] = {kSpellRaiseDead,      kSpellUnholyArmor, kSpellDeathCoil,
                                         kSpellHaste,          kSpellDeathAndDecay, kSpellWhirlwind, -1};
            CHECK(memcmp(defaults.list[kCasterPaladin], wantPaladin, sizeof(wantPaladin)) == 0 &&
                      memcmp(defaults.list[kCasterMage], wantMage, sizeof(wantMage)) == 0 &&
                      memcmp(defaults.list[kCasterOgreMage], wantOgre, sizeof(wantOgre)) == 0 &&
                      memcmp(defaults.list[kCasterDeathKnight], wantKnight, sizeof(wantKnight)) == 0 && defaults.saveMana,
                  "the default [priority] lists must be the order the mod used before, and save_mana on");

            // (b) death and decay first, 60 mana (a wave costs 30, three waves are asked for): a valid building target
            // and a death coil target, and the knight casts NOTHING and keeps its mana.
            for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellDeathAndDecay || i == kSpellDeathCoil;
            config::g.priority = defaults;
            int8_t* knight = config::g.priority.list[kCasterDeathKnight];
            knight[0] = kSpellDeathAndDecay;
            knight[1] = kSpellDeathCoil;
            knight[2] = -1;
            auto knightWorld = [&](int mana, bool building) {
                ResetWorld();
                Unit* c = caster(kTypeDeathKnight, 20, 20);
                Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(mana);
                if (building) enemyBuilding(27, 19, 800);        // 3x3, aimed at 28,20: a valid death and decay target
                AddUnit(kGrunt, 1, 24, 24, 60, 0, kOrderAttack);  // a death coil target, out of the blast
                return c;
            };
            const unsigned castsBefore = autocast::CastCount();
            Unit* dkp = knightWorld(60, true);
            mod::RunAutocastPass();
            CHECK(OrderOf(dkp) == kOrderStand && autocast::CastCount() == castsBefore && autocast::ChannelCount() == 0,
                  "save_mana: the knight must save for death and decay, not coil (order %u, %u cast(s), %u channel(s))",
                  OrderOf(dkp), autocast::CastCount() - castsBefore, autocast::ChannelCount());
            CHECK(LogContains(dir, "saving: death_knight at 20,20 mana 60 for death_and_decay (needs 90)"),
                  "the saving line must name the spell and what it needs");
            const int savingLines = LogCount(dir, "saving: death_knight");
            mod::RunAutocastPass();
            mod::RunAutocastPass();
            CHECK(LogCount(dir, "saving: death_knight") == savingLines && OrderOf(dkp) == kOrderStand,
                  "the saving line is throttled to one per caster per 30 s of play");
            // (c) three waves in the bank: it casts.
            dkp = knightWorld(90, true);
            mod::RunAutocastPass();
            CHECK(OrderOf(dkp) == kOrderDeathAndDecay && autocast::ChannelCount() == 1,
                  "90 mana: death and decay must be cast (order %u)", OrderOf(dkp));
            // (d) no target for it: the lower spell goes ahead, as today.
            dkp = knightWorld(60, false);
            mod::RunAutocastPass();
            CHECK(OrderOf(dkp) == 0x33 && TargetOf(dkp) != nullptr,
                  "no death and decay target: death coil at 60 mana (order %u)", OrderOf(dkp));
            // (e) save_mana = false: the list is only an order.
            config::g.priority.saveMana = false;
            dkp = knightWorld(60, true);
            mod::RunAutocastPass();
            CHECK(OrderOf(dkp) == 0x33, "save_mana = false must let the cheaper spell through (order %u)", OrderOf(dkp));
            config::g.priority.saveMana = true;

            // (g) the same three steps for a mage with blizzard first.
            for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellBlizzard || i == kSpellSlow;
            config::g.priority = defaults;
            int8_t* mageList = config::g.priority.list[kCasterMage];
            mageList[0] = kSpellBlizzard;
            mageList[1] = kSpellSlow;
            mageList[2] = -1;
            auto mageWorld = [&](int mana, bool building) {
                ResetWorld();
                Unit* c = caster(kTypeMage, 20, 20);
                Field<uint8_t>(c, kOffMana) = static_cast<uint8_t>(mana);
                if (building) enemyBuilding(27, 19, 800);
                AddUnit(kGrunt, 1, 24, 24, 60, 0, kOrderAttack);  // a slow target out of the blast
                return c;
            };
            Unit* mgp = mageWorld(50, true);  // a blizzard wave costs 25, three waves are 75
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand && autocast::ChannelCount() == 0, "the mage must save for blizzard (order %u)",
                  OrderOf(mgp));
            mgp = mageWorld(75, true);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderBlizzard, "75 mana: blizzard must be cast (order %u)", OrderOf(mgp));
            mgp = mageWorld(50, false);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == 0x2C, "no blizzard target: slow at 50 mana (order %u)", OrderOf(mgp));

            // (h) the dry run leaves nothing behind: in the SAME pass the lower spell casts, and the next pass still
            // casts the higher one at the same tile (no stale claim, no stale channel, no stale cache).
            mgp = mageWorld(50, true);
            AddUnit(kGrunt, 1, 21, 21, 60, 0, kOrderAttack);  // a slow target beside the mage, outside the blast area
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand, "the blizzard target still wins the walk (order %u)", OrderOf(mgp));
            Field<uint8_t>(mgp, kOffMana) = 75;
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderBlizzard && autocast::ChannelCount() == 1,
                  "after a dry run the same tile must still be castable (order %u, %u channel(s))", OrderOf(mgp),
                  autocast::ChannelCount());

            // (i) hold_for_blocked_area: a blizzard target whose every worthwhile aim has the player's own units in it
            // holds the mage; slow further down the list waits (the author's report: slow cast instead of blizzard).
            // The barracks' centre is 28,20, so every aim that reaches it lies in 26..30 x 18..22: a footman at 28,17
            // and one at 28,23 put every such aim within 4 tiles of one of them.
            auto troopsInTheWay = [&](int mana) {
                Unit* c = mageWorld(mana, true);
                AddUnit(kFootman, 0, 28, 17, 60, 0, kOrderStand);
                AddUnit(kFootman, 0, 28, 23, 60, 0, kOrderStand);
                return c;
            };
            config::g.priority.holdForBlockedArea = false;
            mgp = troopsInTheWay(255);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == 0x2C, "hold off: troops in the blizzard's way, slow goes ahead (order %u)", OrderOf(mgp));
            config::g.priority.holdForBlockedArea = true;
            mgp = troopsInTheWay(255);
            {
                const int before = LogCount(dir, "holding: mage at 20,20 mana 255 for blizzard");
                mod::RunAutocastPass();
                CHECK(OrderOf(mgp) == kOrderStand && Field<uint8_t>(mgp, kOffNextOrder) == kOrderNone &&
                          LogCount(dir, "holding: mage at 20,20 mana 255 for blizzard") == before + 1,
                      "hold on: the mage must hold for blizzard, cast nothing and not walk (order %u, next %u)", OrderOf(mgp),
                      Field<uint8_t>(mgp, kOffNextOrder));
            }
            mgp = troopsInTheWay(60);  // short of the 75 for three waves, rich enough for slow: still held
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand, "hold on, 60 mana: blocked blizzard must still hold slow back (order %u)",
                  OrderOf(mgp));
            config::g.priority.saveMana = false;
            mgp = troopsInTheWay(60);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand, "hold works without save_mana too (order %u)", OrderOf(mgp));
            config::g.priority.saveMana = true;
            // Something of the player's that cannot move (a barracks at 24..26 x 19..21, within 4 of every aim) is no
            // reason to hold: slow goes ahead.
            mgp = mageWorld(255, true);
            {
                Unit* own = AddUnit(kBarracks, 0, 24, 19, 800, 0, kOrderStand);
                Field<uint16_t>(own, kOffStateFlags) = kStateComplete;
            }
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == 0x2C, "an own building in the way must not hold the mage (order %u)", OrderOf(mgp));
            // The footmen walk off: blizzard now.
            mgp = mageWorld(255, true);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderBlizzard, "once the way is clear the held blizzard is cast (order %u)", OrderOf(mgp));

            // (j) Fireball leaves buildings to Blizzard once the owner knows Blizzard and [spells] blizzard is on.
            constexpr uint32_t kBlizzardBit = 0x200;  // src/autocast.cpp kSpells, the button record's research bit
            for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellBlizzard || i == kSpellFireball;
            mageList[0] = kSpellFireball;
            mageList[1] = -1;
            auto fireballAtBuildings = [&](bool grunt) {
                ResetWorld();
                Unit* c = caster(kTypeMage, 20, 30);
                enemyBuilding(26, 30, 800);
                enemyBuilding(29, 30, 800);
                if (grunt) AddUnit(kGrunt, 1, 27, 33, 60, 0, kOrderAttack);  // off the line
                return c;
            };
            mgp = fireballAtBuildings(false);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand, "blizzard known: fireball at two buildings (order %u at %d,%d)", OrderOf(mgp),
                  Field<int16_t>(mgp, kOffOrderX), Field<int16_t>(mgp, kOffOrderY));
            mgp = fireballAtBuildings(false);
            AddUnit(kGrunt, 1, 27, 30, 60, 0, kOrderAttack);  // one unit on the line: 1 + 2 buildings, but only 1 counts
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderStand, "blizzard known: two buildings and one grunt must count as one (order %u)",
                  OrderOf(mgp));
            AddUnit(kGrunt, 1, 28, 30, 60, 0, kOrderAttack);  // two units on the line: fire
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderFireball, "blizzard known: two grunts on the line still get a fireball (order %u)",
                  OrderOf(mgp));
            config::g.spell[kSpellBlizzard] = false;  // [spells] blizzard off: buildings count again
            mgp = fireballAtBuildings(false);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderFireball, "blizzard switched off: fireball at two buildings (order %u)", OrderOf(mgp));
            config::g.spell[kSpellBlizzard] = true;
            At<uint32_t>(kRvaSpellsResearched)[0] &= ~kBlizzardBit;  // not researched: buildings count again
            mgp = fireballAtBuildings(false);
            mod::RunAutocastPass();
            CHECK(OrderOf(mgp) == kOrderFireball, "blizzard not researched: fireball at two buildings (order %u)", OrderOf(mgp));
            At<uint32_t>(kRvaSpellsResearched)[0] |= kBlizzardBit;
            for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i == kSpellBlizzard || i == kSpellSlow;
            config::g.priority = defaults;

            // (f) the reader: an unknown name, a name from another caster, a duplicate, and the spells left out.
            WriteFileText(ini,
                          "[priority]\nsave_mana = false\nhold_for_blocked_area = false\n"
                          "death_knight = [\"death_and_decay\", \"bogus_spell\", \"heal\", \"death_and_decay\", \"death_coil\"]\n"
                          "mage = [\"blizzard\"]\n");
            CHECK(config::Init(dir), "[priority] config rejected");
            {
                const Priority& p = config::g.priority;
                const int8_t wantDk[] = {kSpellDeathAndDecay, kSpellDeathCoil,   kSpellRaiseDead,
                                         kSpellUnholyArmor,   kSpellHaste,       kSpellWhirlwind, -1};
                const int8_t wantMg[] = {kSpellBlizzard,      kSpellPolymorph,   kSpellSlow,
                                         kSpellFireball,      kSpellInvisibility, kSpellFlameShield, -1};
                CHECK(!p.holdForBlockedArea, "[priority] hold_for_blocked_area = false was not read");
                CHECK(!p.saveMana && memcmp(p.list[kCasterDeathKnight], wantDk, sizeof(wantDk)) == 0,
                      "[priority] death_knight: named spells first, the rest appended in the default order");
                CHECK(memcmp(p.list[kCasterMage], wantMg, sizeof(wantMg)) == 0, "[priority] mage: one name, the rest appended");
                CHECK(memcmp(p.list[kCasterPaladin], defaults.list[kCasterPaladin], kSpellCount) == 0,
                      "a caster the file does not mention keeps the default list");
                CHECK(LogContains(dir, "[priority] death_knight: unknown spell \"bogus_spell\" ignored") &&
                          LogContains(dir, "[priority] death_knight: \"heal\" is not a death_knight spell, ignored"),
                      "a misspelled name and a name from another caster must each be logged once");
            }
            DeleteFileW(ini);
            CHECK(config::Init(dir), "the default config did not come back");
            manaCost[0x33] = savedCoilCost;
            EnableEverythingForTests();  // config::Init put the shipped defaults back over the test settings
            config::g.priority = saved;
            memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
            config::g.logCasts = true;
        }

        memset(exploredMap, 0, sizeof(exploredMap));
        memcpy(sizes, savedSizes, sizeof(savedSizes));
        memcpy(rangeT, savedRanges, sizeof(savedRanges));
        memcpy(config::g.spell, savedSpells, sizeof(savedSpells));
        config::g.eyeCast = savedEye;
        config::g.logCasts = savedLog;
    }

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

    // Unlimited oil platforms: the same "left" word, its own switch. Gold mines must not come along, and the other way.
    ResetWorld();
    defType(kTypeGoldMine, kTfBuilding | 0x400000, 25500);
    defType(0x56, kTfBuilding | kTfOilPlatform, 650);
    defType(0x57, kTfBuilding | kTfOilPlatform, 650);
    Unit* oilMine = AddUnit(kTypeGoldMine, kNeutralPlayer, 50, 50, 25500, 0, 0);
    Unit* humanRig = AddUnit(0x56, 0, 20, 20, 650, 0, 0);  // mine
    Unit* orcRig = AddUnit(0x57, 1, 30, 20, 650, 0, 0);    // the computer's
    Field<uint16_t>(oilMine, kOffResources) = 400;
    Field<uint16_t>(humanRig, kOffResources) = 250;
    Field<uint16_t>(orcRig, kOffResources) = 2;
    mod::OnTick();
    Field<uint16_t>(humanRig, kOffResources) = 249;  // a tanker took 100 oil
    mod::OnTick();
    CHECK(Field<uint16_t>(humanRig, kOffResources) == 249 && Field<uint16_t>(orcRig, kOffResources) == 2, "oil platforms refilled while the option is off");
    config::g.oilPlatformsUnlimited = true;
    mod::OnTick();
    Field<uint16_t>(humanRig, kOffResources) = 248;
    Field<uint16_t>(oilMine, kOffResources) = 399;
    mod::OnTick();
    CHECK(Field<uint16_t>(humanRig, kOffResources) == 249, "platform should return to the amount seen when the option came on (%u)", Field<uint16_t>(humanRig, kOffResources));
    CHECK(Field<uint16_t>(orcRig, kOffResources) == 50, "a nearly dry platform (the computer's too) should be lifted to the 5000 oil floor (%u)", Field<uint16_t>(orcRig, kOffResources));
    CHECK(Field<uint16_t>(oilMine, kOffResources) == 399, "[oil_platforms] must not refill gold mines (%u)", Field<uint16_t>(oilMine, kOffResources));
    config::g.oilPlatformsUnlimited = false;
    config::g.goldMinesUnlimited = true;
    mod::OnTick();
    Field<uint16_t>(humanRig, kOffResources) = 200;
    Field<uint16_t>(oilMine, kOffResources) = 398;
    mod::OnTick();
    CHECK(Field<uint16_t>(humanRig, kOffResources) == 200 && Field<uint16_t>(oilMine, kOffResources) == 399,
          "[gold_mines] must not refill oil platforms (platform %u mine %u)", Field<uint16_t>(humanRig, kOffResources), Field<uint16_t>(oilMine, kOffResources));
    config::g.goldMinesUnlimited = false;

    // [gold_mines] amount / [oil_platforms] amount: once per NEW map, on the first tick after the new-map hook; never
    // for a game that came from a save, never a second time, never in a multiplayer map, capped at 65535.
    {
        auto world = [&](Unit*& m, Unit*& rich, Unit*& patch, Unit*& rig) {
            ResetWorld();
            defType(kTypeGoldMine, kTfBuilding | 0x400000, 25500);
            defType(kTypeOilPatch, 0, 0);
            defType(0x56, kTfBuilding | kTfOilPlatform, 650);
            m = AddUnit(kTypeGoldMine, kNeutralPlayer, 50, 50, 25500, 0, 0);
            rich = AddUnit(kTypeGoldMine, kNeutralPlayer, 56, 50, 25500, 0, 0);
            patch = AddUnit(kTypeOilPatch, kNeutralPlayer, 10, 50, 0, 0, 0);
            rig = AddUnit(0x56, 0, 20, 20, 650, 0, 0);
            Field<uint16_t>(m, kOffResources) = 400;      // 40,000 gold
            Field<uint16_t>(rich, kOffResources) = 6375;  // the most a map can hold: 637,500
            Field<uint16_t>(patch, kOffResources) = 250;
            Field<uint16_t>(rig, kOffResources) = 101;
        };
        Unit *m, *rich, *patch, *rig;
        config::g.goldMinesAmount = 3.0;
        config::g.oilAmount = 2.5;
        world(m, rich, patch, rig);
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 400, "mine amounts must wait for a new map (%u)", Field<uint16_t>(m, kOffResources));
        datatweaks::OnNewMapTablesLoaded();  // the hook fires before the map's units exist...
        world(m, rich, patch, rig);          // ...then the map is populated
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 1200 && Field<uint16_t>(rich, kOffResources) == 19125, "gold x3 (%u, %u)",
              Field<uint16_t>(m, kOffResources), Field<uint16_t>(rich, kOffResources));
        CHECK(Field<uint16_t>(patch, kOffResources) == 625 && Field<uint16_t>(rig, kOffResources) == 253, "oil x2.5, patch and platform, rounded (%u, %u)",
              Field<uint16_t>(patch, kOffResources), Field<uint16_t>(rig, kOffResources));
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 1200, "amount applied twice (%u)", Field<uint16_t>(m, kOffResources));

        config::g.goldMinesAmount = 20.0;
        datatweaks::OnNewMapTablesLoaded();
        world(m, rich, patch, rig);
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 8000 && Field<uint16_t>(rich, kOffResources) == 65535, "cap is 65535 = 6,553,500 (%u)", Field<uint16_t>(rich, kOffResources));

        datatweaks::OnNewMapTablesLoaded();  // a save loaded before the first tick of the new map
        world(m, rich, patch, rig);
        *At<uint16_t>(kRvaGameFromSave) = 1;
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 400, "a game that came from a save must keep its amounts (%u)", Field<uint16_t>(m, kOffResources));
        *At<uint16_t>(kRvaGameFromSave) = 0;
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 400, "the pending flag must be spent, not kept for later (%u)", Field<uint16_t>(m, kOffResources));

        *At<uint8_t>(kRvaNetGameAtLoad) = 1;  // multiplayer map
        datatweaks::OnNewMapTablesLoaded();
        *At<uint8_t>(kRvaNetGameAtLoad) = 0;
        world(m, rich, patch, rig);
        mod::OnTick();
        CHECK(Field<uint16_t>(m, kOffResources) == 400, "a multiplayer map load must not arm the amounts (%u)", Field<uint16_t>(m, kOffResources));
        config::g.goldMinesAmount = 1.0;
        config::g.oilAmount = 1.0;
    }

    // [food] hall_food: supply = 4 x farms + amount x (halls + keeps + castles), from the game's own counters. Off and
    // never switched on = not a single write; switched off again = the game's own value (amount 1) comes back.
    {
        ResetWorld();
        AddUnit(kFootman, 0, 5, 5, 60, 0, kOrderStand);  // BuildWorld wants at least one unit
        uint16_t* supply = At<uint16_t>(kRvaFoodSupply);
        uint16_t* farms = At<uint16_t>(kRvaFarmCount);
        uint16_t* halls = At<uint16_t>(kRvaHallCount);
        uint16_t* keeps = At<uint16_t>(kRvaKeepCount);
        uint16_t* castles = At<uint16_t>(kRvaCastleCount);
        for (int pl = 0; pl < 16; ++pl) supply[pl] = farms[pl] = halls[pl] = keeps[pl] = castles[pl] = 0;
        halls[0] = 1; supply[0] = 1;                              // me: one fresh town hall, the custom game start
        farms[1] = 3; keeps[1] = 1; castles[1] = 1; supply[1] = 14;  // the computer: 3 farms, a keep and a castle
        supply[2] = 77;                                           // a value the formula would not produce
        mod::OnTick();
        CHECK(supply[0] == 1 && supply[1] == 14 && supply[2] == 77, "hall food off: the supply words must not be touched");
        config::g.hallFood = true;
        config::g.hallFoodAmount = 5;
        mod::OnTick();
        CHECK(supply[0] == 5 && supply[1] == 22 && supply[2] == 0, "hall food 5: me %u (5), computer %u (3x4 + 2x5 = 22)", supply[0], supply[1]);
        halls[0] = 0; keeps[0] = 1; farms[0] = 2;                 // hall upgraded to a keep, two farms built
        mod::OnTick();
        CHECK(supply[0] == 13, "keep + 2 farms = 13 (%u)", supply[0]);
        farms[3] = 0xFFFF; supply[3] = 40;                        // an underflowed counter: hands off that player
        mod::OnTick();
        CHECK(supply[3] == 40, "a wrapped counter must make the mod leave that player alone (%u)", supply[3]);
        config::g.hallFood = false;
        mod::OnTick();
        CHECK(supply[0] == 9 && supply[1] == 14, "switched off: back to the game's own 1 per hall (me %u, computer %u)", supply[0], supply[1]);
        supply[0] = 123;
        mod::OnTick();
        CHECK(supply[0] == 123, "after the restore the mod must stop writing (%u)", supply[0]);
        for (int pl = 0; pl < 16; ++pl) supply[pl] = farms[pl] = halls[pl] = keeps[pl] = castles[pl] = 0;
    }

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

    // Crash 2026-09-19 (1.4.0, player session, dump in the game's Errors folder): a peon idling at 54,0 was sent to
    // harvest a "tree" at 53,-1, because every tile outside the map counted as forest; the game then indexed its unit
    // grid with -75 (0x4D80BF). On every edge and corner with no tree around: no order at all, nothing for the guard.
    {
        const int refusedBefore = game::RefusedOrderCount();
        const struct { int x, y; } edges[] = {{30, 0}, {0, 30}, {kMap - 1, 30}, {30, kMap - 1}, {0, 0}, {kMap - 1, kMap - 1}};
        uint32_t serial = 1100;
        for (const auto& e : edges) {
            ResetWorld();
            peon = AddUnit(kPeon, 0, e.x, e.y, 30, 0, kOrderStop);
            Field<uint32_t>(peon, kOffSerial) = ++serial;
            mod::OnTick();
            step(2600);
            CHECK(OrderOf(peon) == kOrderStop, "worker on the map edge at %d,%d was given order %u to %d,%d", e.x, e.y, OrderOf(peon),
                  Field<int16_t>(peon, kOffOrderX), Field<int16_t>(peon, kOffOrderY));
        }
        CHECK(game::RefusedOrderCount() == refusedBefore, "the worker code produced %d order(s) outside the map",
              game::RefusedOrderCount() - refusedBefore);
        ResetWorld();  // a real tree on the edge row is still found
        peon = AddUnit(kPeon, 0, 30, 0, 30, 0, kOrderStop);
        Field<uint32_t>(peon, kOffSerial) = ++serial;
        regionMap[0 * kMap + 32] = kRegionTree;
        mod::OnTick();
        step(2600);
        CHECK(OrderOf(peon) == kOrderHarvest && Field<int16_t>(peon, kOffOrderX) == 32 && Field<int16_t>(peon, kOffOrderY) == 0,
              "a tree on the edge row must still be found (order %u at %d,%d)", OrderOf(peon), Field<int16_t>(peon, kOffOrderX),
              Field<int16_t>(peon, kOffOrderY));
        regionMap[0 * kMap + 32] = 1;

        // The guard for the whole class: no positional order outside the map reaches the game, from any feature.
        ResetWorld();
        Unit* walker = AddUnit(kFootman, 0, 10, 10, 60, 0, kOrderStop);
        game::IssueOrder(walker, 5, -1, nullptr, kRvaMoveHandler);
        game::IssueOrder(walker, -1, 5, nullptr, kRvaHarvestHandler);
        game::IssueOrder(walker, kMap, 5, nullptr, kRvaMoveHandler);
        game::IssueOrder(walker, 5, kMap, nullptr, kRvaMoveHandler);
        CHECK(OrderOf(walker) == kOrderStop && game::RefusedOrderCount() == refusedBefore + 4,
              "orders outside the map must be refused (order %u, refused %d)", OrderOf(walker), game::RefusedOrderCount() - refusedBefore);
        game::IssueOrder(walker, kMap - 1, 0, nullptr, kRvaMoveHandler);
        CHECK(OrderOf(walker) == kOrderMove, "an order to the last tile of the map is fine (order %u)", OrderOf(walker));
        Idle(walker);
        game::IssueOrder(walker, -5, -5, peon, kRvaRepairHandler);  // a unit target: the tile is not used
        CHECK(OrderOf(walker) == kOrderRepair, "a targeted order is not subject to the tile check (order %u)", OrderOf(walker));

        // Crash 2026-09-21 (image 0x680000 + 0xE2257): a paladin with a pending Remastered attack-move (resume byte 10)
        // was given Heal; the heal action's own SetOrder(Stop) resumed the attack-move on the spot, which wiped the
        // order target, and the action then read target->hp through NULL. Every order the mod issues must first clear
        // the resume byte, exactly like the player's command path does (FUN_004dcc60).
        {
            const uint32_t savedRuleset = *At<uint32_t>(kRvaRuleset);
            *At<uint32_t>(kRvaRuleset) = 1;  // Remastered rules: the resume byte exists
            Idle(walker);
            Field<uint8_t>(walker, kOffResumeOrder) = kOrderAttackArea;
            game::IssueOrder(walker, 0, 0, peon, kRvaSpellOrderHandler);
            CHECK(Field<uint8_t>(walker, kOffResumeOrder) == kOrderNone,
                  "a spell order must clear a pending attack-move resume (resume byte %u)", Field<uint8_t>(walker, kOffResumeOrder));
            Idle(walker);
            Field<uint8_t>(walker, kOffResumeOrder) = kOrderPatrol;
            game::IssueOrder(walker, 12, 12, nullptr, kRvaMoveHandler);
            CHECK(Field<uint8_t>(walker, kOffResumeOrder) == kOrderNone,
                  "a positional order must clear a pending patrol resume (resume byte %u)", Field<uint8_t>(walker, kOffResumeOrder));
            *At<uint32_t>(kRvaRuleset) = 0;  // classic rules: the byte is not part of the unit, leave it alone
            Idle(walker);
            Field<uint8_t>(walker, kOffResumeOrder) = kOrderAttackArea;
            game::IssueOrder(walker, 0, 0, peon, kRvaSpellOrderHandler);
            CHECK(Field<uint8_t>(walker, kOffResumeOrder) == kOrderAttackArea,
                  "without the Remastered ruleset the byte is not touched (%u)", Field<uint8_t>(walker, kOffResumeOrder));
            Field<uint8_t>(walker, kOffResumeOrder) = kOrderNone;
            *At<uint32_t>(kRvaRuleset) = savedRuleset;
        }
    }
    config::g.workerAutoHarvest = config::g.workerAutoRepair = false;

    // Map-start data tweaks, run against real default values (Data\Rez\unitdata.dat / upgrades.dat, typed in here).
    {
        using namespace units;
        uint16_t* hpT = At<uint16_t>(kRvaMaxHpByType);
        uint32_t* sightT = At<uint32_t>(kRvaSightByType);
        uint8_t* goldT = At<uint8_t>(kRvaGoldCostByType);
        uint8_t* lumberT = At<uint8_t>(kRvaLumberCostByType);
        uint8_t* oilT = At<uint8_t>(kRvaOilCostByType);
        uint8_t* buildT = At<uint8_t>(kRvaBuildTimeByType);
        uint8_t* rangeT = At<uint8_t>(kRvaAttackRangeByType);
        uint16_t* upGold = At<uint16_t>(kRvaUpgradeGold);
        uint16_t* upLumber = At<uint16_t>(kRvaUpgradeLumber);
        uint8_t* upTime = At<uint8_t>(kRvaResearchTime);
        constexpr uint8_t kFarmT = 0x3A, kPigFarm = 0x3B, kFoundry = 0x4E, kKeep = 0x58, kStronghold = 0x59, kGuardTower = 0x60,
                          kDestroyer = 0x1E, kArcher = 8, kAxethrower = 9, kKnight = 6, kMageT = 0x0A, kDeathwing = 0x23, kGrom = 0x19,
                          kLothar = 0x32;
        auto resetTables = [&] {
            hpT[kFootman] = 60; hpT[kGrunt] = 60; hpT[kKnight] = 90; hpT[kOgre] = 90; hpT[kArcher] = 40; hpT[kMageT] = 60;
            hpT[kGrom] = 240; hpT[kLothar] = 90; hpT[kDeathwing] = 800; hpT[kSkeleton] = 40; hpT[kDragon] = 100;
            hpT[kFarmT] = 400; hpT[kTypeGoldMine] = 25500; hpT[0x4A] = 1200;
            sightT[kDragon] = 6; sightT[0x2A] = 6; sightT[kFootman] = 4; sightT[0x28] = 9;
            goldT[kFootman] = 60; lumberT[kFootman] = 0; goldT[kGrunt] = 60; goldT[kArcher] = 50; lumberT[kArcher] = 5; goldT[kDragon] = 225;
            goldT[kDestroyer] = 70; lumberT[kDestroyer] = 35; oilT[kDestroyer] = 70;
            goldT[kFarmT] = 50; lumberT[kFarmT] = 25; goldT[kPigFarm] = 50; goldT[kFoundry] = 70; lumberT[kFoundry] = 40; oilT[kFoundry] = 40;
            goldT[kKeep] = 200; lumberT[kKeep] = 100; oilT[kKeep] = 20; goldT[kStronghold] = 200; goldT[kGuardTower] = 50; lumberT[kGuardTower] = 15;
            buildT[kFootman] = 60; buildT[kGrunt] = 60; buildT[kDragon] = 250; buildT[kFarmT] = 100; buildT[kKeep] = 200; buildT[0x4A] = 255;
            upGold[0] = 800; upGold[2] = 500; upGold[4] = 200; upLumber[4] = 200; upGold[6] = 200; upGold[8] = 300; upGold[12] = 700; upGold[14] = 700;
            upGold[20] = 1500; upGold[22] = 1500; upGold[31] = 1000; upGold[32] = 1000; upGold[33] = 1000; upGold[35] = 1000; upGold[41] = 2000;
            upGold[44] = 1000; upGold[46] = 0; upGold[34] = 0;
            upTime[0] = 200; upTime[2] = 200; upTime[34] = 0;
            rangeT[kArcher] = 4; rangeT[kAxethrower] = 4; rangeT[4] = 8; rangeT[kGuardTower] = 6; rangeT[kFootman] = 1;
        };
        auto resetConfig = [&] {
            config::g.health = Multipliers();
            config::g.costs = Multipliers();
            config::g.time = Multipliers();
            for (auto& row : config::g.unitStat)
                for (int32_t& v : row) v = -1;
            config::g.rangeUpgradeBonus = 1;
        };
        resetTables();
        CHECK(*At<uint8_t>(kRvaMapLoadCallSite) == 0xE8, "map load call site is not a call");

        // Straight from the shipped config: a new map must leave every table exactly as the game made it.
        datatweaks::OnNewMapTablesLoaded();
        CHECK(sightT[kDragon] == 6 && sightT[0x2A] == 6 && hpT[kFootman] == 60 && goldT[kFootman] == 60 && buildT[kFootman] == 60 &&
                  upGold[0] == 800 && rangeT[kArcher] == 4,
              "shipped config changed game data");
        resetTables();

        // Classification tables.
        CHECK(StructureRace(kFarmT) == kHuman && StructureRace(kPigFarm) == kOrc && StructureRace(kKeep) == kHuman &&
                  StructureRace(kStronghold) == kOrc && StructureRace(0x61) == kOrc && StructureRace(0x67) == kHuman &&
                  StructureRace(0x68) == kOrc && StructureRace(kTypeGoldMine) == kNeutral && StructureRace(0x66) == kNeutral,
              "structure race table");
        CHECK(StructureGroupOf(kKeep) == kBuildingUpgrades && StructureGroupOf(0x63) == kBuildingUpgrades && StructureGroupOf(kFarmT) == kBuildings &&
                  StructureGroupOf(0x40) == kBuildings,
              "structure group table");
        {
            const int humanRows[] = {0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 22, 23, 24, 25, 26, 27, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
            int humans = 0;
            for (int i = 0; i < kResearchCount; ++i) humans += ResearchInfoOf(i).race == kHuman;
            bool rowsOk = humans == 26;
            for (int i : humanRows) rowsOk = rowsOk && ResearchInfoOf(i).race == kHuman;
            CHECK(rowsOk, "research race table (%d human rows)", humans);
            CHECK(ResearchInfoOf(20).group == kSiegeUpgrades && ResearchInfoOf(20).race == kOrc && ResearchInfoOf(22).race == kHuman &&
                      ResearchInfoOf(32).group == kKnightUpgrades && ResearchInfoOf(44).group == kKnightUpgrades && ResearchInfoOf(50).group == kKnightUpgrades &&
                      ResearchInfoOf(43).group == kKnightUpgrades && ResearchInfoOf(45).group == kSpells && ResearchInfoOf(51).group == kSpells &&
                      ResearchInfoOf(14).group == kNavalUpgrades && ResearchInfoOf(10).group == kMeleeUpgrades,
                  "research group table");
        }

        // Multiplayer: nothing moves, and the range bonus instruction is forced back to the game's own.
        resetConfig();
        config::g.health.all = 2.0;
        config::g.rangeUpgradeBonus = 3;
        *At<uint8_t>(kRvaNetGameAtLoad) = 1;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60, "data tables were edited for a multiplayer map");
        CHECK(At<uint8_t>(kRvaRangeBonusInsn)[0] == 0xFE && At<uint8_t>(kRvaRangeBonusInsn)[1] == 0xC0, "range bonus patched in a multiplayer game");
        *At<uint8_t>(kRvaNetGameAtLoad) = 0;

        // Defaults: every multiplier is 1.0 and nothing but the default sight bonus changes.
        resetConfig();
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60 && hpT[kGrom] == 240 && goldT[kFootman] == 60 && goldT[kFarmT] == 50 && goldT[kKeep] == 200 &&
                  upGold[0] == 800 && upGold[33] == 1000 && buildT[kFootman] == 60 && upTime[0] == 200 && rangeT[kArcher] == 4,
              "defaults must leave health, prices, times and ranges alone");

        // Unit health: units master x race units x group, heroes by the hero list, neutral via its own "all"; the
        // "units" keys never reach a structure.
        resetTables(); resetConfig();
        config::g.health.units = 2.0;
        config::g.health.race[kOrc].units = 1.5;
        config::g.health.race[kOrc].unit[kMelee] = 2.0;
        config::g.health.race[kHuman].unit[kCasters] = 0.5;
        config::g.health.race[kHuman].unit[kHeroes] = 4.0;
        config::g.health.race[kNeutral].all = 3.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 120 && hpT[kKnight] == 180 && hpT[kArcher] == 80, "human units: master only (footman %u)", hpT[kFootman]);
        CHECK(hpT[kGrunt] == 360 && hpT[kOgre] == 540, "orc melee: 2 x 1.5 x 2 (grunt %u ogre %u)", hpT[kGrunt], hpT[kOgre]);
        CHECK(hpT[kDragon] == 300, "orc air: 2 x 1.5 (dragon %u)", hpT[kDragon]);
        CHECK(hpT[kMageT] == 60, "human casters: 2 x 0.5 (mage %u)", hpT[kMageT]);
        CHECK(hpT[kLothar] == 720 && hpT[kGrom] == 720, "heroes: lothar 90 x2 x4, grom 240 x2 x1.5 (%u, %u)", hpT[kLothar], hpT[kGrom]);
        CHECK(hpT[kSkeleton] == 240, "neutral: 2 x 3 (skeleton %u)", hpT[kSkeleton]);
        CHECK(hpT[kFarmT] == 400 && hpT[0x4A] == 1200 && hpT[kTypeGoldMine] == 25500, "the units keys must leave structures and the gold mine alone");

        // [health] all = EVERYTHING (player request 2026-09-18): units, ships, structures of every race; the race "all"
        // likewise. Only neutral structures (gold mine, dark portal, runestone) stay out.
        resetTables(); resetConfig();
        hpT[kPigFarm] = 400; hpT[0x1E] = 100;  // elven destroyer
        config::g.health.all = 2.0;
        config::g.health.race[kOrc].all = 1.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 120 && hpT[0x1E] == 200 && hpT[kFarmT] == 800 && hpT[0x4A] == 2400 && hpT[kSkeleton] == 80,
              "health all x2: units, ships, structures, neutral units (footman %u destroyer %u farm %u)", hpT[kFootman], hpT[0x1E], hpT[kFarmT]);
        CHECK(hpT[kGrunt] == 180 && hpT[kPigFarm] == 1200, "orc all on top: 2 x 1.5 for grunt and pig farm (%u, %u)", hpT[kGrunt], hpT[kPigFarm]);
        CHECK(hpT[kTypeGoldMine] == 25500, "the gold mine is scenery: never scaled");

        // Structure health by group; the units masters stay out of it.
        resetTables(); resetConfig();
        hpT[kKeep] = 1400; hpT[kStronghold] = 1400; hpT[kPigFarm] = 400;
        config::g.health.units = 3.0;
        config::g.health.race[kHuman].units = 3.0;
        config::g.health.race[kHuman].structure[kBuildings] = 2.0;
        config::g.health.race[kOrc].structure[kBuildingUpgrades] = 1.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFarmT] == 800 && hpT[0x4A] == 2400, "human buildings x2 only, masters ignored (farm %u town hall %u)", hpT[kFarmT], hpT[0x4A]);
        CHECK(hpT[kKeep] == 1400 && hpT[kStronghold] == 2100 && hpT[kPigFarm] == 400, "building upgrades per race (keep %u stronghold %u)", hpT[kKeep], hpT[kStronghold]);
        CHECK(hpT[kTypeGoldMine] == 25500, "neutral structures must never be scaled");
        resetTables(); resetConfig();
        config::g.health.race[kHuman].structure[kBuildings] = 1000.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFarmT] == 32767, "structure health cap is 32767 (%u)", hpT[kFarmT]);

        // The structures masters: [health] structures x [health.orc] structures x group, still blind to the unit dials.
        resetTables(); resetConfig();
        hpT[kPigFarm] = 400; hpT[kStronghold] = 1400;
        config::g.health.units = 5.0;
        config::g.health.structures = 2.0;
        config::g.health.race[kOrc].structures = 1.5;
        config::g.health.race[kOrc].structure[kBuildingUpgrades] = 2.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFarmT] == 800 && hpT[kPigFarm] == 1200 && hpT[kStronghold] == 8400 && hpT[kFootman] == 300 && hpT[kTypeGoldMine] == 25500,
              "structure health masters (farm %u pig farm %u stronghold %u)", hpT[kFarmT], hpT[kPigFarm], hpT[kStronghold]);

        // Same masters for prices and times, where the top "all" does include structures.
        resetTables(); resetConfig();
        config::g.costs.structures = 0.5;
        config::g.costs.race[kOrc].structures = 0.5;
        config::g.costs.units = 0.5;
        config::g.costs.research = 0.5;
        config::g.time.structures = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFarmT] == 25 && goldT[kPigFarm] == 13 && goldT[kStronghold] == 50 && goldT[kFootman] == 30 && upGold[0] == 400,
              "price masters per kind (farm %u pig farm %u stronghold %u)", goldT[kFarmT], goldT[kPigFarm], goldT[kStronghold]);
        CHECK(buildT[kFarmT] == 50 && buildT[kFootman] == 60, "time structures master (farm %u)", buildT[kFarmT]);

        // Costs: master, race, umbrellas and groups; gold, lumber and oil all scale.
        resetTables(); resetConfig();
        config::g.costs.race[kHuman].units = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFootman] == 30 && goldT[kArcher] == 25 && lumberT[kArcher] == 3 && lumberT[kFootman] == 0 && goldT[kDestroyer] == 35 && oilT[kDestroyer] == 35,
              "human units umbrella: gold, lumber and oil halved, free stays free (destroyer oil %u)", oilT[kDestroyer]);
        CHECK(goldT[kGrunt] == 60 && goldT[kDragon] == 225 && goldT[kFarmT] == 50 && upGold[0] == 800, "human units umbrella leaked");

        resetTables(); resetConfig();
        config::g.costs.all = 0.5;
        config::g.costs.race[kOrc].all = 2.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFootman] == 30 && goldT[kFarmT] == 25 && upGold[0] == 400 && goldT[kKeep] == 100, "cost master halves everything human");
        CHECK(goldT[kGrunt] == 60 && goldT[kPigFarm] == 50 && upGold[2] == 500 && goldT[kStronghold] == 200, "orc: 0.5 x 2 = unchanged");

        resetTables(); resetConfig();
        config::g.costs.race[kHuman].structure[kBuildings] = 0.5;
        config::g.costs.race[kOrc].structure[kBuildingUpgrades] = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(goldT[kFarmT] == 25 && lumberT[kFarmT] == 13 && goldT[kFoundry] == 35 && oilT[kFoundry] == 20 && goldT[kPigFarm] == 50 && goldT[kKeep] == 200,
              "human buildings only, oil included (farm lumber %u)", lumberT[kFarmT]);
        CHECK(goldT[kStronghold] == 100 && goldT[kGuardTower] == 50, "orc building upgrades only");

        resetTables(); resetConfig();
        config::g.costs.race[kHuman].researchGroup[kNavalUpgrades] = 0.5;
        config::g.costs.race[kOrc].researchGroup[kMeleeUpgrades] = 0.5;
        config::g.costs.race[kOrc].research = 0.5;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(upGold[12] == 350 && upGold[0] == 800 && upGold[33] == 1000 && upGold[41] == 2000, "human: ship research only (%u)", upGold[12]);
        CHECK(upGold[14] == 350 && upGold[2] == 125 && upGold[20] == 750 && upGold[32] == 500 && upGold[44] == 500 && upGold[46] == 0,
              "orc: research umbrella 0.5, melee 0.5 on top, free stays free (axes %u)", upGold[2]);

        // Time: training, construction, structure upgrade and research, capped at 255.
        resetTables(); resetConfig();
        config::g.time.race[kHuman].units = 0.5;
        config::g.time.race[kHuman].structure[kBuildings] = 2.0;
        config::g.time.race[kHuman].structure[kBuildingUpgrades] = 0.25;
        config::g.time.race[kHuman].researchGroup[kMeleeUpgrades] = 0.5;
        config::g.time.race[kOrc].unit[kAir] = 2.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(buildT[kFootman] == 30 && buildT[kGrunt] == 60 && buildT[kFarmT] == 200 && buildT[0x4A] == 255 && buildT[kKeep] == 50,
              "build times (footman %u farm %u town hall %u keep %u)", buildT[kFootman], buildT[kFarmT], buildT[0x4A], buildT[kKeep]);
        CHECK(buildT[kDragon] == 255 && upTime[0] == 100 && upTime[2] == 200 && upTime[34] == 0, "time cap 255, research time, zero stays zero");

        // Engine caps for health and prices.
        resetTables(); resetConfig();
        config::g.health.all = 1000.0;
        config::g.costs.all = 1000.0;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(hpT[kFootman] == 60000 && hpT[kDeathwing] == 65535, "health cap is 65535 (footman %u deathwing %u)", hpT[kFootman], hpT[kDeathwing]);
        CHECK(goldT[kDragon] == 255 && goldT[kKeep] == 255 && upGold[20] == 65535, "price caps: 2550 for a type, 65535 for research");
        CHECK(hpT[kFarmT] == 32767, "health all reaches structures too, capped at 32767 (%u)", hpT[kFarmT]);

        // Range: listed units only, and the Longbow / Lighter Axes bonus as a 2-byte patch that the UI byte follows.
        resetTables(); resetConfig();
        config::g.unitStat[kArcher][kStatRange] = 6;
        config::g.unitStat[kGuardTower][kStatRange] = 8;
        config::g.rangeUpgradeBonus = 3;
        datatweaks::OnNewMapTablesLoaded();
        CHECK(rangeT[kArcher] == 6 && rangeT[kGuardTower] == 8 && rangeT[kAxethrower] == 4 && rangeT[4] == 8 && rangeT[kFootman] == 1, "range table");
        const uint8_t* bonusInsn = At<uint8_t>(kRvaRangeBonusInsn);
        CHECK(bonusInsn[0] == 0x04 && bonusInsn[1] == 3 && *At<uint8_t>(kRvaRangeBonusDisplay) == 3, "range bonus should be `add al, 3` (%02X %02X)", bonusInsn[0], bonusInsn[1]);
        CHECK(bonusInsn[2] == 0x5D && bonusInsn[3] == 0xC3, "the patch must not touch the following `pop ebp; ret`");
        datatweaks::SyncRangeBonus(true);
        CHECK(bonusInsn[0] == 0xFE && bonusInsn[1] == 0xC0 && *At<uint8_t>(kRvaRangeBonusDisplay) == 1, "multiplayer must restore `inc al`");
        config::g.rangeUpgradeBonus = 0;
        datatweaks::SyncRangeBonus(false);
        CHECK(bonusInsn[0] == 0x04 && bonusInsn[1] == 0, "bonus 0 = `add al, 0`");

        // [unit.<name>]: base stats replace the game's numbers, -1 leaves them, multipliers and sight bonus go on top.
        {
            uint8_t* armorT = At<uint8_t>(kRvaArmorByType);
            uint8_t* basicT = At<uint8_t>(kRvaBasicDamageByType);
            uint8_t* pierceT = At<uint8_t>(kRvaPiercingDamageByType);
            uint8_t* reactC = At<uint8_t>(kRvaReactRangeComputer);
            uint8_t* reactH = At<uint8_t>(kRvaReactRangeHuman);
            auto vanillaDestroyer = [&](uint8_t t) {
                hpT[t] = 100; armorT[t] = 10; basicT[t] = 35; pierceT[t] = 0; rangeT[t] = 4; sightT[t] = 8;
                goldT[t] = 70; lumberT[t] = 35; oilT[t] = 70; buildT[t] = 90;
                reactC[t] = 10; reactH[t] = 8;  // Data\\Rez\\unitdata.dat: the destroyer notices from 10 / 8
            };
            resetTables(); resetConfig();
            vanillaDestroyer(0x1E); vanillaDestroyer(0x1F);
            const int example[kStatCount] = {105, 11, 37, 2, 5, 9, 600, 300, 500, 80, 12};
            for (int i = 0; i < kStatCount; ++i) config::g.unitStat[0x1E][i] = example[i];
            config::g.unitStat[0x1F][kStatPiercingDamage] = 0;  // 0 is a real value, not "default"
            config::g.unitStat[0x1F][kStatArmor] = -1;
            pierceT[0x1F] = 4;
            datatweaks::OnNewMapTablesLoaded();
            CHECK(hpT[0x1E] == 105 && armorT[0x1E] == 11 && basicT[0x1E] == 37 && pierceT[0x1E] == 2 && rangeT[0x1E] == 5 && sightT[0x1E] == 9 &&
                      goldT[0x1E] == 60 && lumberT[0x1E] == 30 && oilT[0x1E] == 50 && buildT[0x1E] == 80,
                  "elven destroyer example (hp %u armor %u basic %u pierce %u range %u sight %u gold %u lumber %u oil %u time %u)", hpT[0x1E],
                  armorT[0x1E], basicT[0x1E], pierceT[0x1E], rangeT[0x1E], sightT[0x1E], goldT[0x1E], lumberT[0x1E], oilT[0x1E], buildT[0x1E]);
            CHECK(pierceT[0x1F] == 0 && armorT[0x1F] == 10 && hpT[0x1F] == 100, "0 must be written, -1 and missing keys must leave the game's value");
            CHECK(reactC[0x1E] == 12 && reactH[0x1E] == 12, "react_range writes both react tables (%u / %u)", reactC[0x1E], reactH[0x1E]);
            CHECK(reactC[0x1F] == 10 && reactH[0x1F] == 8, "a type without react_range keeps the game's react ranges");

            vanillaDestroyer(0x1E);
            config::g.health.all = 2.0;
            config::g.costs.race[kHuman].unit[kNaval] = 0.5;
            datatweaks::OnNewMapTablesLoaded();
            CHECK(hpT[0x1E] == 210 && goldT[0x1E] == 30 && oilT[0x1E] == 25, "multipliers apply on top of the player's base stats (hp %u gold %u oil %u)",
                  hpT[0x1E], goldT[0x1E], oilT[0x1E]);

            // The range table alone does not decide when a unit opens fire: a raised range is followed into both
            // react tables, but only upwards, and never against an explicit react_range.
            resetTables(); resetConfig();
            vanillaDestroyer(0x1E); vanillaDestroyer(0x1F);
            config::g.unitStat[0x1E][kStatRange] = 12;   // above both react ranges: both follow
            config::g.unitStat[0x1F][kStatRange] = 6;    // still below them: they stay as the game made them
            datatweaks::OnNewMapTablesLoaded();
            CHECK(rangeT[0x1E] == 12 && reactC[0x1E] == 12 && reactH[0x1E] == 12,
                  "a range above the react ranges must raise both (range %u react %u / %u)", rangeT[0x1E], reactC[0x1E], reactH[0x1E]);
            CHECK(rangeT[0x1F] == 6 && reactC[0x1F] == 10 && reactH[0x1F] == 8,
                  "a range below them must leave them alone (react %u / %u)", reactC[0x1F], reactH[0x1F]);
            CHECK(LogContains(dir, "unit elven_destroyer: react range computer 10 -> 12 and yours 8 -> 12 to match range 12"),
                  "the raise must name the type and both tables");
            CHECK(!LogContains(dir, "unit troll_destroyer: react range"), "a type that needed no raise must not be logged");

            resetTables(); resetConfig();
            vanillaDestroyer(0x1E);
            config::g.unitStat[0x1E][kStatRange] = 12;
            config::g.unitStat[0x1E][kStatReactRange] = 5;  // the player's own number wins, even below the range
            datatweaks::OnNewMapTablesLoaded();
            CHECK(reactC[0x1E] == 5 && reactH[0x1E] == 5, "react_range must not be lifted by range (%u / %u)", reactC[0x1E], reactH[0x1E]);

            // A tower is the case that made this necessary: the game gives it the same number in all three tables.
            resetTables(); resetConfig();
            rangeT[kGuardTower] = 6; reactC[kGuardTower] = 6; reactH[kGuardTower] = 6;
            config::g.unitStat[kGuardTower][kStatRange] = 9;
            datatweaks::OnNewMapTablesLoaded();
            CHECK(rangeT[kGuardTower] == 9 && reactC[kGuardTower] == 9 && reactH[kGuardTower] == 9,
                  "a tower given more range must also notice from further (range %u react %u / %u)", rangeT[kGuardTower],
                  reactC[kGuardTower], reactH[kGuardTower]);
            CHECK(LogContains(dir, "building human_guard_tower: react range computer 6 -> 9 and yours 6 -> 9 to match range 9"),
                  "a structure is named as a building");

            // Only one of the two tables is below the range: only that one is raised, and only that one is logged.
            resetTables(); resetConfig();
            vanillaDestroyer(0x1E);
            config::g.unitStat[0x1E][kStatRange] = 9;  // computer 10 stays, yours 8 follows
            datatweaks::OnNewMapTablesLoaded();
            CHECK(reactC[0x1E] == 10 && reactH[0x1E] == 9, "only the table below the range moves (%u / %u)", reactC[0x1E],
                  reactH[0x1E]);
            CHECK(LogContains(dir, "unit elven_destroyer: react range yours 8 -> 9 to match range 9"),
                  "and the line says which table it was");
        }

        // [building.<name>] tables use the same stats; a name in the wrong section is refused, not misapplied.
        {
            uint8_t* armorT = At<uint8_t>(kRvaArmorByType);
            uint8_t* pierceT = At<uint8_t>(kRvaPiercingDamageByType);
            resetTables(); resetConfig();
            hpT[kGuardTower] = 130; armorT[kGuardTower] = 20; pierceT[kGuardTower] = 12; sightT[kGuardTower] = 9;
            WriteFileText(ini,
                          "[building.human_guard_tower]\nhit_points = 200\npiercing_damage = 14\nrange = 7\nreact_range = 9\ngold = 450\nbuild_time = -1\n"
                          "[building.farm]\nhit_points = 40000\n"
                          "[unit.ballista]\nreact_range = 21\n"
                          "[unit.keep]\nhit_points = 9\n"
                          "[building.footman]\nhit_points = 9\n"
                          "[health]\nunits = 1.25\nstructures = 1.5\n[health.orc]\nbuildings = 2.0\nstructures = 3.0\n[costs]\nstructures = 0.5\nunits = 0.25\n[oil_platforms]\nunlimited = true\n");
            CHECK(config::Init(dir), "building tables rejected");
            const uint8_t tower = 0x60;
            CHECK(config::g.unitStat[tower][kStatHitPoints] == 200 && config::g.unitStat[tower][kStatPiercingDamage] == 14 &&
                      config::g.unitStat[tower][kStatRange] == 7 && config::g.unitStat[tower][kStatGold] == 450 &&
                      config::g.unitStat[tower][kStatBuildTime] == -1 && config::g.unitStat[tower][kStatReactRange] == 9,
                  "[building.human_guard_tower] did not load");
            CHECK(config::g.unitStat[kFarmT][kStatHitPoints] == -1, "structure hit_points above 32767 must be refused");
            CHECK(config::g.unitStat[4][kStatReactRange] == -1, "react_range above 20 must be refused (%d)",
                  config::g.unitStat[4][kStatReactRange]);
            CHECK(config::g.unitStat[kKeep][kStatHitPoints] == -1 && config::g.unitStat[kFootman][kStatHitPoints] == -1,
                  "a building under [unit.*] or a unit under [building.*] must be refused");
            CHECK(config::g.health.race[kOrc].structure[kBuildings] == 2.0 && config::g.health.structures == 1.5 &&
                      config::g.health.race[kOrc].structures == 3.0 && config::g.costs.structures == 0.5 && config::g.costs.units == 0.25 &&
                      config::g.health.units == 1.25,
                  "structure masters did not load from the file");
            CHECK(config::g.oilPlatformsUnlimited, "[oil_platforms] unlimited did not load from the file");
            datatweaks::OnNewMapTablesLoaded();
            // human guard tower: health 200 x [health] structures 1.5 = 300; gold 450 x [costs] structures 0.5 = 225 -> 23 tens
            CHECK(hpT[kGuardTower] == 300 && pierceT[kGuardTower] == 14 && rangeT[kGuardTower] == 7 && goldT[kGuardTower] == 23 && armorT[kGuardTower] == 20,
                  "guard tower stats (hp %u pierce %u range %u gold %u)", hpT[kGuardTower], pierceT[kGuardTower], rangeT[kGuardTower], goldT[kGuardTower]);
            DeleteFileW(ini);
            CHECK(config::Init(dir), "default config did not come back");
            EnableEverythingForTests();
        }

        resetConfig();
        datatweaks::SyncRangeBonus(false);
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
    CHECK(config::g.isHero[0x19] && !config::g.isHero[kGrunt], "default [heroes] list");
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

    // [unit_regen]: every unit and ship, never a structure; a hero keeps his own rate while [heroes] regen is on; the
    // two rates never add up; regen_for = "mine" leaves the enemy alone.
    {
        ResetWorld();
        defType(0x3A, kTfBuilding, 400);                       // farm
        defType(0x1E, kTfAttacker, 100);                       // elven destroyer: a ship is a unit
        defType(kDragon, kTfFleshy | kTfAttacker | kTfFlyer, 100);
        Unit* hero = AddUnit(0x19, 0, 5, 5, 100, 0, kOrderStand);
        Unit* myGrunt = AddUnit(kGrunt, 0, 7, 5, 10, 0, kOrderStand);
        Unit* myShip = AddUnit(0x1E, 0, 9, 5, 40, 0, kOrderStand);
        Unit* regenDragon = AddUnit(kDragon, 0, 11, 5, 40, 0, kOrderStand);
        Unit* myFarm = AddUnit(0x3A, 0, 13, 5, 200, 0, kOrderStand);
        Unit* foe = AddUnit(kFootman, 1, 15, 5, 10, 0, kOrderStand);
        config::g.heroRegen = true;
        config::g.heroRegenPerSecond = 2;
        config::g.unitRegen = true;
        config::g.unitRegenPerSecond = 1;
        config::g.unitRegenMineOnly = true;
        mod::OnTick();
        for (int i = 0; i < 25; ++i) { Sleep(100); mod::OnTick(); }
        const int gained = Field<uint16_t>(myGrunt, kOffHp) - 10;
        CHECK(gained >= 2 && gained <= 3, "unit regen ~1 HP/s (grunt +%d)", gained);
        CHECK(Field<uint16_t>(myShip, kOffHp) == 40 + gained && Field<uint16_t>(regenDragon, kOffHp) == 40 + gained, "ships and flyers regenerate like any unit (%u, %u)",
              Field<uint16_t>(myShip, kOffHp), Field<uint16_t>(regenDragon, kOffHp));
        CHECK(Field<uint16_t>(hero, kOffHp) == 100 + 2 * gained, "a hero follows [heroes] (2/s), not the sum (%u)", Field<uint16_t>(hero, kOffHp));
        CHECK(Field<uint16_t>(myFarm, kOffHp) == 200, "structures must never regenerate (%u)", Field<uint16_t>(myFarm, kOffHp));
        CHECK(Field<uint16_t>(foe, kOffHp) == 10, "regen_for = \"mine\" must leave the enemy alone (%u)", Field<uint16_t>(foe, kOffHp));
        config::g.heroRegen = false;  // hero switch off: he is a unit like any other now
        const int heroBefore = Field<uint16_t>(hero, kOffHp), gruntBefore = Field<uint16_t>(myGrunt, kOffHp);
        for (int i = 0; i < 15; ++i) { Sleep(100); mod::OnTick(); }
        CHECK(Field<uint16_t>(hero, kOffHp) - heroBefore == Field<uint16_t>(myGrunt, kOffHp) - gruntBefore && Field<uint16_t>(hero, kOffHp) > heroBefore,
              "with [heroes] regen off a hero regenerates at the unit rate (hero +%d grunt +%d)", Field<uint16_t>(hero, kOffHp) - heroBefore,
              Field<uint16_t>(myGrunt, kOffHp) - gruntBefore);
        config::g.unitRegen = false;
        config::g.unitRegenMineOnly = false;
        const int off = Field<uint16_t>(myGrunt, kOffHp);
        for (int i = 0; i < 12; ++i) { Sleep(100); mod::OnTick(); }
        CHECK(Field<uint16_t>(myGrunt, kOffHp) == off, "both switches off: nobody regenerates (%u)", Field<uint16_t>(myGrunt, kOffHp));
        config::g.heroRegen = true;   // what the later scenarios expect (EnableEverythingForTests)
        config::g.heroRegenPerSecond = 1;
    }

    // Tree regrowth ([trees], src/trees.cpp). Play time goes straight into trees::OnTick in 250 ms steps, so minutes
    // cost nothing here. Forests are built the way the map editor builds them (forest corner points), trees are felled
    // with a port of the game's own removal code, and the mod has to put them back without breaking anything.
    {
        g_regionT = regionMap;
        *At<uint16_t*>(kRvaTileMap) = g_tileMap;
        *At<uint16_t*>(kRvaSquareFlags) = g_sqMap;
        *At<uint16_t*>(kRvaRegionMap) = regionMap;
        *At<const uint16_t*>(kRvaTreeTable) = kForestTreeTable;
        *At<uint16_t>(kRvaTreeTableStride) = 10;
        *At<uint16_t>(kRvaTreeTileCount) = kForestTreeTiles;
        *At<uint16_t>(kRvaTreeBase) = kTreeBaseT;
        *At<uint16_t>(kRvaGameFromSave) = 0;

        // The tree removal callback FUN_004eb400 is where every address and the three writes were read from. Absolute
        // operands are relocated in this image, so they are compared against the image base.
        {
            auto abs32 = [&](uint32_t rva) { uint32_t v; memcpy(&v, At<uint8_t>(rva), 4); return v; };
            auto bytes = [&](uint32_t rva, const char* expect, size_t n) { return memcmp(At<uint8_t>(rva), expect, n) == 0; };
            CHECK(bytes(0xEB400, "\x55\x8B\xEC\x8B\x45\x08\xB9\xFE\xFF\x00\x00", 11), "FUN_004eb400 prologue (mov ecx, 0xFFFE)");
            CHECK(bytes(0xEB42B, "\x8B\x35", 2) && abs32(0xEB42D) == g_base + kRvaTileMap, "tile id map pointer is not read at 0x4EB42B");
            CHECK(bytes(0xEB436, "\x8B\x1D", 2) && abs32(0xEB438) == g_base + kRvaTreeTable, "tree table pointer is not read at 0x4EB436");
            CHECK(bytes(0xEB449, "\x0F\xB7\x05", 3) && abs32(0xEB44C) == g_base + kRvaTreeTableStride, "table stride is not read at 0x4EB449");
            CHECK(bytes(0xEB450, "\x66\x2B\x0D", 3) && abs32(0xEB453) == g_base + kRvaTreeBase, "tree base is not subtracted at 0x4EB450");
            CHECK(bytes(0xEB49B, "\x66\x89\x06", 3), "tile id write is not at 0x4EB49B");
            CHECK(bytes(0xEB4A9, "\xA1", 1) && abs32(0xEB4AA) == g_base + kRvaSquareFlags && bytes(0xEB4AE, "\xB9\x7F\xFF\x00\x00\x66\x21\x0C\x02", 9),
                  "square flags &= 0xFF7F is not at 0x4EB4A9..0x4EB4B3");
            CHECK(bytes(0xEB4B7, "\x8B\x0D", 2) && abs32(0xEB4B9) == g_base + kRvaRegionMap && bytes(0xEB4C3, "\x66\x89\x04\x0A", 4),
                  "region word write is not at 0x4EB4B7..0x4EB4C3");
        }

        static TerrainSnap pristine, before;
        static int land4[kMap * kMap], land8[kMap * kMap], after4[kMap * kMap], after8[kMap * kMap];
        auto at = [](int x, int y) { return y * kMap + x; };
        auto freshMap = [&] {
            ResetWorld();
            memset(g_vertex, 0, sizeof(g_vertex));
            AddUnit(kFootman, 0, 60, 60, 60, 0, kOrderStand);  // BuildWorld wants at least one unit
        };
        auto finishMap = [&] {
            BuildTerrain();
            g_sqMap[at(60, 60)] |= 0x0100;
            trees::OnNewMap();
        };
        auto grow = [&](unsigned ms) {
            World w;
            if (!BuildWorld(w)) {
                CHECK(false, "BuildWorld failed in the tree tests");
                return;
            }
            for (unsigned t = 0; t < ms; t += 250) trees::OnTick(w, 250);
        };
        auto isStump = [&](int x, int y) { return g_tileMap[at(x, y)] == kStumpT; };

        // Off by default: not one terrain word changes, however long the game runs.
        CHECK(!config::g.treesRegrow && config::g.treesRegrowMinMinutes == 10 && config::g.treesRegrowMaxMinutes == 20 &&
                  config::g.treesBuildingDistance == 3 && config::g.treesUnitDistance == 3,
              "[trees] defaults");
        trees::SeedForTests(20260919);  // every stump draws its wait from the module's generator: make the run repeatable
        freshMap();
        ForestVertices(10, 10, 20, 20);  // tree tiles 9..20, solid forest 10..19
        finishMap();
        TakeSnap(pristine);
        CHECK(g_tileMap[at(15, 15)] == kSolidT && g_tileMap[at(9, 15)] == kTreeBaseT + 2 - 1 && g_tileMap[at(8, 15)] == kGrassT, "test forest layout");
        FakeFell(15, 15);
        CHECK(isStump(15, 15) && g_sqMap[at(15, 15)] == 0x0001 && regionMap[at(15, 15)] == kLandT && g_tileMap[at(15, 14)] == kTreeBaseT + 23 - 1,
              "the felling port must clear the tile and re-edge its neighbours (tile above is %04X)", g_tileMap[at(15, 14)]);
        TakeSnap(before);
        for (int i = 0; i < 8; ++i) mod::OnTick();
        grow(30 * 60000);
        CHECK(SameAsSnap(before), "[trees] regrow is off by default: no terrain word may change");

        // A hole in solid forest, min = max = 10 minutes (a fixed wait): nothing before that, then exactly the three
        // writes (tile id, flag 0x80, region 0xFFFE) and the eight neighbours get their edges back, which is the map as
        // it was before the felling.
        config::g.treesRegrow = true;
        config::g.treesRegrowMinMinutes = config::g.treesRegrowMaxMinutes = 10;
        grow(599000);
        CHECK(SameAsSnap(before), "a stump must wait regrow_min_minutes (10) from the moment it was first seen");
        grow(11000);
        CHECK(g_tileMap[at(15, 15)] == kSolidT && g_sqMap[at(15, 15)] == 0x0081 && regionMap[at(15, 15)] == kRegionTree,
              "the stump should be solid forest again (tile %04X flags %04X region %04X)", g_tileMap[at(15, 15)], g_sqMap[at(15, 15)], regionMap[at(15, 15)]);
        CHECK(CheckRegrowth(before, "hole in a forest") == 1 && SameAsSnap(pristine), "fell + regrow must give the original forest back");

        // A maximum below the minimum counts as the minimum.
        config::g.treesRegrowMinMinutes = 2;
        config::g.treesRegrowMaxMinutes = 1;
        trees::OnNewMap();
        FakeFell(15, 15);
        TakeSnap(before);
        grow(119000);
        CHECK(SameAsSnap(before), "regrow_max_minutes below regrow_min_minutes must behave as the minimum (2): nothing before it");
        grow(11000);
        CHECK(SameAsSnap(pristine), "regrow_max_minutes below regrow_min_minutes: the stump must be back right after the minimum");

        // A range: every stump draws its own wait between min and max, so a felled patch fills back in bit by bit. Five
        // single holes (each regrows exactly when its own wait is over) and a 3 x 3 patch (a tile there also needs its
        // neighbours around one corner to be due), fixed seed. Nothing before 10 minutes, everything by 20 minutes plus
        // one sweep to be seen and one to be visited, and not all at once.
        {
            config::g.treesRegrowMinMinutes = 10;
            config::g.treesRegrowMaxMinutes = 20;
            freshMap();
            ForestVertices(10, 10, 30, 30);
            finishMap();
            TakeSnap(pristine);
            int watched[14], regrewAt[14], count = 0;
            for (int k = 0; k < 5; ++k) watched[count++] = at(12 + 3 * k, 12);
            for (int y = 18; y <= 20; ++y)
                for (int x = 14; x <= 16; ++x) watched[count++] = at(x, y);
            for (int k = 0; k < count; ++k) {
                FakeFell(watched[k] % kMap, watched[k] / kMap);
                regrewAt[k] = 0;
            }
            int earliest = 0, latest = 0, holeTimes = 0;
            for (int second = 1; second <= 20 * 60 + 8; ++second) {
                grow(1000);
                for (int k = 0; k < count; ++k)
                    if (!regrewAt[k] && g_tileMap[watched[k]] != kStumpT) {
                        regrewAt[k] = second;
                        if (!earliest) earliest = second;
                        latest = second;
                    }
            }
            int patchTimes = 0;
            for (int k = 0; k < count; ++k) {
                bool repeat = false;
                for (int j = k < 5 ? 0 : 5; j < k; ++j) repeat = repeat || regrewAt[j] == regrewAt[k];
                (k < 5 ? holeTimes : patchTimes) += !repeat;
            }
            CHECK(earliest >= 600, "min 10 / max 20: a stump regrew after %d s, before the minimum", earliest);
            CHECK(SameAsSnap(pristine), "min 10 / max 20: every stump must be back 8 s after the maximum (last one at %d s)", latest);
            CHECK(holeTimes >= 2 && patchTimes >= 2 && latest - earliest >= 60,
                  "min 10 / max 20: stumps felled together must not all come back together (%d distinct times among the holes, %d in the patch, %d..%d s)",
                  holeTimes, patchTimes, earliest, latest);
            printf("tree regrowth range 10..20 min: first stump back at %d s, last at %d s, distinct times: %d of 5 single holes, %d in the 3x3 patch\n",
                   earliest, latest, holeTimes, patchTimes);
        }
        config::g.treesRegrowMinMinutes = config::g.treesRegrowMaxMinutes = 1;  // the scenarios below want a short, fixed wait

        // Every tile waits for its OWN timer: a neighbour felled 40 s later is not taken along early. The first stump
        // comes back as a left half, the second one completes it. A tree being felled next door (0xFFFC) keeps that word.
        freshMap();
        ForestVertices(10, 10, 20, 20);
        finishMap();
        regionMap[at(14, 14)] = kRegionChopping;
        TakeSnap(pristine);
        FakeFell(15, 15);
        grow(40000);
        FakeFell(16, 15);
        grow(30000);
        CHECK(g_tileMap[at(15, 15)] == kTreeBaseT + 8 - 1 && isStump(16, 15), "70 s: first stump back as a left half (%04X), second still a stump (%04X)",
              g_tileMap[at(15, 15)], g_tileMap[at(16, 15)]);
        grow(40000);
        CHECK(SameAsSnap(pristine), "110 s: both stumps back, forest as before, the 0xFFFC neighbour untouched (%04X)", regionMap[at(14, 14)]);

        // [trees] building_distance. The building stands on the grass west of the forest; SQ 0x800 is what every tile of
        // a footprint carries, a finished wall carries 0x04 / 0x08.
        freshMap();
        ForestVertices(10, 10, 20, 20);
        finishMap();
        FakeFell(11, 15);
        g_sqMap[at(8, 15)] = 0x0801;  // 3 tiles from the stump
        TakeSnap(before);
        grow(3 * 60000);
        CHECK(SameAsSnap(before), "no regrowth within building_distance (3) of a building");
        g_sqMap[at(8, 15)] = 0x0095;  // a wall instead
        TakeSnap(before);
        grow(2 * 60000);
        CHECK(SameAsSnap(before), "no regrowth within building_distance (3) of a wall");
        g_sqMap[at(8, 15)] = 0x0001;
        g_sqMap[at(7, 15)] = 0x0801;  // 4 tiles away
        TakeSnap(before);
        grow(60000);
        CHECK(!isStump(11, 15) && CheckRegrowth(before, "building distance") == 1, "a stump 4 tiles from a building must regrow");

        // The distance is configurable. A forest-edge tile 2 tiles from a building; regrown, it is the edge state again.
        freshMap();
        ForestVertices(10, 10, 20, 20);
        finishMap();
        TakeSnap(pristine);
        FakeFell(9, 15);
        g_sqMap[at(7, 15)] = 0x0801;
        pristine.sq[at(7, 15)] = 0x0801;
        config::g.treesBuildingDistance = 2;
        TakeSnap(before);
        grow(2 * 60000);
        CHECK(SameAsSnap(before), "building_distance = 2 must block a stump 2 tiles away");
        config::g.treesBuildingDistance = 1;
        grow(60000);
        CHECK(SameAsSnap(pristine), "building_distance = 1: the edge tile grows back as the same right-half edge (%04X)", g_tileMap[at(9, 15)]);
        config::g.treesBuildingDistance = 3;

        // Never on an occupied or marked tile. One stump, one obstacle after the other, each given two minutes.
        {
            freshMap();
            ForestVertices(10, 10, 20, 20);
            finishMap();
            FakeFell(15, 15);
            const int s = at(15, 15);
            defType(kPeon, kTfFleshy | kTfWorker, 30);
            Unit* peon2 = AddUnit(kPeon, 0, 15, 15, 30, 0, kOrderStop);  // in the ground grid, flag not set
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "regrew under a unit that is in the ground grid");
            g_grid[s] = nullptr;
            Field<int16_t>(peon2, kOffX) = 61;  // walked off
            Field<int16_t>(peon2, kOffY) = 60;
            g_grid[at(61, 60)] = peon2;
            const uint16_t marks[] = {0x0100, 0x0400, 0x0800, 0x0010, 0x0002};
            for (uint16_t mark : marks) {
                g_sqMap[s] = static_cast<uint16_t>(0x0001 | mark);
                TakeSnap(before);
                grow(2 * 60000);
                CHECK(SameAsSnap(before), "regrew on a tile carrying square flag %04X", mark);
            }
            g_sqMap[s] = 0x0001;
            Unit* body = AddUnit(kTypeCorpse, 0, 15, 15, 0, 0, 0);  // corpses are in neither grid
            g_grid[s] = nullptr;
            Field<uint8_t>(body, kOffStateFlags) = kStateDying;
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "regrew on a corpse");
            Field<uint8_t>(body, kOffStateFlags) = 4;  // decayed: the slot is dead, its coordinates are stale
            regionMap[s] = 0x0003;                        // not a land region
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "regrew on a tile whose region word is not land");
            regionMap[s] = kLandT;
            TakeSnap(before);
            grow(60000);
            CHECK(!isStump(15, 15) && CheckRegrowth(before, "obstacles gone") == 1, "with every obstacle gone the stump must regrow");
        }

        // [trees] unit_distance: ground units keep regrowth away, flyers do not. The game files a ground unit on its tile
        // with a pointer in the first unit grid AND square flag 0x100; either one must count.
        {
            freshMap();
            ForestVertices(10, 10, 20, 20);
            finishMap();
            TakeSnap(pristine);
            FakeFell(11, 15);
            Unit* footman = AddUnit(kFootman, 0, 8, 15, 60, 0, kOrderStand);  // on the grass, 3 tiles from the stump
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "no regrowth within unit_distance (3) of a ground unit (ground grid entry)");
            g_grid[at(8, 15)] = nullptr;  // the same unit known by its square flag only
            Field<int16_t>(footman, kOffX) = 50;
            Field<int16_t>(footman, kOffY) = 50;
            g_sqMap[at(8, 15)] |= 0x0100;
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "no regrowth within unit_distance (3) of a ground unit (square flag 0x100)");
            g_sqMap[at(8, 15)] = 0x0001;  // one step west: 4 tiles away, filed both ways
            Field<int16_t>(footman, kOffX) = 7;
            Field<int16_t>(footman, kOffY) = 15;
            g_grid[at(7, 15)] = footman;
            g_sqMap[at(7, 15)] |= 0x0100;
            pristine.sq[at(7, 15)] |= 0x0100;
            grow(60000);
            CHECK(SameAsSnap(pristine), "a stump 4 tiles from a ground unit must regrow");

            // Flyers cross forest anyway: one directly overhead and one next door change nothing.
            FakeFell(11, 15);
            Unit* overhead = AddUnit(kDragon, 0, 11, 15, 100, 0, kOrderStand);
            AddUnit(kDragon, 0, 12, 15, 100, 0, kOrderStand);
            CHECK(g_airGrid[at(11, 15)] == overhead && !g_grid[at(11, 15)] && g_airGrid[at(12, 15)], "test setup: flyers are filed in the air grid only");
            g_sqMap[at(11, 15)] |= 0x0200;
            g_sqMap[at(12, 15)] |= 0x0200;
            pristine.sq[at(11, 15)] |= 0x0200;
            pristine.sq[at(12, 15)] |= 0x0200;
            grow(70000);
            CHECK(SameAsSnap(pristine), "a flyer overhead or next door must not hold regrowth up (tile %04X flags %04X)", g_tileMap[at(11, 15)], g_sqMap[at(11, 15)]);

            // unit_distance = 0: only the unit's own tile is off limits.
            FakeFell(15, 15);
            FakeFell(16, 15);
            AddUnit(kGrunt, 0, 16, 15, 60, 0, kOrderStand);
            g_sqMap[at(16, 15)] |= 0x0100;
            config::g.treesUnitDistance = 1;
            TakeSnap(before);
            grow(2 * 60000);
            CHECK(SameAsSnap(before), "unit_distance = 1 must block the stump next to a ground unit");
            config::g.treesUnitDistance = 0;
            grow(60000);
            CHECK(g_tileMap[at(15, 15)] == kTreeBaseT + 8 - 1 && isStump(16, 15),
                  "unit_distance = 0: the stump next to the unit regrows (as a left half, %04X), the unit's own tile never does (%04X)",
                  g_tileMap[at(15, 15)], g_tileMap[at(16, 15)]);
            config::g.treesUnitDistance = 3;
        }

        // The passage rule. A forest belt cuts the map in two and a path one tile wide was chopped through it: that path
        // must stay open forever, a wider spot may shrink back to one tile, and east must stay reachable from west.
        freshMap();
        ForestVertices(10, 0, 20, kMap);
        finishMap();
        for (int x = 9; x <= 20; ++x) FakeFell(x, 15);
        for (int x = 12; x <= 14; ++x) FakeFell(x, 16);  // a wider spot
        LabelOpenLand(land4, false);
        LabelOpenLand(land8, true);
        CHECK(land4[at(3, 30)] == land4[at(40, 30)], "test setup: the chopped path must connect west and east");
        TakeSnap(before);
        grow(10 * 60000);
        LabelOpenLand(after4, false);
        LabelOpenLand(after8, true);
        CHECK(after4[at(3, 30)] > 0 && after4[at(3, 30)] == after4[at(40, 30)], "regrowth closed the only path through the forest");
        CHECK(StillConnected(land4, after4) && StillConnected(land8, after8), "regrowth cut an area off");
        {
            bool pathStumps = true;
            for (int x = 9; x <= 20; ++x) pathStumps = pathStumps && isStump(x, 15);
            CHECK(pathStumps, "every tile of the one-tile path must still be a stump");
            CHECK(CheckRegrowth(before, "path through a forest") == 3, "the wider spot should have grown back to a one-tile path");
        }

        // Dead-end pockets (stumps enclosed by forest) close from their ends and leave the forest as it was. Stumps are
        // taken along in pairs, so an even pocket ends on a tile that never has a turn of its own: its far corners must
        // be completed on the spot, or the forest keeps a scar.
        freshMap();
        ForestVertices(10, 0, 20, kMap);
        finishMap();
        TakeSnap(pristine);
        for (int x = 12; x <= 16; ++x) FakeFell(x, 40);
        for (int x = 12; x <= 15; ++x) FakeFell(x, 44);
        for (int y = 48; y <= 51; ++y) FakeFell(14, y);
        grow(2 * 60000);
        CHECK(SameAsSnap(pristine), "enclosed pockets must grow back completely");

        // Timers are not persisted; a new map or a loaded game starts them over, a pause does not.
        freshMap();
        ForestVertices(10, 10, 20, 20);
        finishMap();
        TakeSnap(pristine);
        FakeFell(15, 15);
        TakeSnap(before);
        grow(50000);
        datatweaks::OnNewMapTablesLoaded();  // the new-map hook
        grow(30000);
        CHECK(SameAsSnap(before), "the new-map hook must start every stump timer over");
        grow(45000);
        CHECK(SameAsSnap(pristine), "75 s after the new map the stump must be back");

        FakeFell(15, 15);
        grow(50000);
        Sleep(600);  // a pause
        grow(20000);
        CHECK(SameAsSnap(pristine), "a pause must not reset the stump timers");

        FakeFell(12, 12);
        FakeFell(15, 15);
        grow(50000);
        memcpy(g_tileMap, pristine.tile, sizeof(g_tileMap));  // "load an earlier save" into the same buffers: 12,12 is a
        memcpy(g_sqMap, pristine.sq, sizeof(g_sqMap));        // tree again, 15,15 was already felled in that save
        memcpy(regionMap, pristine.region, sizeof(pristine.region));
        FakeFell(15, 15);
        TakeSnap(before);
        Sleep(600);  // every load is a long gap between two steps
        grow(30000);
        CHECK(SameAsSnap(before), "a loaded game must start the stump timers over (a timer sat on a tile that is no stump)");
        grow(45000);
        CHECK(SameAsSnap(pristine), "75 s after the load the stump must be back");

        // A failed sanity check switches the feature off for that map: no write, no crash. The next map gets a new look.
        {
            FakeFell(15, 15);
            TakeSnap(before);
            static uint16_t oddTable[sizeof(kForestTreeTable) / sizeof(kForestTreeTable[0])];
            memcpy(oddTable, kForestTreeTable, sizeof(oddTable));
            oddTable[24 * 10 + 1] ^= 1;  // one transition of the solid state differs
            for (int broken = 0; broken < 6; ++broken) {
                const char* what = "";
                switch (broken) {
                    case 0: *At<uint16_t>(kRvaTreeTableStride) = 9; what = "row length 9"; break;
                    case 1: *At<const uint16_t*>(kRvaTreeTable) = nullptr; what = "no table"; break;
                    case 2: *At<const uint16_t*>(kRvaTreeTable) = oddTable; what = "unknown table"; break;
                    case 3: *At<uint16_t>(kRvaTreeBase) = 0x67; what = "tree base 0x67"; break;
                    case 4: *At<uint16_t>(kRvaTreeTileCount) = 20; what = "20 tree tiles"; break;
                    case 5: *At<uint16_t>(kRvaMapSize) = 200; what = "map size 200"; break;
                }
                trees::OnNewMap();
                grow(3 * 60000);
                CHECK(SameAsSnap(before), "sanity check \"%s\" must switch regrowth off for the map", what);
                *At<uint16_t>(kRvaTreeTableStride) = 10;
                *At<const uint16_t*>(kRvaTreeTable) = kForestTreeTable;
                *At<uint16_t>(kRvaTreeBase) = kTreeBaseT;
                *At<uint16_t>(kRvaTreeTileCount) = kForestTreeTiles;
                *At<uint16_t>(kRvaMapSize) = kMap;
            }
            trees::OnNewMap();
            grow(70000);
            CHECK(SameAsSnap(pristine), "the next map must work again after a failed sanity check");
        }

        // Random forests (fixed seed), art variants included, random felling, buildings and units in the way. After
        // every round: only valid tree ids anywhere, the three writes and nothing else, never-forest land untouched, no
        // area cut off, nothing near a building or on a unit. Felling the regrown forest again runs the game's table
        // lookup over every state the mod wrote.
        {
            uint32_t seed = 20260919;
            auto rnd = [&](int n) {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<int>((seed >> 8) % static_cast<uint32_t>(n));
            };
            static const uint8_t kVariantOf[kForestTreeTiles + 1] = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                     0,  0,  0,  0,  0,  24, 24, 9,  1,  23, 6, 8, 5, 2, 3, 7, 13, 13, 12, 12};
            static int order[kMap * kMap];
            auto fellRandomTrees = [&](int oneIn) {
                int count = 0;
                for (int i = 0; i < kMap * kMap; ++i)
                    if (regionMap[i] == kRegionTree) order[count++] = i;
                for (int k = count - 1; k > 0; --k) {
                    const int j = rnd(k + 1), t = order[k];
                    order[k] = order[j];
                    order[j] = t;
                }
                for (int k = 0; k < count / oneIn; ++k)
                    if (regionMap[order[k]] == kRegionTree) FakeFell(order[k] % kMap, order[k] / kMap);
            };
            int totalGrown = 0, nearBuilding = 0, nearUnit = 0;
            config::g.treesRegrowMinMinutes = 1;  // a range, so the random waits are part of what is checked
            config::g.treesRegrowMaxMinutes = 2;
            config::g.treesUnitDistance = 2;      // not the building distance (3): the two boxes are separate rules
            for (int trial = 0; trial < 20; ++trial) {
                freshMap();
                for (int blob = rnd(5) + 3; blob > 0; --blob) {
                    const int x0 = rnd(kMap), y0 = rnd(kMap);
                    const int x1 = x0 + rnd(14), y1 = y0 + rnd(14);
                    ForestVertices(x0, y0, x1 > kMap ? kMap : x1, y1 > kMap ? kMap : y1);
                }
                finishMap();
                for (int i = 0; i < kMap * kMap; ++i) {  // the map editor sprinkles art variants
                    if (regionMap[i] != kRegionTree || rnd(3)) continue;
                    const int state = g_tileMap[i] - kTreeBaseT + 1;
                    for (int v = 26; v <= kForestTreeTiles; ++v)
                        if (kVariantOf[v] == state && rnd(2)) g_tileMap[i] = static_cast<uint16_t>(kTreeBaseT + v - 1);
                }
                fellRandomTrees(2);
                for (int k = 0; k < 4; ++k) {  // buildings on open land
                    const int i = rnd(kMap * kMap);
                    if (g_sqMap[i] == 0x0001 && g_tileMap[i] == kGrassT) g_sqMap[i] = 0x0801;
                }
                for (int k = 0; k < 5; ++k) {  // units, some of them standing on stumps
                    const int i = rnd(kMap * kMap);
                    if (g_sqMap[i] != 0x0001 || g_grid[i]) continue;
                    AddUnit(kGrunt, 1, i % kMap, i / kMap, 60, 0, kOrderStand);
                    g_sqMap[i] |= 0x0100;
                }
                for (int round = 0; round < 2; ++round) {
                    TakeSnap(before);
                    LabelOpenLand(land4, false);
                    LabelOpenLand(land8, true);
                    grow(4 * 60000);
                    totalGrown += CheckRegrowth(before, "random forest");
                    LabelOpenLand(after4, false);
                    LabelOpenLand(after8, true);
                    CHECK(StillConnected(land4, after4) && StillConnected(land8, after8), "random forest %d round %d: an area was cut off", trial, round);
                    for (int i = 0; i < kMap * kMap; ++i) {
                        if (before.tile[i] != kStumpT || g_tileMap[i] == kStumpT) continue;
                        for (int dy = -3; dy <= 3; ++dy)
                            for (int dx = -3; dx <= 3; ++dx) {
                                const int x = i % kMap + dx, y = i / kMap + dy;
                                if (x < 0 || y < 0 || x >= kMap || y >= kMap) continue;
                                if (g_sqMap[at(x, y)] & 0x080C) ++nearBuilding;
                                if (abs(dx) <= 2 && abs(dy) <= 2 && ((g_sqMap[at(x, y)] & 0x0100) || g_grid[at(x, y)])) ++nearUnit;
                            }
                    }
                    fellRandomTrees(3);  // every state the mod wrote goes through the game's table lookup
                }
            }
            CHECK(nearBuilding == 0 && nearUnit == 0, "random forests: %d regrown tile(s) within 3 of a building, %d within 2 of a ground unit", nearBuilding, nearUnit);
            CHECK(totalGrown > 1000, "random forests: only %d tiles grew back, the stress test is not exercising regrowth", totalGrown);
            printf("tree regrowth stress: %d tiles grew back over 20 random forests\n", totalGrown);
        }

        config::g.treesRegrow = false;
        config::g.treesRegrowMinMinutes = 10;
        config::g.treesRegrowMaxMinutes = 20;
        config::g.treesUnitDistance = 3;
        ResetWorld();
    }

    AiWatchTests();
    AiJobsTests();
    FarmTests();
    LiveStatsTests(dir);
    SpellNumberTests(dir, ini);
    UpgradeTests(dir, ini);
    DamageTypeTests(dir, ini);
    HealCooldownTests(dir, ini);
    ResumeOrderTests(dir);
    ComputerPaladinTests(dir);
    ProductionTests(dir, ini);
    ScoutTests(dir, ini);
    DodgeTests(dir);
    AreaValueTests(dir, ini);
    ReaimTests(dir);

    // TOML config: a custom file is honoured, typos and bad values are survivable, a syntax error keeps old settings.
    WriteFileText(ini,
                  "[general]\ntoggle_key = \"F7\"\ninterval_ticks = 3\nbogus_key = 1\n"
                  "[autocast]\nchannel_mana_reserve = 300\narea_min_enemies = 4\nfireball_min_enemies = 1\n"
                  "[spells]\nheal = false\nunholy_armor = true\npolymorph = true\nblizzard = true\nruns = true\nrunes = true\n"
                  "[heal]\nmin_missing_hp = 25\n"
                  "[polymorph]\ntargets = [\"grunt\", \"not_a_unit\", \"dragon\"]\n"
                  "[haste]\nflyers_only = false\n"
                  "[trees]\nregrow = true\nregrow_min_minutes = 7\nregrow_max_minutes = 3\nbuilding_distance = 99\nunit_distance = 0\n");
    CHECK(config::Init(dir), "valid custom toml rejected");
    CHECK(config::g.treesRegrow && config::g.treesRegrowMinMinutes == 7 && config::g.treesRegrowMaxMinutes == 7 &&
              config::g.treesBuildingDistance == 10 && config::g.treesUnitDistance == 0,
          "[trees] section (regrow %d, minutes %d..%d: a maximum below the minimum becomes the minimum; distances %d / %d: 99 is clamped to 10)",
          config::g.treesRegrow, config::g.treesRegrowMinMinutes, config::g.treesRegrowMaxMinutes, config::g.treesBuildingDistance,
          config::g.treesUnitDistance);
    CHECK(config::g.toggleKey == VK_F7 && config::g.intervalTicks == 3, "general section not applied");
    CHECK(!config::g.spell[kSpellHeal] && config::g.spell[kSpellUnholyArmor] && config::g.spell[kSpellSlow], "spells section");
    CHECK(config::g.spell[kSpellBlizzard] && config::g.spell[kSpellRunes] && !config::g.spell[kSpellFireball] &&
              !config::g.spell[kSpellHolyVision],
          "later spell switches from [spells] (a misspelled key must not switch anything on)");
    CHECK(config::g.channelManaReserve == 255 && config::g.areaMinEnemies == 4 && config::g.fireballMinEnemies == 1,
          "[autocast] channel_mana_reserve (300 is clamped to 255) / area_min_enemies / fireball_min_enemies (%d / %d / %d)",
          config::g.channelManaReserve, config::g.areaMinEnemies, config::g.fireballMinEnemies);
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
