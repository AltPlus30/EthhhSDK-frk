# Monochrome texture binding (bounded implementation)

`src/core/cosmetic_textures.{h,cpp}` supplies a CPU pack loader and a **draw-scoped**
SDK-owned texture borrower. It does not install hooks, find actors, clone models,
or write persistent mesh pointers. No game was launched/attached for validation.

**Integrated follow-up:** `ethhhpatch_runtime.cpp` now hooks the two verified
native draw passes and calls `cosmetic::DrawTextures`; v0.4 collects verified
private body/owned-head bindings on tick and draws from a guarded prepared cache.
Lua exposes explicit pack load/stop. Blue and whole-head atlases are enabled;
the mixed secondary atlas needs separate explicit user consent. All three
stay neutral so persistent private material tint applies color only once.
There is no head-mask API or material-color neutralization.
The API/recipe below records the module handoff and its safety contract.

## Integration contract: synchronous pass wrappers

1. `Load(owner, modDirectory, {bluePath, secondaryPath, headPath}, error)` loads
   three relative files transactionally. Existing ownership is preserved on
   failure. Supplied files are copied unchanged to `mods/CosmeticEditor/textures`.
2. Main must establish the exact pinned Ninja generation and independently
   verified NinjaHead ownership, and create private instances with its existing
   fingerprinted native copy recipe. Instance flag `8` alone is not an actor or
   texture ownership proof.
3. Wrap native opaque `0x44D890` and transparent `0x44C5C0` **queue-consuming
   draw passes** with `DrawPrepared(continuation, context)`. On tick call
   `PrepareBatch(bindings)` after exact actor validation and private preparation.
   Before drawing cheaply revalidate the admitted root/head generations and
   appearances; module flat guards validate instance/mesh/descriptor topology.
   Include the head with white RGB, and secondary body only when opted in.
   A `false` return means the continuation did not run: draw natively.
   A `true` return means it ran exactly once. Native faults propagate after
   `__finally` restores **every** texture field. Do not retry a faulted native pass.
4. Install these two entry hooks only after the supported executable hash gate
   and `VerifyPassHookFingerprints()` both succeed. **Do not scope substitution
   around Tick, AppRender or model/mesh queue submission.** `Draw` remains
   available for an independently verified synchronous individual draw.
5. `Release(owner)` / `Shutdown()` drop only SDK resources. There are no persistent
   native pointers to restore. Stop drawing before DLL unload; do not unload
   concurrently with an active native continuation.

For existing persistent material tints pass **`rgb = 0xFFFFFF` for every slot**.
Head requires white; body slots can still bake colors for standalone module
callers. The editor uses only neutral replacements and native private RGB.

### Exact hook ABI and code recipe (main owns runtime hooks)

| Preferred VA | Native trampoline ABI | Original entry bytes |
|---|---|---|
| `0x44D890` | `void __thiscall(void* renderer)` | `81 EC 4C 01 00 00 53 55 56 57 8B F9` |
| `0x44C5C0` | `void __thiscall(void* renderer, int pass)` | `83 EC 1C 53 56 8B F1 8B 86 B0 07 00 00` |

Use `game::A` for both addresses. Module exports typed aliases
`OpaqueDrawFn` / `TransparentDrawFn` and constants
`kOpaqueDraw` / `kTransparentDraw`. Verify prologues before MinHook modifies
either one. Both are void functions: opaque ends at `0x44DC09` with `ret`;
transparent ends at `0x44D184` with `ret 4`. Preserve the transparent argument.

```cpp
namespace tex = eth::cosmetic::textures;
tex::OpaqueDrawFn opaqueOriginal = nullptr;
tex::TransparentDrawFn transparentOriginal = nullptr;
struct PassContext { void* renderer; int pass; };
void __cdecl ContinueOpaque(void* value) {
    opaqueOriginal(static_cast<PassContext*>(value)->renderer);
}
void __cdecl ContinueTransparent(void* value) {
    auto* c = static_cast<PassContext*>(value);
    transparentOriginal(c->renderer, c->pass);
}
void __fastcall OpaqueDetour(void* renderer, void*) {
    PassContext c{renderer, 0};
    if (!ValidatePreparedActorKeys() || !tex::DrawPrepared(ContinueOpaque, &c))
        opaqueOriginal(renderer);
}
void __fastcall TransparentDetour(void* renderer, void*, int pass) {
    PassContext c{renderer, pass};
    if (!ValidatePreparedActorKeys() || !tex::DrawPrepared(ContinueTransparent, &c))
        transparentOriginal(renderer, pass);
}
```

The collector validates pinned Ninja/full actor generation, head's full owner
handle when applicable, appearance/model identity, private-clone provenance, and
current mesh membership. It records `mesh+0x10` as `nativeTexture`. Body `blue` /
`blue_double` entries can reference either blue or secondary atlas: choose slot
from descriptor dimensions (256x256 / 256x128); the module subsequently enforces
full source SHA256, so dimensions alone never authorize substitution. Keep one
RGB value per slot (normally white on both body slots). Do not include sash
textures in the costume batch.
Prepared snapshots retain addresses, never ownership. Invalidate on actor/
appearance/resource-generation changes, rebind or stop; Load/Release also
invalidate. Identical immutable source allocations are hash-cached for that
generation, not hashed per draw. All prepared topology links and descriptor/
binder keys are rechecked with guarded direct reads before borrowing.
See [performance and invalidation contract](cosmetic-texture-performance.md).

An empty batch draws natively. Duplicate meshes, differing RGB values for the
same slot, stale pointers, nonwhite head RGB or any bad asset refuse the
entire batch **before writing any native field**. One stable descriptor exists
per mesh in a pre-sized vector: no vector is resized while borrowed pointers
are installed. Shared GPU handles/source arrays are stable under the module's
recursive lock; reentrant mutation is refused. All original fields are restored
in reverse order in `__finally`, including on a continuation's SEH exception.
No mesh color is borrowed or neutralized.

This is **static experimental support, live-untested**. Main must not catch a
native pass fault and run the original a second time. Disable further overrides
and retain the existing native-fault policy instead.

## Whole headgear

The user accepts whole-head recoloring, including shared face/eye regions.
The complete supplied head image and its alpha replace the original atlas;
no authored selection or heuristic pixel ownership is required.

## Static evidence and lifetime

Offline Capstone/pefile inspection of the supported executable SHA256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`:

- `0x4541A0` copies mesh chains through the mesh virtual clone. At `0x454200`
  it tests mesh `+0x40` bit `0x80`; only when set does it call the texture's
  virtual clone (`[vtable+4]`) and replace copied mesh `+0x10`.
  Thus private mesh != universally private texture.
- `0x454150` releases a private instance's resource list through `0x4497C0`,
  then frees its allocation. Persistent SDK descriptors cannot safely be
  transferred into this ownership without a complete destructor contract.
- `0x44D890` consumes the opaque queue and traverses meshes via `+0x44`,
  dispatching their draw at `0x44DB18..0x44DB21` through `[mesh.vtable+4]`.
  The concrete ordinary mesh draw `0x454760` calls `0x4545D0`, then draws its
  primitive list synchronously (`0x453E60`); it ends with `ret 8`.
- The transparent consumer `0x44C5C0` traverses sorted mesh records at
  renderer `+0x5075C`, count `+0x5275C`, binds each mesh through `0x4545D0`
  (`0x44C72E`) and draws primitives through `0x454470`. At `0x44D14E` it follows
  mesh `+0x44`, then continues the sorted-record loop before `ret 4`.
  There is no entity tick/resource-pool teardown among the direct calls in
  either consumer. Opaque calls these renderer helpers: `0x449030`,
  `0x449300`, `0x4491D0`, plus device state/transform methods and virtual mesh
  draws. Transparent adds `0x449530`, geometry draw/material-state methods.
- `0x4545D0` reads mesh `+0x10`, then calls texture `[vtable+0]` at
  `0x4545FC..0x454605`. **The module now accepts only ordinary texture
  descriptors whose binder equals `game::A(0x455B10)`.** That concrete binder
  `0x455B10..0x455B43` sets sampler state, reads texture handle `+0xC`, and calls
  device `[vtable+0x104]` (`IDirect3DDevice9::SetTexture`) with stage 0 and the
  handle. It does not publish/store the native descriptor or its source pointer.
  D3D may retain the COM texture handle, which is independently reference-counted;
  this does not retain the SDK descriptor or source bytes.
- Direct pass callers at `0x449BB8` / `0x449BD3` invoke opaque then transparent;
  `0x44EBEC` / `0x44EBF5` do so for another scene, passing transparent `0`.
  Both are synchronous calls and return before the scene's subsequent work.
  Hooking both entry points covers those scenes without extending borrowing
  across submission or scene boundaries.
- Original serialized Ninja blue mesh at A+`0x54DD90` has texture field `+0x10`,
  texture A+`0x63740`, and mesh flags `0x89`. Do not clear ownership flags to
  conceal a persistent SDK pointer.
- Descriptor layout matches existing button-prompts code: 32 bytes, width `+4`,
  height `+8`, D3D9 handle `+0xC`, source `+0x10`, size `+0x14`.

Before every substitution the module verifies private-instance chain membership,
the expected current texture pointer, dimensions, exact original source size,
and **SHA256 of the full original TGA bytes**, using the extraction tool's hashes.
It clones descriptors in call-scoped stable storage, provides a canonical SDK-owned
source image and a separately created `D3DPOOL_MANAGED` GPU texture, and restores
the mesh in `__finally`. It never edits native GPU pixels/source/descriptors.
The no-escape/lifetime argument is conditional on the shipped single-threaded
native consumer/binder behaviour and the host's genuine private-clone proof.
Arbitrary other mods changing draw callbacks, freeing actors during render, or
installing retaining texture binders are not covered; unknown binders fail closed.

Managed textures survive ordinary D3D9 `Reset`; `hooks.cpp` Reset handling does
not require deleting managed textures. Device identity changes drop SDK handles
before rebuilding; no upload occurs while `TestCooperativeLevel` fails. New
device objects reusing the same address are not independently identified by this
module: integrate an explicit shutdown/invalidation before whole-device teardown
if such a path is enabled. Reentrant load/release/shutdown is refused during draws.

## Formats / tests

Supplied TGAs are type-10 RLE, 24-bit bottom-origin: blue 256x256 (51621 bytes),
secondary 256x128 (41817 bytes), head 128x128 (14424 bytes). Originals are
uncompressed indexed type 1. Decoder accepts both plus truecolour type 2,
normalizes origin and validates packet bounds. TGA footers may follow pixels.

`tests/CosmeticTextureRuntimeTests.cpp` includes production code and exercises
real files, transactional loading, RLE truncation/overrun/origin, all original
source hashes, whole-head checks, private-chain checks, reentrant
release refusal, stable simultaneous descriptors, duplicate/conflicting batch
refusal, transactional upload failure, managed-cache reuse, and pointer restoration
after normal/SEH batch draws. A minimal ABI-only fake D3D device/texture checks
managed creation and pitched upload; no game process or real GPU is used, so
these tests do not validate live rendering/reset.
Link `bcrypt.lib`, include `src/core` and `src/third_party/imgui`, compile Win32
with `/std:c++17 /EHa`, then run with the supplied texture directory and
`build/cosmetic-textures` as its two arguments. Main owns CMake registration.
