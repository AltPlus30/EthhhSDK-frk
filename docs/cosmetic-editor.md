# Cosmetic Editor v0.5

Experimental, **off by default**, for executable SHA-256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Source: `mods/CosmeticEditor`; staged payload: `build/dist/mods/CosmeticEditor`.
Requires the newly built `build/dist/EthhhSDK.dll`, not just the Lua mod.

## Use

1. Close the game manually. Build with `.\build.ps1 -Reconfigure`.
2. Install with `.\install.ps1 -GameDir 'E:\Games\I-Ninja'` (do not use `-Force`).
3. Enable **Cosmetic Editor** in the manager. Load a level and use Auto+ or
   Manual to pin the exact Ninja.
4. Open F9, select the mod, choose RGB and explicitly **Apply belt color**.
5. Click **Use supplied monochrome costume textures**, then choose Costume RGB
    select parts and **Apply color to selected parts**. The main blue and whole
    headgear atlases are replaced by default;
   secondary body regions need the explicit skin/leather warning option.
    Parts are scoped separately to `body` and `head`; `blue_double` and sash
    materials are hidden from this list. Belt controls cover the sash.
6. Close F9 so gameplay can tick. **Stop and restore all colors** or disabling
   the mod queues restoration on the next tick, not on the UI thread.
7. For models, **Refresh loaded model list**, choose a donor and **Apply donor
   to pinned Ninja**. For swords, explicitly select the `NinjaSword` recipient,
   then choose a built-in appearance or a loaded donor.

Under **Enemy blood**, choose RGB and **Apply blood color**; **Restore original
blood** stops that independent override. Disabling the mod stops all channels.
The same RGB also controls split interiors and the approved shared green cut
glow. **Restore original glow only** disables glow without losing other colors.
See [glow coverage, limitations and rollback](cosmetic-shared-glow.md).

Nothing is installed or launched by building. The three user-supplied edited
monochrome TGAs are included as local mod assets, unchanged from
`D:\AI\opencode\temp\ninja-monochrome`. Original exports remain separate.
Five blood monochromes from `D:\AI\opencode\temp\ininja-blood-monochrome` are also
bundled unchanged. This local use is not public asset-redistribution clearance.

## Implemented

- Arbitrary RGB belt tint, including named sash/knot and the separate moving
  head-owned ribbon. See [ribbon attribution and timing](cosmetic-wavy-sash.md).
- Named-material RGB costume tint on the selected Ninja and verified owned heads.
- Alpha-preserving, reversible edits on independently private render instances.
- Explicit Apply/Restore; no automatic writes from saved settings or mod load.
- Applied palettes/texture choices remain armed across level, respawn and
  appearance changes within this session. Old material snapshots are retired;
  fresh identities/resources are resolved before reapplication.
- If the player pin disappears, cosmetic-only discovery requires a complete
  snapshot with exactly one exact `Ninja`. Ambiguity pauses application.
  It never repins movement/HL or reuses old material pointers.
- Profile, belt progression, sword tier, damage, ranks and unlocks are untouched.
- Experimental player/prop and sword donor appearances, using the donor pose.
- Six built-in cosmetic sword appearances, separate from the real sword tier.

Material tint is a color multiplier/filter. Exact screen
RGB is not guaranteed because the original texture, vertex color and lighting
still contribute. White tint does not remove a blue texture.

## Original textures for editing

Original, attributed TGA exports are in `build/cosmetic-textures`:

| File | Purpose |
|---|---|
| `ninja-blue-original-A00063740.tga` | Main blue clothing atlas, 256×256 |
| `ninja-body-secondary-original-A00073a80.tga` | Additional body regions, 256×128; region roles not fully mapped |
| `ninja-sash-knot-white-original-A0007bdc0.tga` | Knot, 32×32 |
| `ninja-sash-white-original-A0007c230.tga` | Sash, 256×32 |
| `ninja-head-original-A001b0720.tga` | Head atlas, 128×128 |

For hue-independent clothing tint, make a **neutral/grayscale copy of the main
blue atlas**, retaining shading and dimensions. A bright neutral base provides
more tint headroom than a dark luminance conversion. Preserve UV layout and
orientation; do not indiscriminately desaturate skin/details in mixed atlases.
The sash textures already support practical tint without this step.

These are indexed, uncompressed TGAs with 24-bit BGR palettes and 8-bit pixel
indices. Keep originals untouched and supply the path to your edited copy.
`provenance.json` and `README.md` in the export folder record original hashes,
archive identity and model→mesh→texture attribution.

Re-extract into a **new** directory (existing output is always refused):

```powershell
python tools/extract-ninja-cosmetic-textures.py --game 'E:\Games\I-Ninja' --out build/cosmetic-textures-new
```

**Supplied texture binding is implemented**, scoped to synchronous opaque and
transparent native draw passes. Original source hashes, ordinary texture binder,
private instance/mesh membership and device state must match; unsupported cases
draw natively. SDK-managed textures are separate from the game's shared textures,
and descriptor pointers are restored in `__finally` after each pass. No LCS
archive rebuilding or persistent SDK pointer insertion is performed.
See [texture ownership and lifetime](cosmetic-texture-binding.md).

### Whole headgear and parts

Exact `NinjaHead` actors now use their verified full owner handle at `+0x238`;
foreign/stale heads are refused. Their two sash entries follow the belt group.
Select `head: head` in the ordinary parts list to recolor the entire head part.
The supplied `ninja-head-original-A001b0720.tga` is applied with the costume
texture pack; no mask image or separate head controls are required. This can
also affect face/eyes sharing that part, as accepted for this editor.
The texture is not pre-tinted: native private material RGB supplies color once.

Draw passes use tick-prepared bindings and flat guarded identity/topology checks.
v0.5 limits full inspection to 150 ms intervals, while native color reassertion,
fresh guarded lifetime checks and source-byte proof still run every tick.
Previously admitted originals use exact fresh byte comparison, not repeated SHA.
The GUI polls readouts at 5 Hz and leaves experimental model browsing collapsed.
Render-pass counts are not upload counts; the UI now shows both separately.
See [current measurements and safety contract](cosmetic-performance-v05.md).
No game ticks are reduced. Actual in-game FPS improvement remains unmeasured.

### Enemy blood

Green is baked into the four splatter TGAs and the pool atlas, not just a material
tint. Five positively attributed original TGAs are in `build/blood-textures`:

- `blood-splatter-0-original-A001e0060.tga`
- `blood-splatter-1-original-A001dfc20.tga`
- `blood-splatter-2-original-A001df7e0.tga`
- `blood-splatter-3-original-A001df3a0.tga`
- `green-blood-pool-original-A001c7de0.tga`

The first four are 32×32; the pool is 64×64. All five supplied monochromes have
been verified against original dimensions and exact decoded alpha. The picker
now arms managed RGB-baked textures through the verified ordinary binder.
v0.5 also admits two previously missing, SHA-verified Ranx pool variants,
and the separate split-body anatomical atlas. These whole images are neutralized
on cold admission while retaining exact alpha; mixed armor/skin atlases remain
untouched. Interior shading/detail brightness remains, but its whole hue follows
Blood RGB.
Cold effects remain native until a post-tick verification; native descriptors
are never modified. Eight baseline/pool/interior resources are covered, not every possible
enemy effect. Identical shared resources receive the same color regardless of
creator. See [blood implementation and limitations](cosmetic-blood-runtime.md).

Re-extract into a new directory with `python tools/extract-blood-textures.py --out build/blood-textures-new`.
[Evidence and attribution](cosmetic-head-blood-research.md).

## Ownership and restoration

At the post-native-tick safe point the SDK validates the exact actor's live
registry/class/handle identity. It enumerates bounded named entries through both
render-instance lookup tables and makes non-private instances private using the
same `0x454240`/`0x454150` copy/release recipe as native tint preparation.
Each slot's `+0x34 & 8` is checked independently: the shipped `0x457BF0` helper
checks the primary slot's flag even when processing the second slot.

Only private entries' RGB at `+0x14` is changed; alpha is preserved. Unchanged
colors are not rewritten, and instances are not cloned every frame. Native
script recolors become the new restoration baseline. Removing one override
restores that part while retaining other overrides.

Restoration requires the same live actor and matching instance/lookup/entry/name
identity. It never writes freed/rebound entries or overwrites a subsequent native
color change. Destroyed/rebound resources need no restoration. New instance clones
remain engine-owned until ordinary model teardown; Stop does not free instances.
Process teardown clears caches without dereferencing game heap pointers from an
arbitrary shutdown thread.

## Model and sword swapping

**Current live report:** both swap controls appear ineffective. This update
investigates rather than repairs them, as requested. A confirmed UI bug treats
zero-based SDK combo indices as one-based Lua indices; donor-before-recipient
ordering is another likely no-op cause. Earlier synthetic Lua mocks mirrored
the wrong convention, so their pass did not establish working selection.
See [prioritized causes and fixes](cosmetic-model-noop-investigation.md).

**Loaded donor appearances:** choose a currently loaded actor/prop. It must
naturally submit its unmodified model **before** the recipient in the same
primary render scene, and remain visible. Otherwise the game keeps native
rendering. Counters show captures, substitutions, waiting and refusals.
Scenery baked into level geometry is not a selectable strat.

The donor's copied pose follows the Ninja's world transform; it is **not Ninja
animation retargeting**. The donor itself is not hidden. The Ninja's gameplay
skeleton, controller, collision and equipment stay native. Its separate head,
attachments and effects may remain visible alongside a replacement body.
Custom/auxiliary render modes, invalid meshes and larger mesh-chain workloads
are refused; larger visual bounds can still clip/cull. This is a guarded
experimental loaded-model browser, not guaranteed compatibility with every
character/prop. A Ranx character/weapon can be tried when it appears as a
selectable loaded actor; no dedicated Ranx weapon catalog is claimed.

**Built-in swords:** six appearances resolve from the live fingerprinted Ninja
script and use the actual sword binding native (`0/0x29`). No profile tier,
global selector, damage/scale/effect script state or animation controller is
changed. Gameplay/culling bounds are retained. If immediate application is
ineligible (for example a noncanonical/private current instance), the override
waits for the next normal native binding: attack once.

Select `NinjaSword` explicitly: its automatic owner relationship to the Ninja
has not been verified. **Stop all model / sword overrides** before changing
sword recipients. Stop attempts a safe rebind to the current real tier; if that
cannot be done, the existing mesh restores on the next normal game refresh.
Disabling the mod releases both model channels and the built-in override.
No class-wide refresh is broadcast.

Donor lifetime is restricted to an already pending native draw, not an independent
resource pin. A verified first-flush fence prevents using donors in the game's
second scene, and all captured resource pointers are dropped by frame end.
Unrelated mods/threads that destroy pending draw resources remain unsupported.
This is a conditional static lifetime argument, **not runtime safety acceptance**.
See [native evidence and integration](cosmetic-model-swap-research.md).

## Verification

Build and isolated native/Lua tests pass. These exercise queue-only UI behavior,
both render slots, private-instance gates, RGB packing, alpha preservation,
native refresh, partial/all restoration and actor-generation refusal.
Additional model/host tests cover six sword descriptors and binding arguments,
unchanged profile/skeleton, real-tier bounds/restore, exact generations,
thread affinity, donor ordering, synchronous matrix copies and the flush fence.
New head/tint tests cover full-owner generations, grouped body/head sashes,
foreign-head refusal, cosmetic-only unique-Ninja reacquisition and level gaps.
Texture tests use fake D3D interfaces to verify RLE decoding, original-source
hashes, managed uploads, pass-scoped descriptor restoration (including SEH),
whole-head application and no double application of material RGB. Warm prepared
draws assert zero hashing, query-backed reads and heap allocations. Blood tests
cover 100,000 warmed binds, alpha preservation, in-place resource reuse, resets
and failure cleanup. Ribbon tests cover full owner/effect generations and restore.
They **do not establish in-game appearance or native allocation success**.
No game launch, process attachment or installation was performed.

The broader `tests/run-tests.ps1` passes its native/managed/Lua regressions,
then stops at the pre-existing static EthhhPatch fixture's hard-coded
`C:\Users\Administrator\Desktop\ininja\I-Ninja.exe` path. Pointing that isolated
fixture at the installed game instead reveals its `general.vol` fingerprint
mismatch; no fixture/archive was modified to bypass it. Existing notice tests
also expect nine staged notices and an older Valve-license hash, unlike this
checkout. These unrelated failures were not rewritten as part of the mod.

Live checks still required: belt hue and isolation from NPCs; costume selection;
attack-driven refresh; normal/alternate forms; respawn/level transition;
enable/disable; Stop followed by one unpaused tick; device reset.
Also check real donor/recipient ordering, separate head/attachments, six sword
appearances and attack/restore behavior. Strict live descriptor/mode guards can
refuse cases that were accepted by the synthetic fixtures; do not weaken them
without investigating the actual game state.
