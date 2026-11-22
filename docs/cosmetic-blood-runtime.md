# Blood texture binding (static implementation; no game execution)

## v0.5 update

The binder ABI and five baseline original/alpha hashes below are unchanged.
Two more SHA-attributed Ranx pool textures and the separate anatomical split-cap
atlas are now admitted (eight resources total). Their full original image is converted on cold admission using maximum
RGB-channel intensity, then multiplied by the same Blood RGB; original alpha
is copied exactly. No extra authored files are needed. This conversion is
limited to those **whole pool textures** and the geometrically/UV-isolated
anatomical cap, never mixed armor/skin atlases or an arbitrary green texture.
The cap is 128x128, 17170 bytes, SHA256
`b940a1d4a0afda2e49e019c1c6224af1b1c5da732c97207007a8a9ed751c17b8`;
its opaque alpha is retained, including the small contrasting internal detail's
brightness (the whole interior takes the chosen hue). Provenance/links:
`cosmetic-blood-coverage-v05.md`, `build/blood-v05-originals/provenance.json`,
`build/blood-v05-split-verified/inspection.json`.
The old v0.4 cache/update policy described further down is superseded:

- Every native tick still revokes native descriptor/source trust. Post-tick
  validation freshly copies each distinct source allocation once, even if its
  descriptor/header is unchanged.
- Exact byte equality against an SDK-owned SHA-admitted source preserves the
  same identity proof without repeating BCrypt. Eight positive byte patterns are
  pinned; 32 additional bounded negative byte verdicts cannot evict them.
- All 128 admitted descriptor aliases can revalidate; the old eight-positive
  quota could leave later aliases native/green. Only *new unknown byte patterns*
  consume the four-hash cold-admission budget. Deferred candidates stay native.
- Address lookup now probes the bounded table completely when necessary, not
  only eight entries. Recycled/unreadable identities use tombstones so deleting
  an earlier collision does not strand later entries. Under table pressure the
  least-recently drawn older identity can be replaced; stable positive resources
  otherwise remain warm across quiet scenes. Fresh byte proof still runs before
  reuse. This avoids repeatedly flashing a native-green first splat after idle.
- Warm Bind still allocates/uploads/hashes nothing. Successful substitutions
  are counted separately for each baseline source; the F9 readout distinguishes
  splatters and pool, and shows deferrals/cache saturation.

Tests include 32 aliases, twelve same-bucket collisions, tombstone recovery,
same-address byte mutation, 100,000 warmed binds and 1,000 full native boundaries.
One Release run compared 5,000 old SHA checks (hash-only model: 9.611 ms) to full
new source/descriptor validation (2.448 ms, zero SHA/allocations/uploads).
The current benchmark also includes 37 binder calls per new tick (3.333 ms
against 9.809 ms for the old hash-only lower bound in another Release run).
These tiny fake-device timings are **not a diagnosis of live frame drops**.
Source revalidation remains mandatory; no game simulation ticks are skipped.
The ordinary hook still does not cover every world-particle/shader path.

## Integration contract

`src/core/cosmetic_blood.h/.cpp` implements the five positively attributed blood
resources from `cosmetic-head-blood-research.md`, not a generic green-texture
rewrite. Main integration owns hook installation, Lua, packaging and CMake.

1. Require executable SHA-256
   `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
   This is the integrator's existing supported-build gate, NOT a per-frame hash.
2. Call `blood::VerifyHookFingerprint()` **before** installing the hook.
3. Hook preferred-image VA `0x455B10`. Exact ABI:
   `long __thiscall Binder(void* descriptor)` (ECX=this; no stack args;
   EAX=the native `SetTexture` HRESULT). A MinHook detour may use
   `long __fastcall Detour(void* descriptor, void* unusedEDX)`.
   Its entire body should return `blood::Bind(descriptor, trampoline)`.
4. `Configure(owner, modDirectory, fiveRelativePaths, 0xRRGGBB, error)`
   loads/bakes CPU images transactionally, with no D3D work. Order: splatter
   0/1/2/3, pool. See `mods/CosmeticEditor/blood` filenames. Paths may not escape
   the canonical mod root. Files must decode to grayscale, original dimensions,
   and independently fingerprinted original alpha.
5. Call `Tick()` on the **engine/render thread**, outside any draw. This warms
   GPU resources and verifies at most four queued source identities per call.
   Cold candidate binds run natively; they become replacements only after Tick.
6. **Before** level/resource unload, source mutation, pointer reuse or load-island
   generation change: `InvalidateBindings()`. The binder cannot distinguish
   in-place edits with identical descriptor headers. This lifecycle call is
   mandatory, even when addresses are reused. Do not use a global/per-draw
   material scan as discovery.
   Current integration also calls `BeforeNativeTick()` before *every* native
   simulation tick. This revokes positive trust before native resource reuse;
   post-tick `Tick()` rehashes up to eight previously positive descriptors plus
   four cold/rejected candidates (round-robin). Over-budget entries stay native.
   This deliberately pays bounded tick-side hashing rather than claiming an
   unimplemented load/unload hook. No source hash is trusted across a native tick.
   Resource mutation by unrelated mods outside this serialized interval remains
   unsupported; this is not a resource pin.
7. Before D3D reset or device destruction: `OnDeviceReset()`. Tick checks device
   identity and cooperative level, then recreates private textures. Managed
   resources ordinarily survive Reset, but explicit release makes destruction
   and device replacement safe. Bind never tests cooperative level or uploads.
8. `Release(owner)` on owning-mod unload; `Shutdown()` on SDK shutdown, before
   releasing the device/removing hook storage. Do not unload the hook while it
   is executing.

All APIs are single-threaded engine/render-thread only; no API can race native
resource destruction. Reentrant destruction/configuration during a native binder
call is refused. The original is invoked synchronously exactly once (unless a
null trampoline is supplied, which returns `E_FAIL`).

## Why this boundary is synchronous and doesn't own sprites

Offline Capstone 5.0.7 decoding of the hash-verified executable:

* `455B10..455B43`: reads renderer global `525500`, calls `446BF0` for native
  state setup, reads `[ECX+0xC]` via saved EDI, calls device vtable slot
  `0x104` with `(device, 0, handle)`, pops saved EDI/ESI, plain `ret`.
  No descriptor pointer escapes; no source, palette or alpha is read. No stack
  parameters are consumed. The final call's HRESULT remains in EAX.
* `42BF10` allocates a native UI/render sprite through `438260`, then `409290`.
  `409290` calls `446D40`; that stores the exact texture descriptor at sprite
  `+C8`. `447366..447374` loads that descriptor in ECX and dispatches its
  vtable slot zero synchronously (`mov edx,[ecx]; call [edx]`).
  An ordinary descriptor therefore reaches this exact binder.
* Ordinary pool meshes use the same texture slot-zero path as other native
  meshes. The pool is model `pool of green b`, **second** mesh list, texture
  A+`1C7DE0`, white material `FFFFFFFF`; do not require a non-null first list.
  Existing ordinary-mesh binding research is in `cosmetic-texture-binding.md`.
* Emission native `41D130` and manager `519A60` own a separate `0x2C`-object
  registry, not strat handles. This implementation never reads/writes it,
  never hooks allocation, never edits a mesh/material, and never persists a
  pointer in native descriptors.

Reproduction (read-only):
`python -B tools/inspect-cosmetic-research.py --native 0x455b10 0x34`;
repeat at `0x42bf10 0xd0`, `0x409290 0xa6`, `0x446d40 0x9d`,
`0x447366 0x1d`. The complete binder is fingerprinted before hooking, including
relocation-normalized renderer-global operand.

REA Ghidra session was inspected before opening and closed after attempted
inspection: Ghidra did **not** recognize `455B10` as a procedure, so no successful
REA pseudocode/Evidence is claimed for it. Its exported ledger at
`C:\Users\Akira\AppData\Local\Temp\opencode\cosmetic-blood-binder-rea-2026-11-20.json`
contains zero records. ABI and the byte fingerprint above are from original
executable bytes/Capstone, not invented decompiler output or runtime observation.

## Identity, tint and performance

Only 32x32/1050-byte or 64x64/5138-byte ordinary descriptors enter the bounded
cache. A source must SHA-256-match one of the **five exact original TGA hashes**
before becoming positive. Dimensions, names, green color, addresses or palette
similarity alone cannot authorize substitution. Known-size impostors are
negative-cached within the tick interval. The fixed 128-entry table probes at
most eight slots per bind; overflow stays native until invalidation. Tick
reads/hashes at most twelve sources (maximum 61,656 source bytes, typically five
small original TGAs) and never scans native material lists.

Warm Bind performs constant-size guarded descriptor/vtable snapshots, bounded
lookup, full descriptor equality and device-identity check. It has **zero heap
allocation, SHA, VirtualQuery, native list walks, decode, COM creation or
texture uploads**. Native source handle/header changes fail native and enqueue
fresh verification. Source contents must be immutable between a post-native
tick and its synchronous render passes; the next native tick revokes trust.

The verified descriptor is copied on the SDK stack, only `+C` is replaced, and
the native binder receives that temporary copy. Native descriptors and original
GPU textures remain untouched. Five SDK-owned `D3DPOOL_MANAGED` A8R8G8B8 textures
contain grayscale intensity multiplied by the selected RGB; original alpha is
copied without modification. This is per-attributed-effect texture tint, not a
global material green rewrite. Native corner/material modulation, animation,
alpha and blend behavior remain native; the requested RGB is not a promise of
identical final framebuffer RGB under lighting/blending. Initial cold effects
can use native green until their first verification Tick.

Coverage is the four shared NinjaSword blood splatters and named pool; not every
enemy/level/death effect has been inventoried. Other uses of **identical bytes**
will share the replacement; resource identity is verified, creator identity
is not inferred. No enemy-only owner/class claim is made.

## Offline tests

`tests/CosmeticBloodTests.cpp` includes production code with a fake D3D ABI;
link `bcrypt.lib`, compile x86 C++17 `/EHa` with core/imgui include directories.
Arguments: `mods/CosmeticEditor/blood build/blood-textures build/blood-v05-originals build/blood-v05-split-verified`.
The final two directories add pool/cap fixtures; omitting them explicitly skips
that extended fixture coverage rather than claiming those tests passed.

Checks: all five original hashes, supplied decoded grayscale/original alpha,
alpha corruption refusal, transactional configuration/path/owner/RGB refusal,
four-per-Tick warm-up, exactly-once binder call/HRESULT, unchanged native
descriptors, GPU pitch and alpha-preserving pixels, source impostor negative
cache, header/invalidation fail-closed behavior, device identity/reset/lost,
create/lock/unlock failure cleanup, owner release and reentrant destruction.
100,000 warm binds assert no heap allocation, hashing, source-byte reads or
uploads. These passed offline; live rendering/reset/FPS remains untested.
