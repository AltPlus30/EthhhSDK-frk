# Cosmetic tick cost, v0.5

Source analysis and isolated synthetic tests only: no game launch, attachment,
installation, live FPS measurement, or claim about a particular frame rate.
This supersedes the per-tick invalidation description in
`cosmetic-texture-performance.md`.

Main's Lua UI also caches material, donor-model and blood-status snapshots for
200 ms using ImGui's real-time clock (not paused game time). Widgets still draw
and accept input every frame; explicit commands invalidate the corresponding
snapshot immediately. The experimental model browser is collapsed by default,
and does not request/build donor tables while closed. A Lua test renders 1,001
GUI frames over one second and asserts at most six snapshot requests per API,
while all color widgets remain active. None of this reduces engine tick rate.

The motivating live report was roughly 50 FPS in some scenes with F9 closed
and roughly 30 with it open after v0.4. Scene, GPU/CPU timings and a same-scene
disabled-mod baseline were not captured. These are user observations, not an
assistant runtime measurement; synthetic CPU improvements below do not prove
that all overlay/GPU frame drops are explained or fixed.

## What changes

- **No engine ticks are skipped or slowed.** The existing
  `BeforeNativeTick()` / `OnNativeTickCompleted()` signatures are unchanged.
- Full cosmetic inspection, material-view formatting and optional discovery run
  on a **150 ms** cadence for stable appearances. Existing player/head discovery
  retains its 250 ms limit. GUI `Inspect()` polling does not force full work.
- Set, pack load/clear and matching-owner Release are commands, not inspection
  requests: they bypass that cadence on the next native tick. Ownership,
  session persistence, copy-on-write, generation changes and conditional
  restoration retain their existing contracts.
- Every intervening active tick checks cached actor handle/class, registry
  membership, model/animation, render mode, instance pointers/flags/list heads,
  linked-head owner handle, and each material's lookup/count/name-table/
  pointer-table/name/address. Guarded reads are fresh; failure immediately
  falls back to full validation. Cached material colors are read each tick;
  native script refreshes become the new baseline and the requested RGB is
  immediately reasserted without touching alpha.
- Stop disables texture drawing immediately and queues safe material restoration
  for the next native tick, irrespective of the inspection deadline.

## Native texture lifetime boundary

The former host invalidated every prepared binding before every tick, causing
post-tick list collection and fresh BCrypt SHA work even for unchanged resources.

`textures::SuspendPrepared()` now blocks drawing and increments the generation,
also invalidating externally retained batches. Structural snapshots are retained
only as hints. The post-tick convenience `PrepareBatch(bindings)` verifies the
entire flat topology, instance flags/heads, mesh texture pointers, descriptors
and concrete binder again. It then freshly reads **every native source byte**
and compares it to an immutable SDK-owned original previously admitted by SHA.
Only success resumes drawing. A changed/unreadable source, including reuse at
the same address with an identical descriptor, fails closed.

Three canonical originals total 117,046 bytes. Warm comparison uses a bounded
4 KiB stack buffer and guarded copies plus `memcmp`: no heap allocation,
OS memory query, or BCrypt call. Canonical originals may survive invalidation
because they are immutable byte values, not native pointers or cached source
verdicts. Cold admission still requires the shipped source digest. Invalidated
source-verdict entries cannot bypass the next tick's fresh-byte proof.

Full invalidation remains available for teardown, pack/owner/appearance changes
and structural rebuilds. Failed validation does not leave a drawable gap:
drawing remains refused until successful post-tick preparation. Source mutation
outside the native tick must still be preceded by suspension/invalidation;
concurrent resource destruction is not supported.

Prepared draw behavior is unchanged: after GPU warm-up it performs no
allocation, source hashing/comparison, list traversal or `VirtualQuery`.
Borrowed mesh texture pointers are restored in reverse order, including SEH.
Native draw-pass frequency is not upload frequency.

## Counters for the host UI

`textures::GetPerformanceCounters()` keeps existing `hashes`, `sourceBytes`,
`guardedReads`, and `chainNodes` fields, and adds:

| Field | Meaning |
| --- | --- |
| `sourceComparisons` | Fresh whole-source equality attempts against canonical originals |
| `uploads` | Successfully created/uploaded replacement GPU textures |
| `preparations` | Full binding rebuild attempts, including refusals; excludes retained-snapshot hits |
| `suspensions` | Accepted suspension calls at native tick boundaries |

Counters are monotonic until explicit `ResetPerformanceCounters()`; ordinary
release/invalidation does not reset them. `sourceBytes` includes bytes subjected
to SHA or comparison. Failure counts may include the attempted full source size.
`View::fullInspections` and `View::fastTicks` count editor paths since Shutdown.
Hundreds of successful draw passes per second need not mean any repeated upload.

F9 also shows SDK post-tick elapsed work: last, last full inspection and peak
since SDK start for costume work, plus last/peak for blood work. QPC timers
exclude the game's original simulation and draw continuations; editor timing
also excludes mutex waiting. Peaks include cold preparation/uploads and loads,
and elapsed scheduling delays are possible: these are wall-time samples, not
CPU utilization or GPU timing. Idle callbacks do not generate fake samples.

## Reproduce and results

```powershell
cmake --build build --config Release --target eth_cosmetic_editor_tests eth_cosmetic_texture_tests eth_cosmetic_head_tests
& build/tests/interop/eth_cosmetic_editor_tests.exe
& build/tests/interop/eth_cosmetic_texture_tests.exe mods/CosmeticEditor/textures build/cosmetic-textures
& build/tests/interop/eth_cosmetic_head_tests.exe
```

Texture fixture: 64 private meshes sharing one original Blue descriptor,
1,000 native boundaries/preparations plus two actual prepared draws per tick,
after GPU warm-up. The v0.4 model discards preparation and retained originals
every tick, forcing its former full rebuild/SHA policy; both use the same
production preparation and draw implementation.

One local Release run:

| Across 1,000 ticks + 2 draws/tick | v0.4 policy model | v0.5 |
| --- | ---: | ---: |
| SHA calls | 1,000 | 0 |
| Fresh canonical comparisons | 0 | 1,000 |
| `VirtualQuery` calls | 1,000 | 0 |
| C++ allocations | 2,000 | 0 |
| Linked-list traversals | 64,000 | 0 |
| Illustrative elapsed time | 61.797 ms | 8.660 ms |

The new path still compares 66,322 source bytes per tick for this one-slot
fixture. These byte reads are intentional, not throttled source trust.

Separate editor end-to-end fixture uses the real editor and isolated texture
ABI: 1,000 pre-ticks + `Inspect()` + post-ticks + draws. Its cached path used
zero general `SafeRead` calls and 4,000 allocations from copied Views;
forcing full inspection every tick used 50,000 `SafeRead` calls and 13,000
allocations (approximately 0.598 versus 3.215 ms in one run). This is a
conservative forced-full comparison, not a frozen historical editor binary.
The real texture module is measured separately above. A 150 ms deadline is
held in the future for this deterministic stable-path test; production still
performs periodic full inspections.

Tests assert GUI-spam throttling with and without an armed palette, immediate
queued color changes, native color reassertion, no permanent tick suspension
gap, unchanged-address source mutation refusal/recovery, zero-allocation/query/
hash retained texture ticks, owner isolation, restoration, actor generations,
head ownership, full authored head pixels/alpha, and SEH pointer restoration.

Counts instrument C++ `new`/`new[]` and a real `VirtualQuery`-backed safe reader;
they do not instrument internal driver/CRT/BCrypt allocations. Fake D3D cannot
establish live GPU/reset costs. Timings vary and are not asserted. Optional UI
snapshot polling can reduce View-copy/Lua overhead independently. Full
inspection, failures/transitions and first GPU uploads remain more expensive.
Wavy-sash `ApplyKnown` still runs when requested on intervening ticks so native
refreshes are not hidden by inspection throttling; its own checks are outside
the texture-only measurement.

If live drops persist, compare the same camera/scene with Cosmetic Editor
disabled, blood-only, and costume-textures-only, keeping other mods/settings
unchanged; then repeat with F9 open. Record render passes/s versus GPU uploads.
Steady upload counts distinguish ordinary repeated draw passes from resource
recreation; remaining render/UI/GPU costs require live timing evidence.
