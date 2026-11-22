# Blood v0.5 coverage investigation

**Runtime follow-up:** the formerly blocked vertex-stream component is now
implemented and tested in `cosmetic_glow.cpp`, under the user's later explicit
shared-asset permission (not the initially proposed live-ancestor policy).
See [current glow implementation and rollback](cosmetic-shared-glow.md).
The bounded research/history below is retained; its earlier “blocked” status
does not describe the new runtime module.

2026-11-21; **static only**, no game launch, attach, install or archive edits.
Existing `cosmetic_blood.{cpp,h}` and other agents' files were not edited.

## Actionable findings

1. Enemy pools have **three original textures**, not just the sword-pool image.
   Two hashes are absent from the old five-resource allowlist.
2. A verified **animated/multistage texture binder bypasses `455B10`** and calls
   D3D9 SetTexture directly after resolving each child texture's GPU handle.
3. A second, tiny direct binder exists at `455CF0`; **its use by blood is not
   established**. Do not treat its existence as proof that ordinary pools use it.
4. Follow-up below isolates the **central split-interior atlas** and the
   **split-plane glow actor/model chain**. The generic animated-parent hook
   alone does not establish actor ownership when texture descriptors are shared.
   No global green replacement or unverified vertex-color hook was implemented.

## Identity and coordinates

Executable `E:\Games\I-Ninja\I-Ninja.exe` SHA256:
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Archive `Data/09.vol`:
`0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669`.

The new resource observations below use member **`9-99.LCS`**, SHA256
`46e16e5ad34f9469edf4ecbbbb075adb3edec3e7547bea12ba4ac104ff212eb2`;
decompressed A SHA256
`fbd20b892396036df3b52756e3c4df6a2139b149e86b047550ca90144907e8de`.
A+ offsets are file/pool coordinates, **never executable VAs**. Native addresses
below are preferred-image VAs and require relocation.

This member includes `FrontEndRanx`; its enemy helper/resource attribution is
real, but does **not** establish which level variant produced a user's frame.
The search of other core members was bounded; no all-level inventory is claimed.

## Exact pool creation/resource links

`RaxHitGreenGooGen` class A+`1EB3B0`: name A+`1CCB20`,
entry A+`1E8AF4`, code A+`1E8954`.
`RaxHitGreenGoo` class A+`1EB3DC`: name A+`1CCB32`,
entry A+`1E8BCC`, same code base.

Generator entry stages child `RaxHitGreenGoo` at A+`1E8B30..1E8B44`
(native bank 0/2). Goo entry chooses three model descriptors at A+`1E8BF8`,
`1E8C0C`, `1E8C20`, each consumed by native bank 0/`29`. Their exact model name
is `pool of green b`; **primary list model+0 is null, second list model+4 is
non-null**:

| Model descriptor A+ | Model A+ | Second mesh A+ | Texture A+ |
|---|---|---|---|
| `1CC630` | `173440` | `237D70` | `D25C0` |
| `1CC640` | `1733F0` | `236D40` | `D1180` |
| `1CC650` | `1733A0` | `236EB0` | `CFD40` |

Mesh texture is +`10`, material RGB/alpha +`14` is `FFFFFFFF` in these
originals. All textures are ordinary serialized marker A+`54BC30`, 64x64,
source at descriptor+32, size 5138, flags 1, reserved 0.

| Texture A+ | Full original TGA SHA256 |
|---|---|
| `D25C0` | `e64c12bd4c83d000d8812401834835a413c854769201f95376880d5d035e2d2c` |
| `D1180` | `faa3da9594ae2e9d503b78c9a9163b279ef6e7e1494a90387e919a4006d2f9dd` |
| `CFD40` | `c671fe748913aa55a3134305c5ba42ff784dbe948692bd0871b15660913b6a96` |

Only the middle texture is identical to the previously exported sword pool.
These differences alone justify extending **positively attributed asset
coverage**, not globally accepting all green 64x64 textures.

## Animated effect and exact bypass

Shared code A+`1E895C..1E8968` stages descriptor A+`1CC620` via bank 0/`29`.
It resolves model A+`173580`, exact name `fx_enemydeath`, primary null,
second mesh A+`236370`, texture A+`128070`. The serialized texture is:

```
+00 marker=54BAFC  +04 width=128  +08 height=64
+0C stageCount=2   +10 stageRecords=128090
```

Two **0x40-byte records**, child texture pointer at record+0:

| Record A+ | Child texture A+ | Dimensions | Source bytes | SHA256 |
|---|---|---|---|---|
| `128090` | `E6BE0` | 128x32 | 5138 | `e359a924c08bb7ef85ced8382222f33e6bfdae89119e1a389adce606c1aaa0d7` |
| `1280D0` | `E5B70` | 64x64 | 4162 | `96a43be4f9edc74c34998ffa3dbde539eea4618986a94393a4e66a481302a11e` |

These child textures have ordinary descriptor layouts. However, **their ordinary
bind methods need not run**: the parent retrieves GPU handles via virtual +`18`.
Asset usage outside this effect is not excluded; export is not authorization
to replace a shared enemy-death/ring texture globally.

Native **`455D10`** is the animated binder, present in table VA `4C99D8`.
ABI: `void __thiscall(void* texture)`, no stack arguments. Offline decode:

```
455D10 55 8B EC 83 E4 F0 81 EC B4 01 00 00 53 56 57 89 4C 24 3C
...
456193 mov ecx,[esi]       ; stage record's child descriptor
456199 mov edx,[ecx]       ; child vtable
45619D call [edx+18]       ; returns pointer to native GPU handle
4561A0 mov edx,[eax]
4561A4 push edx            ; texture COM handle
4561A5 push ebx            ; stage index
4561A6 push edi            ; device
4561A7 call [ecx+104]      ; IDirect3DDevice9::SetTexture
...
4561D4 add edx,40          ; next stage record
4561E9 ret
```

Native count +`C`, array +`10` and 0x40 stride corroborate packed resource
layout. Shader/transform/texture-stage state is also set; do not model this as
a single ordinary texture or replace the parent descriptor with a 32-byte TGA
descriptor. A native SetTexture interception sees this path; the old ordinary
binder hook does not. Attributed child descriptor/handle admission is still
required before replacement.

### Other native paths and source fields

- Mesh setup `4545D0` reads mesh+`10` and dispatches texture virtual slot 0.
  Ordinary opaque mesh draw `454760` calls it before drawing primitives.
- Transparent world consumer `44C5C0` also calls `4545D0` for each mesh before
  drawing. **No separate pool-specific direct SetTexture bypass was proved.**
  The multiple pool hashes are a concrete alternative explanation.
- Ordinary uploader `455B50` passes descriptor+`10` / +`14` to texture creation
  and passes address descriptor+`C` for the output GPU handle. It does not
  explicitly normalize those source fields in its body. Its destructor
  `455B90` releases/zeros +`C`. This is **not proof of whole-resource-lifetime
  source immutability**; continue guarded source/topology revalidation.
- Tiny binder `455CF0` uses only this+`C`, stage 0; constructor `455CC0`
  installs table `4C9BAC`. ABI no-argument thiscall, `ret`. Full bytes:
  `A1 00 55 52 00 8B 49 0C 8B 90 B0 07 00 00 8B 02 8B 10 51 6A 00 50 FF 92 04 01 00 00 C3`.
  Normalize its absolute `525500` operand if fingerprinting. Blood use unknown.

## Initial-pass blockers (superseded where explicitly resolved below)

The `split_Rax` model at A+`167680` has primary mesh A+`232650`, secondary
A+`237600`, five model nodes. Its primary chain includes ordinary atlases
A+`B0CE0`, `C3540`, `C8BC0`, animated descriptor A+`127EA0`, and another
serialized descriptor kind A+`127A60` (marker `54BAEC`). Material colors on
the examined chain are white. **No exact named inner-surface material or UV
isolation was established**. Recoloring every atlas here could change skin,
armor, eyes or unrelated shared artwork. Do not enable an interior patch yet.

World particle emitter `42C3B0` is distinct from GUI-sprite manager `519A60`.
Its static body copies emitter+`74..77` into newly allocated particle+`8..B`,
optionally applies per-channel random deltas from emitter+`78..84`, and copies
emitter+`88..8B` into particle+`6C..6F`. It also records source strat pointer
and full handle at particle+`64/+68`. This establishes byte flow, **not**
channel/blend ownership or an approved packed-RGB override. The native draw
consumer, fade evolution and exact blood-only emitter caller still require
verification. Do not rewrite all green emitters or scan the particle pool.

The `fx_enemydeath` path is a useful glow candidate, not a positive match to
every user's cut/delimb glow. Bank 0/`11A` in the shared script resolves to
`4212B0` and calls `438C70` after private-model preparation; it must not be
advertised as an RGB setter just from its floating arguments. Bank 0/`17F`
also manipulates a model subrecord. No vertex-alpha-preserving RGB mutation
was implemented without complete consumer attribution.

## Deliverables and integration boundary

- `src/core/cosmetic_blood_fx.{h,cpp}`: read-only, fixed-size, transactional
  `CollectAnimated`. Verifies the parent's exact native binder and ordinary
  child binders, reports at most eight child descriptor/GPU/source tuples.
  Unknown nested descriptor kinds fail closed. No hooks or writes.
- Use an exact `455D10` entry hook only to capture/enqueue the parent pointer;
  inspect bounded candidates on >=250ms discovery ticks, not per draw.
  The main agent owns hooks and cached GPU-handle substitution. Revalidate
  identity/source after native ticks and discard stale pointers on teardown.
  Descriptor schema alone is not blood attribution; require source hashes and,
  for shared art, an independently established draw/effect scope.
- `tests/CosmeticBloodFxTests.cpp`: real module compiled/run with MSVC Win32
  `/std:c++17 /EHsc /W4`; passed. Covers two stages, handles, transactional
  rejection, count cap, unknown child binder, null stages and unreadable arrays.
  No test/CMake files belonging to another agent were edited.
- `tools/inspect-blood-fx-v05.py`: read-only packed/native queries plus
  fingerprinted original-only export. Successfully exported five TGAs and
  provenance to `build/blood-v05-originals/`. No mod assets were changed.
  Reproduce with `--export <NEW-directory>`; output must not exist or be in
  the game tree. No automatic grayscale/hue conversion is provided.

### REA static Evidence

| Observation | Evidence |
|---|---|
| mesh setup, emitter entry, sprite constructor | `ev_9bcb6977ca428a0f1b20b22568df98706be8524def66d23bce76723f76846cc0` |
| animated binder decompilation (plus explicit missing tiny binder) | `ev_5e0beae8a62c6e3c9637eb3d0e8e5b0209bb7adebdabda15fc47cd674f988b48` |
| ordinary uploader/destructor and direct-binder constructor | `ev_e639d62c45801490493e4b2679b1e7ae3ad73ff93f33d63d63f1c50cb35b22f6` |
| world transparent/opaque consumers and animated binder | `ev_79d438537c729de2406d7d669ad08bacc6607a1b308634c5588e2e0fa232c30f` |
| transparent consumer and ordinary mesh draw | `ev_fb3eb95ae2b4ef045da0803cb6b264cb80267597e9d6dac2fcdb927bb5338acb` |
| world emitter byte flow | `ev_6c9601a11b1c8a11e847cc740fadf051690e6fc4b65cb81ff1feda542a90afb0` |

Ghidra 12.1.4 derived pseudocode is not original source or runtime observation.
Tiny functions missing from Ghidra were decoded from admitted executable bytes
with Capstone 5.0.7; no successful decompilation is claimed for missing wrappers.
Packed resource observations and fingerprints are separately reproducible
offline, not REA runtime Evidence.

Complete retained Evidence exported to
`C:\Users\Akira\AppData\Local\Temp\opencode\cosmetic-blood-v05-evidence-2026-11-21.json`
(1,089,162 bytes, 10 records). The owned static session was then closed.
The export's zero ledger unknowns does not erase the explicit coverage blockers.

## Focused follow-up: gameplay Ranx, flying blood, split interior and glow

No live work. Scope was limited to the existing frontend helper plus gameplay
`10.vol/10-01.LCS` and selected native render/emission functions.

Gameplay provenance:

- `10.vol` SHA256:
  `d5b3fe796b9b68d45f144553ca4d94df6765c3bcfca198cd6efa042eb9f43ab1`
- `10-01.LCS` SHA256:
  `d19e0cbef4d3f1ac24834704229ee6c714710dac244ceb7bc67af5fb65ff8b5b`
- Decompressed A SHA256:
  `7560654f9db15d5fcf44995162e6f04eb1d3362dd6581b873d1200d65b8f3372`

### User-request checklist

| Request | Shipped-artifact result | Implementation/live status |
|---|---|---|
| Pools | Three attributed variants; two additional hashes | Main reports seven-resource ordinary module/tests complete |
| Flying blood only partly red | `RaxBodyBit` emits the existing four splatter textures with white RGB; ordinary binding path proved | Main cache fixes cover this traced path; no live proof that every reported green particle is this path |
| Split Ranx insides | Separate central anatomical cap atlas isolated by mesh, geometry and UVs in frontend and gameplay | Exact source hash available for main's ordinary image replacement; no mixed body atlas rewrite |
| Green split glow | Actor/model chain and two art sources isolated, but baked green vertex diffuse also proved | Getter APIs are infrastructure only; complete recolor BLOCKED on private GPU vertex-stream/shader handling |
| Every delimb/damage glow / every level | Not proved | Do not claim universal coverage from one gameplay member |

### Flying blood: RGB and exact ordinary binder path

In `9-99.LCS`, `RaxBodyBit` entry A+`1E85A8` sets emission texture through bank
0/`BA` at A+`1E8708`, `1E87B0`, `1E87CC`, `1E87E8` to descriptors:

```
E8CE0 -> db74cc596032f738f79efacff7d7458c7f74f8bedcbf788d4793b5209b77907d
E88A0 -> ea28e3a9dd00917e68adc5dc5ee5d188b9a87cec1f4b4213e729368b402b1481
E8460 -> f68aee3ba871deab29fc94333e3253fe059e772a319c217b59cf784317d2fe04
E8020 -> 136d3d094796f53cea81e94b2fcb38c1d4d498cd36d1128043eedca2ce794dbb
```

These are the **existing four sword/GUI splatter hashes**, ordinary 32x32
descriptors (source length 1050). No additional world-blood atlas is needed for
this path. Bank 0/`D7` emission occurs at A+`1E8848..1E884C`.

Script bank 0/`C3` stages literal RGB `(200,200,200)`, alpha 40. Its wrapper
`420D60` **doubles and clamps all four channels**: actual source RGB is
`(255,255,255)`, alpha 80. `42C9E0` writes these to manager+`74..77`, clears
per-channel random deltas +`78..84`, and clears random-color flag `20000000`.
Bank 0/`C5` stages zero RGB deltas and alpha byte FF; wrapper `420E20` calls
`42CAA0`, writing manager+`88..8B`. There is no green RGB drift in this recipe.
Emission `42C3B0` copies those values into each particle.

Native **particle draw `459760`**, called by `44A340` at `44A3DA` with manager
global `510194`, traverses its own live buckets and invokes the current batch's
texture **virtual slot 0**. It uses FVF `142` (XYZ, diffuse, one UV set), packs
particle+`8/9/A/B` into diffuse RGBA, and draws batches with DrawPrimitiveUP.
The attributed texture objects therefore invoke **ordinary `455B10`**.
The SDK must not independently scan those native buckets.

This is positive evidence that the ordinary binder/cache repairs cover the
traced world blood. It does not prove why a user's old frame was half green;
cache alias starvation and other effect families remain distinguishable causes.

### Split interior: exact cap atlas, not armor/skin

Exact original TGA:

```
SHA256 b940a1d4a0afda2e49e019c1c6224af1b1c5da732c97207007a8a9ed751c17b8
128x128, 17170 bytes, TGA type 1, 8-bit indices, 256-entry 24-bit BGR palette
Opaque: no stored per-pixel alpha
```

The atlas depicts anatomical cross-section tissue and is separate from the
mixed armor/eye/clothing texture. **Whole-cap recolor also recolors the small
contrasting internal detail**; it is not a hue-selected mask.

In `9-99.LCS`, model A+`167680` (`split_Rax`), primary mesh chain
`232650 → 2324F0 → ...`: only mesh `2324F0` directly uses this texture
(`C3540`), with white material `FFFFFFFF`. The other aligned occurrence is the
texture inventory, not another mesh. Model named-material lookup is absent;
**no invented material label is used**.

Buffer A+`217020`: 423 vertices, 32-byte stride, 833 uint16 strip indices.
Mesh FVF `112` is XYZ/normal/one UV set, without vertex diffuse. Geometry is a
thin central cut surface:

```
X [-0.0019579381, 0.0028737187]   thickness 0.004831657
Y [ 0.26001385,  1.47886717]    extent    1.21885332
Z [-0.43175709,  0.39609325]    extent    0.82785034
```

382 normals round to ±X at three decimals; 27 are bit-exact ±X.
Vertex SHA256 `b4e91476cc18b05d6a85586e6205eefc0e8f180748e52360408e926ab9b81be9`;
index SHA256 `cd0ef2ba4e3a03aa68488e2a5399c20977797ab383bf413eeabd579b0330da40`.
Native buffer binding `454D00` confirms stride +8; `453E60` confirms the native
indexed primitive fields. Offline strip interpretation and UV overlay show the
147 nondegenerate-index triangles occupy the anatomical atlas.

In gameplay `10-01.LCS`, `split_Rax` model A+`24C960` has cap mesh A+`30B390`,
texture A+`9E5E0`, **the identical full atlas hash**. Its geometry differs
(175 vertices/362 indices), so a single geometry hash must not be mistaken for
all Ranx variants. Atlas replacement can use the exact positively attributed
whole-cap hash; any material/mesh mutation additionally needs private ownership.

Reproduce/export:

```
python -B tools/inspect-blood-fx-v05.py --inspect-split <NEW-directory>
```

Final verified output: `build/blood-v05-split-verified/`:
original cap and two glow TGAs, `cap.obj`, `cap-uv-inspection.png`,
`inspection.json` (including exact per-primitive vertex diffuse values).
The earlier `build/blood-v05-split-inspection/` predates that diffuse report.
The OBJ is derived inspection geometry, not original source
or a runtime capture. UV V is inverted for the OBJ convention. PNG is an
inspection overlay only; original TGA bytes are unchanged.

### Split glow: exact creation chain and scope

Gameplay class `Ranx` A+`271DB0`, code `2664FC`, entry `268058`, stages class
`SplitEffect` at A+`268FB8`, then calls native bank 0/2 at `268FC0`.
Corresponding frontend call is A+`1E7910..1E791C`.

| Class | Gameplay class A+ | Frontend class A+ |
|---|---|---|
| `SplitEffect` | `271F94` | `1EB48C` |
| `EffectSequence` | `271FC0` | `1EB4B8` |
| `EffectTimeDelayed` | `272018` | `1EB510` |
| `Effect` | `271FEC` | `1EB4E4` |

`SplitEffect` creates a sequence and two delayed effects using bank 0/2.
Those classes create `Effect` children, forwarding their model argument;
`Effect` binds that argument through bank 0/29. The earlier verified native
child initializer `411160` stores creator's full handle at child+`238`.
Thus the admissible actor chains are:

```
Effect -> EffectSequence -> SplitEffect -> Ranx
Effect -> EffectTimeDelayed -> SplitEffect -> Ranx
```

All links must resolve full generations and exact classes. Dead/stale ancestors
mean refusal, not fallback to a class-name substring or any green texture.
`RaxExplode` separately binds `fx_enemydeath`; it is **not the split-plane model**.

| Model | Frontend descriptor/model A+ | Gameplay descriptor/model A+ |
|---|---|---|
| `Plane11` | `1CC690` and `1CC6A0` / `167630` | `259BA0` and `259BB0` / `24C910` |
| `Plane08` | `1CC6B0` / `1675E0` | `259BC0` / `24C8C0` |
| `Plane05` | `1CC6C0` / `167590` | `259BD0` / `24C870` |

Each has two meshes, each mesh references a **one-stage animated parent**:

| Art | Frontend child A+ | Gameplay child A+ | Dimensions/bytes | Full SHA256 |
|---|---|---|---|---|
| Orange flare | `AE460` | `95600` | 128x32 / 5138 | `fa4baac8f42dee07a978720a754a43f76c57d4e39c1073815fc44643bda407cf` |
| Green glow | `AF8A0` | `96A40` | 64x64 / 5138 | `fbde93538ad88d9648c1994fb3c78b43f448a2ef94e745ee9fa31c0619f77185` |

Mesh RGB is white; glow-layer material alpha is `3F`, flare alpha is `FF`.
**Final FVF verification: all six meshes are `142`, not `112`: XYZ + vertex
diffuse + one UV set, 24-byte stride, diffuse at vertex+12.** They share a larger
buffer, so inspecting its entire color array would conflate unrelated geometry.
The exact primitive's min-vertex/start-index fields select these colors:

| Frontend mesh A+ | Referenced vertex diffuse values (count of unique referenced vertices) |
|---|---|
| `237480`, `237770` | `3F001E44` (12), `3FC0E2FF` (3), `3F668EB6` (3) |
| `237BF0` | `3F001E44` (16), `3FFFFFFF` (8) |
| `237300`, `2378F0`, `237A70` | `FF000000` (80), **`FF52FF00` (8), `FF1E8400` (8)** |

Thus **texture neutralization alone is NOT a complete arbitrary-RGB glow fix**.
The precise attributed geometry is known, but preserving its diffuse shading/
alpha while changing RGB requires private GPU-stream or verified shader-stage
handling. Getter scope below does not implement that missing component.
No native/shared vertex buffer was mutated. Do not enable it as a finished glow
feature. Both images can be shared elsewhere; **do not globally enable these
two hashes**.

### Implemented scope APIs / exact native hook contracts

Ordinary texture table VA `4C9A38`: slot 0=`455B10`, slot +18=`446870`.
Getter **`446870`** ABI `void* __thiscall(void* descriptor)`:

```
446870 8D 41 0C     lea eax,[ecx+C]
446873 C3           ret
446874..44687F      90 padding (12 bytes)
```

It returns the **address** of +C, not the GPU handle itself. Full body is only
four bytes; hook installation must correctly account for trailing padding.
Constants and 16-byte body/padding fingerprint are in `cosmetic_blood_fx.h`.
No native virtual getter calls are made by the module during inspection.

`PrepareAnimated` captures fixed-size header/stage/ordinary-child snapshots on
the cold tick, verifies the exact getter, and calls a mandatory host `Authorize`
callback. Host must verify effect/private-mesh provenance AND full source hashes
and supply SDK-owned managed GPU handles. No default authorization exists.

`RunAnimated` always invokes its continuation exactly once, unchanged native
parent passed by the host. `ScopedGpuHandle(child)` returns an address in stable
SDK prepared storage only during this TLS scope, after rechecking the child's
descriptor/binder/getter snapshot. Invalid scope means original getter.
Nested unadmitted scopes suppress outer scope; MSVC `__finally` restores TLS on
normal return, C++ exception or native SEH. No descriptors/materials/alpha/state
are written. Do not destroy/reassign prepared storage or release handles in scope.

**Shared-parent caveat:** a cached parent descriptor can belong to other actors.
Its hash pair alone is not a creator proof. Prefer **private-mesh scope**:

- Hook native mesh setup **`4545D0`**, `void __thiscall(void* mesh)`.
- Body `4545D0..454707`, ends plain `ret`.
- Entry bytes `51 A1 00 55 52 00 53 56 57 8B F9`; normalize absolute operand
  at bytes 2..5 to relocated global `525500`.
- It reads mesh+10, calls texture slot 0 synchronously at `454605`, and only
  returns after native texture-stage setup. Opaque/transparent draws use it.
- Main admits only independently proven private mesh keys from the exact
  owner-chain/model cohort. `RunMesh` rechecks mesh+10 against the prepared
  parent and wraps this original setup call. The hooked `446870` supplies
  handles only inside this exact mesh setup scope. Unknown mesh draws stay
  native even when they share the parent/source images.
- This does not require copying/truncating animated descriptors, patching
  ownership bits or modifying the native parent's stage records.

No runtime hooks, actor discovery or GPU creation were installed here. Host
discovery remains bounded >=250ms; draw-path checks use already admitted fixed
snapshots, never source hashing, asset discovery or all-strat scans.
The scoped APIs intentionally supply no policy that could silently turn the
known incomplete glow recipe on.

Updated Win32 `/std:c++17 /EHsc /W4` tests passed: exact getter admission,
mandatory authorization rejection, two stages, nested unknown suppression,
native-memory invariance, mismatched mesh/native handle refusal, exactly-once
continuations, C++ unwind and **real RaiseException/SEH unwind**.

### If the reported half-green effect remains after main's ordinary fixes

First obtain level/checkpoint and a short clip distinguishing flying splats,
pool, central cap, orange flare and green split-plane layer. Do not infer that
all are the same particle family.

Only with separate user authorization, a useful **bounded controlled capture**
would log one cut (max 60 frames / 64 distinct admitted records):

1. Ordinary binder `455B10`: descriptor, +C GPU handle, +10 source, +14 length,
   cached exact source-hash verdict, and substitution/refusal reason.
2. For that same known splatter cohort, particle renderer `459760` batch entry
   and DrawPrimitiveUP diffuse RGBA (first six vertices, stride 24), to separate
   unneutralized texture from colored diffuse/blend behavior.
3. For glow only, private mesh setup `4545D0`: mesh+10 parent identity, stage
   count/child keys, owner-generation/model admission result, and getter
   `446870` replacement/refusal. No full actor/particle-pool dump is needed.

These are capture requirements, not authorization to attach or launch. No such
capture was attempted. Do not silently compensate unresolved output by rewriting
global green pixels or native particle colors.

Follow-up native REA Evidence:

- primitive/buffer binding:
  `ev_52775b0223793e98fa47f5841b54f7b7a15ade0920f8e6832b4c28601dd0fa5c`,
  `ev_36eef428113ef9ad86c30ace8d817a7eeb893455c94588f7d3d5bcd70542d3df`
- world particle render:
  `ev_7bc7308bbac64b672c670a2d82b2451781e0ce0b83e62e8c8dd96b29628ea603`
- particle RGB/random deltas:
  `ev_89c0cb672d656d3de69815ead6f0ea85ec5da8700a334045695c7e41e3637835`
- exact mesh setup body/return:
  `ev_d50a6196b53fe93809526277971e960d3d9dcaa2f1fa3451e6a8f6bee8bb8dc5`

Focused Evidence exported to
`C:\Users\Akira\AppData\Local\Temp\opencode\cosmetic-blood-v05-focused-evidence-2026-11-21.json`
(2,590,844 bytes, 12 records); owned REA session closed again.
