# Why the computer stops cutting lumber (static RE, Warcraft II: Remastered 1.0.2.2818)

Issue #24 follow-up. Static analysis only (Ghidra headless, capstone, the 1.23.0 `log_ai` session log); nothing here was
run in the game. Inferred points are marked `[unverified]`. VA = RVA + 0x400000.

## Verdict

**A vanilla savegame bug, not a mod feature.** Loading a savegame zeroes the computer's per-player worker-job counters
but restores every worker's job bits. The first worker that delivers after the load decrements a zero counter, which
wraps to 65535. From then on, the "send this worker to lumber" branch can never fire (`65535 < target` is false), so
every worker that delivers is sent to gold. Lumber comes back only through a fallback, and that fallback runs only
when no gold mine with gold left is reachable. The computer's lumber income stops within one harvest trip, the hall
never upgrades (a keep costs 1000 lumber, a castle 1200), and the script waits on `WAITFOR have_keep` /
`have_castle` for ever.

The mod's `[gold_mines] amount = 4.0` makes this worse: in the stock game the mines near the base empty and the
fallback eventually sends workers back to the trees, but with 4x the gold that does not happen within a mission.
`unlimited` is **off** in the author's config, so it is not a factor here. It would make the stall permanent.

**Confidence: high** for the mechanism: every step below is read from code, and the counter compare is unsigned in the
disassembly. **Medium-high** that it caused this session: the author's session log shows a savegame load at 20:36:21
(the tree module re-attached with no `new map` line), and all three computers stopped gaining lumber within the next
1 to 2 minutes, when the lumber workers made their next delivery. Nothing else in the log changes at that time.
Tree regrowth did not start until 20:48, 12 minutes later.

## The worker manager: `FUN_004DAD80`

A computer worker comes back to the manager after **every delivery**: the LEAVE action `FUN_004C9BF0` calls it at
`0x4C9C8B` for units of computer players (`controller 0x918CAC[p] == 1`) with the worker flag 0x100. It also runs
when a worker goes idle (`FUN_004A8A10`, `0x4A8A5E`). So the gold / lumber split is decided again on every trip.

1. `FUN_004DB420(unit)` first **releases** the worker's current job: for each bit set in the AI job word `unit+0x20`,
   it decrements the matching per-player counter. Bit 0x20 is repair (`0x9231F8`), 1 is gold (`0x9231B8`), 2 is
   lumber (`0x9231D8`), 8 / 0x10 are builder (`0x923218[p*0x2F + unit+0x76]`). Then it clears those bits
   (`&= 0xFFC4`).
2. Build and repair branches (not relevant here). Repair is gated by `0x9231F8[p] < (peasants-1)/2 + 1` at `0x4DB017`.
3. Lumber target `T` (`0x4DB05E..0x4DB0C8`). Here `g` is gold `0x919128[p]`, `L` is lumber `0x9190E8[p]` and `n` is
   the peasant count `0x91B66C[p]`:

   | condition | T |
   |---|---|
   | g < 500 and n < 5 | 0 |
   | L >= 2000 | 0 |
   | g < 1000, L < 500 | n / 2 |
   | g < 1000, L >= 500 | 0 |
   | g >= 1000, L < 500 | (n + 1) * 3 / 4 |
   | g >= 1000, 500 <= L < 2000 | n / 2 |

4. `0x4DB0CB cmp word [0x9231D8 + p*2], ax / jae`: this is an **unsigned** 16-bit compare. If lumber workers < T, it
   calls `FUN_004DAA80`, which sends the worker to the nearest tree, sets bit 2 and does `0x9231D8[p]++`. Otherwise,
   or if no tree is found, it calls `FUN_004DA8B0`, which sends the worker to the nearest gold mine with
   `+0x82 != 0` in the worker's region, sets bit 1 and does `0x9231B8[p]++`. If that fails too, `FUN_004DAA80` is
   tried again as the fallback.

With correct counters, player 0 in the log (`gold 56200 lum 825`, 7 workers) would get T = 3 lumber workers, and
player 7 (`lum 0`, 8 workers) would get T = 6. Both stayed at zero lumber income for 30 minutes.

## The load path

| Step | Address | Effect |
|---|---|---|
| Load driver | `FUN_004C4280`, AI init call at `0x4C45D1` | calls `FUN_004E8920(saveBuf)` for new maps and savegames alike |
| AI init | `FUN_004E8920`, `0x4E895A call 0x4DAC70` | **zeroes** `0x9231B8..0x923217` (gold / lumber / repair counters) and `0x923218` (0x5E0 bytes, builder counts), plus `0x9237F8` |
| AI state from the save | `FUN_004E0590` | restores only the 16 x 0x30 AI blocks at `0x91D650`. The counters are **not** in the save |
| Upgrade flags | `FUN_004E8AF0` | recomputes `0x934358` (build-list done flags). No worker recount |
| Unit records | `FUN_004EE210` -> `FUN_004E0AD0` -> `FUN_004ABF10` | copies every field back, **including `unit+0x20` (the job bits)** and, for workers, `+0x75` / `+0x76` |

So after a load, `0x9231D8[p] = 0` while `k` of player p's workers still carry bit 2. Each of them releases on its next
delivery, which takes the counter to 0xFFFF, 0xFFFE, and so on. The lumber branch is then closed, and nothing
increments the counter back except the fallback. The same wrap hits the repair counter, so a computer that had a
repairer at save time never repairs again. It also hits a builder count, which blocks that building type (the build
branch needs `0x923218[..] == 0`).

Invariant the engine keeps outside this bug: every increment sets the job bit and every decrement clears it. The
decrement happens in `FUN_004DB420` (from the manager) or through unit removal: `FUN_004EE380` -> `0x4EE553 call
0x4E8840` -> `0x4E88E9 call 0x4DACC0` (a thunk of `FUN_004DB420`). So between simulation steps, each counter should
equal the number of that player's units with the bit.

Loading a **second** save after the freeze can cure it, if no worker carries bit 2 at save time (everyone is on
gold). The lumber counter then restarts at 0, and only the gold counter wraps, which nothing gates on. So the symptom
comes and goes with which save is loaded `[unverified]`.

## Other hypotheses, checked and rejected for this session

- **Gold refill / "AI thinks gold is endless"**: the split rule reads the treasury, not mine contents, and `unlimited`
  is off in the author's config. The only mine test is `+0x82 != 0` in `FUN_004DA8B0`. More gold per mine only
  delays the fallback.
- **Tree regrowth / the forest search**: `FUN_004DAA80` -> `FUN_004EAAB0` searches fresh on each call (radius 3/4 of
  the map size via `FUN_004E3010`, test `FUN_004EAF60`). The test is a `0xFFFE` forest tile next to a tile of the
  worker's own region whose square flags have none of `0x9CE`. No per-player forest cache exists. The only state is
  the scratch globals `0x9347F8` / `0x9341E0`. Regrown tiles (region word 0xFFFE) are valid forest for this test.
  Regrowth began 12 minutes after lumber stopped. A regrown ring that walls off a hall is possible in principle with
  `building_distance = 0`, but it would show as `FUN_004DAA80` failing, and the manager would then still fall
  through to gold. It cannot explain lumber frozen at 825 with a full treasury.
- **Mod worker features**: `src/workers.cpp` and `src/production.cpp` act on `w.localPlayer` only. Nothing in the mod
  writes `unit+0x20`, `+0x75` or the counters.

## Smallest experiment for the author

Same mission, same config, `[general] log_ai = true`:

1. Start the mission fresh and **do not load any save**. Expected: each computer's `lum` value in the `ai:` lines
   keeps rising and falling (income and spending) past the 30-minute mark, and `have_keep` clears.
2. Save, then load that save while the computers are cutting wood. Expected: within 1 to 2 minutes every computer's
   `lum` value stops rising for good while `gold` keeps climbing. That is the log shape of the 20:36 session.

If step 1 also freezes lumber without a load, this verdict is wrong and the next suspect is the forest search (see
above).

## Proposed fix (not implemented)

**Recount after load.** This is a plain data write, not a new hook. On the first mod tick after a savegame load (the
identity check that `trees.cpp` already uses, `kRvaGameFromSave` `0x91BFB0`), for each player with
`0x918CAC[p] == 1`, recompute from the unit array:

- `0x9231B8[p]` = number of units with bit 1 in `+0x20`
- `0x9231D8[p]` = number with bit 2
- `0x9231F8[p]` = number with bit 0x20
- `0x923218[p*0x2F + unit+0x76]` = number with bit 8, plus number with bit 0x10

Count every occupied slot, including dying units, so that the removal path's later decrement stays balanced. Which
slot states count as occupied still has to be read from `FUN_004EE380` `[unverified]`. The recount is idempotent.
Running it again every few seconds would also repair any drift, at the cost of writing AI state more often. It must
stay off in multiplayer, like every other state write.

Workaround until then: avoid loading a mid-mission save, or restart the mission. Setting `[gold_mines] amount` back to
1.0 only shortens the stall (it ends when the base mines run dry). It does not remove it.

Optional diagnostic: add the three counters to the `log_ai` line (`jobs gold/lumber/repair`). A value above the
worker count, such as 65535, proves the wrap directly.
