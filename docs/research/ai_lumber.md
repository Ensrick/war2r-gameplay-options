# Why the computer stops cutting lumber (static RE, Warcraft II: Remastered 1.0.2.2818)

Issue #24 follow-up. Fixed by `[general] fix_ai_after_load` (last section). Static analysis only (Ghidra headless, capstone, the 1.23.0 `log_ai` session log); nothing here was
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

## The fix: `[general] fix_ai_after_load` (src/aijobs.cpp)

These are plain data writes. The mod adds no hook and gives no order. It runs on every single-player step, after the
multiplayer gate in `mod.cpp`, and does the following:

1. For every player with `controller 0x918CAC[p] == 1`, it counts the units that the game will still release, bit by
   bit. That means units owned by p, of a worker type (type flag 0x100), whose state `+0x1E` has none of the bits
   0x07. For each such unit:
   - bit 1 counts toward gold, bit 2 toward lumber and bit 0x20 toward repair;
   - bits 8 and 0x10 each count one toward `builders[p][unit+0x76]`, and only when that index is below 0x2F.
2. If every count already matches (every fresh map, and every step between loads), it writes nothing. Otherwise it
   writes the counts back and logs one line with the old and new values:
   `ai: recounted workers after load: player 3 gold 0 lumber 65535 repair 0 build 65535 -> gold 2 lumber 3 repair 1
   build 2` (`build` is the sum of the player's row). After 20 recounts in a row with no step in between where
   everything matched, it stops logging.

Why these units, with addresses:

- **State bits 0x07, not 0x0F.** `FUN_004EE380` (unit removal) returns at once when `+0x1E & 7` is set. Otherwise it
  releases the jobs through `0x4EE553 call FUN_004E8840` -> `0x4E88E9 call 0x4DACC0` (thunk of `FUN_004DB420`), and
  only then marks the unit dying (`|= 2`). So a unit with `& 7` set has already been released or never will be.
  Workers inside a mine, a hall or a building site carry state bit 0x08 and are still released: `FUN_004ED860` kills
  the units inside a destroyed building through the same path. That is why the mod's `IsActive` (mask 0x0F) is not
  used here.
- **Worker types only.** The manager is reached only for type flag 0x100 (`0x4A8A50`, `0x4C9C7E`), and
  `FUN_004E8840` releases only for 0x100 (`0x4E88E1`). A non-worker with stale bits is never released, so it is not
  counted.
- **Computer players only, human not touched.** The manager is called only when `controller == 1` (`0x4A8A50`,
  `0x4C9C70`), and so is the removal release (`0x4E884C`). The counters are referenced only by the manager, its
  helpers `FUN_004DA8B0` / `FUN_004DAA80`, the release `FUN_004DB420` and the reset `FUN_004DAC70` (a full scan for
  absolute references to `0x9231B0..0x923220`). The human player's words are never read, so they are left alone.
- **One per bit.** `FUN_004DB420` subtracts once for each of 0x20, 1, 2, 8 and 0x10, and the two builder bits both
  index the row with the unit's `+0x76` word.

Why it runs on every step instead of "once after a load": `kRvaGameFromSave` (`0x91BFB0`) stays 1 from one load to
the next, so a second load could not be told apart. A mismatch between the counters and the units is the load's own
fingerprint, and outside that bug the game keeps the two equal between simulation steps.

`[general] log_ai` now ends each status line with `| jobs gold g lum l rep r`, so a wrapped counter (65535, or just
below it) shows up directly when `fix_ai_after_load` is off.

Tests (`test/selftest.cpp`, `AiJobsTests`): a fake load with wrapped counters and units carrying every kind of job bit
(inside a mine, dying, non-worker, builder index past the row, the human's peon) is recounted exactly. A second pass
changes nothing and logs nothing. Counters that already match are not written. The off switch, a network game through
`mod::OnTick` and a human slot are all left untouched. A drift that repeats stops logging after 20 lines. Twelve
mutations were each killed: state mask 0x0F, no worker check, humans recounted, farm bit ignored, off switch ignored,
log cap 21, cap never reset, builder row not written, owner ignored, not wired into the tick, run before the
multiplayer gate, `log_ai` columns swapped.

## What the author should see

With the fix on, after loading a mid-mission save the log has one `ai: recounted workers after load:` line whose
`lumber` goes from 0 (or 65535) to the number of wood cutters. The computers' `lum` values in the `log_ai` lines then
keep moving, and `WAITFOR have_keep` / `have_castle` clears once the hall upgrade is paid for.
