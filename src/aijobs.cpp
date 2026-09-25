#include "aijobs.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "log.h"

using namespace game;

namespace aijobs {

namespace {

// A counter that drifted again on every step (not seen in the game) must not flood the log: after this many recounts
// in a row, without one step where everything already agreed, the lines stop.
constexpr int kMaxLogLines = 20;

Sink g_sink = nullptr;
int g_logLines = 0;

void Emit(const char* line) {
    if (g_sink)
        g_sink(line);
    else
        logx::Write("%s", line);
}

struct Counts {
    uint16_t gold, lumber, repair;
    uint16_t build[kAiBuildKinds];
};

// Which units the game will still take a job off, i.e. what each counter must equal between two simulation steps:
// - the manager FUN_004dad80 (0x4A8A5E, 0x4C9C8B) and the removal path FUN_004e8840 (0x4EE553, gated by the
//   controller byte at 0x4E884C) both act only for players whose controller is 1, and only on worker types (0x100);
// - FUN_004ee380 releases a unit's jobs only while its state has none of the bits 0x07 (it returns early at the top,
//   then sets 2 = dying), so a unit with state & 7 has already been released or never will be. Workers inside a mine,
//   a hall or a building site carry state bit 0x08 and ARE still counted (FUN_004ed860 kills them through the same
//   path), which is why this does not use IsActive (mask 0x0F).
// - FUN_004db420 subtracts one per bit: 0x20 repair, 1 gold, 2 lumber, and 8 and 0x10 each one from the builder
//   entry at the unit's +0x76 index. An index past the row is not counted (the game would write outside the row).
void Recount(const World& w, int player, Counts& c) {
    memset(&c, 0, sizeof c);
    for (unsigned i = 0; i < w.unitCount; ++i) {
        Unit* u = UnitAt(w, i);
        if (OwnerOf(u) != player) continue;
        if (Field<uint16_t>(u, kOffStateFlags) & 0x07) continue;
        if (!(w.typeFlags[TypeOf(u)] & kTfWorker)) continue;
        const uint16_t job = Field<uint16_t>(u, kOffAiJob);
        if (job & kAiJobGold) ++c.gold;
        if (job & kAiJobLumber) ++c.lumber;
        if (job & kAiJobRepair) ++c.repair;
        const uint16_t kind = Field<uint16_t>(u, kOffAiBuildKind);
        if (kind < kAiBuildKinds) {
            if (job & kAiJobBuildFarm) ++c.build[kind];
            if (job & kAiJobBuild) ++c.build[kind];
        }
    }
}

uint16_t* BuilderRow(int player) { return At<uint16_t>(kRvaAiBuilders) + player * kAiBuildKinds; }

unsigned Sum(const uint16_t* row) {
    unsigned s = 0;
    for (int k = 0; k < kAiBuildKinds; ++k) s += row[k];
    return s;
}

}  // namespace

Jobs Read(int player) {
    return {At<uint16_t>(kRvaAiGoldWorkers)[player], At<uint16_t>(kRvaAiLumberWorkers)[player],
            At<uint16_t>(kRvaAiRepairWorkers)[player]};
}

void SetSinkForTests(Sink sink) {
    g_sink = sink;
    g_logLines = 0;
}

// Runs every step instead of only once after a load: the "game came from a save" word stays 1 from one load to the
// next, so a second load could not be told apart, while a counter that differs from its units only ever comes from a
// load. When everything already agrees (every fresh map, every step between loads) nothing is written.
void OnTick(const World& w) {
    if (!config::g.fixAiAfterLoad) return;
    const uint8_t* controller = At<uint8_t>(kRvaController);
    char line[768];
    size_t len = 0;
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (controller[p] != 1) continue;  // the counters of a human player are never read or written by the game
        Counts want;
        Recount(w, p, want);
        uint16_t* gold = At<uint16_t>(kRvaAiGoldWorkers) + p;
        uint16_t* lumber = At<uint16_t>(kRvaAiLumberWorkers) + p;
        uint16_t* repair = At<uint16_t>(kRvaAiRepairWorkers) + p;
        uint16_t* row = BuilderRow(p);
        if (*gold == want.gold && *lumber == want.lumber && *repair == want.repair &&
            memcmp(row, want.build, sizeof want.build) == 0)
            continue;

        char part[160];
        _snprintf_s(part, sizeof part, _TRUNCATE, "%splayer %d gold %u lumber %u repair %u build %u -> gold %u lumber %u repair %u build %u",
                    len ? "; " : "", p, *gold, *lumber, *repair, Sum(row), want.gold, want.lumber, want.repair,
                    Sum(want.build));
        if (len == 0) len = static_cast<size_t>(_snprintf_s(line, sizeof line, _TRUNCATE, "ai: recounted workers after load: "));
        strncat_s(line, sizeof line, part, _TRUNCATE);
        len = strlen(line);

        *gold = want.gold;
        *lumber = want.lumber;
        *repair = want.repair;
        memcpy(row, want.build, sizeof want.build);
    }
    if (len == 0) {
        g_logLines = 0;
        return;
    }
    if (g_logLines < kMaxLogLines) {
        Emit(line);
        if (++g_logLines == kMaxLogLines) Emit("ai: recounted workers: further recounts are not logged");
    }
}

}  // namespace aijobs
