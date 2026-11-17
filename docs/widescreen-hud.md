# Combined gameplay HUD and movie widescreen fix — 2026-11-17

## Status

The user reports that the combined HP/rage/inventory/movie build "looks good
and plays well." Its performance protections remain in place. Follow-up reports
identify partially visible timer text, misaligned grade/belt panels, and display
detachment after minimizing. The new [Reset recovery](display-reset-recovery.md)
and [HP-attached clock clipping correction](attached-clock-clipping.md) are
built/offline-tested and need live retests. The [grade/belt investigation](grade-panel-investigation.md)
identifies a separate 3D/2D transform mismatch; no artwork or grade rendering
changes were made.

The display patch now shares one gameplay HUD policy in
`src/core/ethhhpatch_hud.h`:

| Group | Authored anchor | Behavior |
| --- | --- | --- |
| Inventory | parent origin | Existing percentage slide/reveal rule, including transition/reset call |
| HP bar | background's left edge | Percentage left margin, unchanged bar/child sizes |
| Rage diamond/selector | parent origin | Percentage placement while idle/hidden; expanded center stays centered |

`ethhhpatch_inventory.h` remains a compatibility include/namespace alias for
existing tests. These are not separate global HUD projection rewrites or
coordinate-range guesses. Other HUD/menu components are not claimed fixed.

## Anchor policy

For `r = viewportAspect / (4/3)`, a group position `X` with local anchor `A`
is mapped to:

`newX = (X + A) * r - A`

The anchor retains its authored screen percentage under the existing uniform
HUD projection containment. Local offsets, sizes, Y, colors, health/ability
values, timers and native animation registers remain unchanged. At 4:3 no
position compensation is applied.

The HP background is 256x32. The native constructor stores its centered
half-width in sixteenths, so its normal local left edge is `-2048/16 = -128`.
The displayed parent X is `-165`, placing the logical left edge at authored
`320-165-128 = 27`. Centered containment alone moves that logical edge by
**219.75 pixels at 1920x1080**, consistent with the reported ~219px.
Anchoring the *center* instead would still leave an unwanted left-margin shift.

Production derives the HP anchor from the current native sprite half-width and
horizontal scale, rather than adding 219 pixels. Unreadable, nonfinite, rotated
or cropped geometry is refused. Transparent artwork padding remains part of
the uniformly scaled image.

The rage diamond uses its parent origin, matching inventory. Its native
animation can bring the selector to X=0: that remains screen-centered instead
of forcing the expanded selector against the left edge.

There are independently bounded slots for four inventory parents, one HP
parent and one rage parent. Every changed position still requires exact
`Panel` identity, current full-generation UI handle/owner, expected vtable,
no parent, the verified cdecl caller and a matching relocated ASL sequence.
Image and text descendants share the existing clip provenance mechanism.
Restore only touches live, still-owned installed positions.

## Movie correction

The old hook mistakenly declared **seven arguments**, including an `int height`.
Native `0x497D40` takes only:

`(surface, x, y, scaleX, scaleY, alpha)` — cdecl, **24 bytes** of caller cleanup.

Consequently the old patch kept the original horizontal scale in the fictitious
height slot, while applying the calculated centering offset. At 1080p that
left a 1920-wide image shifted right by 240px instead of centering a uniformly
scaled 1440-wide image.

The corrected hook uses the shared six-argument type, a compile-time signature
assertion, and a production-shared submit helper tested with an x86 stack
sentinel. It changes **both** scales, retains alpha/surface, and applies only
at movie return site `0x49A40A`. Dispatch entry, caller and 24-byte cleanup
instructions are checked before installing. Unrelated surface draws remain
native.

A 640x480 surface at 1920x1080 now fits to **1440x1080 at (240,0)**: equal
240px pillarboxes, not an extra right offset. Portrait/narrow targets use the
corresponding centered letterbox fit. No Bink codec, file, playback timer or
movie data is changed.

## Static evidence

For executable SHA-256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`
and the fingerprinted `09.vol/9-96.LCS` member
`7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4`:

| Panel first-block package offset | Position call |
| --- | --- |
| `0x9BA00` | inventory update prefix |
| `0x9BF9C` | inventory transition prefix |
| `0x9BE78` | HP root update prefix |
| `0x9D1A0` | rage root update prefix |

These are package offsets, not native addresses. Resource descriptors at
`0x8E830` (HP, 256x32) and `0x23D60` (rage diamond, 32x32), their constructor
references and the parent-call sequences corroborate the component roles;
those names are layout inference, not recovered engine symbols.

REA/Ghidra corroborated native sprite initialization and centered half-extents:

- `0x409290`: `ev_e885ffa56a7a5323fe71a845b7bd800cea1f15d154b61af8ddf653f42ec91a84`;
- `0x446D40`: `ev_f27b00cfdc7bac1db749003c4010c09a2b8bc1415ef384aad8e9320d61973d7c`;
- `0x42BF10`: `ev_8e8b03dacaed3573dd61c022eaf31de646bee66b5386893865e9505ba77e90a2`.

The tiled movie implementation's six-argument pseudocode is Evidence
`ev_dc0b7bbbb97d75729166d05e38a1d98a00cbf21a0a7ebcb517a301f6df014c43`.
The exact argument count is established by the native call sequence and
`add esp,0x18`, checked by Capstone—not by trusting incomplete decompiler
parameter recovery for the alternate movie path.
The REA bundle is retained in the approved temporary directory as
`widescreen-hud-20261117-rea.json`; no game code or artwork is bundled into the SDK.

```powershell
python tools/inspect-widescreen.py --game "E:\Games\I-Ninja"
cmake --build build --config Release --target EthhhSDK eth_ethhhpatch_inventory_tests eth_inventory_performance_tests
.\build\tests\interop\eth_ethhhpatch_inventory_tests.exe
.\build\tests\interop\eth_inventory_performance_tests.exe
```

The read-only inspector prints hashes, bounded ASL contracts, asset dimensions,
native dispatch/argument evidence and fit expectations. It does not extract
assets or access a process.

## Offline verification

- **10505** assertions in the shared HUD/text/restore/movie Win32 ABI suite.
  HP/rage coverage includes 4:3, 16:10, 16:9, 21:9 and 32:9, four viewport
  widths, hidden states, centered selector, geometry refusal, all signature
  bits and independent root slots.
- Existing inventory/tick/zero-tick text behavior, fresh protected copies,
  header provenance, custom clipping and stale/foreign-edit restoration remain
  covered.
- The combined fixture (64 UI objects, 1024 glyph calls) measured **0.0258 ms**
  per tick/render pair, **5 queries / 1467 fresh copies**. Its conservative
  control measured 0.9201 ms / 1467 queries. These are SDK-only fixture timings,
  not observed FPS or a live performance guarantee.
- 29 native-only legacy evidence tests pass with the actual local executable.
  The full legacy asset suite is unavailable here: installed `general.vol`
  differs from that fixture's expected digest, and its historical decoded Ninja
  copies are absent. Those assets/fingerprints were not replaced or relaxed.
- Projection policy, hot-path guards, ABI exports and button-prompt production
  loader checks are exercised separately.
- Follow-up verification: 24,452 display-policy assertions (including Reset
  recovery), 10,505 HUD/movie assertions and 30 hot-path guards pass. The
  HP-attached-clock fixture measured 0.0323 ms/tick+render, 5 region queries
  and 1479 protected copies; this remains an offline fixture, not live FPS.

## Original acceptance checklist / follow-up regression checks

The staged `build/dist/EthhhSDK.dll` is not automatically installed. Close the
game manually before replacing it; do not force-terminate the game.

1. Compare HP at full/partial health, damage, healing, death/respawn and
   pause/unpause. Its left margin should be stable without stretching the bar.
2. Charge/use rage, open/close the ability selector, and check idle/hidden
   diamond placement, centered expanded selection, icons and any associated text.
3. Recheck inventory changes, expiry and pause visibility; the new groups must
   not displace its four tracked parents.
4. Play a `.bik` with the patch active: equal side margins at 16:9/ultrawide,
   preserved image aspect and opacity, and no rightward crop/offset. Recheck
   at 4:3 and after display changes.
5. Check F9-closed FPS with all groups hidden/revealed, and load/restore/reset
   behavior. The existing bounded `inventory profile` log now measures the
   shared HUD paths; it still disables itself after 300 renders.
