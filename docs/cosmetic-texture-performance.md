# Cosmetic texture draw performance

## Scope and evidence

This is an offline source analysis and synthetic benchmark, **not a live FPS
measurement**. No game was launched, attached to, or modified for profiling.
The production loader, SHA verification, preparation, upload and scoped draw
code are compiled into `CosmeticTextureRuntimeTests.cpp`, using isolated native
mesh fixtures and a fake D3D9 device.

The previous `BatchDraw` did expensive work on every opaque/transparent pass:

- `PrivateMesh` searched from list heads for each mesh, twice. For N meshes in
  one list this required N(N−1) next-link reads per pass.
- Each general-purpose `SafeRead` queried memory with `VirtualQuery`.
- Source verification copied and SHA-256-hashed each distinct native source
  every pass, even with an already uploaded replacement texture.
- Owning vectors for borrowed descriptors, verified sources, source bytes and
  the BCrypt hash object allocated repeatedly.

The caller also performed actor discovery and collected bindings per pass.
Those costs are **not included** in this module's benchmark.

## Caller API and lifetime contract

```cpp
// On engine tick, after actor identity validation and private COW setup:
bool ready = textures::PrepareBatch(bindings);

// At the actual synchronous native draw pass, after cheap actor lifetime guards:
bool ran = ready && textures::DrawPrepared(continuation, context);

// BEFORE actor/resource generation change, rebind, teardown, reload or stop:
textures::InvalidatePrepared();
```

`PrepareBatch(vector)` owns one module-level prepared snapshot. Repeated ticks
with identical binding values AND current native descriptor/topology snapshots
are guarded-read-only no-ops. A descriptor change is checked even if binding
pointers have not changed. Preparation is CPU-only; the first draw, palette
change or device change can allocate/upload. Once warmed, draws do not hash,
allocate, call `VirtualQuery`, rediscover actors, or follow linked lists.

The current editor integration conservatively calls `InvalidatePrepared()`
**before every native simulation tick**, since no exact resource-unload hook is
available. It rebuilds and hashes each distinct source once after that tick,
not once per render pass. Thus the standalone API's cross-tick no-op capability
is not used by this host. GPU handles stay warm. This trades bounded tick-side
work for rejecting identical-address/header source reuse; no persistent source
trust is assumed across an unobserved native lifetime boundary.

`PrepareBatch(vector, PreparedBatch&)` and
`DrawPreparedBatch(const PreparedBatch&, continuation, context)` provide
explicit snapshot storage if needed. The explicit preparation overload always
rebuilds membership; use the single-caller overload for tick no-ops.
`InvalidateBindings()` is equivalent to `InvalidatePrepared()` and also
invalidates explicit snapshots by generation.

`false` means the continuation has **not** run; the host may draw natively once.
The module does not call fallback itself. Native continuation exceptions
propagate after `__finally` restoration: never catch them and retry the pass.
Reentrant substitution/release/invalidation is refused while borrowing.

The host must still verify the complete actor identity/generation, registry
membership, class, appearance, owner and instance association before each
prepared draw. These cannot be proven by a mesh descriptor alone. Use the
engine thread and do not concurrently destroy resources.

The module checks:

- Instance private-clone flag and both current list heads, once per instance.
- Every cached node's `next` value via a **flat array of guarded reads**. This
  detects middle-chain edits/detachment even when heads do not change.
- Every bound mesh's current texture pointer.
- Complete current native descriptor and concrete binder, deduplicated by
  descriptor address. A changed handle/source/size/flags/vtable fails closed.

Checks occur after any first-use D3D work and before any borrowed pointer write.
Only texture pointers are borrowed; they are restored in reverse order on
normal return or native SEH unwind. No mesh tint/color field is modified.

## Cache bounds and invalidation

- At most 128 bindings/instances and 2,048 total cached chain nodes.
- At most 512 nodes per native list; cycles/oversize lists fail closed.
- 64 native-source verdicts, replaced round-robin. Both positive and negative
  verdicts use the full descriptor, source pointer, size and slot as their key.
  Cache misses hash only during preparation; eviction may cause another hash
  on a later tick, never during `DrawPrepared`.
- Three replacement images/GPU textures, one palette value per slot. Conflicting
  palette values for a slot in the same batch are refused.

`Load`, matching-owner `Release` and `Shutdown` invalidate snapshots/source
verdicts automatically. Failed loads remain transactional. Device identity
changes drop/recreate GPU handles, without per-pass source hashes.

The caller **must invalidate before native source memory mutates or is freed
or reused**, including address reuse with identical descriptor values.
Descriptor comparisons cannot detect in-place byte mutation or an ABA lifetime
change; repeatedly hashing would not establish actor ownership either.
Invalidate on scene/resource generations, clone destruction/rebind and actor
identity/appearance changes. Unexpected topology/descriptor changes already
fail the draw; the next tick can rebuild. Do not prepare from the render hook
as a fallback.

## Full-head replacement

There is no head-mask API, inferred mask, native-head decode/compositing, or
head RGB neutralization. Head bindings must use `rgb = 0xFFFFFF`; the full
authored replacement image, including its alpha, is uploaded unchanged.
The native material head tint provides the color. The three-image `Load` API
and public transactional `Decode` TGA decoder remain available.

## Reproduce

With the existing supported-build original TGA fixtures extracted into
`build/cosmetic-textures`:

```powershell
cmake --build build --config Release --target eth_cosmetic_texture_tests
& build/tests/interop/eth_cosmetic_texture_tests.exe `
    mods/CosmeticEditor/textures build/cosmetic-textures
```

The test runs 64 private meshes × 1,000 passes, sharing one native texture.
The frozen old validation/setup model uses the former two membership searches,
per-pass SHA deduplication and owning scratch vectors. It excludes GPU calls
and native draw. Prepared timing includes production pointer substitution and
restoration around a trivial continuation, after GPU warm-up.

One local Release run:

| Work across 1,000 passes | Old validation model | Warm prepared draw |
| --- | ---: | ---: |
| SHA-256 calls | 1,000 | 0 |
| Source bytes copied/hashed | 66,322,000 | 0 |
| `VirtualQuery` calls | 4,481,000 | 0 |
| C++ `new`/`new[]` calls | 11,000 | 0 |
| Linked-list next traversals | 4,032,000 | 0 |
| Elapsed time (illustrative) | 1,723.950 ms | 6.469 ms |

Prepared draw still performs 133 guarded reads per pass for this fixture:
64 flat next-field checks, 64 mesh texture pointers, two instance reads,
two shared descriptor/binder reads, and one device-global read. These are
bounded direct reads, not list traversal or OS queries.

Query counts come from a real `VirtualQuery`-backed test safe reader. Allocation
counts instrument C++ `new`/`new[]`, not internal BCrypt/driver/CRT allocations.
The fake device isolates warm-up and cannot establish real GPU timing, reset
behavior, driver overhead or live FPS. Elapsed times vary by machine and are
not asserted. Exact operation counts and zero-allocation/query/hash warm paths
are asserted, including tick preparation's no-op path.

Additional checks cover full-head authored pixels/alpha, tint preservation,
bad source hashes and negative caching, descriptor changes at unchanged
addresses, mesh pointer/flag/head/middle-chain changes, invalidation, duplicate
bindings, unreadable memory, failed D3D creation, reentrancy and native SEH
restoration.
