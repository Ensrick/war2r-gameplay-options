#include "aiwatch.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "log.h"

using namespace game;

namespace aiwatch {

namespace {

constexpr unsigned kReportMs = 60u * 1000u;       // one status line per player per minute of play
constexpr unsigned kStallMs = 5u * 60u * 1000u;   // "stuck" threshold, and the repeat interval of the stall line
constexpr uint32_t kMaxBlobSize = 1u << 20;       // sanity bound on the loader's out-param (ai.bin is ~22 KB)
constexpr int kFoodCap = 200;                     // every reader of the supply word clamps to this

// Condition names as used in docs/research/ai_stall.md. Conditions 3..6 carry a number and are printed by hand.
const char* const kCondName[kAiCondCount] = {
    "have_shipyard", "have_keep", "have_castle", "peasants",
    "landForce", "seaForce", "airForce", "enemy_alive"};

struct Track {
    bool valid;            // we have a program counter from a previous tick
    uint32_t pc;           // blob-relative, so a reload that moves the blob is not mistaken for progress
    unsigned sameMs;       // play time the program counter has not moved
    unsigned nextStallMs;  // when to write the next stall line
};

Track g_track[kAiPlayerCount];
unsigned g_sinceReportMs = 0;
uint16_t g_lastFromSave = 0xFFFF;
Sink g_sink = nullptr;

void Emit(const char* line) {
    if (g_sink)
        g_sink(line);
    else
        logx::Write("%s", line);
}

void Forget() {
    memset(g_track, 0, sizeof(g_track));
    g_sinceReportMs = 0;
}

const uint8_t* AiState(int player) { return At<uint8_t>(kRvaAiState) + player * kAiStateStride; }

// The loaded ai.bin, or null when there is no game, no script data, or the size out-param is not believable.
const uint8_t* Blob(uint32_t& size) {
    const uint8_t* blob = *At<const uint8_t*>(kRvaAiScriptBlob);
    const uint32_t bytes = *At<uint32_t>(kRvaAiScriptBlobSize);
    if (!blob || bytes == 0 || bytes > kMaxBlobSize) {
        size = 0;
        return nullptr;
    }
    size = bytes;
    return blob;
}

// The player's program counter as an offset into the blob. False when it does not point inside the blob with room
// for the longest instruction, which is the only guard between this file and a wild read.
bool PcOffset(const uint8_t* st, const uint8_t* blob, uint32_t size, uint32_t& out) {
    const uint8_t* pc = *reinterpret_cast<const uint8_t* const*>(st + kAiOffPc);
    if (!pc || pc < blob) return false;
    const size_t delta = static_cast<size_t>(pc - blob);
    if (delta > size || size - delta < static_cast<uint32_t>(kAiMaxInstructionSize)) return false;
    out = static_cast<uint32_t>(delta);
    return true;
}

// Names the instruction at `off`. The caller has already proved that kAiMaxInstructionSize bytes are readable there.
// An opcode or condition the report does not know prints its number: this never guesses.
void DescribeOp(const uint8_t* blob, uint32_t off, const uint8_t* st, char* out, size_t cap) {
    const uint8_t op = blob[off];
    switch (op) {
        case kAiOpSet:
            _snprintf_s(out, cap, _TRUNCATE, "SET st[0x%02X] = %u", blob[off + 1], blob[off + 2]);
            return;
        case kAiOpJump: {
            uint16_t target;
            memcpy(&target, blob + off + 1, sizeof target);
            _snprintf_s(out, cap, _TRUNCATE, "JUMP 0x%04X", target);
            return;
        }
        case kAiOpSleep: {
            uint32_t steps;
            memcpy(&steps, blob + off + 1, sizeof steps);
            _snprintf_s(out, cap, _TRUNCATE, "SLEEP %u", steps);
            return;
        }
        case kAiOpWaitFor: {
            const uint8_t cond = blob[off + 1];
            switch (cond) {
                case 3:
                    _snprintf_s(out, cap, _TRUNCATE, "WAITFOR peasants >= %u", st[kAiOffPeasantTarget]);
                    return;
                case 4:
                    _snprintf_s(out, cap, _TRUNCATE, "WAITFOR landForce >= %u",
                                st[kAiOffLandWaveCount] * st[kAiOffLandWaveSize]);
                    return;
                case 5:
                    _snprintf_s(out, cap, _TRUNCATE, "WAITFOR seaForce >= %u",
                                st[kAiOffSeaWaveCount] * st[kAiOffSeaWaveSize]);
                    return;
                case 6:
                    _snprintf_s(out, cap, _TRUNCATE, "WAITFOR airForce >= %u",
                                st[kAiOffAirWaveCount] * st[kAiOffAirWaveSize]);
                    return;
                default:
                    if (cond < kAiCondCount)
                        _snprintf_s(out, cap, _TRUNCATE, "WAITFOR %s", kCondName[cond]);
                    else
                        _snprintf_s(out, cap, _TRUNCATE, "WAITFOR cond %u", cond);
                    return;
            }
        }
        default:
            _snprintf_s(out, cap, _TRUNCATE, "op %u", op);
            return;
    }
}

bool OwnsLiveUnit(const World& w, uint8_t player) {
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) == player && IsActive(u)) return true;
    }
    return false;
}

// First build-list entry the computer has not started yet: what its worker is stuck on when the list is blocked.
int BuildListIndex(int player) {
    const uint8_t* done = At<uint8_t>(kRvaAiBuildDone) + player * kAiBuildListMax;
    for (int i = 0; i < kAiBuildListMax; ++i)
        if (!done[i]) return i;
    return kAiBuildListMax;
}

void FormatLine(const World& w, int player, const uint8_t* blob, uint32_t off, unsigned sameMs, char* out, size_t cap) {
    const uint8_t* st = AiState(player);
    char op[64];
    DescribeOp(blob, off, st, op, sizeof op);

    const uint16_t* supply = At<uint16_t>(kRvaFoodSupply);
    const uint16_t* counted = At<uint16_t>(kRvaUnitsCounted);
    const uint16_t* foodFree = At<uint16_t>(kRvaFoodFreeUnits);
    const int used = static_cast<int>(counted[player]) - static_cast<int>(foodFree[player]);
    const int grown = supply[player] > kFoodCap ? kFoodCap : supply[player];

    _snprintf_s(out, cap, _TRUNCATE,
                "ai: player %d script %u pc 0x%04X %s same pc for %um | gold %d lum %d oil %d | food %d/%d | "
                "force land %u sea %u air %u | foot %u/%u arch %u/%u siege %u/%u knight %u/%u | workers %u/%u | "
                "buildlist %d/%u",
                player, At<uint8_t>(kRvaAiScriptId)[player], off, op, sameMs / 60000u,
                At<int32_t>(kRvaPlayerGold)[player], At<int32_t>(kRvaPlayerLumber)[player],
                At<int32_t>(kRvaPlayerOil)[player], used, grown,
                At<uint16_t>(kRvaLandForce)[player], At<uint16_t>(kRvaSeaForce)[player],
                At<uint16_t>(kRvaAirForce)[player],
                At<uint16_t>(kRvaAiFootCount)[player], st[kAiOffFootTarget],
                At<uint16_t>(kRvaAiArcherCount)[player], st[kAiOffArcherTarget],
                At<uint16_t>(kRvaAiSiegeCount)[player], st[kAiOffSiegeTarget],
                At<uint16_t>(kRvaAiKnightCount)[player], st[kAiOffKnightTarget],
                At<uint16_t>(kRvaPeasantCount)[player], st[kAiOffPeasantTarget],
                BuildListIndex(player), st[kAiOffBuildListLen]);
    (void)w;
}

}  // namespace

void OnNewMap() { Forget(); }

void SetSinkForTests(Sink sink) { g_sink = sink; }

void ResetForTests() {
    Forget();
    g_lastFromSave = 0xFFFF;
}

void OnTick(const World& w, unsigned elapsedMs) {
    if (!config::g.logAi) return;

    // Loading a savegame reloads the scripts and can move the blob, so nothing learned before it still applies.
    const uint16_t fromSave = *At<uint16_t>(kRvaGameFromSave);
    if (fromSave != g_lastFromSave) {
        g_lastFromSave = fromSave;
        Forget();
    }

    uint32_t size = 0;
    const uint8_t* blob = Blob(size);
    if (!blob) return;  // no game, or the script data is not where it should be: report nothing rather than guess

    const uint8_t* controller = At<uint8_t>(kRvaController);
    g_sinceReportMs += elapsedMs;
    const bool report = g_sinceReportMs >= kReportMs;
    if (report) g_sinceReportMs = 0;

    char line[512];
    for (int p = 0; p < kAiPlayerCount; ++p) {
        Track& t = g_track[p];
        uint32_t off = 0;
        if (controller[p] != 1 || !PcOffset(AiState(p), blob, size, off)) {
            t = Track{};  // not a computer, or a state block we cannot trust: keep no history for it
            continue;
        }
        if (!t.valid || t.pc != off) {
            t.valid = true;
            t.pc = off;
            t.sameMs = elapsedMs;  // the script was already on this instruction for the step just played
            t.nextStallMs = kStallMs;
        } else {
            t.sameMs += elapsedMs;
        }

        if (!OwnsLiveUnit(w, static_cast<uint8_t>(p))) continue;  // a dead player has nothing to diagnose

        if (t.sameMs >= t.nextStallMs) {
            t.nextStallMs += kStallMs;
            char op[64];
            DescribeOp(blob, off, AiState(p), op, sizeof op);
            _snprintf_s(line, sizeof line, _TRUNCATE, "ai: player %d has been on %s for %u min", p, op,
                        t.sameMs / 60000u);
            Emit(line);
        }
        if (report) {
            FormatLine(w, p, blob, off, t.sameMs, line, sizeof line);
            Emit(line);
        }
    }
}

}  // namespace aiwatch
