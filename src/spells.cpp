#include "spells.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "world.h"

using namespace game;

namespace spells {

namespace {

constexpr int kFirstSpellOrder = 0x26;
constexpr int kSpellOrderCount = 19;  // orders 0x26..0x38
// The game's table from order 0x26 on. 0x28 is an unused spell slot (no action, no string) and keeps its 5.
constexpr uint16_t kVanillaCost[kSpellOrderCount] = {70, 5, 5, 4, 80, 100, 50, 200, 200, 25, 70, 60, 50, 100, 100, 50, 100, 200, 30};
// Order id of every [spell_cost] key.
constexpr uint8_t kCostOrder[kSpellCostCount] = {0x26, 0x27, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
                                                 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
constexpr int kRegenBase = 40;  // simulation steps per mana point (FUN_004ef480 reloads the counter with 0x28)

unsigned g_writes = 0;

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// The epsilon keeps 70 x 1.1 = 77.00000000000001 at 77 and a product meant as 2.5 rounding to 3.
int RoundUp(double v) { return static_cast<int>(std::ceil(v - 1e-9)); }
int RoundNearest(double v) { return static_cast<int>(std::floor(v + 0.5 + 1e-9)); }

struct Numbers {
    int cost[kSpellCostCount];
    int damage[kSpellDamageCount];  // kDamageHeal = hit point cap per cast
    int regenInterval;              // steps per mana point
};

Numbers GameNumbers() {
    Numbers n;
    for (int s = 0; s < kSpellCostCount; ++s) n.cost[s] = kVanillaCost[kCostOrder[s] - kFirstSpellOrder];
    for (int d = 0; d < kSpellDamageCount; ++d) n.damage[d] = kDamageBase[d];
    n.regenInterval = kRegenBase;
    return n;
}

Numbers FromConfig(const Config& c) {
    Numbers n;
    for (int s = 0; s < kSpellCostCount; ++s) {
        const int base = kVanillaCost[kCostOrder[s] - kFirstSpellOrder];
        const int v = c.spellCost[s] >= 1 ? c.spellCost[s] : base;
        int cost = Clamp(RoundUp(v * c.spellCostAll), 1, kMaxCost);
        // Heal and exorcism charge mana per hit point, so their damage multiplier can only act as a lower price.
        if ((s == kCostHeal || s == kCostExorcism) && c.spellDamageAll > 0)
            cost = Clamp(RoundUp(cost / c.spellDamageAll), 1, kMaxCost);
        n.cost[s] = cost;
    }
    for (int d = 0; d < kSpellDamageCount; ++d) {
        const int v = c.spellDamage[d] >= 1 ? c.spellDamage[d] : kDamageBase[d];
        n.damage[d] = Clamp(RoundNearest(v * c.spellDamageAll), 1, kDamageMax[d]);
    }
    n.regenInterval = c.manaRegen > 0 ? Clamp(RoundNearest(kRegenBase / c.manaRegen), 1, 255) : kRegenBase;
    return n;
}

// ---- the cost table (0x8C5EB8, u16 by order id): plain data, never saved, never reloaded by the game ----

bool g_costChecked = false;
bool g_costOff = false;
uint16_t g_costWritten[kSpellCostCount];
bool g_costHaveWritten[kSpellCostCount];
bool g_costRefused[kSpellCostCount];

void SyncCosts(const Numbers& n) {
    uint16_t* table = At<uint16_t>(kRvaManaCostByOrder);
    if (!g_costChecked) {  // the first call comes before the mod ever writes the table
        g_costChecked = true;
        if (memcmp(table + kFirstSpellOrder, kVanillaCost, sizeof(kVanillaCost)) != 0) {
            g_costOff = true;
            logx::Write("spells: the mana cost table is not the one this mod knows, [spell_cost] is ignored");
        }
    }
    if (g_costOff) return;
    for (int s = 0; s < kSpellCostCount; ++s) {
        if (g_costRefused[s]) continue;
        uint16_t& cell = table[kCostOrder[s]];
        const uint16_t base = kVanillaCost[kCostOrder[s] - kFirstSpellOrder];
        if (cell != base && !(g_costHaveWritten[s] && cell == g_costWritten[s])) {
            g_costRefused[s] = true;
            logx::Write("spells: mana cost of %s is %u, neither the game's %u nor the mod's, left alone", config::kSpellCostKeys[s], cell, base);
            continue;
        }
        const uint16_t want = static_cast<uint16_t>(n.cost[s]);
        if (cell == want) continue;
        cell = want;
        g_costWritten[s] = want;
        g_costHaveWritten[s] = true;
        ++g_writes;
    }
}

// ---- code patches: one group per number, each a few instruction immediates that must always agree ----

constexpr int kMaxSites = 5;
constexpr int kMaxSiteLen = 16;

struct Site {
    uint32_t rva;
    int len;
    uint8_t original[kMaxSiteLen];
};

struct Group {
    const char* name;
    int siteCount;
    Site site[kMaxSites];
    uint8_t written[kMaxSites][kMaxSiteLen];
    bool haveWritten;
    bool refused;
};

// Groups 0..7 follow SpellDamageKey, the last one is the mana regeneration interval.
enum { kGroupManaRegen = kSpellDamageCount, kGroupCount };
static_assert(kDamageFireball == 0 && kDamageDeathCoil == 5 && kDamageRunes == 6 && kDamageHeal == 7, "group order");

Group g_groups[kGroupCount] = {
    {"fireball", 2,
     {{kRvaFireballDamageInsn, 2, {0xB0, 0x28}},
      {kRvaFireballMarker, 16, {0xB8, 0x28, 0x00, 0x00, 0x00, 0x38, 0x47, 0x37, 0xB9, 0x19, 0x00, 0x00, 0x00, 0x0F, 0x44, 0xC8}}}},
    {"flame_shield", 1, {{kRvaFlameShieldDamageInsn, 4, {0xC6, 0x40, 0x37, 0x04}}}},
    {"blizzard", 1, {{kRvaBlizzardDamageInsn, 4, {0xC6, 0x47, 0x37, 0x0A}}}},
    {"death_and_decay", 1, {{kRvaDeathAndDecayDamageInsn, 4, {0xC6, 0x46, 0x37, 0x0A}}}},
    {"whirlwind", 1, {{kRvaWhirlwindDamageInsn, 4, {0xC6, 0x46, 0x37, 0x04}}}},
    {"death_coil", 5,
     {{kRvaDeathCoilBudgetInsns[0], 3, {0x83, 0xF8, 0x32}},
      {kRvaDeathCoilBudgetInsns[1], 3, {0x83, 0xFB, 0x32}},
      {kRvaDeathCoilBudgetInsns[2], 3, {0x83, 0xF8, 0x32}},
      {kRvaDeathCoilBudgetInsns[3], 5, {0xB8, 0x32, 0x00, 0x00, 0x00}},
      {kRvaDeathCoilBudgetInsns[4], 5, {0xBB, 0x32, 0x00, 0x00, 0x00}}}},
    {"runes", 2, {{kRvaRunesDamageInsn, 5, {0xB9, 0x32, 0x00, 0x00, 0x00}}, {kRvaRunesSubtractInsn, 3, {0x83, 0xC0, 0xCE}}}},
    {"heal cap", 1, {{kRvaHealCapInsn, 5, {0xB8, 0x28, 0x00, 0x00, 0x00}}}},
    {"mana regen", 3,
     {{kRvaManaRegenReloadInsn, 4, {0xC6, 0x46, 0x74, 0x28}},
      {kRvaManaRegenCreateInsn, 4, {0xC6, 0x46, 0x74, 0x28}},
      {kRvaManaRegenConvertInsn, 4, {0xC6, 0x42, 0x74, 0x28}}}},
};

// The bytes a group should hold for `value`: the game's own bytes when it is the game's number.
void Desired(int g, int value, uint8_t out[kMaxSites][kMaxSiteLen]) {
    const Group& grp = g_groups[g];
    for (int i = 0; i < grp.siteCount; ++i) memcpy(out[i], grp.site[i].original, grp.site[i].len);
    if (value == (g == kGroupManaRegen ? kRegenBase : kDamageBase[g])) return;
    const uint8_t b = static_cast<uint8_t>(value);
    switch (g) {
    case kDamageFireball: {
        out[0][1] = b;  // mov al, D
        // The spell's damage doubles as its marker: damage == 40 picks the 40-step trail (5 blasts), the dragon and
        // gryphon missiles of the same class get 25. Same 16 bytes, stack balanced, flags of the cmp kept:
        // mov al, D / cmp [edi+0x37], al / push 0x19 / pop ecx / jne +3 / push 0x28 / pop ecx / 3 x nop
        const uint8_t marker[16] = {0xB0, b, 0x38, 0x47, 0x37, 0x6A, 0x19, 0x59, 0x75, 0x03, 0x6A, 0x28, 0x59, 0x90, 0x90, 0x90};
        memcpy(out[1], marker, sizeof(marker));
        break;
    }
    case kDamageDeathCoil:
        out[0][2] = out[1][2] = out[2][2] = b;  // cmp reg, imm8: sign-extended, hence the 127 limit
        out[3][1] = out[4][1] = b;              // mov reg, imm32 (the upper bytes stay 0)
        break;
    case kDamageRunes:
        out[0][1] = b;                             // mov ecx, D: a unit with hp <= D dies
        out[1][2] = static_cast<uint8_t>(-value);  // add eax, -D: sign-extended, hence the 128 limit
        break;
    case kDamageHeal:
        out[0][1] = b;  // mov eax, cap
        break;
    default:  // mov byte [reg+0x37] (missile damage) / [reg+0x74] (mana regen counter), imm8
        for (int i = 0; i < grp.siteCount; ++i) out[i][3] = b;
    }
}

void SyncGroup(int g, int value) {
    Group& grp = g_groups[g];
    if (grp.refused) return;
    uint8_t want[kMaxSites][kMaxSiteLen];
    Desired(g, value, want);
    bool same = true;
    for (int i = 0; i < grp.siteCount; ++i) {
        const Site& s = grp.site[i];
        const uint8_t* at = At<uint8_t>(s.rva);
        if (memcmp(at, s.original, s.len) != 0 && !(grp.haveWritten && memcmp(at, grp.written[i], s.len) == 0)) {
            grp.refused = true;
            logx::Write("spells: %s: unexpected code at 0x%06X (%02X %02X %02X), left alone", grp.name, 0x400000 + s.rva, at[0],
                        at[1], at[2]);
            return;
        }
        same = same && memcmp(at, want[i], s.len) == 0;
    }
    if (same) return;

    // All sites or none: the numbers of one spell sit in several instructions that must agree.
    DWORD old[kMaxSites];
    int unlocked = 0;
    while (unlocked < grp.siteCount &&
           VirtualProtect(At<uint8_t>(grp.site[unlocked].rva), grp.site[unlocked].len, PAGE_EXECUTE_READWRITE, &old[unlocked]))
        ++unlocked;
    if (unlocked == grp.siteCount) {
        for (int i = 0; i < grp.siteCount; ++i) {
            memcpy(At<uint8_t>(grp.site[i].rva), want[i], grp.site[i].len);
            memcpy(grp.written[i], want[i], grp.site[i].len);
        }
        grp.haveWritten = true;
        ++g_writes;
    } else {
        grp.refused = true;
        logx::Write("spells: %s: VirtualProtect failed (%lu), left alone", grp.name, GetLastError());
    }
    while (unlocked > 0) {  // reverse order: two sites on one page get the page's first protection back last
        --unlocked;
        uint8_t* at = At<uint8_t>(grp.site[unlocked].rva);
        VirtualProtect(at, grp.site[unlocked].len, old[unlocked], &old[unlocked]);
        FlushInstructionCache(GetCurrentProcess(), at, grp.site[unlocked].len);
    }
}

void Append(char* line, size_t size, const char* fmt, const char* key, int from, int to) {
    const size_t used = strlen(line);
    sprintf_s(line + used, size - used, fmt, key, from, to);
}

}  // namespace

void Sync(bool multiplayer) {
    const Numbers n = multiplayer ? GameNumbers() : FromConfig(config::g);
    SyncCosts(n);
    for (int d = 0; d < kSpellDamageCount; ++d) SyncGroup(d, n.damage[d]);
    SyncGroup(kGroupManaRegen, n.regenInterval);
}

void OnNewMap(bool multiplayer) {
    Sync(multiplayer);
    if (multiplayer) return;
    const Numbers n = FromConfig(config::g), game = GameNumbers();
    char line[480] = "";
    for (int s = 0; s < kSpellCostCount; ++s)
        if (n.cost[s] != game.cost[s]) Append(line, sizeof(line), " %s %d->%d", config::kSpellCostKeys[s], game.cost[s], n.cost[s]);
    if (*line) logx::Write("spells: mana cost%s", line);
    *line = '\0';
    for (int d = 0; d < kSpellDamageCount; ++d)
        if (n.damage[d] != game.damage[d])
            Append(line, sizeof(line), " %s %d->%d", d == kDamageHeal ? "heal_cap" : config::kSpellDamageKeys[d], game.damage[d], n.damage[d]);
    if (*line) logx::Write("spells: damage%s", line);
    if (n.regenInterval != game.regenInterval)
        logx::Write("spells: mana regeneration 1 point per %d steps (the game: %d)", n.regenInterval, game.regenInterval);
}

unsigned WriteCount() { return g_writes; }

}  // namespace spells
