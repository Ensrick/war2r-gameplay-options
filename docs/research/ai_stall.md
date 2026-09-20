# Why the computer player stops attacking (static RE, Warcraft II: Remastered 1.0.2.2818)

Target: `Warcraft II.exe` 1.0.2.2818, PE timestamp 1771967463, 32-bit, preferred base 0x400000; RVA = VA - 0x400000.
Static analysis only: Ghidra 12.1.3 headless, capstone disassembly, raw dumps of `Data\Rez\ai.bin` and
`Data\Rez\unitdata.dat`, and byte-level decoding of four savegames. **Nothing here was run in the game.** Anything
inferred rather than read is marked `[unverified]`.

## Summary

1. The computer player is a **4-opcode script interpreter**; its only blocking instruction, `WAITFOR` (`0x4CA2D0`),
   re-polls its condition **every simulation step for ever, with no timeout**. A condition that stops being reachable
   stops the player attacking permanently, while its build and train logic keeps running - it keeps its resources.
2. **The mod is not the cause.** Every `[costs]` multiplier is 1.0, every per-unit override is *cheaper and faster*
   than vanilla, keep and castle prices are untouched, and `hall_food` raises the computer's supply. Nothing in the
   config blocks anything the computer needs.
3. The load path **reads** as though it should corrupt the AI's army counters - unpack restores them from the file,
   the reset skips them, the recount adds every live unit again - but the savegames say it does not: measured against
   the live unit count decoded from the same file, every counter that gates land production and land attacks is
   **exactly right** (ratio 1.00), in a file that has itself been through a load. The contradiction is **not
   resolved**; section 3 lists what was checked. Treat the load path as **`[unverified]`**, not as a known bug.
4. One counter group is provably wrong in `_Nerzhul 9.sav`: player 6's sea force reads 5, destroyers 3, battleships 2,
   submarines 3, while its whole fleet is **two oil tankers and no warship in any state**. The cause is
   **`[unverified]`** - a stale restore, a death path that does not decrement, and the load path above all fit.
5. What the save does show at 12:14: player 3 parked on `WAITFOR have_castle` with **1000 lumber against the 1200 a
   castle costs**, and player 6 healthy (landForce 15 against a threshold of 6, 24950 gold, 48950 lumber).
   **Verdict: mechanism proven, cause unproven.** Confidence low-to-medium until a save of the stalled state exists;
   the diagnostic log line under Recommendations is what settles it.

---

## Evidence

### 1. The AI script interpreter

`FUN_004CA440` runs once per simulation step, called first thing from the AI tick `FUN_004E89A0` (the mod's hook site
`0x4E89A6` is this call):

```c
// FUN_004ca440, players 0..7 only
for (p = 0; p < 8; p++) {
    if (controller[p] != 1) continue;                 // 0x918CAC, 1 = computer
    st = (int *)(0x91D650 + p * 0x30);
    if (*st == 0) {                                   // wait counter expired
        currentAiPlayer = p;                          // 0x91D958, read by every condition
        do {
            op = *(u8 *)st[1];                        // st+4 = script program counter
            st[1] += 1;
            opTable_0x8C3DE8[op](st);
        } while (*st == 0);                           // keep running until something sets a wait
    } else {
        *st -= 1;
    }
}
```

The script blob is loaded once by `FUN_004CA360`:

```c
DAT_0091d950 = FUN_00484e70("Rez\\ai.bin", 0, &DAT_0091d954,
                            "...\\War2BNE\\LANG\\Source\\ICESEQ.cpp", 0x126);
```

`FUN_004CA390` initialises one block per player from the blob (`aiId` comes from the map's `AI` section, handler
`FUN_004D23E0`, which reads 16 bytes into `0x91D640`):

```c
hdr        = *(u16 *)(blob + aiId * 2);
st[0x23]   = blob + *(u16 *)(blob + hdr);        // build list
st[0x27]   = blob + *(u16 *)(blob + hdr + 2);    // per-building-type delay table
st[0x00]   = 0;                                  // wait
st[0x22]   = 0xFF;                               // build list length
st[0x04]   = blob + hdr + 4;                     // program counter
```

#### Per-player AI state block - `0x91D650 + p * 0x30`

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x00 | u32 | wait counter, decremented once per step | `FUN_004CA440` |
| +0x04 | u8* | script program counter | `FUN_004CA440`, `FUN_004CA160` |
| +0x09 / +0x0A / +0x0B | u8 | launch land / sea / air attack (cleared after dispatch) | `FUN_004CC020` |
| +0x0C | u8 | set to 1 for all 8 players at map init | `FUN_004CBCA0` (`0x4CBE20`) |
| +0x0D / +0x0E | u8 | land units per wave / number of waves | `FUN_004CC020`, `FUN_004CA240` |
| +0x0F / +0x10 | u8 | sea units per wave / number of waves | `FUN_004CC020`, `FUN_004CA270` |
| +0x11 / +0x12 | u8 | air units per wave / number of waves | `FUN_004CC020`, `FUN_004CA2A0` |
| +0x13 | u8 | peasant target | `FUN_004CA1E0`, `FUN_004ADB30` |
| +0x14 / +0x15 / +0x16 / +0x17 | u8 | wanted footmen / archers / siege / knights | `FUN_004AD700` |
| +0x18 .. +0x21 | u8 | wanted counts for the other production buildings | `FUN_004AD900`, `FUN_004ADC40`, ... |
| +0x22 | u8 | build list length (entries of +0x23 that are active) | `FUN_004E8A0B`, `FUN_004DA300` |
| +0x23 | u8* | build list, terminated by 0xFF | `FUN_004DA300` |
| +0x27 | u16* | per-building-type AI think delay, 0xFFFF = never | `FUN_004BE4F0` |

Globals: `0x91D640` u8[16] AI script id per player, `0x91D950` blob base, `0x91D954` blob size, `0x91D958` the player
whose script is running.

#### The four opcodes - table `0x8C3DE8`

| Op | Handler | Encoding | Effect |
|---|---|---|---|
| 0 | `0x4CA140` | `00 field value` | `st[field] = value` |
| 1 | `0x4CA160` | `01 lo hi` | `pc = blob + u16` |
| 2 | `0x4CA310` | `02 dword` | `wait = dword` (sleep that many steps) |
| 3 | `0x4CA2D0` | `03 cond` | poll: see below |

`WAITFOR` is the whole story:

```
0x4ca2d8: mov eax, [esi+4]          ; pc, pointing at the condition byte
0x4ca2db: movzx eax, byte [eax]
0x4ca2de: mov eax, [eax*4 + 0x8c3dc8]
0x4ca2e5: call eax
0x4ca2ec: test eax, eax
0x4ca2ee: setne cl
0x4ca2f1: lea ecx, [ecx*2 - 1]      ; true -> +1, false -> -1
0x4ca2f8: add [esi+4], ecx          ; false: step BACK onto the 03 opcode byte
0x4ca2fb: mov dword [esi], 1        ; wait 1 step, then run the same opcode again
```

**There is no counter, no timeout and no alternative branch.** A false condition costs one step and is retried for
ever. The build list, the training functions and the per-unit combat AI all keep running meanwhile, which is why a
stalled computer still looks alive, still gathers, and still sits on gold.

#### The eight conditions - table `0x8C3DC8`

| # | VA | Test |
|---|---|---|
| 0 | `0x4CA180` | `shipyards[p] != 0` (`0x91B60C`) |
| 1 | `0x4CA1A0` | `keeps[p] != 0` (`0x91B54C`) |
| 2 | `0x4CA1C0` | `castles[p] != 0` (`0x91B56C`) |
| 3 | `0x4CA1E0` | `peasants[p] >= st[0x13]` (`0x91B66C`) |
| 4 | `0x4CA240` | `landForce[p] >= st[0x0E] * st[0x0D]` (`0x91B9EC`) |
| 5 | `0x4CA270` | `seaForce[p]  >= st[0x10] * st[0x0F]` (`0x91BA0C`) |
| 6 | `0x4CA2A0` | `airForce[p]  >= st[0x12] * st[0x11]` (`0x91BA2C`) |
| 7 | `0x4CA200` | some player `i < 8` is not allied (`0x919578`) and has peasants |

#### Launching a wave

`FUN_004CC020`, every 50 steps from the AI tick (`0x4E89CC`), for each computer player whose unit list
(`0x934848[p]`) is not empty:

```c
for (slot = 0; slot < 3; slot++)                      // 0 land, 1 sea, 2 air
    if (st[0x09 + slot]) {
        waveSize  = st[0x0D + slot * 2];              // 0x921BDD
        waveCount = st[0x0E + slot * 2];
        currentSlot = slot;                           // 0x921BDC
        for (i = 0; i < waveCount; i++) FUN_004cc120(p);
        st[0x09 + slot] = 0;                          // consumed even if nothing was sent
    }
```

`FUN_004CC120` picks the target (`FUN_004CC3B0` = the **human** player, `controller == 0`, with the most units) and
walks the player's unit list handing out attack orders through `FUN_004CCEC0` to at most `waveSize` units that pass the
slot's filter: land `0x4CBAE0`, sea `0x4CBB20`, air `0x4CBB60` (table `0x8C3E04`); target finders `0x4CB770` /
`0x4CB8E0` / `0x4CB9C0` (table `0x8C3DF8`).

### 2. The force counters the waits read

`FUN_004B5220(unit, delta)`, called from CountAdd / CountRemove for every **complete** unit whose `+0x5F & 2` is clear:

```c
if (counter_0x8C0B80[type] && !(unit[0x5f] & 2) && counter_0x8C0D38[type])
    *(short *)(counter_0x8C0D38[type] + owner * 2) += delta;      // per-type army counter
f = typeFlags[type];                                               // 0x9185F0
if ((f & 0x1F) && (f & 0x80000) && !(f & 0x10A)) { landForce[owner] += delta; return; }  // 0x4B5281
if ((f & 0x1F) && (f & 0x80000) && (f & 8))      { seaForce[owner]  += delta; return; }  // 0x4B52A1
if ((f & 0x1F) && (f & 0x80000) && (f & 2))      { airForce[owner]  += delta; }          // 0x4B52C1
```

`0x10A` = worker | ship | flyer. Flags read out of `Data\Rez\unitdata.dat` (file offset 0x1486, u32 per type):
footman/grunt/knight/ogre/archer `0x08080001`, ballista `0x00084004`, destroyer `0x00080008`, gryphon `0x08080082`.
So land attackers do qualify for `landForce`.

The per-type counter pointers (`0x8C0D38`, index = unit type) map to a block of 12 `u16[16]` arrays:

| VA | Counts |
|---|---|
| `0x91B86C` | footman, grunt, Grom, Danath, Korgath |
| `0x91B88C` | archer, axethrower, ranger, berserker, Alleria |
| `0x91B8AC` | ballista, catapult |
| `0x91B8CC` | knight, ogre, paladin, ogre-mage, Dentarg, Turalyon |
| `0x91B8EC` / `0x91B90C` / `0x91B92C` / `0x91B94C` | destroyers / transports / battleships / submarines |
| `0x91B96C` | mage, death knight, Teron, Khadgar |
| `0x91B98C` / `0x91B9AC` / `0x91B9CC` | flying machines, zeppelins / dwarves, sappers / gryphons, dragons |

### 3. The load path, and why it is [unverified]

`FUN_004C4280` (LoadGameOrMap) runs, in order: unpack the saved globals (`0x4C43AA call 0x4E0AB0`), then
`0x4C45CB call 0x4B5390` (ZeroCounters, `saveBuf` in the argument), then `0x4C460D call 0x4EE210` (load the unit array
and re-count every unit).

`FUN_004B5390` zeroes `0x91B38C..0x91B86B` unconditionally, but the army and force counters only on a new map:

```
0x4b55d0: cmp dword ptr [ebp + 8], eax      ; eax = 0: saveBuf == 0 ?
0x4b55d3: jne 0x4b572e                      ; a savegame load SKIPS the whole block below
...          zeroes 0x9193F0.., 0x91B86C, 0x91B88C, 0x91B8AC, 0x91B8CC, ...,
             0x91B9EC (land), 0x91BA0C (sea), 0x91BA2C (air)
```

The tail of `FUN_004EE210` then re-adds every live unit:

```c
while (n--) { if ((u[0x1e] & 5) == 0) FUN_004f0bc0(u);
              if ((u[0x1e] & 7) == 0) FUN_004b52d0(u);   // CountAdd -> FUN_004b5220(u, +1)
              u += 0x98; }
```

The counters really are restored from the file first. The unpack `FUN_004AA6E0` copies them straight out of the save
buffer (`edx`) into the live arrays, immediately before the reset that skips them:

```
0x4aaf41: movups xmm0, xmmword ptr [edx + 0x134]
0x4aaf4a: movups xmmword ptr [0x91b86c], xmm0      ; ... and so on through 0x91b9dc
0x4aaee7: movups xmm0, xmmword ptr [edx + 0xd4]
0x4aaeee: movups xmmword ptr [0x91b9ec], xmm0      ; land, sea and air force counters
```

Read literally, the three steps give **counter = value restored from the file + live count** after every load, and
nothing recomputes these counters during play - they only ever move by +1 / -1 events.

**The savegames contradict that, and the contradiction is unresolved.** `_Nerzhul 9.sav` was written at 12:14 in a
session whose first tick is at 12:08:34 with no new-map line in the mod's log, so that game came from a load; yet its
land counters match the live unit count exactly (section 7). Everything below was checked and none of it explains the
discrepancy:

| Checked | Result |
|---|---|
| Does the recount run on the load path at all? | **Yes.** The `CountAdd` loop is in the tail of `FUN_004EE210`, outside the new-map / from-save branch, so it runs on both paths |
| Order inside `FUN_004C4280` | unpack `0x4C43AA` (guarded by `test si, si` = from-save only), reset `0x4C45CB`, AI init `0x4C45D1`, units + recount `0x4C460D` - the order is as stated |
| Is the reset really told "from a save"? | Its argument is `esi`, the value returned by `0x4C45BD call 0x4C6110`, threaded on as the read position; it is non-zero on the load path |
| Does the unpack really write the counters? | **Yes**, and unconditionally within that function: `_DAT_0091b9ec = *(u32 *)(buf + 0xF40)` ... `_DAT_0091b86c = *(u32 *)(buf + 0xF70)`, and the packer `FUN_004AB090` writes the same offsets |
| Do the packer's buffer offsets match the file? | **No, and this is the loose end.** In the buffer the arrays sit 0x10 apart (8 players), in the savegame file the same arrays sit 0x20 apart, so the globals block in the file is not the packer's buffer laid down verbatim and buffer offsets cannot be mapped to file offsets with confidence |

Until that last row is settled the load path must be treated as **`[unverified]`**. What *is* certain from the file is
the naval discrepancy: player 6's sea force reads 5 and its destroyer / battleship / submarine counters 3 / 2 / 3,
while the unit array holds two oil tankers for that player and **no warship at all, in any state** (so no
"already dying, already decremented" explanation). Something leaves those counters above the truth; which of the
candidates it is was not determined.

### 4. What the inflated counters do

**(a) Production stops.** `FUN_004AD700` (barracks; the shipyard `0x4AD900`, lumber mill `0x4ADC40`, foundry
`0x4ADD00`, blacksmith `0x4ADDC0`, church `0x4ADE90`, mage tower `0x4ADF50`, aviary `0x4AE070`, inventor `0x4AE0E0`
are the same shape) trains only while the counter is below the script's target:

```c
if (st[0x14] != 0 && footmen[owner] == 0)  StartProduction(b, race, 0);        // 0x91B86C
if (st[0x17] != 0 && knights[owner] == 0)  ...                                 // 0x91B8CC
if (archers[owner] < st[0x15]) ...                                             // 0x91B88C
if (siege[owner]   < st[0x16]) ...                                             // 0x91B8AC
if (knights[owner] < st[0x17]) ...
if (footmen[owner] < st[0x14]) ...
```

Targets in the campaign scripts are 2..10, so residue of that size puts a target out of reach permanently: the
computer believes its army is full, trains nothing, and its gold and lumber pile up. That would match "it stopped
attacking although it had all the resources it needed" - but section 7 shows it was **not** happening in the one save
we can measure, so this remains a mechanism, not a finding.

**(b) The waits go wrong in both directions.** An inflated `landForce` makes `WAITFOR landForce >= n` pass while the
real army is much smaller, so the script races ahead and launches waves that `FUN_004CC120` cannot fill (it sends
whatever units it finds, then stops). The script then arrives early at the next building-tier wait - and that one it
cannot fake.

### 5. The building-tier waits, and why they are terminal

`have_keep` / `have_castle` need the hall AI to run the upgrade. `FUN_004ADB30`:

```c
if (!(b[0x1e] & 0x80) || (b[0x1c] & 0x10)) return;                 // complete and idle
if (peasants[owner] < st[0x13]) { StartProduction(b, peon, 0); return; }   // <<< gate
if (b->type == hall) {
    if (want[owner][0x98] && barracks[owner] && StartProduction(b, keep, 3)) FUN_004ae160(0x98, b);
} else if (b->type == keep && want[owner][0x99] && stables[owner]
           && blacksmith[owner] && lumberMill[owner]
           && StartProduction(b, castle, 3)) FUN_004ae160(0x99, b);
```

`want[owner][n]` is `0x918F30 + owner * 0x1B + n` (`0x4ADB90 cmp byte [eax + 0x918fc8], 0` and
`0x4ADBDF ... 0x918fc9`, written at `0x4DA4BC` and `0x4E8A3F`, cleared at `0x4AE1A3`). It is set by walking the build
list, `FUN_004DA300`:

```c
for (i = 0; i < st[0x22] && i < 0x40; i++) {
    if (done[owner][i]) continue;                     // 0x934358 + owner * 0x40
    e = buildList[i];
    if ((char)e < 0) { want[owner][e] = 1; return; }  // a research / upgrade entry BLOCKS the queue
    if (!requirement_0x8C5A28[e](owner)) return;
    if (FUN_004ac610(worker, type)) return;           // cost check: too expensive -> queue blocked
    ... find a site, issue the build order ...
}
```

So the build list is a **strict queue**: the first unfinished research-class entry stops it, and an entry only counts
as done once its production actually starts. In script 40's list
`4a 3c 4c 52 88 98 86 42 80 89 87 81 99 3e ...` the castle (`0x99`) sits behind hall, barracks, lumber mill,
blacksmith, one upgrade, **keep (`0x98`)**, another upgrade, stables and four more upgrades. Every one of those must
succeed, and the peasant gate above must be satisfied, or `WAITFOR have_castle` never becomes true.

### 6. Savegame decode

The savegame writer `FUN_004BF8E0` packs each player's 0x30-byte AI block and converts its three pointers (pc +0x04,
build list +0x23, delay table +0x27) into blob offsets. Decoder: `aisave.py` (finds the 8-record array by requiring
every saved pc to land on a valid opcode), `aitail.py` (disassembles a script), `aidis.py` (dumps all scripts).

Calibration note: the offsets stored in the save are **0x66 lower** than the offsets of the same structures in the
shipped loose `Data\Rez\ai.bin`. The constant was confirmed five ways (four distinct
`{build list, delay table}` header pairs, plus the passive script's `SLEEP 65535` / `JUMP` loop landing exactly on the
saved pc). The shipped loose file has an 83-entry script table, the blob the running game uses has a 32-entry one and
is otherwise the same data - **why the two builds differ is [unverified]**, but the mapping is exact.

#### Which player is which

The controller table (`0x918CAC`, u8[16]: 0 human, 1 computer, 2 rescue / neutral, 3 unused) sits at save file offset
`0x3B0`:

| Save | Controller bytes, players 0..7 | Computers |
|---|---|---|
| `D Nerzhul 8.sav` | `01 03 01 03 02 00 01 03` | 0, 2, 6 - human is 5 |
| `_Nerzhul 9.sav` | `03 03 03 01 03 00 01 02` | 3, 6 - human is 5 |
| `A Nerzhul 10 The Dark Portal.sav` | `02 01 01 01 03 00 01 03` | 1, 2, 3, 6 - human is 5 |

This cross-validates the AI decode exactly: in every file, precisely the slots marked `1` have a script that has
advanced past its first instruction, and every other slot still sits on the untouched default. Two further
independent confirmations that the human is player 5: the local player's death knight hero decodes out of the unit
array with `+0x2C = 5` (and `+0x22 = 1440` hit points, which is the hero base 180 multiplied by the config's 2 x 2 x 2),
and every enemy in the mod's session log is owner 6 (many entries) or owner 3 (a few).

**Which of them is the red one was not determined.** No colour data was found: nothing in the decoded save carries a
per-player colour, and the exe has no player-colour string table (the only "Violet" / "Orange" / "White" strings in
the binary belong to a game-controller name list). Warcraft II's classic order is slot 0 red, 1 blue, 2 green,
3 violet, 4 orange, 5 black, 6 white, 7 yellow, which would make these two **violet** and **white** rather than red -
but whether this build or this map keeps that order is `[unverified]`, so it should not be relied on. By weight of
contact in the session log, the enemy he fought almost all of the time is **player 6**; player 3 appears twice.

#### `_Nerzhul 9.sav` - saved mid-mission, about 25-35 minutes before the stall was noticed

| Player | Script | Wait | Program counter | Parked on |
|---|---|---|---|---|
| 3 (computer) | 41 | 1 | `0x30BE` | **`WAITFOR have_castle`** |
| 6 (computer) | 40 | 2924 | `0x2FC8` | mid-`SLEEP`, next stop `WAITFOR landForce >= 6` at `0x2FCB` |
| 0,1,2,4,5,7 | 0 | 1 | `0x1BA6` | first instruction of the default script - never ran (not computers) |

Player 3's state: `land wave 1 x 5`, `peasant_target 8`, `buildlist_len 13`, unit targets
`st[0x14..0x17] = 6 3 0 4`. Player 6: `land wave 1 x 6`, `air wave 1 x 2`, unit targets `6 4 0 4`.

Resources (verified against the mod's own log line for the local player at 12:14:21, "gold 6900 lum 750 oil 1000"):

| Player | gold | lumber | oil |
|---|---|---|---|
| 3 (computer, waiting for a castle) | 3750 | **1000** | 4700 |
| 5 (human) | 6900 | 750 | 1000 |
| 6 (computer) | 24950 | 48950 | 55150 |

**A castle costs 2500 gold, 1200 lumber, 500 oil.** Player 3 has the gold and the oil, one keep, eight peasants
against a target of eight, a barracks, stables, blacksmith and lumber mill - and is **200 lumber short**. That is the
proximate reason it is sitting on `WAITFOR have_castle` at 12:14. Whether it stayed short is not knowable from this
file. Player 6 is short of nothing.

### 7. The decisive measurement: counters against live units

The savegame stores the unit array as 1600 records; the writer `FUN_004AC290` emits, per unit, a 0x10-byte header
(`FUN_004ABC40`, unit `+0x00..+0x0F`), then unit `+0x18..+0x6B` densely (so record `+0x10 + k` is unit `+0x18 + k`),
then a 0x10-byte per-class block (building / oil platform / mobile, the last written by `FUN_004ABEB0` from unit
`+0x7C`), then unit `+0x8C..+0x8F` and, keyed on the resume-order byte unit `+0x8D`, 4 or 8 more bytes. **The record
is 0x90 bytes when unit `+0x8D` is 5 or 10, otherwise 0x8C.** In all three decodable saves the array is exactly
1600 records at file offset `0x595B6..0x900B6` (224000 bytes = 1600 x 0x8C), bounded by a clean section break.

Live counts were then computed exactly as the post-load recount does - `(state & 7) == 0`, `state & 0x80`,
`(unit[0x5F] & 2) == 0`, and the type-to-counter map read out of the exe tables `0x8C0B80` and `0x8C0D38` - and each
counter array was located in the file by matching it against those counts rather than by guessing the layout
(`0x91B9EC` land force at file `0x1F6C`, `0x91B86C` footmen at `0x1FCC`).

| Save | Counter | Player | saved | live | ratio |
|---|---|---|---|---|---|
| `A Nerzhul 10 The Dark Portal.sav` (control, mission start) | land force `0x91B9EC` | 1 / 2 | 18 / 4 | 18 / 4 | **1.00** |
| | siege `0x91B8AC` | 1 | 4 | 4 | **1.00** |
| | knight `0x91B8CC` | 1 / 2 | 8 / 4 | 8 / 4 | **1.00** |
| | mage `0x91B96C` | 1 | 6 | 6 | **1.00** |
| `Autosave.sav` (control) | all of the above | 1, 2 | same | same | **1.00** |
| `_Nerzhul 9.sav` | land force `0x91B9EC` | 3 / 6 | 13 / 15 | 13 / 15 | **1.00** |
| | footman `0x91B86C` | 3 / 6 | 6 / 6 | 6 / 6 | **1.00** |
| | archer `0x91B88C` | 3 / 6 | 3 / 4 | 3 / 4 | **1.00** |
| | knight `0x91B8CC` | 3 / 6 | 4 / 5 | 4 / 5 | **1.00** |
| | sea force `0x91BA0C` | 6 | 5 | 0 | **residue +5** |
| | destroyer `0x91B8EC` | 6 | 3 | 0 | **residue +3** |
| | battleship `0x91B92C` | 6 | 2 | 0 | **residue +2** |
| | air force / gryphon | 6 | 3 / 3 | 2 / 2 | **residue +1** |

Two conclusions, and they point in opposite directions:

- **The residue is real.** Player 6's sea force counter says 5 while it owns no ship at all, and three of its per-type
  counters are likewise stuck above zero. Nothing but the skipped reset can produce that, so the bug is confirmed on
  real data.
- **It was not stalling anything at 12:14.** Every counter that gates land training (`FUN_004AD700` compares against
  `st[0x14..0x17]` = 6/3/0/4 for player 3 and 6/4/0/4 for player 6) and every counter a land `WAITFOR` reads was
  exactly right. Both computers were at their target army, which is why their barracks were idle - by design, not
  through inflation. The earlier reading of "roughly twice the target" came from a mis-aligned block base and is
  withdrawn.

The session log has the author loading at 12:08 and reloading at **12:15**; this save is from **12:14**. It is
therefore the baseline from before the reload that preceded the stall, and it cannot test the hypothesis. What it
does establish is that one load from a fresh process leaves the land counters clean.

#### `D Nerzhul 8.sav` - the previous mission, which he says also stalled

| Player | Script | Wait | Program counter | Parked on |
|---|---|---|---|---|
| 0 (computer) | 0 | 0 | `0x1BB1` | `WAITFOR landForce >= 3` - the script's **first** wave gate |
| 2 (computer) | 1 | 58900 | `0x00FD` | passive script, `SLEEP 65535` / `JUMP` loop; 6635 steps elapsed |
| 6 (computer) | 36 | 0 | `0x2EC7` | `WAITFOR have_keep`, with `buildlist_len 7`, `peasant_target 6` |
| 1, 3, 5, 7 | 0 / 1 | - | - | not computers |

**This file does not capture a stall.** Every computer is on an opening wait, its build list is 3-7 entries long and
its per-type counters are empty (only player 2 has anything: 3 archers, 3 submarines). It is an early-game snapshot of
mission 8, not the state he is describing, so it neither confirms nor rules anything out for that mission.

#### `Autosave.sav` and `A Nerzhul 10 The Dark Portal.sav`

Byte-identical in AI state; both are the **start of the next mission**, every computer still on its opening
`WAITFOR peasants >= n` (players 1, 2, 3, 6 with targets 12, 7, 8, 9). Nothing stalled here yet. Neither file
captures the stalled state of mission 9.

#### Script tails (the terminal loops)

```
script 40 (player 6)                     script 41 (player 3), after have_castle
  3016 SET land_wave_size = 8              317e SET land_wave_size = 8
  3019 WAITFOR landForce >= 8              3181 WAITFOR landForce >= 8
  301b SET launch_land_attack = 1          3183 SET launch_land_attack = 1
  301e SLEEP 6000                          3186 SET air_wave_size = 2
  3023 JUMP 0x3016                         3189 SET launch_air_attack = 1
                                           318c SLEEP 1000
                                           3191 SET land_wave_size = 12
                                           3194 WAITFOR landForce >= 12
                                           3196 SET launch_land_attack = 1
                                           3199 SLEEP 1500
                                           319e JUMP 0x317e
```

Both end in a loop whose only gate is "do I have N live land attackers". Once production is dead, that gate is the
last thing that ever stops, and it stops for good.

---

## Stall conditions

| Condition | Code path | Can the mod trigger it | Verified |
|---|---|---|---|
| `WAITFOR` never times out - a false condition is re-polled every step for ever | `0x4CA2D0`, `lea ecx,[ecx*2-1]` / `mov dword [esi],1` | No - vanilla design | **verified** (disasm) |
| A savegame load leaves residue in the per-type army and land/sea/air force counters | unpack restores them, `0x4B55D3 jne` skips the reset, `0x4C460D -> FUN_004EE210 -> FUN_004B52D0 -> FUN_004B5220` re-adds | No | **[unverified]**: the code reads that way but the land counters in a loaded save are exact (section 3) |
| Player 6's naval counters stand above the truth (sea force 5, no warship alive) | not determined | No | **observed in the save**, cause **[unverified]** |
| Inflated counters stop all unit production (`count < target` fails) | `FUN_004AD700` and the eight sibling production functions | No (vanilla) | code path **verified**; **not present** in the one save we have - every land counter matched the live count exactly |
| Player 3 cannot afford the castle it is waiting for | `FUN_004ADB30 -> StartProduction`, 2500 / 1200 / 500 | **No** - `[costs]` are all 1.0 and keep / castle are untouched by the config | **verified** at 12:14: 3750 gold, **1000 lumber**, 4700 oil |
| `have_keep` / `have_castle` never true because the hall never upgrades | `FUN_004ADB30`: peasant gate, `want` flag, prerequisite buildings, cost | Indirectly (food and peasant pressure) | **verified** that the gate exists; that it is what blocked these two games is `[unverified]` |
| Build list blocks on its first unfinished research entry, so the castle entry is never reached | `FUN_004DA300`, `if ((char)e < 0) { want=1; return; }` | Yes, via the cost check `FUN_004AC610` that precedes it | **verified** (decompile) |
| FOOD blocks peon training, so `peasants >= peasant_target` never holds and the hall never upgrades | `FUN_004AC610` `supply > used`, supply clamped to 200 | **Yes** - `hall_food` raises supply (helps), while x4 HP + 1 HP/s regen for every player keep the computer's army alive and its food full (hurts) | mechanism **verified**, the numbers at the stall are `[unverified]` - the save does not carry food or the peasant count (`FUN_004B5390` zeroes `0x91B38C..0x91B86B` on every load and recomputes them) |
| Tree regrowth fills the computer's base, so a build site cannot be found | `FUN_004DA300 -> FUN_004DBC30` | Yes in principle, but a failed site search only skips that entry, it does not block the queue | **verified** that it does not block; whether regrowth ever denies a site is `[unverified]` |
| Unit cost / build-time table edits price the computer out of a build-list entry | `FUN_004AC610` inside `FUN_004DA300`, queue blocked on failure | **No, in this config**: all `[costs]` multipliers are 1.0 and the per-unit overrides (footman/grunt 400 gold, peasant/peon 200, archers 350, destroyers 600) are all **cheaper and faster** than vanilla; keep and castle are untouched | **verified** (config + `game.h` tables) |
| Gold exhaustion | `gold_mines.unlimited = false` in the config | Mitigated: `amount = 10.0` makes every mine last ten times longer | **verified** (config) |
| Unit array full | not reached - the force and food caps bind first | - | `[unverified]` - the array size was not measured |
| `FUN_004CC3B0` finds no human player, so a wave dispatches nothing | `0x4CC3B0` returns -1 if no `controller == 0` player has units | No | **verified**; not a permanent stall (the launch flag is cleared either way) |

---

## Recommendations

### Config changes he can test, smallest experiment first

1. **Do not reload during a mission.** Highest-value test, no config change: play a mission from its start to the
   point where the enemy normally goes quiet **without loading a save once**. The load path is the one place where
   the counters the computer reasons with can come out wrong, and one of them demonstrably is (player 6's sea force
   reads 5 with no warship alive). If the enemy keeps attacking across a no-reload run, that is the answer.
2. **Nothing in the config needs changing to fix this.** Every `[costs]` multiplier is 1.0, the `[unit.*]` overrides
   are cheaper and faster than vanilla, keep and castle prices are untouched, and `hall_food` *raises* the computer's
   supply. Leave them all as they are.
3. If a second experiment is wanted, `[unit_regen] regen_for = "mine"` (and the same in `[heroes]`) restores normal
   attrition for the computer's army. This is a play-feel change, not a fix: it was not shown to be involved.

### Mod-side fix - feasible, and worth doing in two steps

**Step 1, the diagnostic log line (first deliverable).** Single player only, from the existing tick hook, once every
N seconds, for every `p` with `controller[p] == 1` (`0x918CAC`):

```
ai p<P> script@<PC> wait=<W> <STATE> | land <LF>/<need> sea <SF>/<need> air <AF>/<need>
   | peas <PE>/<PT> food <USED>/<SUP> gold <G> lum <L> oil <O>
   | army F<f> A<a> S<s> K<k> (want <st14>/<st15>/<st16>/<st17>) | buildlist <i>/<len> want98=<x> want99=<y>
```

Fields, all relative to `st = 0x91D650 + p * 0x30` and the blob base `0x91D950`:

| Token | Read from |
|---|---|
| `PC` | `*(u32 *)(st + 4) - *(u32 *)0x91D950` - print the **blob-relative** offset so it can be looked up in `ai.bin` |
| `W` | `*(u32 *)st` |
| `STATE` | decode `*(u8 *)(blob + pc)`: `0 SET`, `1 JUMP`, `2 SLEEP`, `3 WAITFOR <cond>`; for `3`, name the condition from the byte at `pc + 1` using the table under "The eight conditions". A player parked on the same `WAITFOR` for more than ~60 s is the stall; log it once as `STALLED <cond>` |
| `LF` / `SF` / `AF` | `0x91B9EC[p]`, `0x91BA0C[p]`, `0x91BA2C[p]` (u16[16]) |
| `need` | `st[0x0E] * st[0x0D]`, `st[0x10] * st[0x0F]`, `st[0x12] * st[0x11]` |
| `PE` / `PT` | `0x91B66C[p]` (u16[16]) / `st[0x13]` |
| `USED` / `SUP` | `0x91B38C[p] - 0x91B6AC[p]` / `min(0x91B50C[p], 200)` |
| `G` / `L` / `O` | `0x919128[p]` / `0x9190E8[p]` / `0x919168[p]` (i32[16]) |
| `f a s k` | `0x91B86C[p]`, `0x91B88C[p]`, `0x91B8AC[p]`, `0x91B8CC[p]` (u16[16]) |
| `st14..st17` | `st[0x14]`, `st[0x15]`, `st[0x16]`, `st[0x17]` |
| `i` / `len` | first `i` with `0x934358[p * 0x40 + i] == 0`, and `st[0x22]` |
| `want98` / `want99` | `0x918F30[p * 0x1B + 0x98]`, `0x918F30[p * 0x1B + 0x99]` - the keep and castle permissions |

Naming the wait: use the condition names in this document (`have_shipyard`, `have_keep`, `have_castle`,
`peasants_ready`, `land_force`, `sea_force`, `air_force`, `enemy_alive`). That one line answers, from a single
session, which of the two candidate mechanisms is real: if `f a s k` are far above `st14..st17` while the real army is
small, it is the counter residue; if `PE < PT` with `USED == SUP`, it is the food deadlock; if the wait is
`have_keep` or `have_castle` with `L` under 1200, it is the lumber shortfall player 3 shows at 12:14.

**Step 2, the repair (a genuine fix, not a workaround).** The residue is a pure bookkeeping error, and the correct
values are recomputable from the unit array, exactly the way `food_supply.md` recomputes the food supply. On the
**map-load hook** (`0x4D2C46` is the new-map path; the savegame path needs the equivalent, and `0x91BFB0 == 1` marks a
game that came from a save), or on the first tick after a load, rebuild all 15 counters from scratch:

```
zero 0x91B86C .. 0x91B9CC (12 x u16[16]) and 0x91B9EC / 0x91BA0C / 0x91BA2C
for each unit in the array (stride 0x98, count 0x91BFB8) with (state & 7) == 0 and (u[0x5F] & 2) == 0:
    t = u[0x27]; o = u[0x2C]; f = typeFlags[t]
    if (counter_0x8C0B80[t] && counter_0x8C0D38[t]) counter_0x8C0D38[t][o]++
    if ((f & 0x1F) && (f & 0x80000)) {
        if      (!(f & 0x10A)) landForce[o]++
        else if (f & 8)        seaForce[o]++
        else if (f & 2)        airForce[o]++
    }
```

This writes only plain `.data`, needs no code patch and no `VirtualProtect`, is exactly what the engine itself would
have computed, and is inert in multiplayer behind the existing `0x91C6F4` gate. It is the same shape as the food
recompute already in the mod, so it fits the existing design.

A watchdog that force-clears a `WAITFOR` is **not** recommended: the script's program counter is a pointer into the
blob and skipping a wait would desynchronise the build list from the wave sizes. Fixing the counters lets the vanilla
script proceed on its own.

### What to capture next time

**None of the four saves captures a stalled state.** `_Nerzhul 9.sav` is 25-35 minutes early (though it already shows
player 3 waiting on a castle and both computers' counters at or above their targets), `D Nerzhul 8.sav` is an
early-game snapshot, and `Autosave.sav` / `A Nerzhul 10 The Dark Portal.sav` are the opening of the next mission.
Next time the enemy goes quiet:

- save **at that moment**, while the enemy is visibly not attacking, and keep playing for a few minutes without
  loading;
- send that save together with `x86\gameplay_options.log` from the same session with `log_casts = true`;
- note whether the session included any savegame load before the stall, and how many.

---

## Open questions

- **What actually stalled these two games is still `[unverified]`.** The counter residue is proven in code and seen in
  data, but it was not touching the land counters at 12:14; player 3's 200-lumber shortfall is real at 12:14 but a
  snapshot, not proof that it lasted. No file covers the period after the 12:15 reload, which is when the stall was
  noticed. One save of the stalled state, or one run of the diagnostic line, decides it.
- **The central open question:** the load path reads as "restore from file, skip the reset, recount and add", which
  would corrupt the counters on every load, yet a save taken from a loaded game has exact land counters. The packer /
  savegame offset mismatch in section 3 is the likely place the reasoning breaks; settling it needs the globals block
  of the save mapped field by field, which was not done.
- What leaves player 6's naval counters above the truth. Not a dying-unit artefact: the array holds no warship of
  that player in any state.
- Whether a second load in the same process changes anything. No save from after the 12:15 reload exists to check.
- Why the shipped loose `Data\Rez\ai.bin` (83-entry script table) and the blob the running game loads (32-entry table,
  all offsets 0x66 lower) differ. The content maps 1:1, so the decoders are correct, but the script **id** numbers
  printed by `aisave.py` are the loose file's, not the game's.
- `FUN_004E89A0`'s second build-list pass runs only for a computer player with **zero** peasants
  (`0x4E8A05 cmp word [ecx], 0` / `jne`). Its purpose is unclear; the normal `want` flags come from `0x4DA4BC` inside
  `FUN_004DA300`.
- The layout of `0x918F30` (`base + player * 0x1B + entry`, entry `0x80..0xFF`) overlaps between players. Reads and
  writes agree, so it is self-consistent, but it looks like a latent engine bug of its own.
- `unit + 0x5F` bit 1 ("do not count") - what sets it, and whether any campaign unit carries it.
- The size of the unit array, and therefore whether a computer with ten times the usual gold can exhaust it.
- Nothing in this report was observed in a running game.
