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
constexpr uint32_t kRvaStopHandler = 0xD8580;        // order 2 entry of 0x8C1498: SetOrder(unit, 2); reads only the unit's own tile
constexpr uint32_t kRvaShowMessage = 0xD3160;    // void __cdecl (const char* text, int 8, int duration, int 0), the cheat-toggle banner
// Spell numbers (docs/research/spells.md): every one is an instruction immediate, patched in place and byte-verified.
constexpr uint32_t kRvaFireballDamageInsn = 0xAF189;    // `mov al, 0x28` (B0 28) in FUN_004af0e0, stored to missile+0x37
constexpr uint32_t kRvaFireballMarker = 0xAE83B;        // 16 bytes in FUN_004ae7c0: missile damage == 40 selects the spell's 40-step trail
constexpr uint32_t kRvaFlameShieldDamageInsn = 0xAF437; // `mov byte [eax+0x37], 4` in FUN_004af3a0 (one orbiting flame)
constexpr uint32_t kRvaBlizzardDamageInsn = 0xAECAC;    // `mov byte [edi+0x37], 0xa` in FUN_004aec70 (one shard)
constexpr uint32_t kRvaDeathAndDecayDamageInsn = 0xAF549; // `mov byte [esi+0x37], 0xa` in FUN_004af500 (one cloud)
constexpr uint32_t kRvaWhirlwindDamageInsn = 0xAF62D;   // `mov byte [esi+0x37], 4` in FUN_004af5c0
constexpr uint32_t kRvaDeathCoilBudgetInsns[5] = {0xE1DAA, 0xE1DC5, 0xE1DDB, 0xE1DE0, 0xE1DE7};  // the five 0x32 in FUN_004e1a90
constexpr uint32_t kRvaRunesDamageInsn = 0xE2D89;       // `mov ecx, 0x32` in the rune tick FUN_004e2cd0: kill at hp <= this
constexpr uint32_t kRvaRunesSubtractInsn = 0xE2D9E;     // `add eax, -0x32` right after it: hp -= 50
constexpr uint32_t kRvaHealCapInsn = 0xE2289;           // `mov eax, 0x28` in FUN_004e2220: at most 40 HP per cast
constexpr uint32_t kRvaManaRegenReloadInsn = 0xEF586;   // `mov byte [esi+0x74], 0x28` in FUN_004ef480, after +1 mana
constexpr uint32_t kRvaManaRegenCreateInsn = 0xEDF17;   // same store for a new caster in CreateUnit FUN_004edb10
constexpr uint32_t kRvaManaRegenConvertInsn = 0xED334;  // same store in the type change FUN_004ed2c0 (knight -> paladin ...)

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
constexpr uint32_t kRvaManaCostByOrder = 0x4C5EB8;    // uint16[order id], plain .data: not saved, never reloaded, 46 readers
constexpr uint32_t kRvaOrderRange = 0x4C1744;         // uint8[order id]: cast range in tiles, 0xFF = anywhere (FUN_004d9420)
// Missiles (docs/research/autocast_all_spells.md): a pool of kMissileSize records, slot count written once at 0x4C498D.
constexpr uint32_t kRvaMissilePool = 0x51C700;        // uint8_t* [slots * kMissileSize]
constexpr uint32_t kRvaMissileSlots = 0x51BFBC;       // uint32: 400 in Remastered mode
// Runes: one table for all players, no owner (FUN_004e2ba0 places, FUN_004e2cd0 triggers, FUN_004e2ca0 clears per map).
constexpr uint32_t kRvaRuneX = 0x518D14;              // uint8[kMaxRunes]
constexpr uint32_t kRvaRuneY = 0x518D48;              // uint8[kMaxRunes]
constexpr uint32_t kRvaRuneTimers = 0x518D80;         // uint16[kMaxRunes], 0 = free slot, placed with 0x800 steps
constexpr uint32_t kRvaExploredMap = 0x51AD60;       // uint8_t* [mapSize*mapSize] for the LOCAL player, 0x10 = never explored
constexpr uint32_t kRvaVisibleMap = 0x51AD5C;        // uint8_t* same layout, 0x10 = currently fogged
constexpr uint32_t kRvaRegionMap = 0x51AD7C;         // uint16* [mapSize*mapSize]: 0xFFFE tree, 0xFFFC tree being chopped, else region id
// Terrain (TILE.cpp), evidence in docs/research/tree_regrowth.md. The three maps are always 0x8000-byte buffers
// (FUN_004c6110), so a map is at most 128 x 128, and all three are stored verbatim in a savegame.
constexpr uint32_t kRvaTileMap = 0x51AD68;           // uint16* [mapSize*mapSize]: tileset tile index (not the PUD id); the tree
                                                     // callback FUN_004eb400 rewrites it at 0x4EB49B
constexpr uint32_t kRvaSquareFlags = 0x51AD58;       // uint16* same layout, kSq* bits; FUN_004eb400 clears 0x80 at 0x4EB4B3
constexpr uint32_t kRvaTreeTable = 0x5347E8;         // uint16*: the tileset's tree removal table (n?_tree.bin), read at 0x4EB436
constexpr uint32_t kRvaTreeTableStride = 0x5347E0;   // uint16, words per table row: 10 (movzx at 0x4EB449)
constexpr uint32_t kRvaTreeTileCount = 0x5347E2;     // uint16, tree tile ids of the tileset = table rows - 1 (FUN_004ea900)
constexpr uint32_t kRvaTreeBase = 0x5347F0;          // uint16, first tree tile id: 0x66 in all four tilesets (sub at 0x4EB450)
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
// Food (COUNT.cpp). Supply is a plain counter that only two callbacks touch: farms add 4 (0x4B50EA lea eax,[edx*4]),
// every hall tier adds 1 (0x4B5182). Readers clamp it to 200 when they read it; "used" is computed, never stored.
constexpr uint32_t kRvaFoodSupply = 0x51B50C;        // uint16[16 players]
constexpr uint32_t kRvaFarmCount = 0x51B48C;         // uint16[16]: completed farms / pig farms (0x8C0B80[0x3A])
constexpr uint32_t kRvaHallCount = 0x51B52C;         // uint16[16]: town / great halls
constexpr uint32_t kRvaKeepCount = 0x51B54C;         // uint16[16]: keeps / strongholds
constexpr uint32_t kRvaCastleCount = 0x51B56C;       // uint16[16]: castles / fortresses
constexpr uint32_t kRvaGameFromSave = 0x51BFB0;      // uint16: 1 while the running game came from a savegame (mov [0x91BFB0],si at 0x4C4295)
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
constexpr uint16_t kRegionChopping = 0xFFFC;      // a tree some worker is felling right now (FUN_004eb3b0)
constexpr uint16_t kRegionLandBit = 0x4000;       // land region ids are 0x4000 | n, water ids plain n (REGM in every stock map)
constexpr uint16_t kRegionFirstSpecial = 0xFFFA;  // 0xFFFA and up are not region ids (coast, chopping, rocks, forest)
constexpr int kMaxMapSize = 128;                  // what the 0x8000-byte map buffers hold
// Square flag bits (kRvaSquareFlags). Land units are blocked by 0x09CE (table 0x8C1AC8), a building site by 0x09DE.
constexpr uint16_t kSqLand = 0x0001;         // grass 0x0001, dirt 0x0011, forest and rocks 0x0081 in the stock maps
constexpr uint16_t kSqCoast = 0x0002;
constexpr uint16_t kSqWalls = 0x000C;        // 0x04 / 0x08, set by wall placement FUN_004eb9d0
constexpr uint16_t kSqWater = 0x0040;
constexpr uint16_t kSqUnpassable = 0x0080;   // forest, rocks, walls: the one bit tree felling clears
constexpr uint16_t kSqGroundUnit = 0x0100;   // a land / sea unit is filed on the tile (FUN_004b4a00, cleared by FUN_004b5000)
constexpr uint16_t kSqAirUnit = 0x0200;      // the same for a flyer; the only bit that blocks air movement
constexpr uint16_t kSqBuilding = 0x0800;     // every tile of a building footprint (FUN_004b4910)
// Tree removal table: row s ("state", 1-based) belongs to tile id treeBase + s - 1. Rows 0..25 are the same in all four
// tilesets: 1..24 hold forest, 24 is solid forest, 25 is the only cleared state (the stump tile 0x7E).
constexpr uint16_t kTreeBaseExpected = 0x66;
constexpr int kTreeTableStride = 10;
constexpr int kTreeStateSolid = 24;
constexpr int kTreeStateCleared = 25;
constexpr int kMaxRunes = 50;
constexpr int kMissileSize = 0x40;
constexpr int kMisOffSource = 0x30;        // Unit*: the one unit its splash never hurts (FUN_004afb50)
constexpr int kMisOffType = 0x34;          // uint8
constexpr int kMisOffFlags = 0x35;         // uint8, bit 0 = free slot
constexpr uint8_t kMissileWhirlwind = 0x0C;  // FUN_004af5c0
constexpr uint8_t kStateDying = 2;     // low nibble of kOffStateFlags; a raisable corpse is type kTypeCorpse in this state
constexpr uint8_t kTypeCorpse = 0x69;   // what the game AI's raise-dead filter (FUN_004ca8d0) looks for

// Unit type flags (PUD UDTA layout; defaults ship in Data\Rez\unitdata.dat at file offset 0x1486).
constexpr uint32_t kTfFlyer = 0x00000002;
constexpr uint32_t kTfBuilding = 0x00000020;
constexpr uint32_t kTfWorker = 0x00000100;
constexpr uint8_t kTypeOilPatch = 0x5D;         // neutral; a platform built on it takes its oil over (0x4EDCB4)
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
constexpr uint8_t kOrderHolyVision = 0x26, kOrderFlameShield = 0x2A, kOrderFireball = 0x2B, kOrderInvisibility = 0x2D;
constexpr uint8_t kOrderBlizzard = 0x2F, kOrderWhirlwind = 0x34, kOrderRunes = 0x37, kOrderDeathAndDecay = 0x38;
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
