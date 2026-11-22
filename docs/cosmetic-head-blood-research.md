# Ninja head ownership and blood cosmetics (static research)

2026-11-20. No launch, attach, install or game-file modification.
Only new research/extraction files are owned by this investigation.

## Verified head handoff (ready for integration)

**`NinjaHead+0x238` is the creator's full generation handle for the normal
Ninja-created head.** This is now positively corroborated, not inferred from
the role name or proximity.

Use the existing full-handle registry resolution and live-list/self-handle
validation. Require exact class name `NinjaHead`, and require the resolved
owner to be the explicitly selected exact `Ninja`. Reject ambiguity, stale
generation, unreadable identities and mismatched owner. This does not justify
enabling arbitrary classes' owner fields or changing `NinjaSword` policy.

### Creation and native ownership path

Coordinates prefixed A+ are decompressed `Data/09.vol` member `9-95.LCS`
offsets, NEVER process addresses. Native addresses are preferred-image VAs.

1. Exact head class A+`4D1778`: name pointer A+`4B43E7` = `NinjaHead`,
   code A+`4C4D40`, event entry A+`4C5254`.
2. Ninja script A+`4B63B0..4B63C4` contains:
   `5A740000 004D1778 32750000 80FF7400 00020044 422A108D`.
   This stages the exact head class, calls native **0/2**, and reads the newly
   created child handle (property `0x8D`, actor+`0x23C`) into Ninja register 42.
3. Native bank-0 table VA `4C7E70 + 2*4` resolves `416DD0`. Offline Capstone:
   this wrapper calls thiscall `410EF0(creator,args,2)`, then writes returned
   child's `+1A0` into creator's `+23C`.
4. `410EF0` forwards creator as argument 7 to `4248C0`; that allocator forwards
   it to child initializer `411160`.
5. `411160` explicitly assigns child `+238` to creator `+1A0` (or zero when no
   creator). It also sets child `+1A8` from the supplied exact class.
6. The head script consumes owner property `0x8E`, e.g. at A+`4C4EDC`, stages
   a node-name string and calls native **0/0x106** at A+`4C4EE8..4C4EEC`.
   `421680` getter case `8E` reads actor+`238`; `41C5D0` resolves the argument's
   full handle against `51019C + (handle & 1FF)*8`, obtains owner model/control/
   node matrices and derives the child's attachment transform.

REA derived static pseudocode (Ghidra 12.1.4), not runtime observations:

| Function | Evidence ID |
|---|---|
| `410EF0` creator forwarding | `ev_d5bf05385f3405806e1a9aabd391a3bcecb9505f9512d73f149583e327bec783` |
| `4248C0` allocator forwarding | `ev_58f2fcde8f0a18a903daaade81ae6db2626402018d0cdba480bfdb3c4c077336` |
| `411160` owner assignment | `ev_0c424a4ca3b2744970473809d533cb1b10f613ca22ce545ff084597e127202c2` |
| `421680` owner getter | `ev_33905bc612bf6799191d38f86522040a6578d3afb6ea9bd44c476e71949272e5` |
| `41C5D0` attachment/full-handle lookup | batch `ev_0a216cae4e1d9f4cb9808432b962f7b6ad0b0e5419706e8130b1e0f1fc05e34a` |

The batch also records missing Ghidra tiny-wrapper functions. Those wrappers
were decoded from original executable bytes with the existing offline inspector,
not silently treated as successfully decompiled.

### Exact normal-head material entries

Descriptor A+`4B3BA0` -> model A+`4B3740`, model name **`Ninja_Head`**.
Model +`3C` -> lookup A+`4B3790`: exactly three named entries.

| Exact entry | Name A offset | Mesh A offset | Texture A offset |
|---|---:|---:|---:|
| `head` | `4B37D0` | `5465C0` | `1B0720` |
| `sash white` | `4B37F0` | `546460` | `7C230` |
| `sash knot white` | `4B3810` | `546300` | `7BDC0` |

There is **no separate named mask/blue-cloth material** in this normal-head
lookup. Combined belt/sash controls should include both head sash names as well
as body sash names, using independently validated/private render instances.

Uniform tint of `head` is whole-entry tint: do NOT advertise it as preserving
face/eyes/skin. Its atlas A+`1B0720` is the existing exported
`build/cosmetic-textures/ninja-head-original-A001b0720.tga`, 128x128.
Per-pixel mask-only recolor would require an authored/verified texture mask or
palette/UV attribution and private texture replacement, not simply the material
RGB multiplier. A blanket grayscale atlas plus RGB tint still tints every
nonblack texel. No face-independent mask tint mechanism is established here.

## Artifact identity

- Executable SHA-256:
  `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
- `Data/09.vol`:
  `0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669`.
- `9-95.LCS`:
  `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65`.
- Decompressed A:
  `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd`.

Reproduce packed head observations with
`python -B tools/inspect-cosmetic-research.py --range 0x4b3740 0x4b3830`,
`--range 0x4b6380 0x4b63cc`, and `--range 0x4c4ed0 0x4c4ef0`.
Tiny creation wrapper:
`python -B tools/inspect-cosmetic-research.py --native 0x416dd0 0x21`.

## Blood status

**Green blood is texture-colored, not merely a neutral sprite with a green
material RGB override.** No arbitrary-color runtime blood module/API was added:
the existing body-material tint path does not cover these sprite allocations,
and multiplication of a green texture cannot yield arbitrary hues.

### Positively attributed script/resource paths

In the inspected core member:

| Exact class | Class A offset | Code A offset | Entry A offset |
|---|---:|---:|---:|
| `NinjaSword` | `4D1618` | `4C3474` | `4C403C` |
| `NinjaSwordBlood` | `4D1644` | `4C3474` | `4C44E8` |
| `BloodSplatter` | `4D16F4` | `4C4934` | `4C493C` |
| `SpawnBloodSplatter` | `4D1720` | `4C4934` | `4C4AA0` |
| `SplatterSpawner` | `4D174C` | `4C4934` | `4C4D08` |

- NinjaSword code A+`4C3948..4C3968` stages exact `SpawnBloodSplatter` and calls
  native 0/2; a second class reference is at A+`4C4D18` in SplatterSpawner.
- Shared splatter helper A+`4C493C` selects one of four texture descriptors:
  A+`1E0060`, `1DFC20`, `1DF7E0`, `1DF3A0` (operands A+`4C496C`,
  `4C4978`, `4C4984`, `4C4990`). It stages texture and coordinates at
  A+`4C49D4..4C49DC` and calls **bank 1/0x68** at A+`4C49E0..4C49E4`.
- Bank 1/0x68 is native `41D130`, not bank 0/0x68 (`41BA70`, an unrelated
  animation helper). The linker `426A20` explicitly selects bank 0 table
  `4C7E70` or bank 1 table `4C87C8` from the operand's high nibble.
- `41D130` calls sprite manager `42B7C0` at manager `519A60`, returns the
  allocated object's +`10` handle into the VM destination. This is a separate
  handle registry, **not strat registry `51019C`**. The manager allocates a
  `0x2C`-byte object and calls `42BF10`; ownership/free lifecycle was not
  completely investigated and is NOT approved for SDK direct mutation.
- Helper A+`4C49F4..4C4A00` applies alpha through native 0/`E8` and a further
  native 0/`245`; its size/position animation uses 0/`E7` and 0/`E6`.
  `E8` -> `41DD90` -> `42BAC0` changes alpha only, preserving RGB.
  Sprite RGB exists separately: native 0/`E9` wrapper `41DDD0` calls
  `42BA90`; `42B9C0` packs `R<<16|G<<8|B`, preserving per-corner alpha.
  RGB capability is not a texture-neutralization mechanism.
- NinjaSword A+`4C44A4` also references exact `NinjaSwordBlood`. Its entry
  A+`4C44E8` binds descriptor A+`4B3E20` through native 0/`29`.
  Descriptor -> model A+`4B3920`, name **`pool of green b`** (exact stored
  15-character label). Primary mesh list is null; **second** list model+`4`
  points to mesh A+`54FA20`; mesh+`10` -> texture A+`1C7DE0`, mesh+`14` is
  white `FFFFFFFF`. Do not misidentify the null primary as an absent model.

The four splatter textures are 32x32 indexed TGA type 1, 8-bit indices,
**32-bit BGRA palettes**, two entries: both have RGB **(78,255,0)**, one
opaque and one transparent. Their green is unambiguously in texture bytes.
The 64x64 pool atlas is also indexed type 1 with 256 BGRA entries: among 206
nontransparent palette entries, 186 have green strictly larger than red/blue.
It is not a neutral grayscale base either. These facts do not establish the
entire engine blending equation or that every effect has these same textures.

### Deliverables / safe next step

`tools/extract-blood-textures.py` validates executable/archive/member/pool hashes,
exact class/script/resource links, dimensions, palette layout, pixel bounds and
all five original TGA hashes before writing. It refuses existing directories
and output under the game tree. Successfully exported originals to:

- `build/blood-textures/blood-splatter-0-original-A001e0060.tga`
- `build/blood-textures/blood-splatter-1-original-A001dfc20.tga`
- `build/blood-textures/blood-splatter-2-original-A001df7e0.tga`
- `build/blood-textures/blood-splatter-3-original-A001df3a0.tga`
- `build/blood-textures/green-blood-pool-original-A001c7de0.tga`
- `build/blood-textures/provenance.json` and `README.md`.

Offline validation passed for all five originals; five independently corrupted
class/native/mesh/palette/pixel cases were refused, as were existing-output and
game-tree output paths. These are extraction/guard checks, not live cosmetics tests.

These are originals, not bundled mod assets. A blood-color extension needs
either private neutralized replacement textures with preserved alpha plus
appropriate per-effect tint, or a verified color replacement shader path.
Ordinary RGB tint alone supplies filtering/darkening, not arbitrary hue.
No direct sprite writes, script literal patches, guessed enemy class hooks,
archive repacking or unsafe global texture mutations were implemented.

**Coverage limitation:** this traces NinjaSword's shared splatter creation and
the named blood pool, not every enemy class/hit/death script across all level
archives. It positively attributes these five resources to blood; it does not
claim all on-screen enemy blood is covered, nor that a generic Sprite hook can
isolate enemy blood without validating the creating class/resource at emission.
The installed game's engine was never executed.

REA blood Evidence:

- Sprite RGB/alpha/size helpers:
  `ev_2d81a1565904b040d36d1d388e6440bfa897e4d3aadf80417ed356b60cb9d853`.
- RGB packing:
  `ev_c885a1e9dc6edb1345948c3491b3c9981649d226701528f0b4af855be9a9cc7c`.
- Sprite allocator, emission helper `42D0F0`, native-bank linker:
  `ev_85e3dcc68c9395c89a5be3a7fa4e05d403a3fcc45b402ed759393c708cfed494`.

Complete retained REA records exported to
`C:\Users\Akira\AppData\Local\Temp\opencode\cosmetic-head-blood-evidence-2026-11-20.json`
(424,896 bytes, 9 records), including missing-wrapper results. The owned REA
session was closed after export. The export reports zero ledger unknown records;
that does NOT erase the investigation/coverage/lifecycle unknowns above.
