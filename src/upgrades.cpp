#include "upgrades.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "world.h"

using namespace game;

namespace upgrades {

namespace {

// Index into the game's effect table for every [upgrades] key. Entry 5 (byte 10) and entry 7 (0) are read by
// nothing in this build, 8 is the range bonus ([range] upgrade_bonus patches the instruction instead), 9 and 10
// are printed by the status panels only: none of them is exposed.
constexpr int kEffectIndex[kUpgradeEffectCount] = {0, 1, 2, 3, 4, 6};

unsigned g_writes = 0;
bool g_refused[kUpgradeEffectCount] = {};
bool g_haveWritten[kUpgradeEffectCount] = {};
uint8_t g_written[kUpgradeEffectCount] = {};
bool g_tableChecked = false, g_tableOff = false;

int Wanted(int key, bool multiplayer) {
    const int v = config::g.upgradeEffect[key];
    return (multiplayer || v < 0) ? kVanilla[key] : v;
}

// The table sits in the image's data, which is writable in this build; a page that is not is made writable once,
// the way the code patches do it, instead of taking an access violation on the first write.
bool MakeWritable(uint8_t* table) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(table, &mbi, sizeof(mbi))) return false;
    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.Protect & kWritable) return true;
    DWORD old = 0;
    return VirtualProtect(table, kUpgradeEffectTableLen, PAGE_READWRITE, &old) != 0;
}

}  // namespace

void Sync(bool multiplayer) {
    uint8_t* table = At<uint8_t>(kRvaUpgradeEffects);
    if (!g_tableChecked) {  // the first call happens before the mod has ever written a byte
        g_tableChecked = true;
        for (int i = 0; i < kUpgradeEffectCount; ++i)
            if (table[kEffectIndex[i]] != kVanilla[i]) g_tableOff = true;
        if (g_tableOff)
            logx::Write("upgrades: the upgrade effect table is not the one this mod knows, [upgrades] is ignored");
        else if (!MakeWritable(table)) {
            g_tableOff = true;
            logx::Write("upgrades: the upgrade effect table is not writable (%lu), [upgrades] is ignored", GetLastError());
        }
    }
    if (g_tableOff) return;
    for (int i = 0; i < kUpgradeEffectCount; ++i) {
        if (g_refused[i]) continue;
        uint8_t& cell = table[kEffectIndex[i]];
        const uint8_t base = static_cast<uint8_t>(kVanilla[i]);
        if (cell != base && !(g_haveWritten[i] && cell == g_written[i])) {
            g_refused[i] = true;
            logx::Write("upgrades: %s is %u, neither the game's %u nor the mod's, left alone", config::kUpgradeEffectKeys[i],
                        cell, base);
            continue;
        }
        const uint8_t want = static_cast<uint8_t>(Wanted(i, multiplayer));
        if (cell == want) continue;
        cell = want;
        g_written[i] = want;
        g_haveWritten[i] = true;
        ++g_writes;
    }
}

void OnNewMap(bool multiplayer) {
    Sync(multiplayer);
    if (g_tableOff) return;
    char line[256] = "";
    for (int i = 0; i < kUpgradeEffectCount; ++i) {
        const int want = Wanted(i, multiplayer);
        if (want == kVanilla[i]) continue;
        char one[48];
        sprintf_s(one, " %s %d->%d", config::kUpgradeEffectKeys[i], kVanilla[i], want);
        if (strlen(line) + strlen(one) < sizeof(line)) strcat_s(line, one);
    }
    if (*line) logx::Write("upgrades:%s", line);
}

unsigned WriteCount() { return g_writes; }

void ResetForTest() {
    g_tableChecked = g_tableOff = false;
    memset(g_refused, 0, sizeof(g_refused));
    memset(g_haveWritten, 0, sizeof(g_haveWritten));
}

}  // namespace upgrades
