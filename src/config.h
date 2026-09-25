#pragma once
#include <cstdint>

#include <cstring>

#include "units.h"

enum Spell {
    kSpellHeal,
    kSpellExorcism,
    kSpellSlow,
    kSpellPolymorph,
    kSpellBloodlust,
    kSpellDeathCoil,
    kSpellHaste,
    kSpellUnholyArmor,
    kSpellRaiseDead,
    // Added in the "every spell" round: all off by default. Evidence: docs/research/autocast_all_spells.md.
    kSpellHolyVision,
    kSpellFlameShield,
    kSpellFireball,
    kSpellInvisibility,
    kSpellBlizzard,
    kSpellDeathAndDecay,
    kSpellWhirlwind,
    kSpellRunes,
    kSpellCount
};

// [priority]: the order a caster tries its spells in, one list per caster. Eye of Kilrogg is not in it: it has its
// own rule in eye.cpp. The hero variants use their caster's list.
enum CasterKind { kCasterPaladin, kCasterMage, kCasterOgreMage, kCasterDeathKnight, kCasterKindCount };

struct Priority {
    // Spell ids in the order they are tried, -1 after the last one. The defaults are the order the mod has always
    // used, so a config without a [priority] section behaves as before.
    int8_t list[kCasterKindCount][kSpellCount] = {
        {kSpellHeal, kSpellExorcism, kSpellHolyVision, -1},
        {kSpellPolymorph, kSpellSlow, kSpellFireball, kSpellInvisibility, kSpellBlizzard, kSpellFlameShield, -1},
        {kSpellBloodlust, kSpellRunes, -1},
        {kSpellRaiseDead, kSpellUnholyArmor, kSpellDeathCoil, kSpellHaste, kSpellDeathAndDecay, kSpellWhirlwind, -1}};
    // true = a caster with a valid target for a spell higher in its list saves its mana for it instead of casting
    // something cheaper. false = the list is only an order.
    bool saveMana = true;
    // true = a Blizzard / Death and Decay with a spot worth casting on, blocked only by the player's own units (not
    // buildings, not walls), holds the caster: nothing further down its list is cast until the way is clear.
    bool holdForBlockedArea = true;
};

// [upgrades] keys: what one level of an upgrade line is worth. The game keeps one byte per upgrade group and
// the damage code multiplies it by the player's level counter (docs/research/damage.md).
enum UpgradeEffect {
    kUpgradeMissileDamage, kUpgradeMeleeDamage, kUpgradeShields, kUpgradeShipDamage, kUpgradeShipArmor,
    kUpgradeSiegeDamage, kUpgradeEffectCount
};

// [weapon_types] / [armor_types] / [damage_bonus]: names the player invents, the units that carry them, and one
// multiplier per (weapon type, armor type) pair. The game has no such system; the mod adds it at the four call
// sites where a normal attack's damage is decided (docs/research/damage.md).
constexpr int kMaxDamageTypes = 32;
constexpr int kDamageTypeNameLen = 32;
constexpr uint8_t kNoDamageType = 0xFF;      // this unit type carries no weapon / armor type
constexpr uint16_t kDamageBonusOne = 256;    // x256 fixed point: 256 = x1.0, no scaling

struct DamageTypes {
    char weaponName[kMaxDamageTypes][kDamageTypeNameLen] = {};
    char armorName[kMaxDamageTypes][kDamageTypeNameLen] = {};
    int weaponCount = 0, armorCount = 0;
    uint8_t weaponOf[units::kTypeCount] = {};  // filled with kNoDamageType by the constructor
    uint8_t armorOf[units::kTypeCount] = {};
    uint16_t bonus[kMaxDamageTypes][kMaxDamageTypes] = {};
    bool any = false;                          // at least one bonus differs from x1.0
    int bonusCount = 0;

    DamageTypes() {
        memset(weaponOf, kNoDamageType, sizeof(weaponOf));
        memset(armorOf, kNoDamageType, sizeof(armorOf));
        for (auto& row : bonus)
            for (uint16_t& b : row) b = kDamageBonusOne;
    }
};

// Keys of a [unit.<name>] table, same order as config::kStatKeys.
enum UnitStat {
    kStatHitPoints, kStatArmor, kStatBasicDamage, kStatPiercingDamage, kStatRange, kStatSight,
    kStatGold, kStatLumber, kStatOil, kStatBuildTime, kStatReactRange, kStatCount
};

// [spell_cost] keys, in the game's order-id order (0x26..0x38 without the unused slot 0x28).
enum SpellCostKey {
    kCostHolyVision, kCostHeal, kCostExorcism, kCostFlameShield, kCostFireball, kCostSlow, kCostInvisibility,
    kCostPolymorph, kCostBlizzard, kCostEyeOfKilrogg, kCostBloodlust, kCostRaiseDead, kCostDeathCoil, kCostWhirlwind,
    kCostHaste, kCostUnholyArmor, kCostRunes, kCostDeathAndDecay, kSpellCostCount
};

// [spell_damage] keys: every spell that deals damage, plus heal (its hit point cap per cast).
enum SpellDamageKey {
    kDamageFireball, kDamageFlameShield, kDamageBlizzard, kDamageDeathAndDecay, kDamageWhirlwind, kDamageDeathCoil,
    kDamageRunes, kDamageHeal, kSpellDamageCount
};

// [auto_production] unit classes. Workers and tankers follow their own rule; the rest form the army, split into a
// land group and a navy group whose sizes come from the map. Transports, flying machines / zeppelins, dwarves /
// sappers and heroes are in no class: the mod never builds them.
enum ProductionClass {
    kProdWorkers, kProdInfantry, kProdArchers, kProdKnights, kProdCasters, kProdFlyers, kProdSiege, kProdTankers,
    kProdDestroyers, kProdBattleships, kProdSubmarines, kProdClassCount
};
constexpr int kProdTiers = 3;
constexpr int kResourceCount = 3;  // gold, lumber, oil

struct AutoProduction {
    bool enabled = false;
    int toggleKey = 0x79;             // VK_F10, pressed together with Ctrl
    int workersTier[kProdTiers] = {12, 16, 24};  // workers wanted at the best hall tier: hall, keep, castle (0 = none)
    int foodFreeMin = 4;              // food left free for your own peasants, transports, zeppelins: the larger of
    int foodFreePercent = 10;         // these two, counted AFTER the unit the mod is about to train
    double bankMultiple = 4.0;        // train only while the spare bank holds this many of the unit's current price
    double classBankMultiple[kProdClassCount] = {};  // [auto_production.bank_multiple] per class, 0 = use the one above
    bool workersIgnoreReserve = true;  // workers ARE the economy: the count target, the food and the price, nothing else
    bool tankersIgnoreReserve = true;  // the one tanker pays for itself in oil
    // While no hostile player owns a shipyard or a warship, the mod holds at most this many of a class (-1 = no cap):
    // one tanker, five destroyers, two battleships, two submarines. Everything else is uncapped.
    int noEnemyNavyCap[kProdClassCount] = {-1, -1, -1, -1, -1, -1, -1, 1, 5, 2, 2};
    double reserveExtra = 0.25;       // bank kept for upgrades: the dearest one + this x all the others
    double classUpgradeBias[kProdClassCount] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};  // [auto_production.class_upgrade_bias], -1 = upgrade_bias
    double upgradeBias = 0.25;        // each upgrade level of a class's line raises its share by this much
    int fillerMin = 10;               // nothing of the mix affordable here: build what the bank buys this many of
    // Being able to buy this many of a class is "plenty": above it the class is at its full configured share and
    // how much the bank holds stops counting, below it its share shrinks in proportion to what the bank buys.
    int plentyUnits = 10;
    // The class furthest behind its share cannot be paid for, and a cheaper one at the same building would spend the
    // very resource it is waiting for: the building keeps its money instead. Gives up after this many seconds without
    // that resource growing, so a stalled economy can never deadlock a building. 0 = never save up.
    int saveUpSeconds = 60;
    double navyWeight = 1.0;          // x the navy share the map asks for
    int navyMax = 80;                 // percent of the army, ships at most
    bool unitClass[kProdClassCount] = {true, true, true, true, true, true, true, true, true, true, true};
    // Land army shares in percent, per hall tier: infantry, archers, knights, casters, flyers, siege.
    int land[kProdTiers][kProdClassCount] = {
        {0, 75, 20, 0, 0, 0, 5, 0, 0, 0, 0},
        {0, 10, 25, 60, 0, 0, 5, 0, 0, 0, 0},
        {0, 0, 15, 40, 25, 15, 5, 0, 0, 0, 0}};
    // Navy shares in percent, per hall tier: destroyers, battleships, submarines (submarines never above 20 %).
    int navy[kProdTiers][kProdClassCount] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 80, 20, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 25, 60, 10},
        {0, 0, 0, 0, 0, 0, 0, 0, 25, 60, 10}};
};

// One multiplier tree, used three times: [health], [costs], [time].
// Effective multiplier = all x race.all x (race.units | race.research umbrella) x group. Everything defaults to 1.0.
struct RaceMultipliers {
    double all = 1.0;         // everything of the race: units (ships included), structures, research
    double units = 1.0;       // umbrella over every unit group
    double structures = 1.0;  // umbrella over both structure groups
    double research = 1.0;    // umbrella over every research group ([costs] / [time])
    double unit[units::kUnitGroupCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    double structure[units::kStructureGroupCount] = {1, 1};
    double researchGroup[units::kResearchGroupCount] = {1, 1, 1, 1, 1, 1};
};

struct Multipliers {
    double all = 1.0;         // everything: units (ships included), structures, research. Same in all three sections
    double units = 1.0;       // masters per kind, all races
    double structures = 1.0;
    double research = 1.0;
    RaceMultipliers race[units::kRaceCount];

    double Unit(units::Race r, units::UnitGroup g) const { return all * units * race[r].all * race[r].units * race[r].unit[g]; }
    double Structure(units::Race r, units::StructureGroup g) const {
        return all * structures * race[r].all * race[r].structures * race[r].structure[g];
    }
    double Research(units::Race r, units::ResearchGroup g) const {
        return all * research * race[r].all * race[r].research * race[r].researchGroup[g];
    }
};

struct Config {
    // [general]
    bool enabled = true;
    int toggleKey = 0x78;         // VK_F9, pressed together with Ctrl
    int intervalTicks = 10;       // game steps between autocast passes
    bool logCasts = false;
    bool logAi = false;           // read-only diagnostic for the computer players, see src/aiwatch.cpp

    // [autocast]
    int searchRadius = 8;         // tiles around the caster
    int combatRadius = 6;         // an ally counts as fighting when an enemy is this close to it
    bool ownUnitsOnly = true;     // friendly spells skip allied players' units
    bool castWhileAttacking = true;
    int channelManaReserve = 0;   // a Blizzard / Death and Decay the mod started is stopped below this mana; 0 = never
    int areaMinEnemies = 3;       // Blizzard, Death and Decay, Whirlwind: enemies within 2 tiles of the target tile
    int areaBuildingValue = 3;    // an enemy building in that blast is worth this many units when the tile is picked
    bool resumeOrders = true;     // hand an attack-move / patrol back to a caster once its spell is done
    int areaFriendlyClearance = 4;  // tiles around a Blizzard / Death and Decay aim tile that must hold nothing of yours
    int fireballMinEnemies = 2;   // enemies the Fireball's splash line would hit; 1 = the computer's own rule

    // [spells]
    // Out of the box only Heal, Slow, Bloodlust and Raise Dead run; everything else is opt-in. Holy Vision may move the
    // camera to its target for your own paladins [unverified, test in game].
    bool spell[kSpellCount] = {true,  false, true,  false, true,  false, false, false, true,
                               false, false, false, false, false, false, false, false};

    // [heal]
    int healMinMissingHp = 10;    // a scratch is not worth a paladin's attention
    int healBelowPct = 100;
    int healCooldownSeconds = 0;   // shared by Heal and Exorcism, per caster, 0 = no cooldown
    bool healCooldownForComputer = true;  // the computer's paladins keep to the same timer
    int healUrgentBelowPercent = 10;  // a heal target at or below this share of its maximum hit points cannot wait       // optional extra gate, 100 = off

    // [polymorph] targets: rank by unit type, 1 = first choice, 0 = never
    uint8_t polymorphRank[256] = {};

    // [haste]
    bool hasteFlyersOnly = true;

    // [eye_of_kilrogg]
    bool eyeCast = false;
    int eyeCastAtMana = 255;        // the computer only casts it at full mana
    int eyeMaxActive = 3;
    bool eyeAutoScout = false;

    // [gold_mines] / [oil_platforms]
    bool goldMinesUnlimited = false;
    bool oilPlatformsUnlimited = false;
    // [food]
    bool hallFood = false;            // halls give hallFoodAmount food instead of the game's 1
    int hallFoodAmount = 5;           // per town hall / keep / castle (and the orc ones); a farm gives 4
    double goldMinesAmount = 1.0;     // x the gold in every mine, once, when a new map starts
    double oilAmount = 1.0;           // x the oil in every patch and platform, likewise

    // [workers]
    bool workerAutoHarvest = false;
    int workerHarvestIdleSeconds = 10;
    int workerHarvestRadius = 5;
    bool workerAutoRepair = true;
    int workerRepairIdleSeconds = 1;
    int workerRepairRadius = 10;

    // [trees]
    bool treesRegrow = false;         // felled forest grows back
    int treesRegrowMinMinutes = 10;   // play time a stump waits, counted from when the mod first saw it: every stump
    int treesRegrowMaxMinutes = 20;   // draws its own wait from min..max, so a felled patch fills back in gradually
    int treesBuildingDistance = 3;    // no regrowth this close (in tiles) to a building or a wall
    int treesUnitDistance = 3;        // nor this close to a ground unit (flyers do not count)

    // [health] [costs] [time] [unit.*]: applied once when a NEW map starts (a savegame keeps the values it was made with)
    // 1.0 = the game's own numbers. In [health], "all" and the unit groups never touch structures; structures only
    // follow their own two keys (buildings, building_upgrades).
    Multipliers health, costs, time;

    // [range]: what Longbow / Lighter Axes add to attack range
    int rangeUpgradeBonus = 1;

    // [spell_cost] / [spell_damage] / [mana]: live, re-applied every step (neither the cost table nor the patched code
    // is part of a savegame). -1 = the game's own number. Global: the computer's casters get the same numbers.
    double spellCostAll = 1.0;          // x every mana cost, rounded up
    int spellCost[kSpellCostCount];     // heal / exorcism: mana per hit point
    double spellDamageAll = 1.0;        // x every damage number and the heal cap; heal / exorcism get a lower price
    int spellDamage[kSpellDamageCount];
    double manaRegen = 1.0;             // x the rate casters regain mana (the game: 1 point per 40 steps)

    // [unit.<name>] and [building.<name>]: base stats by type id, -1 = the game's own value. The multipliers apply on top.
    int32_t unitStat[256][kStatCount];
    Config() {
        for (auto& row : unitStat)
            for (int32_t& v : row) v = -1;
        for (int& v : spellCost) v = -1;
        for (int& v : spellDamage) v = -1;
    }

    // [heroes]
    bool isHero[256] = {};          // unit types listed in [heroes] units
    bool heroRegen = false;         // [heroes] regen
    int heroRegenPerSecond = 2;     // used while heroRegen is on; 0 also means off
    bool heroRegenMineOnly = false; // regen_for = "mine"

    // [unit_regen]: every unit and ship (never a structure); heroes keep their own numbers while [heroes] regen is on
    bool unitRegen = false;
    int unitRegenPerSecond = 1;
    bool unitRegenMineOnly = false;

    // [auto_production] and its sub-tables: your idle production buildings train by themselves (never the computer's)
    AutoProduction production;
    Priority priority;

    DamageTypes damageTypes;

    // [upgrades]: -1 = the game's own number for that line.
    int upgradeEffect[kUpgradeEffectCount] = {-1, -1, -1, -1, -1, -1};
};

namespace config {

extern Config g;
extern const char* const kSpellKeys[kSpellCount];
extern const char* const kStatKeys[kStatCount];
extern const char* const kSpellCostKeys[kSpellCostCount];
extern const char* const kSpellDamageKeys[kSpellDamageCount];
extern const char* const kProductionClassKeys[kProdClassCount];
extern const char* const kCasterKindKeys[kCasterKindCount];
extern const char* const kUpgradeEffectKeys[kUpgradeEffectCount];

// Loads <dir>\gameplay_options.toml, writing the default file first if it is missing.
// Returns false when the file has a syntax error (the previous / default settings stay in force).
bool Init(const wchar_t* dllDir);
// Re-reads the file when its timestamp changed. 0 = unchanged, 1 = reloaded, -1 = changed but has a syntax error.
int ReloadIfChanged();

}  // namespace config
