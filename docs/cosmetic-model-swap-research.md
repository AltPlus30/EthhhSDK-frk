# Experimental cosmetic model swapping

2026-11-20. Implemented only `src/core/cosmetic_models.h/.cpp`; no existing
integration, Lua binding, UI or tests changed. No game launch, attach or install.
This is static research and an **unintegrated, not runtime-validated prototype**.

**Integration follow-up:** now connected to `ethhhpatch_runtime.cpp` through
shared AppRender/EntitySubmit/NodeSubmit hooks plus a verified flush fence.
`cosmetic_model_host.cpp` marshals Lua requests to post-tick work and owns the
sword native hook; `mods/CosmeticEditor` supplies explicit controls.
Win32 synthetic model/host and Lua UI tests cover these paths. The dated
research/integration recipe below describes the original handoff, not a claim
that current code remains unintegrated. **No live acceptance was performed.**
Inactive donor submission now has an atomic-only fast path; captured addresses
are cleared at frame end. Bounds restoration is a protected-by-caller data
store, not a page-protection/code patch.

## Outcome

- **Loaded donors:** guarded render-only submission substitution. Only a donor
  already naturally queued earlier in the same primary scene is eligible.
  Its pose is copied; its geometry/resources are borrowed under the native
  pending-draw lifetime contract. Invisible/later donors are skipped.
- **Six built-in swords:** separate native binding interception, using live
  descriptors resolved from the fingerprinted Ninja selector. No profile tier,
  shared selector literals or global 97 is modified. Includes guarded immediate
  application when the current sword is a canonical built-in instance.
- Player and sword donor selections are independent. The exact sword recipient
  is explicitly selected and registry-validated; no NinjaSword owner field is
  assumed. All selections identify address/class/handle generation.
- No animation retargeting, imported models, cross-tick resource retention,
  scratch strat, guessed resource loader or native clone lifecycle.

## Artifact / provenance

Target: `E:\Games\I-Ninja\I-Ninja.exe`, SHA-256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Native addresses below are preferred-image VAs and must be rebased.
`A+...` denotes decompressed `9-95.LCS` A offsets, NEVER process addresses.

`Data/09.vol` SHA-256:
`0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669`;
member SHA-256:
`390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65`;
A SHA-256:
`68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd`.

New native REA work used Ghidra 12.1.4 against the admitted executable. REA was
inspected first: no open target; this investigation then owned the native
session. Pseudocode is derived static evidence, not original source/runtime
observation. Tiny wrappers missing Ghidra function bodies were decoded offline
with existing pefile/Capstone 5.0.7 through
`python -B tools/inspect-cosmetic-research.py --native VA SIZE`.
Packed observations used that inspector and bounded in-memory Python searches;
no full new script CFG decode is claimed.

Complete retained REA records were exported to
`C:\Users\Akira\AppData\Local\Temp\cosmetic-model-swap-evidence-2026-11-20.json`
(336,848 bytes, 11 records), and the owned REA target was closed. That bundle
includes explicit missing-function results for tiny wrappers; it does not
convert those failures or indirect-call limitations into positive observations.

## Frame interval: positive and negative evidence

| Boundary | Static observation |
|---|---|
| `0x43D8F0` | Main-loop message pump, timer, zero/multiple native ticks, then synchronous `0x43E170`; message dispatch and those tick calls are outside that render invocation. |
| `0x43E170` | Increments frame count, handles device state, initializes via `0x449E00`, submits several render paths, then calls `0x44A340`. |
| `0x449E00` | Resets primary/transparency queue state through `0x44C580/0x44D870`, then updates View matrices through `0x43F290`. |
| `0x406C70`, `0x40E650` | Submission traversal invokes virtual render callbacks. This is NOT evidence that all indirect callbacks are harmless or that all possible rebind/delete sites have been exhausted. |
| `0x44A490` | Stores owner and instance pointers in queue records. Matrix submissions call `0x449EB0`; second mesh list creates additional transparency records. |
| `0x449EB0` | Iterates mesh `+0x44`; computes/copies 64-byte matrices into renderer-owned queue storage. It does not retain SDK node-array pointers. |
| `0x44A340` | Prepares queued geometry (`0x44A010`), sorts and consumes it through draw paths, including `0x4499C0`. |
| `0x44D890` | Opaque path reads queued owner/instance/matrix references and invokes mesh draw methods. |
| `0x44C5C0` | Transparency path reads queued mesh/matrix/owner data and invokes mesh draw methods. |
| `0x44EB80` | Later in the SAME AppRender resets queues and submits/draws a SECOND scene. An AppRender-wide donor cache without a flush fence is insufficient. |
| `0x454150` | Releases an owned instance immediately; no observed frame-marker lifetime pin. |

REA records:

- Main loop and opaque/transparency consumers:
  `ev_2046b695d993cf140a87f1b33263f69427b408bc2a651f93fd8e0e219d4c69b6`.
- AppRender/View/queue initialization:
  `ev_9b02f4c69e1ecacbd05d2519f78f025f476d9857d056910ee7130114aa9dc348`.
- Submission traversals and flush:
  `ev_be50c918e1698de81185acaffd0de82a4dc3c342eb5cf9a2acecfd3d8186b033`.
- Draw dispatch and second-scene reset:
  `ev_31626eeeb56ea96899e99446ebbbddcba07ee011bbe899fa1cd4f22ac23e3c27`.
- Resolved AppRender caller:
  `ev_9253c7dc0ee8ec23b1a78682569ffa4dbd30286507f7166f4b7e9ec73c902777`;
  resolved caller list is not an exclusion of unknown indirect callers.

**Bounded conclusion:** the main-loop tick/render separation is established.
A universal absence of deletion/streaming through every indirect callback or
other thread is NOT proved. The implementation narrows the lifetime claim:
it captures only AFTER the engine actually queues the selected donor's
unmodified instance. It verifies that exact queue record remains pending and
that the donor's registry/storage still match before substituting. Its borrowed
meshes already have to survive the primary scene's native donor draw.
The first flush fence disables all further capture/substitution before the
second scene. Nothing is carried into the next frame.

This inherits the native scene resource lifetime; it does not create an
independent pin. Mods/threads that delete, rebind, mutate geometry or unload a
pool during pending draws are unsupported. Such actions already jeopardize the
native donor queue. Runtime thread identity is enforced by the module, but
static main-thread sequencing is not proof that no resource worker exists.

## Exact donor integration

Native ABIs:

```cpp
// 0x44A490, ret 12
void __thiscall EntitySubmit(void* renderer, void* owner, void* instance,
                            const Matrix* world);
// 0x449EB0, ret 12
void __thiscall NodeSubmit(void* renderer, void* mesh, const Matrix* world,
                          const Matrix* nodes);
// 0x44A340, ECX=renderer, no stack arguments
void __thiscall Flush(void* renderer);
```

Use `eth::cosmetic::models` (abbreviated `cm` below). All changes below are
instructions for the integrator; they were NOT applied to existing files.

1. Include `cosmetic_models.h` in `ethhhpatch_runtime.cpp`.
2. Rename the old `EntitySubmitDetour` BODY into a plain cdecl continuation:

   ```cpp
   static void EntitySubmitBody(void* self, void* owner, void* instance,
                                const entity::Matrix* world) {
       // Existing EntitySubmitDetour body, unchanged.
   }
   void __fastcall EntitySubmitDetour(void* self, void*, void* owner,
                                     void* instance, const entity::Matrix* world) {
       cm::SubmitEntity(self, owner, instance, world,
                        &EntitySubmitBody, g_entitySubmitOrig);
   }
   ```

   This wrapper must surround EVERY entry, including interpolation-disabled,
   alpha==1, overlay-open, budget fallback and nested calls. Do not put it inside
   the interpolation acceptance branch. Cosmetic submissions bypass recipient
   interpolation; ordinary submissions still use the unchanged continuation.

3. At the FIRST line of existing `NodeSubmitDetour`:

   ```cpp
   if (cm::SubmitNodes(self, mesh, world, nodes, g_nodeSubmitOrig)) return;
   ```

4. Install a verified additional fence hook on `0x44A340`:

   ```cpp
   using FlushFn = void(__thiscall*)(void*);
   FlushFn flushOriginal = nullptr;
   void __fastcall FlushDetour(void* self, void*) {
       cm::BeforeFlush(self);
       flushOriginal(self);
   }
   ```

   Use the same executable SHA/address/prologue verification policy as existing
   hooks. If fence installation fails, pass false to BeginFrame: donors remain
   disabled. Do not install duplicate EntitySubmit/NodeSubmit hooks.

5. In the OUTERMOST `AppRenderDetour`, after its nested-render early return but
   immediately before calling the original:

   ```cpp
   const bool cosmeticFrame = cm::BeginFrame(game::Renderer(), flushHookReady);
   __try { if (g_appRenderOrig) g_appRenderOrig(self); }
   __finally {
       if (cosmeticFrame) cm::EndFrame();
       // Existing camera/entity/inventory/frame cleanup remains here.
   }
   ```

   BeginFrame is BEFORE native frame-count increment; the module expects +1.
   Nested invocations must not begin/end the outer frame. Never call EndFrame
   just after submission or before deferred drawing.

Select from the engine tick thread, outside rendering:

```cpp
cm::Actor ninja, sword, bodyDonor, bladeDonor;
// CaptureActor(address, actor) validates each explicitly chosen live actor.
cm::Select(cm::Channel::Player, ninja, bodyDonor);
cm::Select(cm::Channel::Sword, sword, bladeDonor);
```

Both owners must have `+0x144==0`. Both mesh chains are bounded/index-checked.
Donors with more mesh links than the native recipient are refused: substitution
must not consume extra matrix slots needed by later native submissions. Current
matrix/queue capacity is also checked before overriding.
Indexed skeletons use existing SDK model/controller/allocation checks. Static
world-only models can have zero nodes; no fake skeleton is synthesized. Frozen
donor matrices are copied separately from recipient world transformation.
Current/last-frame `Stats()` reports captures, substitutions, waiting and refusals.
No render-only geometry is independently cloned, released or mutated.

## Six swords: VM alternative and native outcome

### Important correction to earlier cosmetic research

`A+0x4B86AC` is `0x43747061`. The `0x70` addressing selector does NOT use
native getter/setter case 0x61 on an actor. Offline VM table dispatch gives:

- Read selector `0x70`: jump-table byte at `0x4267A8+0x60` selects `0x425404`,
  which reads `[0x5111A0 + index*4]`.
- Write selector `0x70`: byte at `0x4268C0+0x60` selects `0x425612`,
  which writes that same global array.
- Therefore global 97 is **`0x511324`**, rebased, NOT `Ninja+0x2C0`.
- Native getter `0x421680` case 0x61 really does read receiver+0x2C0, but
  belongs to a DIFFERENT VM selector. The old getter evidence alone did not
  establish the sword instruction's receiver.

VM decompilation:
`ev_be30a226bce7f088d32d44165e378f898922efa022cad840df79e18febbb71c9`.
Native getter/setter comparison:
`ev_7e635be7753ee2ba4da4356b9cb095b2e44af388b9ded5f8e488dee92fd8fee1`.

The bounded search over `A+0x4B5F20..0x4D15C0` found six writes with selector
0x70/property0x61 in the selector and two matching reads in NinjaSword at
`A+0x4C3578` and `A+0x4C35A0`. Other property0x61 reads with selector0x10 exist:
they are not evidence of consumers of global97. This is a bounded word search,
not a proof that global97 has no other indirect consumers elsewhere.

Patching six selector literals would leave real profile tier unchanged, but
would affect SHARED global97; exact-Ninja VM scoping does not make that global
actor-local. The module deliberately avoids that alternative.

### Native binding route

At `A+0x4C35A0`, `0x422C7061` reads global97; `A+0x4C35A4` stages the argument;
`A+0x4C35A8` calls native **0/0x29**. Table entry resolves to:

```cpp
// VA 0x416F20, plain ret, cdecl
void Bind(void* strat, const uint32_t* args); // args[0]=descriptor
// wrapper calls thiscall 0x410C40(strat, descriptor), ret 4
```

`0x410C40` tears down render slots, may unregister/free `+0x2FC`, binds
descriptor[0] through `0x43E5D0`, and updates render flags. It does not write
profile tier, skeleton `+0x268` or animation controller `+0x26C`.
`0x4129E0` is a DIFFERENT path that additionally sets up descriptor[1] through
`0x412170`; the sword helper's observed native is 0/0x29, not that path.

REA:
`ev_7fd658c69f3c1dae9b550a33f29d55c28b032021a28e779e41e1b29a92b4ef44`.
Auxiliary registration/removal:
`ev_3faa67471988d83ced790154d098bca8f96aeff483c014cbfe9cd1aa7d2c755e`.
Tiny wrapper bytes were decoded offline because Ghidra had no function there.

The six descriptor literal operands are:

| Tier | Operand A offset | Serialized descriptor A offset |
|---:|---:|---:|
| 0 | `0x4B86A8` | `0x4B3CC0` |
| 1 | `0x4B86B8` | `0x4B3CD0` |
| 2 | `0x4B86C8` | `0x4B3CE0` |
| 3 | `0x4B86D8` | `0x4B3CF0` |
| 4 | `0x4B86E8` | `0x4B3D00` |
| 5 | `0x4B86F8` | `0x4B3D10` |

Resolution uses the live exact Ninja's class+0xC code pointer, validates entry,
code-header and selector instructions, linked native getter, each literal/set
pair, descriptor shape, and the expected loader-relocated contiguous A-pool
relation. If the loader representation differs, resolution FAILS CLOSED.
No serialized offset is called/dereferenced as an assumed process address.
Native linker evidence:
`ev_5d17b125994919ec38d59b4b99877a63e2283991e0af29e0d34fee7f2d02b210`.

Module API / integration:

```cpp
cm::SelectBuiltinSword(exactResourceNinja, exactExplicitSword, tier0to5);
// Hook native 0/0x29 only after SHA/prologue verification:
void __cdecl SwordBindDetour(void* strat, const uint32_t* args) {
    if (cm::BindBuiltinSword(strat, args, swordBindOriginal)) return;
    swordBindOriginal(strat, args);
}
// Optional immediate refresh at an engine tick safe point:
bool applied = cm::ApplyBuiltinSword(swordBindOriginal);
```

Binding overrides only normal requests whose incoming descriptor matches one of
the six resolved originals. It refuses nonzero mode or `+0x2FC`, avoiding
destruction of auxiliary physics state. Render-flag inputs must match. Mesh
indices must fit the sword's existing node stream. The native binder copies
model bounds into owner+0xD0..0xDC; the module restores the REAL requested tier's
bounds after interception, or preserves current bounds for immediate apply.
The real tier's scale/color/effect logic remains untouched.

Immediate apply additionally requires the CURRENT instance to equal a
canonical built-in model. It can refuse after tint COW. The interception still
works at the next normal game refresh. A successful rebind invalidates tint
entry caches and loses private tint instances; the tint module must notice and
reapply on its normal engine-thread path. No class-wide refresh is broadcast.

ClearBuiltinSword stops future interception but does not itself restore the
existing mesh: select the actual tier and apply before clearing when eligible,
or let the next ordinary refresh restore it. Do not change profile tier.
Builtin selection and donor-sword selection are independent; enable only one
for a given intended UI operation to avoid an override hiding the other.

## Integration / acceptance limits

- No Lua or exported C ABI was added. Only the C++ signatures in the new header
  are shipped. Existing tint/UI APIs are untouched.
- UI must marshal requests to the engine thread. First API use binds module
  thread affinity; do not initialize it accidentally from the UI thread.
- No native shader/texture creation or arbitrary asset loading is supplied.
- Generic donor poses are not retargeted to Ninja animation. Equipment remains
  in its native actor attachment path. Source material sharing is not isolation.
- Source virtual draw behavior/custom deformers have not been exhaustively
  classified. Nonzero mode is refused, but this is not a universal custom-mesh
  support claim.
- Existing gameplay bounds remain in effect: larger costumes may clip/cull.
- Strict descriptor fingerprints may reject a runtime representation not seen
  in static evidence. That is a refusal, not justification to weaken checks.
- No production build or live acceptance was performed in this pass. Integration,
  tests, actual source/recipient ordering, transitions, hidden donors, tint COW,
  sword attacks, visual correctness and performance remain the integrator's
  acceptance work. No installed files were changed.
