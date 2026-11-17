# Grade/belt alignment — static investigation, 2026-11-17

## Finding

**The current patch applies different horizontal transforms to grades and their
2D backing.** Grades are model-bearing script objects in the separate
orthographic overlay list, not children of the belt sprite. Belt/pip sprites use
the contained 2D projection. This explains outward spill and, for negative-X
grades, an apparent left shift. Wider artwork is **not established as necessary**;
it would not correct this coordinate/scale disagreement.

Read-only inspection only: no game execution, asset writes,
artwork extraction/replacement, or production changes. Screen appearance and
exact visible silhouette overlap have not been measured.

## Fingerprints and offset conventions

Inputs: `E:\Games\I-Ninja\I-Ninja.exe` and `Data\09.vol`, decoded in memory using
`tools/inspect-widescreen.py::panel()` and the existing local decoder/Capstone.

| Input | SHA-256 |
| --- | --- |
| EXE | `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0` |
| `09.vol` | `0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669` |
| Packed `9-96.LCS` | `7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4` |
| Decoded blob (722352 bytes) | `127243447e13d1bc29960008952c6f6033446b03a67fbe36b0bfebdab13f9edc` |

Package offsets below refer to that decoded blob, **not** the packed archive.
Native addresses are executable VAs. Class headers store entry at `+8` and
shared unit at `+12`; confusing those loses the pause grade entry.

| Class | Header | Entry | Shared unit |
| --- | --- | --- | --- |
| `ProgressScreen_Grade` | `0xAA97C` | `0xA239C` | `0xA2394` |
| `ProgressScreen_Pip` | `0xAA9A8` | `0xA2470` | `0xA2394` |
| `ProgressScreen_Line` | `0xAA9D4` | `0xA26F4` | `0xA2394` |
| `PauseMenu_ShowGrades` | `0xAAB34` | `0xA8148` | `0xA7EBC` |
| `PauseMenu_Grade` | `0xAAB60` | `0xA8968` | `0xA7EBC` |

## Observed script layout

- **Pause belt:** `0xA8234..0xA8240` assigns root X/Y `(-40,120)`.
  Width is `490` at `0xA82C8`, or `560` with root X `-63` in the
  conditional branch at `0xA82E0..0xA82EC`. The 128×32 resource descriptor
  at `0xC100` is passed to return-native `0x68` at `0xA8304`.
  `0xA8308..0xA8320` scales it by `(width/128,1.6)`: the authored belt
  width is explicitly parameterized, not limited to its 128 source pixels.
  The update repeats root placement/scaling at `0xA85F8..0xA8624`.
- **Pause contents:** child sprite creation uses return-native `0xFA`,
  with the belt handle as parent (`0xA8324..0xA83EC`, further calls through
  `0xA8530`). But `0xA8550..0xA8584` spawns `PauseMenu_Grade` instances
  through native `0x2`, passing six parameters. These are separate script
  instances, not those UI children.
- **Pause grade:** `0xA8968..0xA8974` assigns model descriptor `0x96160`.
  `0xA89A8..0xA89BC` calls native `0x197`, applies the passed scale through
  native `0x2A`, and sets position Z to `400`. Width/spacing parameters
  are `(490,82)` or `(560,62)` at `0xA8978..0xA89A4`, repeated inside the
  loop. Helpers at `0xA8848` and `0xA8900` compute X/Y; writes at
  `0xA8A60/0xA8A64` target object position fields `9/A`, not sprite native
  `0xE6`. No aspect compensation appears in this layout.
- **Progress/results grade:** `0xA239C..0xA23C0` assigns model descriptor
  `0x96150`, calls native `0x197`, and applies uniform model scale `0.2`.
  `0xA23C4..0xA23E4` sets X to the supplied offset plus its parent's
  exported X, Y to the parent's exported Y, and Z to `150`.
  The loop updates X/Y again at `0xA2404..0xA241C`.
- **Progress/results pip:** `0xA2470..0xA24CC` instead creates 2D sprites
  from descriptors `0x16460` (128×64) and `0x15020` (64×64), using the
  same parent X and an authored Y conversion. They are scaled to `0.32`
  at `0xA25DC..0xA2610`; subsequent position calls are native `0xE6`
  at `0xA2674/0xA2694`.
  `ProgressScreen_Line` exports IDs `0xA7/0xA8` as registers `1/2`.
  These are **parent script exports**, not builtin fields with similar IDs:
  the `F` selector routes through `0x425370..0x425387` to export reader
  `0x421580`.

## Observed native drawing/projection split

1. Native `0x197` is `0x41D1F0`: it sets strat flag `0x20000` and calls
   `0x409790` with list head `[*0x51AA98 + 0x7C]`.
   `0x448FB0` later walks precisely that list, invoking object render virtuals.
   Native `0x2A` (`0x416AD0`) writes strat scale at `+0xE0/E4/E8`.
   Position setters `9/A/B` (`0x422EF8/0x422F08/0x422F18`) write
   strat `+0x160/164/168`.
2. That list is drawn by `0x44EB80`: `0x44EBBD` calls projection setup
   `0x44EA50`; `0x44EBD7` calls `0x448FB0`. The intervening
   `0x44D870/0x44C580` only clear renderer queues/counters.
   Setup calls orthographic builder `0x45EDA3` and submits projection at
   **return VA `0x44EAA0`**. Builder X/Y scales are `2/width`, `2/height`.
3. Its logical width/height are renderer `+0x5C1B8/+0x5C1BC`.
   Constructor call `0x44C2DB` initializes the block at `+0x5C1A0` through
   `0x406810`: block `+0x18/+0x1C` are **640/480**.
   The executable's sole direct call to block updater `0x406890`,
   at `0x44AB42`, selects block **2** (`+0x40`), not these logical dimensions.
4. Return-natives `0x68/0xFA` are `0x41D130/0x41DBD0`, both reaching UI
   sprite factory `0x42B7C0` and constructor `0x42BF10`.
   Native `0xE6` (`0x41DD10`) reaches UI positioning `0x42BEF0/0x42BC60`.
   Native relocation distinguishes return/nonreturn tables at
   `0x426A43..0x426A77`: use `0x4C87C8`, not `0x4C7E70`, for `0x1068/0x10FA`.
5. Current `src/core/ethhhpatch_runtime.cpp::AdjustHudProjection` accepts
   only identity submissions returning to `0x44726B`, `0x4474D6`,
   `0x44884A`, setting X scale to `(4/3)/aspect`. It does **not** accept
   `0x44EAA0` or its nonidentity orthographic matrix.
   `src/core/ethhhpatch_hud.h::Identity/PositionCall` additionally restricts
   position compensation to exact `Panel` gameplay call signatures;
   neither grade class nor `PauseMenu_ShowGrades` qualifies.

## Derived consequence, limits, and artwork decision

For centered authored X `x`, at 1920×1080 the two paths predict:

- 2D backing: `screenX = 960 + 2.25*x`.
- Orthographic grade: `screenX = 960 + 3*x`.
- Difference: **`0.75*x` pixels**, leftward for negative X, rightward for
  positive X. Horizontal grade spacing/size is correspondingly 4/3 of the
  contained backing's scale. At 4:3 the disagreement disappears.

This is a static explanation for the reported direction/spill, not a measured
pixel match to a particular screenshot. Grade animation, model silhouettes,
transparent padding, and the exact level-results screen/state still need live
correlation; the progress-grade/pip route is verified, not every results asset.

**First establish a common authored-coordinate transform for this overlay and
its 2D backing.** Widening only the artwork would mask some overlap while leaving
grade spacing, model aspect, and left/right displacement wrong. A deliberately
edge-to-edge redesigned panel might need wider/tiled art later, but this evidence
does not require replacing the shipped artwork to restore original alignment.

REA/Ghidra additionally corroborated orthographic setup from logical renderer
dimensions at `0x44EA50` (Evidence
`ev_f786f01142ebfe9276d9621d7fbadf111237c1d9180e7b308b728784c428e2ec`)
and the 640/480 initialization at `0x406810` (Evidence
`ev_b702d624d58fc975b7513366348e7a30141d90e6a7dfbecf2a3bb9641dc05d01`).
Other address-level claims above come from the read-only Capstone/script pass.
