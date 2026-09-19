#include "world.h"

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

void IssueOrder(Unit* unit, int16_t x, int16_t y, Unit* target, uint32_t handlerRva) {
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
