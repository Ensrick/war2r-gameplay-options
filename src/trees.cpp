#include "trees.h"

#include <windows.h>
#include <cstring>

#include "config.h"
#include "log.h"

using namespace game;

namespace trees {

namespace {

// The game has no "place a tree" code, only removal: FUN_004eb400 looks the felled tile and its 8 neighbours up in the
// tileset's removal table and writes three maps (tile id, square flag 0x80, region word). Growing a tree back is the
// same three writes in reverse. The table is a 4-corner automaton: every tree tile holds a set of forest corners, and
// felling a tile clears the corners its neighbours share with it. The inverse is "paint one corner point": the up to
// four tiles around the point each gain that corner.
constexpr uint8_t kTL = 1, kTR = 2, kBL = 4, kBR = 8;

// Rows 0..25 of n?_tree.bin, byte-identical in all four tilesets. A map whose loaded table differs is left alone.
constexpr uint16_t kCanonicalRows[kTreeStateCleared + 1][kTreeTableStride] = {
    {0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009},
    {0x0001, 0x0001, 0x0001, 0x0001, 0x0016, 0x1019, 0x0001, 0x0016, 0x1019, 0x0001},
    {0x0002, 0x0010, 0x0001, 0x0002, 0x0015, 0x1019, 0x0002, 0x000E, 0x0003, 0x0002},
    {0x0003, 0x0014, 0x1019, 0x0003, 0x0014, 0x1019, 0x0003, 0x0003, 0x0003, 0x0003},
    {0x0004, 0x000D, 0x0001, 0x0002, 0x000F, 0x1019, 0x0002, 0x0012, 0x0005, 0x0004},
    {0x0005, 0x0006, 0x1019, 0x0003, 0x0006, 0x1019, 0x0003, 0x0005, 0x0005, 0x0005},
    {0x0006, 0x0006, 0x1019, 0x0014, 0x0006, 0x1019, 0x0014, 0x0006, 0x0006, 0x0006},
    {0x0007, 0x0008, 0x0009, 0x000C, 0x0008, 0x1019, 0x000E, 0x0007, 0x0005, 0x0012},
    {0x0008, 0x0008, 0x0009, 0x0011, 0x0008, 0x1019, 0x0015, 0x0008, 0x0006, 0x000F},
    {0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x1019, 0x0016, 0x0009, 0x1019, 0x0016},
    {0x000A, 0x000A, 0x0017, 0x0013, 0x0008, 0x1019, 0x0010, 0x0008, 0x0006, 0x000D},
    {0x000B, 0x0013, 0x0017, 0x000B, 0x0011, 0x1019, 0x0002, 0x000C, 0x0003, 0x0002},
    {0x000C, 0x0011, 0x0009, 0x000C, 0x0011, 0x1019, 0x000E, 0x000C, 0x0003, 0x000E},
    {0x000D, 0x000D, 0x0001, 0x0010, 0x000F, 0x1019, 0x0010, 0x000F, 0x0006, 0x000D},
    {0x000E, 0x0015, 0x0016, 0x000E, 0x0015, 0x1019, 0x000E, 0x000E, 0x0003, 0x000E},
    {0x000F, 0x000F, 0x0016, 0x0015, 0x000F, 0x1019, 0x0015, 0x000F, 0x0006, 0x000F},
    {0x0010, 0x0010, 0x0001, 0x0010, 0x0015, 0x1019, 0x0010, 0x0015, 0x0014, 0x0010},
    {0x0011, 0x0011, 0x0009, 0x0011, 0x0011, 0x1019, 0x0015, 0x0011, 0x0014, 0x0015},
    {0x0012, 0x000F, 0x0016, 0x000E, 0x000F, 0x1019, 0x000E, 0x0012, 0x0005, 0x0012},
    {0x0013, 0x0013, 0x0017, 0x0013, 0x0011, 0x1019, 0x0010, 0x0011, 0x0014, 0x0010},
    {0x0014, 0x0014, 0x1019, 0x0014, 0x0014, 0x1019, 0x0014, 0x0014, 0x0014, 0x0014},
    {0x0015, 0x0015, 0x0016, 0x0015, 0x0015, 0x1019, 0x0015, 0x0015, 0x0014, 0x0015},
    {0x0016, 0x0016, 0x0016, 0x0016, 0x0016, 0x1019, 0x0016, 0x0016, 0x1019, 0x0016},
    {0x0017, 0x0017, 0x0017, 0x0017, 0x0009, 0x1019, 0x0001, 0x0009, 0x1019, 0x0001},
    {0x0018, 0x000A, 0x0017, 0x000B, 0x0008, 0x1019, 0x0002, 0x0007, 0x0005, 0x0004},
    {0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019, 0x1019},
};

// Forest corners of the states 1..25 (derived from the table: the tile at brush cell c loses the corners that touch
// the felled tile), and the state the map editor uses for each corner set. States 14..22 only come from harvesting
// and repeat a corner set of an editor state; 20..22 are tree tiles with no forest corner left.
constexpr uint8_t kMaskOfState[kTreeStateCleared + 1] = {0,  2, 10, 8, 14, 12, 4, 13, 5,  1, 7, 11, 9,
                                                         6,  8, 4,  2, 1,  12, 3, 0,  0,  0, 3, 15, 0};
constexpr uint8_t kStateOfMask[16] = {0, 9, 1, 23, 6, 8, 13, 10, 3, 12, 2, 11, 5, 7, 4, 24};
constexpr uint8_t kUnknownMask = 0xFF;

constexpr bool MaskTablesAgree() {
    for (int mask = 1; mask < 16; ++mask)
        if (kStateOfMask[mask] < 1 || kStateOfMask[mask] > kTreeStateSolid || kMaskOfState[kStateOfMask[mask]] != mask) return false;
    return true;
}
static_assert(MaskTablesAgree(), "every corner set must map to a forest state 1..24 that holds exactly that set");

// Pacing. The map is swept a slice of rows per step, one full pass per kSweepMs of play time; a 128 x 128 map at
// 25 steps a second is about 160 tiles a step. At most kMaxRegrowPerSweep due stumps are grown back per pass (each
// can take its due neighbours along), which is 120 a minute; whatever is due beyond that waits for a later pass.
constexpr unsigned kSweepMs = 4000;
constexpr int kMaxRegrowPerSweep = 8;
constexpr uint8_t kRetrySweeps = 7;        // a due stump that could not grow is looked at again after this many passes
constexpr unsigned kLogEveryMs = 60000;    // without log_casts the regrowth count is written once a minute at most
constexpr ULONGLONG kLongGapMs = 500;      // same threshold as the play-time clock in mod.cpp: a pause, a menu or a load
constexpr int kMaxStates = 96;             // rows we are prepared to read from the loaded table (the tilesets have 36..41)

constexpr int kMaxTiles = kMaxMapSize * kMaxMapSize;
uint32_t g_firstSeenMs[kMaxTiles];  // play time at which a sweep first saw the tile as a stump; 0 = no timer
uint8_t g_waitFraction[kMaxTiles];  // drawn at first sight: where in regrow_min..max_minutes this stump's wait lies (0..255)
uint8_t g_skipSweeps[kMaxTiles];    // passes left before a stump that failed to grow is tried again
uint8_t g_maskOfState[kMaxStates];  // corner set per row of the LOADED table (art variants included)
uint32_t g_playMs = 1;              // never 0, so that 0 can mean "no timer"
uint32_t g_random = 0;              // xorshift32 state, 0 = not seeded yet

// The module's own generator: the game's is part of the simulation and must not be advanced from here.
uint32_t NextRandom() {
    if (!g_random) g_random = static_cast<uint32_t>(GetTickCount64()) | 1;
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

// What "the same game is still running" is judged by. None of it is proof on its own (the allocator can hand a new
// game the same addresses), so the new-map hook and the timer check after a long gap back it up.
struct Identity {
    const void *tile, *sq, *region, *table, *units;
    int size;
    uint16_t fromSave;

    bool operator==(const Identity& o) const {
        return tile == o.tile && sq == o.sq && region == o.region && table == o.table && units == o.units && size == o.size &&
               fromSave == o.fromSave;
    }
};
Identity g_identity;
bool g_attached = false;
bool g_newMapPending = false;
bool g_disabled = false;  // a sanity check failed: nothing is touched until the next map
int g_cursorRow = 0;
unsigned g_rowCredit = 0;  // play ms x map size not yet turned into rows
int g_sweepBudget = kMaxRegrowPerSweep;
int g_grownSinceLog = 0;
uint32_t g_lastLogMs = 0;
ULONGLONG g_lastTickMs = 0;

struct Map {
    uint16_t* tile;
    uint16_t* sq;
    uint16_t* region;
    const uint16_t* table;
    int size;
    int treeCount;      // tree tile ids in this tileset: states 1..treeCount
    uint16_t treeBase;  // tile id of state 1
    uint16_t stump;     // tile id of the cleared state: a forest stood here and was felled in this game
    uint32_t waitMinMs, waitSpanMs;  // [trees] regrow_min_minutes, and how much longer regrow_max_minutes is
};

void ClearTimers() {
    memset(g_firstSeenMs, 0, sizeof(g_firstSeenMs));
    memset(g_waitFraction, 0, sizeof(g_waitFraction));
    memset(g_skipSweeps, 0, sizeof(g_skipSweeps));
    g_cursorRow = 0;
    g_rowCredit = 0;
    g_sweepBudget = kMaxRegrowPerSweep;
}

// Everything FUN_004eb400 relies on, checked once per map. It indexes the table with the tile id and no bounds check,
// so the feature only runs when the loaded table is exactly the one the corner tables above were derived from.
const char* Validate(const Map& m) {
    if (!m.tile || !m.sq || !m.region) return "a terrain map is missing";
    if (m.size <= 0 || m.size > kMaxMapSize) return "unexpected map size";
    if (!m.table) return "the tree table is not loaded";
    if (*At<uint16_t>(kRvaTreeTableStride) != kTreeTableStride) return "unexpected tree table row length";
    if (m.treeBase != kTreeBaseExpected) return "unexpected first tree tile id";
    if (m.treeCount < kTreeStateCleared || m.treeCount >= kMaxStates) return "unexpected number of tree tiles";
    if (memcmp(m.table, kCanonicalRows, sizeof(kCanonicalRows)) != 0) return "the tree table is not the one this mod knows";
    return nullptr;
}

// Corner set for every row of the loaded table. Rows past 25 are art variants: each behaves exactly like one of the
// rows 1..24 (same nine transitions), and that row's corners are its corners. A row that matches none stays unknown
// and a tile holding it is never repainted.
void BuildMaskOfState(const Map& m) {
    memset(g_maskOfState, kUnknownMask, sizeof(g_maskOfState));
    for (int s = 1; s <= kTreeStateCleared; ++s) g_maskOfState[s] = kMaskOfState[s];
    g_maskOfState[kTreeStateCleared] = kUnknownMask;  // a stump is not a tree tile
    for (int s = kTreeStateCleared + 1; s <= m.treeCount; ++s) {
        const uint16_t* row = m.table + s * kTreeTableStride;
        for (int c = 1; c <= kTreeStateSolid; ++c)
            if (memcmp(row + 1, kCanonicalRows[c] + 1, (kTreeTableStride - 1) * sizeof(uint16_t)) == 0) {
                g_maskOfState[s] = kMaskOfState[c];
                break;
            }
    }
}

// False = leave this map alone. Notices a new game and starts every timer over.
bool Attach(const World& w, Map& m) {
    m.tile = *At<uint16_t*>(kRvaTileMap);
    m.sq = *At<uint16_t*>(kRvaSquareFlags);
    m.region = *At<uint16_t*>(kRvaRegionMap);
    m.table = *At<uint16_t*>(kRvaTreeTable);
    m.size = w.mapSize;
    m.treeCount = *At<uint16_t>(kRvaTreeTileCount);
    m.treeBase = *At<uint16_t>(kRvaTreeBase);
    m.stump = static_cast<uint16_t>(m.treeBase + kTreeStateCleared - 1);
    const int minMinutes = config::g.treesRegrowMinMinutes;
    const int maxMinutes = config::g.treesRegrowMaxMinutes > minMinutes ? config::g.treesRegrowMaxMinutes : minMinutes;
    m.waitMinMs = static_cast<uint32_t>(minMinutes) * 60000u;
    m.waitSpanMs = static_cast<uint32_t>(maxMinutes - minMinutes) * 60000u;

    const Identity now = {m.tile, m.sq, m.region, m.table, w.units, m.size, *At<uint16_t>(kRvaGameFromSave)};
    if (!g_attached || g_newMapPending || !(now == g_identity)) {
        g_attached = true;
        g_newMapPending = false;
        g_identity = now;
        ClearTimers();
        const char* problem = Validate(m);
        g_disabled = problem != nullptr;
        if (problem) {
            logx::Write("trees: %s, regrowth stays off for this map", problem);
        } else {
            BuildMaskOfState(m);
            logx::Write("trees: regrowth on, %dx%d map, stumps grow back after %d to %d min, not within %d tiles of a building or %d of a ground unit",
                        m.size, m.size, minMinutes, maxMinutes, config::g.treesBuildingDistance, config::g.treesUnitDistance);
        }
    }
    return !g_disabled;
}

// A savegame can be loaded into the very same buffers, with no new-map hook. What gives it away: in a running game a
// stump never turns back into anything by itself (we clear the timer of every tile we regrow), so a timer on a tile
// that is no stump means the map under us was swapped. Checked after every long gap, which every load is.
void CheckTimersAfterGap(const Map& m) {
    const int tiles = m.size * m.size;
    for (int i = 0; i < tiles; ++i)
        if (g_firstSeenMs[i] && m.tile[i] != m.stump) {
            ClearTimers();
            logx::Write("trees: the map changed under the stump timers (a game was loaded?), every stump starts its wait over");
            return;
        }
}

bool OnMap(const Map& m, int x, int y) { return x >= 0 && y >= 0 && x < m.size && y < m.size; }

// The wait is worked out from the stored fraction every time, never stored itself: a config reload applies to stumps
// that are already waiting, and min == max is a fixed wait.
bool Due(const Map& m, int i) {
    if (!g_firstSeenMs[i]) return false;
    const uint32_t wait = m.waitMinMs + static_cast<uint32_t>(static_cast<uint64_t>(m.waitSpanMs) * g_waitFraction[i] / 255);
    return g_playMs - g_firstSeenMs[i] >= wait;
}

struct Cell {
    int x, y, i;
    bool stump;  // becomes a tree tile (three writes); false = already a tree, only its tile id changes
    uint16_t newId;
};

// Land a ground unit could stand on, units themselves ignored. `pending` are stumps of the same paint that are
// already counted as trees.
bool Open(const Map& m, int x, int y, const Cell* pending, int pendingCount) {
    if (!OnMap(m, x, y)) return false;
    if (m.sq[y * m.size + x] & (kSqBuilding | kSqUnpassable | kSqWater | kSqWalls | kSqCoast)) return false;
    for (int k = 0; k < pendingCount; ++k)
        if (pending[k].stump && pending[k].x == x && pending[k].y == y) return false;
    return true;
}

// Every condition for turning the stump at x,y into a tree tile. Cheap tests first: a base full of blocked stumps is
// asked again every few sweeps.
bool Eligible(const Map& m, const World& w, int x, int y, const Cell* pending, int pendingCount) {
    const int i = y * m.size + x;
    if (m.tile[i] != m.stump) return false;
    // Plain land and nothing else: no ground unit (0x100), no AI keep-clear mark (0x400), no building, wall, water,
    // coast or no-build bit. A flyer overhead (0x200, air grid) does not matter: flyers cross forest anyway, their
    // blocking mask is 0x0200 alone (table 0x8C1AC8). The top nibble is pathfinder scratch.
    if ((m.sq[i] & 0x0FFF & ~kSqAirUnit) != kSqLand) return false;
    if (w.grid[i]) return false;
    const uint16_t region = m.region[i];
    if (!(region & kRegionLandBit) || region >= kRegionFirstSpecial) return false;

    // The passage rule: the open neighbours must form at most one unbroken run around the tile. Neighbours in a run
    // are side by side, so any route through this tile can go around it instead; no area gets cut off, no unit walled
    // in, and the game's region ids (which only ever merge) stay true.
    static const int kRingDx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kRingDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    bool open[8];
    for (int k = 0; k < 8; ++k) open[k] = Open(m, x + kRingDx[k], y + kRingDy[k], pending, pendingCount);
    int runs = 0;
    for (int k = 0; k < 8; ++k) runs += open[k] && !open[(k + 7) % 8];
    if (runs > 1) return false;

    // [trees] building_distance and unit_distance, both a box around the tile. Every footprint tile of a building
    // carries 0x800, a finished wall 0x04 / 0x08. A ground unit is filed on its tile with flag 0x100 AND a pointer in
    // the first unit grid, by the same two functions (FUN_004b4a00 / FUN_004b5000): either one counts. That grid also
    // holds buildings, so where the two distances differ the larger one wins around a building. The air grid is not
    // looked at: flyers do not hold regrowth up.
    const int buildingReach = config::g.treesBuildingDistance, unitReach = config::g.treesUnitDistance;
    const int reach = buildingReach > unitReach ? buildingReach : unitReach;
    for (int ty = y - reach; ty <= y + reach; ++ty)
        for (int tx = x - reach; tx <= x + reach; ++tx) {
            if (!OnMap(m, tx, ty)) continue;
            const int t = ty * m.size + tx;
            const int distance = abs(tx - x) > abs(ty - y) ? abs(tx - x) : abs(ty - y);
            if (distance <= buildingReach && (m.sq[t] & (kSqBuilding | kSqWalls))) return false;
            if (distance <= unitReach && ((m.sq[t] & kSqGroundUnit) || w.grid[t])) return false;
        }

    // Corpses are in neither grid. Free (1) and dead (4) slots hold stale coordinates; flyers are never on the ground.
    for (unsigned k = 0; k < w.unitCount; ++k) {
        Unit* u = UnitAt(w, k);
        if ((Field<uint8_t>(u, kOffStateFlags) & 0x05) || (w.typeFlags[TypeOf(u)] & kTfFlyer)) continue;
        if (Field<int16_t>(u, kOffX) == x && Field<int16_t>(u, kOffY) == y) return false;
    }
    return true;
}

// Sets the corner point vx,vy (the top-left corner of tile vx,vy) to forest: all or nothing for the tiles around it.
// Every one of them must already be a tree, or (growStumps) be a stump that may grow right now; anything else means
// the point was never inside a forest, or is not free yet. Returns the number of stumps that became trees and lists
// them in grownOut (room for 4).
int PaintCorner(const Map& m, const World& w, int vx, int vy, bool growStumps, Cell* grownOut) {
    static const struct {
        int dx, dy;
        uint8_t corner;  // which corner of that tile the point is
    } kAround[4] = {{-1, -1, kBR}, {0, -1, kBL}, {-1, 0, kTR}, {0, 0, kTL}};
    Cell cells[4];
    int count = 0;
    for (const auto& a : kAround) {
        Cell c = {vx + a.dx, vy + a.dy, 0, false, 0};
        if (!OnMap(m, c.x, c.y)) continue;
        c.i = c.y * m.size + c.x;
        uint8_t mask = 0;
        const uint16_t region = m.region[c.i];
        if (region == kRegionTree || region == kRegionChopping) {
            const int state = static_cast<int>(m.tile[c.i]) - m.treeBase + 1;
            if (state < 1 || state > m.treeCount || g_maskOfState[state] == kUnknownMask) return 0;
            mask = g_maskOfState[state];
            if (mask & a.corner) continue;  // has it already: its tile (maybe an art variant) stays as it is
        } else {
            if (!growStumps || !Due(m, c.i) || !Eligible(m, w, c.x, c.y, cells, count)) return 0;
            c.stump = true;
        }
        const int newState = kStateOfMask[mask | a.corner];
        if (newState < 1 || newState > kTreeStateSolid) return 0;  // cannot happen (static_assert above); never write past it
        c.newId = static_cast<uint16_t>(m.treeBase + newState - 1);
        cells[count++] = c;
    }

    int grown = 0;
    for (int k = 0; k < count; ++k) {
        const Cell& c = cells[k];
        // Tile id first: a forest region word over a non-tree tile id is the one state the game cannot handle.
        m.tile[c.i] = c.newId;
        if (!c.stump) continue;  // already a tree: flags and region word stay as they are (0xFFFC = being felled)
        m.sq[c.i] |= kSqUnpassable;
        m.region[c.i] = kRegionTree;
        g_firstSeenMs[c.i] = 0;
        g_skipSweeps[c.i] = 0;
        if (grownOut) grownOut[grown] = c;
        ++grown;
    }
    return grown;
}

// Grows the due stump at x,y back by painting whichever of its four corner points can be painted. A hole inside a
// forest gets all four and is solid forest again; a tile on a harvest front gets an edge state, like the map editor
// puts on the rim of a forest (and like those it is a full tree tile: blocked and harvestable).
int RegrowTile(const Map& m, const World& w, int x, int y) {
    if (!Eligible(m, w, x, y, nullptr, 0)) return 0;
    Cell taken[4 * 4];  // x,y itself and at most its 8 neighbours, each once
    int grown = 0;
    for (int corner = 0; corner < 4; ++corner) grown += PaintCorner(m, w, x + (corner & 1), y + (corner >> 1), true, taken + grown);
    // A due neighbour that was taken along only got the corner points it shares with x,y. It will never have a turn
    // of its own (it is no stump any more), so give it now what it can have without growing anything else: the points
    // that lie between trees only. A point next to a remaining stump is painted when that stump's turn comes.
    for (int k = 0; k < grown; ++k) {
        if (taken[k].x == x && taken[k].y == y) continue;
        for (int corner = 0; corner < 4; ++corner) PaintCorner(m, w, taken[k].x + (corner & 1), taken[k].y + (corner >> 1), false, nullptr);
    }
    return grown;
}

void SweepRow(const Map& m, const World& w, int y) {
    for (int x = 0; x < m.size; ++x) {
        const int i = y * m.size + x;
        if (m.tile[i] != m.stump) {
            g_firstSeenMs[i] = 0;
            continue;
        }
        if (!g_firstSeenMs[i]) {
            g_firstSeenMs[i] = g_playMs;
            g_waitFraction[i] = static_cast<uint8_t>(NextRandom() >> 24);
            g_skipSweeps[i] = 0;
            continue;
        }
        if (!Due(m, i) || g_sweepBudget <= 0) continue;
        if (g_skipSweeps[i]) {
            --g_skipSweeps[i];
            continue;
        }
        const int grown = RegrowTile(m, w, x, y);
        if (grown) {
            --g_sweepBudget;
            g_grownSinceLog += grown;
        } else {
            g_skipSweeps[i] = kRetrySweeps;
        }
    }
}

void EndSweep() {
    g_sweepBudget = kMaxRegrowPerSweep;
    if (!g_grownSinceLog) return;
    if (!config::g.logCasts && g_playMs - g_lastLogMs < kLogEveryMs) return;
    logx::Write("trees: %d tile%s grew back", g_grownSinceLog, g_grownSinceLog == 1 ? "" : "s");
    g_grownSinceLog = 0;
    g_lastLogMs = g_playMs;
}

}  // namespace

void OnTick(const World& w, unsigned elapsedMs) {
    if (!config::g.treesRegrow) {
        g_attached = false;  // switched back on later = like a new map: every stump starts its wait at first sight
        return;
    }
    Map m;
    if (!Attach(w, m)) return;

    const ULONGLONG now = GetTickCount64();
    if (g_lastTickMs && now - g_lastTickMs > kLongGapMs) CheckTimersAfterGap(m);
    g_lastTickMs = now;

    g_playMs += elapsedMs;
    if (!g_playMs) g_playMs = 1;  // wrapped after 49 days of play

    g_rowCredit += elapsedMs * static_cast<unsigned>(m.size);
    int rows = static_cast<int>(g_rowCredit / kSweepMs);
    g_rowCredit %= kSweepMs;
    if (rows > m.size) rows = m.size;  // never more than one full pass per step
    for (; rows > 0; --rows) {
        if (g_cursorRow >= m.size) g_cursorRow = 0;
        SweepRow(m, w, g_cursorRow);
        if (++g_cursorRow >= m.size) {
            g_cursorRow = 0;
            EndSweep();
        }
    }
}

void OnNewMap() { g_newMapPending = true; }

void SeedForTests(uint32_t seed) { g_random = seed | 1; }

}  // namespace trees
