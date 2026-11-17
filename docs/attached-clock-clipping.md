# HP-attached timer/counter clipping — 2026-11-17

## Finding and correction

The user's `Desktop/cut_timer.png` shows the HP panel and its timer artwork in
the expected position, with the **left portion of the digits missing**.
The clock belongs to a separate script even when its artwork/labels are
parented to the HP panel. The existing text provenance deliberately refused
cross-script parent chains. Consequently these digits did not receive the
expanded viewport clip when their HP parent moved left.

The correction permits exactly one supported ownership boundary:
`MissionTimer`/`MissionCounter` -> the live, tracked **HP** root.
It still requires generation-checked UI handles at every link, the expected
UI vtable, verified clock class name, and the HP root's current class, owner,
handle and installed position. It does not admit arbitrary foreign owners,
menu text, inventory/rage parents, or standalone clocks.

No clock position, number formatting, font size, animation or artwork changes.
The existing queued-string provenance, full-screen-only X clip expansion,
custom/Y clip preservation, and tick/render memory caches are reused.
No per-glyph ancestry or class-name reads are added.

## Shipped-artifact evidence

Same supported EXE / fingerprinted `9-96.LCS` as `widescreen-hud.md`.
All package offsets below are in the decoded first block.

- Panel's export table at `0xADF48` maps export **AD** to register **0**,
  the already verified HP root.
- MissionTimer's constructor at `0x9FC7C..0x9FC88` obtains the global Panel
  reference (`702B`), reads its `F0AD` export and passes that parent to
  return-native `FA`. The alternate branch repeats this at `0x9FCA0`.
- Its numeric objects (`0x9FCC4..0x9FD24`) pass their timer root (register 9)
  as parent to return-native `13C`.
- MissionCounter uses the same exported HP parent at `0xA00C0`,
  `0xA0110`, `0xA0130`, and `0xA0150`.
- Return table `0x4C87C8` maps FA to `0x41DBD0` and 13C to `0x41DCC0`.
  Both resolve the **parent's full-generation handle** independently of the
  script owner passed to the factory. FA calls sprite factory `0x42B7C0`;
  13C calls numeric-label factory `0x42B830`. Cross-script parenting is
  supported by native creation, not an excuse to weaken arbitrary ownership.
- REA corroborates sprite factory `0x42B7C0` allocating from the creating
  script's class allocator, passing that script and the independent parent to
  its constructor (Evidence
  `ev_018324335c2af2f36567fa6decb698dbc909e371afbf8c819edc0f3c01b33aa6`).

Reproduce the bounded contracts with `python tools/inspect-widescreen.py`.

## Verification / live acceptance

The HUD suite covers both supported clock owners, negative authored X and
queued-string clipping, zero-tick renders, unrelated menus, wrong HUD parent,
recycled child/background handles, edited HP position and standalone clocks.
The performance fixture adds cross-script clock labels to the combined scene
while retaining phase-scoped reads. Offline tests do not establish a live fix.

Retest the same timer screenshot scene at 16:9, pause/unpause, load/respawn,
clock expiry and counter changes. Recheck inventory/rage text and F9-closed
performance. Only the staged DLL is rebuilt; nothing is installed automatically.
