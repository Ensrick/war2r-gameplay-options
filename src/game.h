// Warcraft II: Remastered 1.0.2.2818 (PE timestamp 1771967463) memory map.
// Every value here was read out of the shipped exe with Ghidra; see docs/RE_NOTES.md for the evidence per entry.
// All addresses are RVAs (VA at preferred base 0x400000 minus 0x400000) because the exe has ASLR enabled.
#pragma once
#include <cstdint>

namespace game {

constexpr uint32_t kPeTimestamp = 1771967463;

// --- code ---
constexpr uint32_t kRvaTickCallSite = 0xE89A6;   // `call 0x4ca440`, first call inside the per-step AI tick FUN_004e89a0
constexpr uint32_t kRvaTickCallee = 0xCA440;     // original target of that call
constexpr uint32_t kRvaMapLoadCallSite = 0xD2C46; // `call 0x4c4ba0` on the NEW-map path only (the savegame path calls it from 0x4C4602)
constexpr uint32_t kRvaFinalizeTables = 0xC4BA0;  // turns raw sight ranges into reveal-function pointers, last step of a data load
constexpr uint32_t kRvaRangeBonusInsn = 0xEE689;  // `inc al` (FE C0) in GetAttackRange FUN_004ee660: the Longbow / Lighter Axes +1
constexpr uint32_t kRvaIssueOrder = 0xEF210;     // void __cdecl (Unit*, int16 x, int16 y, Unit* target, void (__cdecl*)(Unit*))
constexpr uint32_t kRvaSpellOrderHandler = 0xE2970;  // handler passed to IssueOrder for every spell cast
constexpr uint32_t kRvaMoveHandler = 0xD8690;        // order 3 entry of the handler table at 0x8C1498; x,y MUST be on the map
constexpr uint32_t kRvaHarvestHandler = 0xD85E0;     // order 23: target = gold mine, or target null and x,y = a forest tile
constexpr uint32_t kRvaReturnHandler = 0xD8960;      // order 24: null target = the game finds the depot itself
constexpr uint32_t kRvaRepairHandler = 0xD8920;      // order 27: target = building
constexpr uint32_t kRvaShowMessage = 0xD3160;    // void __cdecl (const char* text, int 8, int duration, int 0), the cheat-toggle banner

// --- data ---
constexpr uint32_t kRvaLocalPlayer = 0x518CCD;        // uint8, compared against unit owner by the selection code
constexpr uint32_t kRvaMaxHpByType = 0x5177C0;        // uint16[unit type], what FUN_004ee1f0 reads
constexpr uint32_t kRvaPendingSpellOrder = 0x5348BC;  // uint16, read by the spell order handler
constexpr uint32_t kRvaUnitArray = 0x51C704;          // Unit* (contiguous array, stride kUnitSize)
constexpr uint32_t kRvaUnitCount = 0x51BFB8;          // uint32, the AI loop only uses the low 16 bits
constexpr uint32_t kRvaUnitGrid = 0x51AD6C;           // Unit** [mapSize*mapSize], one unit pointer per tile: land and sea units
constexpr uint32_t kRvaAirUnitGrid = 0x51AD70;        // same shape, the AIR layer: FUN_004b4a00 files a unit here when unit+0x1C & 4
constexpr uint32_t kRvaMapSize = 0x518D10;            // uint16
constexpr uint32_t kRvaController = 0x518CAC;         // uint8[16]: 0 = human, 1 = computer
constexpr uint32_t kRvaAlliance = 0x519578;           // uint8[16*16], [caster*16 + target] != 0 means allied
constexpr uint32_t kRvaTypeFlags = 0x5185F0;          // uint32[unit type]
constexpr uint32_t kRvaSpellsResearched = 0x519250;   // uint32[16], PUD ALOW bit layout
constexpr uint32_t kRvaManaCostByOrder = 0x4C5EB8;    // uint16[order id]
constexpr uint32_t kRvaExploredMap = 0x51AD60;       // uint8_t* [mapSize*mapSize] for the LOCAL player, 0x10 = never explored
constexpr uint32_t kRvaVisibleMap = 0x51AD5C;        // uint8_t* same layout, 0x10 = currently fogged
constexpr uint32_t kRvaRegionMap = 0x51AD7C;         // uint16* [mapSize*mapSize]: 0xFFFE tree, 0xFFFC tree being chopped, else region id
constexpr uint32_t kRvaUnitSizeByType = 0x517AD0;    // {uint16 w, uint16 h}[unit type], tiles (UDTA "unit size")
constexpr uint32_t kRvaPlayerGold = 0x519128;        // int32[16]
constexpr uint32_t kRvaPlayerLumber = 0x5190E8;      // int32[16]
constexpr uint32_t kRvaRuleset = 0x51C178;           // uint32, nonzero enables the Remastered resume-order byte (+0x8D)
constexpr uint32_t kRvaSightByType = 0x517608;       // uint32[110]: range 0..9 until FinalizeTables, a function pointer after
constexpr uint32_t kRvaBuildTimeByType = 0x517910;   // uint8[110], doubled into the production timer
constexpr uint32_t kRvaAttackRangeByType = 0x517E40; // uint8[110], tiles
constexpr uint32_t kRvaArmorByType = 0x517F90;       // uint8[110]
constexpr uint32_t kRvaBasicDamageByType = 0x5180E0; // uint8[110]
constexpr uint32_t kRvaPiercingDamageByType = 0x518150;  // uint8[110]
constexpr uint32_t kRvaResearchTime = 0x5188A8;      // uint8[52], PUD UGRD order
constexpr uint32_t kRvaRangeBonusDisplay = 0x4C11E4; // uint8, entry 8 of the per-upgrade-group effect table 0x8C11DC: what both
                                                     // status panels multiply the longbow / lighter axes counter by
constexpr uint32_t kRvaGoldCostByType = 0x517980;    // uint8[110], price / 10
constexpr uint32_t kRvaLumberCostByType = 0x5179F0;  // uint8[110], price / 10
constexpr uint32_t kRvaOilCostByType = 0x517A60;     // uint8[110], price / 10
constexpr uint32_t kRvaUpgradeGold = 0x5188E0;       // uint16[52], PUD UGRD order
constexpr uint32_t kRvaUpgradeLumber = 0x518948;     // uint16[52]
constexpr uint32_t kRvaUpgradeOil = 0x5189B0;        // uint16[52]
constexpr uint32_t kRvaNetGameAtLoad = 0x522F5B;     // uint8, network flag that is already valid while a map loads
constexpr uint32_t kRvaNetGame = 0x51C6F4;            // nonzero while the game loop runs DONETWORKTURN (multiplayer)

constexpr int kUnitSize = 0x98;
constexpr int kMaxPlayers = 16;
constexpr uint8_t kNeutralPlayer = 15;

// Unit field offsets (identical to the 1999 BNE layout).
constexpr int kOffSerial = 0x14;        // uint32 creation serial, unique per created unit
constexpr int kOffX = 0x18;             // int16 tile x
constexpr int kOffY = 0x1A;             // int16 tile y
constexpr int kOffStateFlags = 0x1E;    // low nibble: free/dying/dead/hidden
constexpr int kOffHp = 0x22;            // uint16
constexpr int kOffMana = 0x26;          // uint8
constexpr int kOffType = 0x27;          // uint8
constexpr int kOffOwner = 0x2C;         // uint8
constexpr int kOffOrder = 0x2E;         // uint8, the order being executed
constexpr int kOffNextOrder = 0x2F;     // uint8, written by SetOrder (FUN_004ef080); promoted at 0x4ED9C3, then reset to kOrderNone
constexpr int kOffInvisTimer = 0x44;    // uint16
constexpr int kOffArmorTimer = 0x46;    // uint16 (unholy armor)
constexpr int kOffBloodTimer = 0x48;    // uint16
constexpr int kOffHasteTimer = 0x4A;    // int16, > 0 haste, < 0 slow
constexpr int kOffFlameTimer = 0x4E;    // uint16
constexpr int kOffWorkerFlags = 0x75;   // uint8: 0x80 gold job, 0x40 lumber job, 0x20 carrying
constexpr int kOffResources = 0x82;     // uint16, gold mine / oil: what is left, in hundreds
constexpr int kOffOrderX = 0x84;        // int16, order destination when there is no target unit
constexpr int kOffOrderY = 0x86;        // int16
constexpr int kOffOrderTarget = 0x88;   // Unit*
constexpr int kOffResumeOrder = 0x8D;   // uint8, Remastered: order resumed after a stop (0x3C none, 5 patrol, 10 attack-move)

constexpr uint16_t kStateComplete = 0x80;  // building finished (full 16-bit state word at kOffStateFlags)
constexpr uint8_t kWorkerCarrying = 0x20;
constexpr uint8_t kTypeGoldMine = 0x5C;
constexpr uint16_t kRegionTree = 0xFFFE;
constexpr uint8_t kStateDying = 2;      // low nibble of kOffStateFlags; a raisable corpse is type kTypeCorpse in this state
constexpr uint8_t kTypeCorpse = 0x69;   // what the game AI's raise-dead filter (FUN_004ca8d0) looks for

// Unit type flags (PUD UDTA layout; defaults ship in Data\Rez\unitdata.dat at file offset 0x1486).
constexpr uint32_t kTfFlyer = 0x00000002;
constexpr uint32_t kTfBuilding = 0x00000020;
constexpr uint32_t kTfWorker = 0x00000100;
constexpr uint32_t kTfOilPlatform = 0x00000800;  // types 0x56 / 0x57; "oil left" lives in kOffResources like a mine's gold
constexpr uint32_t kTfUndead = 0x00008000;
constexpr uint32_t kTfCaster = 0x00020000;
constexpr uint32_t kTfAttacker = 0x00080000;
constexpr uint32_t kTfFleshy = 0x08000000;

// Caster unit types, as dispatched by the game's own caster AI (FUN_004ca4a0).
constexpr uint8_t kTypeMage = 0x0A, kTypeMageHero = 0x18;
constexpr uint8_t kTypeDeathKnight = 0x0B, kTypeDeathKnightHero = 0x15;
constexpr uint8_t kTypePaladin = 0x0C, kTypePaladinHero = 0x2C;
constexpr uint8_t kTypeOgreMage = 0x0D, kTypeOgreMageHero = 0x17;
constexpr uint8_t kTypeChogall = 0x31;  // caster flag set in unitdata.dat, but the game's own caster AI skips it
constexpr uint8_t kTypeEye = 0x2D;
constexpr uint8_t kTileUnexplored = 0x10;

// Orders.
constexpr uint8_t kOrderStop = 2, kOrderMove = 3, kOrderMovePatrol = 4, kOrderPatrol = 5;
constexpr uint8_t kOrderAttack = 8, kOrderAttackTarget = 9, kOrderAttackArea = 10, kOrderAttackWall = 11;
constexpr uint8_t kOrderDefend = 12, kOrderStand = 13, kOrderStandAttack = 14, kOrderDefendGround = 15, kOrderDefendStopped = 16;
constexpr uint8_t kOrderHarvest = 23, kOrderReturnGoods = 24, kOrderRepair = 27;
constexpr uint8_t kOrderSpellEye = 0x30;
constexpr uint8_t kOrderSpellFirst = 38;  // spell order id = kOrderSpellFirst + spell index
constexpr uint8_t kOrderNone = 60;        // "no next order"

// What the unit is doing or about to do: a freshly issued order sits in the next-order slot until the unit's
// current action step ends (the game's own UI reads it the same way at 0x4E85B2).
inline uint8_t EffectiveOrder(const uint8_t* unit) {
    return unit[kOffNextOrder] != kOrderNone ? unit[kOffNextOrder] : unit[kOffOrder];
}

struct Unit;  // opaque, accessed through the offsets above

template <typename T>
inline T& Field(Unit* u, int off) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(u) + off); }

}  // namespace game
