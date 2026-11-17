# Right inventory HUD — 2026-11-16

**2026-11-17:** The user reports that the performance correction fixed the FPS
drop. The reported inventory scenario is accepted; this is not an exhaustive
performance guarantee. HP/rage/movie work now shares the same policy/caches:
see [the combined widescreen fix](widescreen-hud.md). The sections below retain
the original investigation and incremental verification history.

## FPS regression investigation

The user reported a large constant FPS drop after the text correction. The
installed log confirms that both inventory anchoring and text clipping matched,
without repeated match messages. It contains no per-hook timings, so it does
not establish which hook explains the full reported drop.

The new paths were doing repeated general `SafeReadBytes`/`VirtualQuery` work
for UI-object ancestry, font queue snapshots, and every matched glyph, plus a
linear tag search and repeated clip-bound math. A real Win32 isolated fixture
(64 UI objects, 1024 glyph calls per tick/render pair) measured the previous
helper path at **0.6864 ms and 1040 memory queries**. That is observed offline
overhead, not sufficient evidence by itself to explain a huge live FPS drop.

The staged correction:

- reuses the existing protected-copy reader with **separate tick/render region
  caches**, reset at their respective phase boundaries;
- still copies/checks fresh native bytes under SEH on every read—no object,
  queue, font, or handle identity is cached;
- indexes queued text with a bounded fixed hash table; unrelated glyphs perform
  zero native reads, rather than scanning every tag;
- computes percentage clip bounds once per render, not per glyph;
- moves the first text-match file log out of the glyph callback.

The updated fixture measured **0.0190 ms and 4 memory queries**, with the same
1040 protected copies and inventory decisions. Its conservative-reader control
measured 0.6525 ms/1040 queries using the updated indexed policy. Timing is
informational, not a flaky pass threshold or an in-game FPS recovery claim.
The fixture also revokes/guards real pages and edits queue headers to prove that
cached permissions do not bypass protected copying or retain stale native data.

A bounded one-shot profiler now samples UI SDK work and glyph SDK/native work
for the first **300 renders after inventory activation**, then disables itself.
It emits one `EthhhPatch inventory profile` line with estimated SDK
milliseconds/render, native clip microseconds/call, callback counts and total
memory queries. Sampling rotates across object/string positions; original UI
drawing and native glyph clipping are excluded from SDK timings. It does not
change simulation ticks, Present pacing or graphics settings.

The user subsequently reported that live performance was fixed. The fixture
numbers alone were not treated as acceptance; that later user feedback confirms
the reported scenario. New combined HUD changes still require their own retest.

```powershell
cmake --build build --config Release --target eth_inventory_performance_tests
.\build\tests\interop\eth_inventory_performance_tests.exe
```

## Live feedback and text correction

The user tested the first build: **inventory images slide correctly, but the
text disappears**. That build is not accepted as a complete HUD fix.

The missed path is native font clipping. `0x40D3A0` initializes each font's
horizontal clip to `[0,640]`. Text queueing (`0x40D1D0`) preserves the new parent
position, but glyph clipping (`0x4484B0`, called at `0x448BE4`) still rejects
glyphs outside that original rectangle **before** the HUD projection squeeze.
For example, a shown 16:9 parent at authored X=341.33 is already beyond the
original right boundary after adding the coordinate origin at 320.

The follow-up keeps the working image/parent fix. A display-scoped hook on the
native UI-object draw tracks only font queue entries appended by generation-
checked descendants of the four inventory roots. At the exact glyph clip
caller, only those entries receive a **local copy** of the font rectangle with
horizontal bounds `320 +/- 320 * aspect/(4/3)`, rounded outward. The original
clipper still trims UVs/vertices, including hiding digits beyond the real screen.
Vertical/custom subrect clipping stays native. No shared font fields, font
lists, text bytes, atlas pointers, or button-prompt hooks are changed.

UI-object labels are queued inside the native tick (`0x43E119` calls the
UI-object loop at `0x42B580`), after the tick's font-queue reset at `0x43E0A8`.
Queue tags are bounded, checked against fresh queue headers, and expire at the
next native tick or display deactivation, not on every render: zero-tick render
frames must retain the last tick's labels. The clip thunk captures the queued-string
pointer from the verified caller's stack slot, not a coordinate heuristic.
This correction is built/offline-tested but **still needs live retesting**.

## Finding

The existing centered 4:3 containment is not edge anchoring. In `09.vol/9-96.LCS`,
exact class `Panel` (decoded record `0xAA740`, unit `0x9AC50`) drives four
inventory roots with centered authored X coordinates:

- hidden/rest: **396**;
- shown: **256**, alternate shown target **272**;
- slide-out easing target: **416**, then reset to 396.

At 1920x1080, the old canvas puts X=396 at approximately **1851px**: inside
the actual screen. X=256 appears at **1536px**, a **315px** difference, consistent
with the reported ~316–318px. These numbers are static coordinate calculations,
not a captured live measurement. The native animation is intentional; its hidden
endpoint had become visible.

## Change

For only this inventory root-position call, pass a copy of the native arguments
with `X *= viewportAspect / (4/3)` when wider than 4:3. Under the existing HUD
projection containment, this puts the shown root at 90% of screen width (10%
right margin), or 92.5% for the alternate target, and the rest endpoint at
111.875%, beyond the real right edge. The full animation follows the same
percentage mapping, including ultrawide modes; a constant translation would
not keep the hidden endpoint off-screen at sufficiently wide aspects.

Only parent anchors move. Native child sprite offsets, text, icon sizes, Y,
values, reveal timers and animation registers remain unchanged. The native
parent path propagates the anchor to icons and number text.

The lazy display hook at `0x41DD10` accepts only:

- active, verified display patch, wider than 4:3;
- cdecl VM caller `0x426063`, with native ESI captured before a C++ prologue;
- exact preceding array-load/call sequence for the inventory (`unit+0xDB0`);
- exact `Panel` owner, current full-generation UI handle, sprite-object vtable,
  owner/self handle consistency, and no parent.

Only four roots are retained, with no per-call allocation. Deactivation restores
the last unscaled arguments via the original native function, after fresh
identity and installed-position checks. Stale/replaced/foreign-edited objects
are left alone. No executable/archive files or ASL registers are modified.
Applying a display plan while already paused may require the next native Panel
update before the new anchor takes effect; there is no guessed forced script tick.

## Evidence and reproduction

Supported executable SHA-256:
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.

`9-96.LCS` SHA-256:
`7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4`.
Decoded first block:
`127243447e13d1bc29960008952c6f6033446b03a67fbe36b0bfebdab13f9edc`.

```powershell
python tools/inspect-inventory.py --game "E:\Games\I-Ninja"
cmake --build build --config Release --target EthhhSDK eth_ethhhpatch_inventory_tests
.\build\tests\interop\eth_ethhhpatch_inventory_tests.exe
```

The read-only inspector checks the fingerprint, Panel association, four-item
loop, position call/anchors, native entry, cdecl dispatcher, vtable and native
parent refresh. It extracts no files and never opens the game process.

REA/Ghidra corroborated:

- `0x42BEF0`: stores parent X/Y, delegates derived sprite coordinates;
  Evidence `ev_8fe2f47331b776765f4d6da5d22212de98363dd04b1280dd4249d3f5ca61747b`.
- `0x42BC60`: adds parent anchor and converts to sprite coordinates;
  Evidence `ev_801dcba15ba89c760b325d4c1178a6a045827bdb69cb7b2fa0622f67bd610dfc`.
- `0x446DE0`: fixed authored sprite-to-NDC conversion;
  Evidence `ev_c2dbbf7ef2bac1a1ba0d7abbc42e6cfe5c9ca23c392b0bf68abc3437c5b15e7d`.

The exported REA bundle is in the approved temporary directory:
`inventory-hud-20261116-rea.json`. Some native entry points were not defined
functions in Ghidra; their exact instructions were checked separately with
Capstone by the reproducible inspector. Static relationships are not proof of
live rendering.

The inventory-stage policy/Win32 thunk tests passed **7540 assertions**, covering
percentage positions at 4:3/16:10/16:9/21:9/32:9 and 640–3840 widths, off-screen
endpoints, invalid inputs, every call-signature bit, class/owner/handle guards,
four-root bounds, restoration/foreign edits, stack cleanup and preserved
nonvolatile registers. This does not execute the game's UI renderer.
The text regression additionally covers pre-projection rejection at 16:9+,
visible versus hidden glyph bounds, unchanged vertical/custom clipping, exact
parent membership, queue provenance/edits/cycles/tick expiry/zero-tick retention, and the production
clip thunk's stack slot, caller, return value and register preservation.
New cases fill all 128 tag slots, check collision/rollback/expiry behavior,
assert one fresh header read for a tagged lookup and zero native reads for 1024
unrelated lookups, and compare precomputed versus original clip decisions.

REA text-path Evidence:

- `0x40D3A0` default `[0,640]` font clip:
  `ev_40aef977bc58e7abf9b3ca8064747d61c4720edb5179124a3759a992b3347103`.
- `0x40D1D0` prepends queued text with a 24-byte header:
  `ev_12e04d151054b613c841f7c28698eb4bbcc1deafd55afd1a87c4a5c3444bf991`.
- `0x4484B0` clips vertices/UVs against font rectangle fields:
  `ev_bcc26578e13bb6bcb3f29d50aed5e39863e15b35dba7db11b106cc9e25d76458`.

The reproducible inspector now also checks these clip/queue offsets, the
renderer stack-slot contract, and the clipper's rectangle-only font reads.
The new REA records are exported to the approved temporary directory as
`inventory-hud-text-20261116-rea.json`. Button-prompt production
loader/ownership/replacement checks also pass with the existing Xbox atlas;
this is an offline compatibility check, not live visual acceptance.

Targeted existing regressions also passed: display policy (24433 assertions),
projection/runtime policy (274), camera-relative heading/ABI (150929),
hot-path guards (28 tests), and the 219-export ABI check. The complete legacy
test runner was not run.

## Required live acceptance

Initial image anchoring is user-confirmed; the text correction is not yet
installed or live-tested by this agent. Install the new staged DLL only after
manually closing the game; do not force-terminate it.

1. At 1080p, let inventory timers expire: all four entries should hide beyond
   the right edge, not remain visible there.
2. Pick up/spend coins, throw darts/shurikens, change lives: the corresponding
   entry should reveal near the right edge, with icon and digits moving together,
   then hide on the native timer.
3. Pause/unpause, including during a reveal. Native pause visibility must remain.
4. Repeat at 4:3, 16:10, 4K and ultrawide; verify percentage margins, unstretched
   icon/text sizes, and unchanged other HUD/menu elements.
5. Apply/change/restore the display patch, reload and respawn. Check for stale
   offsets, duplicate scaling and old-object writes.

The log records the first accepted inventory call per activation:
`inventory percentage anchoring matched Panel`. A changed/unrecognized script
or native contract is refused rather than corrected by coordinate heuristics.
The follow-up also logs `inventory text clipping matched queued inventory label`
on its first accepted glyph. If text is still missing, check whether this second
message appears.
