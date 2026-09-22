#include "resume.h"

#include <cstring>

#include "config.h"
#include "game.h"
#include "log.h"

using namespace game;

namespace resume {

namespace {

// The engine's own resume, read at 0x4EF130..0x4EF1CB: an attack-move keeps its destination at +0x90 / +0x92 and a
// patrol at +0x94 / +0x96, and after re-issuing one the game writes the word 0x140A at +0x8D (resume 10, state 0x14).
constexpr uint8_t kResumeAttackMove = 10;
constexpr uint8_t kResumePatrol = 5;
constexpr unsigned kForgetAfterMs = 30000;
constexpr int kMaxRecords = 64;

struct Record {
    Unit* unit;
    uint32_t serial;
    uint8_t order;   // 10 attack-move, 5 patrol
    int16_t x, y;
    uint8_t state;   // the +0x8E byte the engine keeps beside the resume order
    uint32_t sinceMs;
};

Record g_records[kMaxRecords];
int g_count = 0;
// Set while this module is handing an order back: the order goes through game::IssueOrder like any other, and
// without this Remember would file the very order being restored and the pass would feed itself.
bool g_restoring = false;
unsigned g_restored = 0;
uint32_t g_playMs = 0;

// A freshly issued order sits in the next-order slot until the unit picks it up, so both slots have to be read:
// that is what OrderOf does everywhere else in the mod.
bool IsIdle(Unit* u) {
    const uint8_t order = OrderOf(u);
    return order == kOrderStop || order == kOrderStand;
}

// An order the mod itself gave (a spell, or the eye): the unit is busy with our work, not the player's.
bool IsModOrder(uint8_t order) { return order >= kOrderSpellFirst || order == kOrderSpellEye; }

void Drop(int i) { g_records[i] = g_records[--g_count]; }

}  // namespace

void Remember(Unit* unit) {
    if (g_restoring || !config::g.resumeOrders || *At<uint32_t>(kRvaRuleset) == 0) return;
    const uint8_t order = Field<uint8_t>(unit, kOffResumeOrder);
    if (order != kResumeAttackMove && order != kResumePatrol) return;
    if (TypeOf(unit) == kTypeEye) return;  // the eye is the mod's own unit and never has a player order
    const int offset = order == kResumeAttackMove ? 0x90 : 0x94;
    const Record r = {unit,
                      Field<uint32_t>(unit, kOffSerial),
                      order,
                      Field<int16_t>(unit, offset),
                      Field<int16_t>(unit, offset + 2),
                      Field<uint8_t>(unit, kOffResumeState),
                      g_playMs};
    for (int i = 0; i < g_count; ++i)
        if (g_records[i].unit == unit) {  // a second cast before the first was handed back: keep the first order
            g_records[i].sinceMs = g_playMs;
            return;
        }
    if (g_count < kMaxRecords) g_records[g_count++] = r;
}

void Tick(const World& w) {
    g_playMs += 1;  // one simulation step; the timeout only needs to be monotonic
    if (!config::g.resumeOrders) {
        g_count = 0;
        return;
    }
    for (int i = 0; i < g_count;) {
        const Record& r = g_records[i];
        Unit* u = r.unit;
        // Gone, outside the live array, replaced in its slot, or dead: forget it.
        const uintptr_t base = reinterpret_cast<uintptr_t>(w.units), p = reinterpret_cast<uintptr_t>(u);
        const bool inArray = u && p >= base && (p - base) < static_cast<uintptr_t>(w.unitCount) * kUnitSize &&
                             (p - base) % kUnitSize == 0;
        if (!inArray || Field<uint32_t>(u, kOffSerial) != r.serial || !IsActive(u)) {
            Drop(i);
            continue;
        }
        // The player gave it something of their own, or a new resume order: leave it alone.
        const uint8_t now = OrderOf(u);
        if (Field<uint8_t>(u, kOffResumeOrder) != kOrderNone || (!IsIdle(u) && !IsModOrder(now))) {
            Drop(i);
            continue;
        }
        if (g_playMs - r.sinceMs > kForgetAfterMs / 50) {  // ~30 s of play at 50 ms a step
            Drop(i);
            continue;
        }
        if (!IsIdle(u)) {  // still casting: wait for it
            ++i;
            continue;
        }
        const uint32_t handler = r.order == kResumeAttackMove ? kRvaAttackMoveHandler : kRvaPatrolHandler;
        g_restoring = true;
        IssueOrder(u, r.x, r.y, nullptr, handler);
        g_restoring = false;
        // The engine writes the resume order back after re-issuing it (0x4EF1CB); so does the mod, or the unit
        // would forget its destination the moment anything stops it again.
        if (*At<uint32_t>(kRvaRuleset) != 0) {
            Field<uint8_t>(u, kOffResumeOrder) = r.order;
            Field<uint8_t>(u, kOffResumeState) = r.state;
            Field<int16_t>(u, r.order == kResumeAttackMove ? 0x90 : 0x94) = r.x;
            Field<int16_t>(u, (r.order == kResumeAttackMove ? 0x90 : 0x94) + 2) = r.y;
        }
        ++g_restored;
        if (config::g.logCasts)
            logx::Write("resumed %s of caster type %u at %d,%d -> %d,%d",
                        r.order == kResumeAttackMove ? "attack-move" : "patrol", TypeOf(u), Field<int16_t>(u, kOffX), Field<int16_t>(u, kOffY), r.x, r.y);
        Drop(i);
    }
}

void OnNewMap() {
    g_count = 0;
    g_playMs = 0;
}

unsigned Count() { return static_cast<unsigned>(g_count); }
unsigned RestoreCount() { return g_restored; }

}  // namespace resume
