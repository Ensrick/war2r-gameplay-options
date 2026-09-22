#include "world.h"

#include "log.h"
#include "resume.h"

namespace game {

uintptr_t g_base = 0;

bool BuildWorld(World& w) {
    w.units = *At<Unit*>(kRvaUnitArray);
    w.unitCount = *At<uint32_t>(kRvaUnitCount) & 0xFFFF;
    w.grid = *At<Unit**>(kRvaUnitGrid);
    w.airGrid = *At<Unit**>(kRvaAirUnitGrid);
    w.mapSize = *At<uint16_t>(kRvaMapSize);
    w.localPlayer = *At<uint8_t>(kRvaLocalPlayer);
    w.alliance = At<uint8_t>(kRvaAlliance);
    w.typeFlags = At<uint32_t>(kRvaTypeFlags);
    w.maxHpByType = At<uint16_t>(kRvaMaxHpByType);
    if (!w.units || !w.grid || w.mapSize <= 0 || w.mapSize > 256 || w.unitCount == 0) return false;
    return w.localPlayer < kMaxPlayers && At<uint8_t>(kRvaController)[w.localPlayer] == 0;  // 0 = human
}

static int g_refusedOrders = 0;

int RefusedOrderCount() { return g_refusedOrders; }

void IssueOrder(Unit* unit, int16_t x, int16_t y, Unit* target, uint32_t handlerRva) {
    // The handlers index the map grids with the order tile before they check anything (the move handler FUN_004d8690,
    // the harvest handler through FUN_004d8090 at 0x4D80BF). A positional order outside the map is a crash, so none
    // ever leaves the mod, whatever feature asked for it.
    if (!target) {
        const int size = *At<uint16_t>(kRvaMapSize);
        if (x < 0 || y < 0 || x >= size || y >= size) {
            if (++g_refusedOrders <= 5)
                logx::Write("refused an order to %d,%d outside the %dx%d map (unit type %u at %d,%d)", x, y, size, size,
                            Field<uint8_t>(unit, kOffType), Field<int16_t>(unit, kOffX), Field<int16_t>(unit, kOffY));
            return;
        }
    }
    // Remastered keeps a "resume order" per unit (+0x8D: 10 attack-move, 5 patrol, set by the player's command path) and
    // SetOrder(unit, Stop) re-issues that order on the spot when the unit is still 2+ tiles from its destination
    // (0x4EF130..0x4EF1CB), through IssueOrder with a NULL target, which wipes +0x88. The Heal and Flame Shield actions
    // call exactly that SetOrder and then read the target again (0x4E2244 / 0x4E224A): a paladin healing while an
    // attack-move was pending crashed the game on target->hp (2026-09-21, image 0x680000 + 0xE2257). The player's own
    // command path clears the resume byte before every new order (FUN_004dcc60); every order of the mod stands in for
    // a click, so it does the same. The worker code did this already; now nothing can forget it.
    resume::Remember(unit);  // what the unit was on, so it can be given back once the spell is over
    if (*At<uint32_t>(kRvaRuleset) != 0) Field<uint8_t>(unit, kOffResumeOrder) = kOrderNone;
    using OrderHandlerFn = void(__cdecl*)(Unit*);
    using IssueOrderFn = void(__cdecl*)(Unit*, int16_t, int16_t, Unit*, OrderHandlerFn);
    reinterpret_cast<IssueOrderFn>(g_base + kRvaIssueOrder)(unit, x, y, target,
                                                             reinterpret_cast<OrderHandlerFn>(g_base + handlerRva));
}

void IssueSpell(Unit* caster, uint8_t order, int16_t x, int16_t y, Unit* target) {
    *At<uint16_t>(kRvaPendingSpellOrder) = order;
    IssueOrder(caster, x, y, target, kRvaSpellOrderHandler);
    *At<uint16_t>(kRvaPendingSpellOrder) = 0;
}

void ShowMessage(const char* text) {
    using ShowMessageFn = void(__cdecl*)(const char*, int, int, int);
    reinterpret_cast<ShowMessageFn>(g_base + kRvaShowMessage)(text, 8, 100, 0);
}

}  // namespace game
