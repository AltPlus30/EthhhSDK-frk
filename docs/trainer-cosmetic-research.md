# Cosmetic Editor: research only

2026-11-19. **No cosmetic production code or UI was added.** No game launch,
process attachment, live memory access, installed-file writes, or native REA
session operations were performed. The movement investigator owns that session.

## Result / readiness

| Question | Answer | Readiness |
|---|---|---|
| Belt recolor | Exact named-render-entry tint native and built-in normal/alternate model refresh events located | High static confidence; not runtime tested |
| Arbitrary player colors | Existing per-instance named-entry RGB/alpha paths are better leads than texture pixel edits | Partial: other player entry names and lifecycle resets need mapping |
| Equipment models | Existing six-tier sword selector and game's refresh recipe are located | High static recipe confidence; not an arbitrary model-swap API |
| Arbitrary player model swaps | Render-model binding native located, but separate skeleton/controller compatibility remains unproved | **Not safe to expose** |
| Texture replacement | Packed uncompressed TGAs exist; named texture-binding path is a lead | Partial; player texture attribution, GPU ownership and replacement ABI unresolved |
| Shared versus instance data | Native tint first makes mutable render instances; direct resource edits bypass this | Strong static ownership evidence, not universal texture isolation |

**Best first future experiment:** instance-local named-entry tint on the exact
selected Ninja at an engine-thread safe point, without changing profile state or
rebinding its model. This is a research recommendation, not implemented code.

## Identity, coordinates and provenance

Executable: `E:\Games\I-Ninja\I-Ninja.exe`, SHA-256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Native addresses below are preferred-image VAs for that executable and must be
rebased for a running instance. **Every `A+...` address is an offset in the
decompressed BIGB A pool, not executable code or a callable process address.**

Read-only archive inspection:

| Artifact | SHA-256 |
|---|---|
| Installed `Data/09.vol` | `0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669` |
| Member `9-95.LCS` | `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65` |
| Decompressed A | `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd` |

Evidence classes, deliberately separate:

1. **Retained REA native evidence:** `trainer-movement-equipment-evidence.json`,
   especially linker/getter record
   `ev_f4b57eb06e584b63de546f13632f49c5857dfa143557abd1c5875f12f1a2e328`
   and property getter record
   `ev_a0ee524f29d25bce39cd84a8f9e01f06349cc0dccede42cac1bd00817621b0f9`.
   The latter maps property `0x61` to its receiver `+0x2C0`; this is **not**
    the diagnostic skeleton pointer `+0x268`.
   Follow-up [`trainer-movement-experimental-evidence.json`](trainer-movement-experimental-evidence.json)
   corroborates class-wide queued delivery (`ev_5f9abcb1d11ec2f292f06cd288f7c5dd39614c02d5b8b0c17bb48f01c1a86166`,
   `ev_f0b81a1f6ed120f5551fdcd80a9e6837865974d3eac3f158287d6bb11efdc5c7`,
   `ev_833044a933fafcd9bd70a7d708187090760d4b9fbd87a5a38adc0d9045d88716`)
   and render-instance copy helper (`ev_404e144edab890913ad4311351c1a6e633045a9bc8729e01a68b9c86d80a32e4`).
2. **New offline observations:** original executable bytes mapped with pefile
   and linearly decoded with Capstone **5.0.7**; PC archive decompressed with
   `tools/inspect-mission-timer.py::unpack`. These are reproducible static byte
   inspections, **not new REA Evidence IDs or runtime observations**.
3. **Prior research/source:** `trainer-movement-equipment-research.md`,
   `ninja-connection.md`, `ethhhpatch.md` (entity interpolation contract), and
   `src/core/game.h` / `game.cpp`. Existing implementation/comments corroborate
   integration constraints, not observed cosmetic behavior.

The historical `ref/tools/strats.py` mentioned in prior research was **not
present** in the suggested modkit directory or the bounded local searches.
This pass does not claim another full VM decode or zero-error CFG validation.
Its relative-call searches are aligned dword pattern searches, with selected
surrounding words inspected. Event IDs are supported both by packed event-table
entries and matching native event dispatch; unexamined branches remain unknown.

## Exact belt paths and parameters

Ninja descriptor `A+0x4D15C0`, code `A+0x4B5F20`, entry `A+0x4BA6F0`.
Its packed event table includes:

| Table entry | Event | Handler |
|---|---:|---|
| `A+0x4D1330`: `00130008,004BA218` | 19 | `A+0x4BA218` |
| `A+0x4D1338`: `00140008,004BA24C` | **20** | `A+0x4BA24C` |
| `A+0x4D1320`: `00230008,004B8708` | 35 | `A+0x4B8708` |

Event 20:

```text
A+4BA24C  read script property 0 into r246
A+4BA250  call native 0/5B with r246       ; render-model binding, NOT color-only
A+4BA258  r118 = A+4B5186                 ; "sash white"
A+4BA260  r119 = A+4B5176                 ; "sash knot white"
A+4BA268  r117 = 0
A+4BA26C  call A+4B76A0
A+4BA274  event-handler end
```

Event 19 performs the related rebind using property **1**, then the same names,
`r117=0` and helper. This supports calling 20 the property-0/normal-model refresh
and 19 the alternate-model path; complete gameplay/form semantics are not proven.
**Do not fire event 20 indiscriminately during an alternate form.**

Helper `A+0x4B76A0` sets `r116=0`, reads profile integer **23** into `r120`
(`A+0x4B76A4..0x4B76AC`) and selects packed colors. Existing native profile
evidence gives `P=*(uint32_t*)0x51AA88`, tier at **`P+0x90`**.

| Tier | RGB integer inputs | Packed low 24 bits |
|---:|---|---|
| 0 | 200,200,200 | `C8C8C8` |
| 1 | 130,120,20 | `147882` |
| 2 | 20,100,20 | `146414` |
| 3 | 15,52,140 | `8C340F` |
| 4 | 142,34,1 | `01228E` |
| 5 | 41,41,41 | `292929` |
| 6 | 41,41,41 | `292929` |

Packing is observed native behavior: `R | (G<<8) | (B<<16)`, **not**
`0xRRGGBB`. The wrapper does not clamp or mask each input; keep each input
within 0..255 in a future experiment. The actual material tint path masks the
combined input to 24 bits and preserves the previous alpha byte.

At `A+0x4B779C..0x4B77D8`, each sash name is checked using native `1/0xF3`
and, when present, tinted with native `0/0x184` using `[name,packedColor]`.
The helper also conditionally resolves the actor handle in `r117` via
`1/0x10A` and calls `0/0x1D8` (`0x41DB90`) with `[handle,color]`.
The latter writes `color|0xFF000000` at resolved object `+0x2898` through
`0x45A900`: it is a **special object path**, not a generic Ninja color field.
The event-20 recipe supplies zero for that optional handle; do not invent one.

Other bounded relative-call patterns to this helper occur at `A+0x4BA000`,
`0x4BA240`, `0x4BB2DC`, `0x4C3154`. These establish likely reset/reapply sites,
not the semantics of every enclosing state. A custom color cannot be assumed
to survive these calls, model rebinds, actor recreation, or level transitions.

## Native entry points and recovered stack ABI

Table bases are `0x4C7E70` (0) and `0x4C87C8` (1). The linker record above
confirms top nibble selects the table and bits 16..27 select its index.
All rows below were independently read from those tables and inspected offline.
Names are descriptive analyst labels, not shipped debug symbols.

| Script native | VA | Observed role / arguments |
|---|---|---|
| `1/0x33` | `0x41FD60` | `cdecl(context,outputDword,inputRGB[3])`; writes packed color; context unused |
| `1/0xF3` | `0x458120` | `cdecl(strat,outputDword,args)`; `args[0]=name`; returns presence 0/1 |
| `0/0x184` | **`0x458330`** | `cdecl(strat,args)`; `[name,packedRGB]`; mutates named render-entry colors |
| `0/0x183` | `0x4581C0` | Same name-driven instance path; float argument converted to entry alpha byte |
| `0/0x5B` | `0x41BE80` | `cdecl(strat,args)`; `args[0]=modelDescriptor`; calls `0x410CD0` |
| `0/0x11D` | **`0x417380`** | `cdecl(sender,args)`; `[className,eventNumber]`; broadcasts by class name |
| `0/0x180` | `0x457C50` | Two-name render binding path; texture-related candidate, not a validated external texture loader |

These ABI descriptions follow stack reads and plain `ret` in the wrappers;
they are not live-call validation. No call is approved from an arbitrary render
or UI thread. Color mutation assumes a non-null primary instance and lookup
table; the native function itself does not provide SDK-style identity guards.

Notification wrapper `0x417380` calls:

```text
ECX = 0x51019C
0x423560(sender, className, eventNumber, 0, 0) ; thiscall, ret 20
```

`0x423560` walks manager `+0x189C` / strat `+0x198`, compares class names
case-insensitively via `0x4B6E9B`, and invokes `0x4130B0` on each match. The latter checks active/event
state, looks for event type 8 and the requested number in the instance's
event records, then queues through `0x4235E0`. Therefore this is **class-wide
queued delivery**, not a direct immediate call on one selected Ninja. A valid
ABI does not guarantee acceptance or immediate appearance refresh.

## Ownership: why raw material pointers are the wrong starting point

`0x458330` first calls `0x457BF0(strat)`. That helper processes both render
instance slots **`strat+0x128` and `+0x12C`**. It uses the first instance's
ownership flag `instance+0x34 & 8` to decide whether copying is needed, then
calls `0x454240(heap,instance,1)`, replaces the slots with the results, and
releases the old instances through `0x454150`. Do not infer independently
checked ownership flags for both slots.

`0x454240` force-copy path allocates and copies the 0x50-byte model header,
clones subordinate buffers and mesh chains (`0x4541A0`, virtual copy methods),
rebuilds `+0x3C` lookup arrays, and sets flags `0xC`. This is direct evidence
for **copy-on-mutation of render instances**. It does not prove that every
referenced texture pixel allocation or GPU texture becomes private.

The tint native then visits both instance slots, looks up names through
`instance+0x3C`, obtains the selected render entry from lookup `+4`, and updates
the entry's packed color at **`entry+0x14`**, preserving its high byte. It repeats
for subsequent same-name matches. `0x454020` searches fixed-size name records
(0x20-byte stride; its fast compare checks the first 16 bytes); the wrapper
builds zero-filled bounded name buffers. Use the game's observed exact names,
not arbitrary unbounded labels.

Consequences:

- Mesh/render-entry color is **not texture pixel color**. Tint may multiply
  existing textured/vertex color; the complete shader/material equation was
  not recovered in this pass.
- Do not write a resource model, mesh or material because a pointer appears
  under the selected actor. It may still be shared until this native copy path.
- Do not patch only one double-buffered instance: colors can alternate or reset.
- A native copy can invalidate cached instance/entry addresses. Resolve them
  anew after every appearance/resource change.
- Existing default color restoration needs more than a guessed white: save
  original per-entry packed color/alpha or replay the applicable native recipe
  only after verifying the current model/form.

## Sword and other equipment models

Prior PC script evidence remains the strongest supported equipment route:
profile integer **17**, `P+0x78`, range **0..5**, then queued notifications
`"Ninja",35` and `"NinjaSword",54` as done by `PauseMenu_Cheats`
in `9-96.LCS` (`A+0xA9440..0xA9460`).

Ninja event 35 calls selector `A+0x4B8670`. Its six resource-reference operands
are `A+0x4B3CC0, 0x4B3CD0, 0x4B3CE0, 0x4B3CF0, 0x4B3D00, 0x4B3D10`.
They are serialized references/descriptors, **not six executable addresses or
NUL-terminated model names**. The selected value is stored through script
property `0x61`; retained getter evidence maps that property to **receiver+0x2C0**.
Earlier notes called this `global[97]`; do not confuse that script notation
with process-global storage or directly overwrite `strat+0x268`. This pass did
not independently decode the VM receiver/context selection for that instruction,
so `+0x2C0` is not authorization for a guessed direct actor write.

NinjaSword color helper `A+0x4C347C` returns packed RGB from tier 17:
`(150,150,150)`, `(130,120,20)`, `(20,100,20)`, `(22,33,59)`,
`(142,34,1)`, `(60,60,60)`; default returns zero. One call pattern is
`A+0x4C3ECC`, with its result handed to native `1/0x107` together with a resource
reference. This is **not independently proven to be the sword mesh's diffuse
color**; it could belong to associated effects. Do not label it as a universal
blade texture recolor API.

Event 54 (`A+0x4C3F9C`) calls `A+0x4C3FEC`, which calls `A+0x4C3578`, then
refreshes a tier-dependent scale `1 + 0.5*tier`. This is not a cosmetic-only
edit: sword tier also affects derived gameplay state. Other equipment attachment
types, attachment sockets and independent cosmetic-versus-stat overrides remain
unmapped.

## Arbitrary model swaps: skeleton and animation boundary

Native `0/0x5B` passes a **descriptor pointer** into
`thiscall 0x410CD0(strat,descriptor)`, not a filename or arbitrary mesh address.
Observed descriptor words are `[renderModel,modeOrAuxiliary]`. Before rebind it
calls `0x4107F0`, which releases and clears both render-instance slots and clears
mode `+0x144`. It then binds the new model via `0x43E5D0`, making additional
instances through `0x454240` when the descriptor's second word is nonzero.
The complete meaning of that second word is not established.

That routine does **not establish replacement skeleton/controller setup**.
Existing SDK identity distinguishes:

- Render instances: `+0x128/+0x12C`, mode `+0x144`.
- Skeleton/model resource: `+0x268`.
- Animation control: `+0x26C`.
- Node matrices: `+0x13C`, count `+0x140`.

Existing renderer research requires node count to agree with `model[0]` and
controller `+0x24`, and requires bounded valid mesh node indices (`0xFFFF`
is world-only). Matching counts alone do **not** establish joint-order,
bind-pose, animation-channel or socket compatibility. Existing native facing
writes update root angles/quaternion, not a replacement skeleton.

Accordingly, only the game's known descriptor/refresh combinations are
supported as static leads. Arbitrary enemy/player mesh substitution, importing
models, copying node matrices or calling animation update functions is **not**
a validated cosmetic workflow. Inspect exact descriptor, skeleton hierarchy,
controller channels and attachment consumers before any such implementation.
Rebind/copy operations must invalidate interpolation/cosmetic caches.

## Texture override feasibility

Read-only use of existing external modkit `textures.py::find_textures` found
**174 validated serialized TGA descriptors in `9-95.LCS` A**, including indexed
8-bit TGAs with 24-/32-bit palettes. Example: descriptor `A+0xAC70`, source
`A+0xAC90`, 8x8, 3 palette entries, 91-byte TGA. This proves texture carriers
exist in the inspected member, **not which texture belongs to Ninja**.

The modkit matches descriptor marker `0x0054BC30`, checks inline source offset,
dimensions and complete TGA length. Its replacement CLI is restricted to
scenery `.LCM` bundles; this is **not evidence that it can safely rebuild player
`.LCS` data**. No replacement/export was performed here.

Native `0x457C50` / script `0/0x180` first prepares mutable instances, resolves
two named lookup entries (`0x454020` / `0x454080`), and dispatches a virtual
binding operation. It is a promising existing-resource rebinding lead, but
external texture creation, exact argument semantics, reference ownership and
device-reset/reload handling were not resolved. Nearby `0x457E00` manipulates
render state; adjacency is not proof of a texture replacement API.

**Feasibility verdict:** built-in RGB tint is substantially better established
than pixel or GPU texture overrides. Before texture editing, identify the Ninja
render-entry → material/texture carrier → GPU resource chain, establish sharing,
format/mips/palette/alpha and reference lifetimes, then prove clone-and-restore
and device-loss behavior. Do not recolor a globally shared GPU texture in place.

## Reproduction and remaining gates

```powershell
python tools/inspect-cosmetic-research.py --summary
python tools/inspect-cosmetic-research.py --range 0x4B76A0 0x4B77FC
python tools/inspect-cosmetic-research.py --range 0x4BA218 0x4BA278
python tools/inspect-cosmetic-research.py --native 0x458330 0x182
python tools/inspect-cosmetic-research.py --native 0x457BF0 0x60
python tools/inspect-cosmetic-research.py --native 0x417380 0x1F
python tools/inspect-cosmetic-research.py --native 0x423560 0x5B
```

The tool validates executable SHA, decompressed length and selected script
fingerprints; it only prints identities, words and optional native instructions.
Default dependencies are standard library; native mode requires existing
pefile/Capstone. It is not a production API or a complete VM decoder.

Still required before a Cosmetic Editor:

1. Runtime validation under separate explicit authorization: selected-actor
   isolation, form changes, respawn/level transitions and proper engine-thread
   timing. None occurred here.
2. Map all desired body/equipment names and exact default colors on every
   supported model. Belt names do not prove body/hair/eyes coverage.
3. Establish reversible instance-local restoration and invalidation on rebind,
   actor generation change and normal script refresh; avoid per-frame mutation.
4. Validate skeleton/channel/socket compatibility before model swaps.
5. Resolve texture-specific ownership and GPU lifecycle before texture overrides.

Research answers the entry-point/ABI/ownership questions partially to strongly;
runtime safety, unrestricted model compatibility and player texture replacement
remain **unresolved**, not silently confirmed.
