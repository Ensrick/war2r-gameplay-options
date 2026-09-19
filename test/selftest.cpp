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
// feature, so they switch everything on; unholy armor stays off because several scenarios rely on that.
static void EnableEverythingForTests() {
    for (int i = 0; i < kSpellCount; ++i) config::g.spell[i] = i != kSpellUnholyArmor;
    config::g.eyeCast = config::g.eyeAutoScout = true;
    config::g.workerAutoHarvest = config::g.workerAutoRepair = true;
    config::g.heroRegen = true;
    config::g.heroRegenPerSecond = 1;
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
    wcscat_s(dir, L"war2r_gameplay_options_selftest");
    CreateDirectoryW(dir, nullptr);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\gameplay_options.toml", dir);
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
        CHECK(!config::g.eyeCast && !config::g.eyeAutoScout && !config::g.workerAutoHarvest && config::g.workerAutoRepair &&
                  !config::g.heroRegen && config::g.heroRegenPerSecond == 2 && !config::g.unitRegen && config::g.unitRegenPerSecond == 1 && !config::g.goldMinesUnlimited && !config::g.oilPlatformsUnlimited && config::g.rangeUpgradeBonus == 1,
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
            auto vanillaDestroyer = [&](uint8_t t) {
                hpT[t] = 100; armorT[t] = 10; basicT[t] = 35; pierceT[t] = 0; rangeT[t] = 4; sightT[t] = 8;
                goldT[t] = 70; lumberT[t] = 35; oilT[t] = 70; buildT[t] = 90;
            };
            resetTables(); resetConfig();
            vanillaDestroyer(0x1E); vanillaDestroyer(0x1F);
            const int example[kStatCount] = {105, 11, 37, 2, 5, 9, 600, 300, 500, 80};
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

            vanillaDestroyer(0x1E);
            config::g.health.all = 2.0;
            config::g.costs.race[kHuman].unit[kNaval] = 0.5;
            datatweaks::OnNewMapTablesLoaded();
            CHECK(hpT[0x1E] == 210 && goldT[0x1E] == 30 && oilT[0x1E] == 25, "multipliers apply on top of the player's base stats (hp %u gold %u oil %u)",
                  hpT[0x1E], goldT[0x1E], oilT[0x1E]);
        }

        // [building.<name>] tables use the same stats; a name in the wrong section is refused, not misapplied.
        {
            uint8_t* armorT = At<uint8_t>(kRvaArmorByType);
            uint8_t* pierceT = At<uint8_t>(kRvaPiercingDamageByType);
            resetTables(); resetConfig();
            hpT[kGuardTower] = 130; armorT[kGuardTower] = 20; pierceT[kGuardTower] = 12; sightT[kGuardTower] = 9;
            WriteFileText(ini,
                          "[building.human_guard_tower]\nhit_points = 200\npiercing_damage = 14\nrange = 7\ngold = 450\nbuild_time = -1\n"
                          "[building.farm]\nhit_points = 40000\n"
                          "[unit.keep]\nhit_points = 9\n"
                          "[building.footman]\nhit_points = 9\n"
                          "[health]\nunits = 1.25\nstructures = 1.5\n[health.orc]\nbuildings = 2.0\nstructures = 3.0\n[costs]\nstructures = 0.5\nunits = 0.25\n[oil_platforms]\nunlimited = true\n");
            CHECK(config::Init(dir), "building tables rejected");
            const uint8_t tower = 0x60;
            CHECK(config::g.unitStat[tower][kStatHitPoints] == 200 && config::g.unitStat[tower][kStatPiercingDamage] == 14 &&
                      config::g.unitStat[tower][kStatRange] == 7 && config::g.unitStat[tower][kStatGold] == 450 &&
                      config::g.unitStat[tower][kStatBuildTime] == -1,
                  "[building.human_guard_tower] did not load");
            CHECK(config::g.unitStat[kFarmT][kStatHitPoints] == -1, "structure hit_points above 32767 must be refused");
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

    // TOML config: a custom file is honoured, typos and bad values are survivable, a syntax error keeps old settings.
    WriteFileText(ini,
                  "[general]\ntoggle_key = \"F7\"\ninterval_ticks = 3\nbogus_key = 1\n"
                  "[spells]\nheal = false\nunholy_armor = true\npolymorph = true\n"
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
