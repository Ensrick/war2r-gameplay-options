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
constexpr uint32_t kRvaIssueOrder = 0xEF210;     // void __cdecl (Unit*, int16 x, int16 y, Unit* target, void (__cdecl*)(Unit*))
constexpr uint32_t kRvaSpellOrderHandler = 0xE2970;  // handler passed to IssueOrder for every spell cast
constexpr uint32_t kRvaShowMessage = 0xD3160;    // void __cdecl (const char* text, int 8, int duration, int 0), the cheat-toggle banner

// --- data ---
constexpr uint32_t kRvaLocalPlayer = 0x518CCD;        // uint8, compared against unit owner by the selection code
constexpr uint32_t kRvaMaxHpByType = 0x5177C0;        // uint16[unit type], what FUN_004ee1f0 reads
constexpr uint32_t kRvaPendingSpellOrder = 0x5348BC;  // uint16, read by the spell order handler
constexpr uint32_t kRvaUnitArray = 0x51C704;          // Unit* (contiguous array, stride kUnitSize)
constexpr uint32_t kRvaUnitCount = 0x51BFB8;          // uint32, the AI loop only uses the low 16 bits
constexpr uint32_t kRvaUnitGrid = 0x51AD6C;           // Unit** [mapSize*mapSize], one unit pointer per tile
constexpr uint32_t kRvaMapSize = 0x518D10;            // uint16
constexpr uint32_t kRvaController = 0x518CAC;         // uint8[16]: 0 = human, 1 = computer
constexpr uint32_t kRvaAlliance = 0x519578;           // uint8[16*16], [caster*16 + target] != 0 means allied
constexpr uint32_t kRvaTypeFlags = 0x5185F0;          // uint32[unit type]
constexpr uint32_t kRvaSpellsResearched = 0x519250;   // uint32[16], PUD ALOW bit layout
constexpr uint32_t kRvaManaCostByOrder = 0x4C5EB8;    // uint16[order id]
constexpr uint32_t kRvaNetGame = 0x51C6F4;            // nonzero while the game loop runs DONETWORKTURN (multiplayer)

constexpr int kUnitSize = 0x98;
constexpr int kMaxPlayers = 16;
constexpr uint8_t kNeutralPlayer = 15;

// Unit field offsets (identical to the 1999 BNE layout).
constexpr int kOffX = 0x18;             // int16 tile x
constexpr int kOffY = 0x1A;             // int16 tile y
constexpr int kOffStateFlags = 0x1E;    // low nibble: free/dying/dead/hidden
constexpr int kOffHp = 0x22;            // uint16
constexpr int kOffMana = 0x26;          // uint8
constexpr int kOffType = 0x27;          // uint8
constexpr int kOffOwner = 0x2C;         // uint8
constexpr int kOffOrder = 0x2E;         // uint8
constexpr int kOffInvisTimer = 0x44;    // uint16
constexpr int kOffArmorTimer = 0x46;    // uint16 (unholy armor)
constexpr int kOffBloodTimer = 0x48;    // uint16
constexpr int kOffHasteTimer = 0x4A;    // int16, > 0 haste, < 0 slow
constexpr int kOffFlameTimer = 0x4E;    // uint16
constexpr int kOffOrderTarget = 0x88;   // Unit*

// Unit type flags.
constexpr uint32_t kTfBuilding = 0x00000020;
constexpr uint32_t kTfUndead = 0x00008000;
constexpr uint32_t kTfCaster = 0x00020000;
constexpr uint32_t kTfAttacker = 0x00080000;
constexpr uint32_t kTfFleshy = 0x08000000;

// Caster unit types, as dispatched by the game's own caster AI (FUN_004ca4a0).
constexpr uint8_t kTypeMage = 0x0A, kTypeMageHero = 0x18;
constexpr uint8_t kTypeDeathKnight = 0x0B, kTypeDeathKnightHero = 0x15;
constexpr uint8_t kTypePaladin = 0x0C, kTypePaladinHero = 0x2C;
constexpr uint8_t kTypeOgreMage = 0x0D, kTypeOgreMageHero = 0x17;

// Orders.
constexpr uint8_t kOrderStop = 2, kOrderMovePatrol = 4, kOrderPatrol = 5;
constexpr uint8_t kOrderAttack = 8, kOrderAttackTarget = 9, kOrderAttackArea = 10, kOrderAttackWall = 11;
constexpr uint8_t kOrderDefend = 12, kOrderStand = 13, kOrderStandAttack = 14, kOrderDefendGround = 15, kOrderDefendStopped = 16;
constexpr uint8_t kOrderSpellFirst = 38;  // spell order id = kOrderSpellFirst + spell index

struct Unit;  // opaque, accessed through the offsets above

template <typename T>
inline T& Field(Unit* u, int off) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(u) + off); }

}  // namespace game
