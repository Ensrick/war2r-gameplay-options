#pragma once

enum Spell {
    kSpellHeal,
    kSpellExorcism,
    kSpellSlow,
    kSpellPolymorph,
    kSpellBloodlust,
    kSpellDeathCoil,
    kSpellHaste,
    kSpellUnholyArmor,
    kSpellCount
};

struct Config {
    bool enabled = true;
    bool spell[kSpellCount] = {true, true, true, true, true, true, true, false};
    int intervalTicks = 10;       // game steps between autocast passes
    int searchRadius = 8;         // tiles around the caster
    int combatRadius = 6;         // an ally counts as fighting when an enemy is this close to it
    bool ownUnitsOnly = true;     // friendly spells skip allied players' units
    bool castWhileAttacking = true;
    int healBelowPct = 80;
    int polymorphMinHp = 90;      // max HP of the target's unit type; casters are always eligible
    int toggleKey = 0x78;         // VK_F9, pressed together with Ctrl
    bool logCasts = false;
};

namespace config {

extern Config g;
extern const char* const kSpellKeys[kSpellCount];

void Init(const wchar_t* dllDir);  // loads autocast.ini, writing a default one if missing
bool ReloadIfChanged();            // true when the file was re-read

}  // namespace config
