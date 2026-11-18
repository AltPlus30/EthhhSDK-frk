# Native movement / equipment trainer research

2026-11-18; **static only**. No game launch, process access, installed-file edits,
or production-code changes. Target `E:\Games\I-Ninja\I-Ninja.exe`, SHA-256
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
All executable addresses below are this build's VAs; script addresses are
**decompressed BIGB A offsets**, NOT process addresses.

## Result

| Feature | Finding | Confidence / implementation status |
|---|---|---|
| Earned ranks/grades, **not Ranx** | Profile integer **19**, `P+0x80` | High static; pause grade-count consumer confirmed |
| Belt tier | Profile integer **23**, `P+0x90` | High static; pause belt and Ninja color consumers confirmed |
| Sword tier | Profile integer **17**, `P+0x78` | High static; model, color, damage-scale consumers and built-in cheat refresh path confirmed |
| Native speed multiplier | No validated scalar control | Unresolved; do not offer a guessed memory write |
| Native jump height | Multiple script velocity/state paths, not a verified single height field | Partial mechanics evidence only |
| Native jump cooldown | No validated cooldown field or bypass | Unresolved; do not equate held-key/state gating with a timer |

Here `P = *(uint32_t*)0x51AA88`. These three fields are 32-bit integers,
not floats. The supplied Ranx offset `P+0xA0` corresponds to integer **27**:
it is separate from earned grades.

## Native backing and exact evidence

REA/Ghidra 12.1.4 confirmed:

- Native linker `0x426A20` resolves table 0 at `0x4C7E70`, table 1 at
  `0x4C87C8`, using `(linkedWord >> 16) & 0xFFF`.
- Table 1 index `0x3E`: pointer at `0x4C88C0` is `0x414F80`
  (bytes `80 4F 41 00`).
- `0x414F80..0x414F9B` obtains the input index, loads `P`, adds `0x14`,
  calls `0x40EAC0`, then writes the returned dword to the output register.
- `0x40EAC0(this,index)` reads `this+0x20+index*4`;
  `0x40EA50(this,index,value)` writes the same location. Thus the absolute
  profile formula is **`P+0x34+index*4`**.

REA IDs (complete records in `trainer-movement-equipment-evidence.json`):

- Linker and table getter/setter decompilation:
  `ev_f4b57eb06e584b63de546f13632f49c5857dfa143557abd1c5875f12f1a2e328`.
- Table pointer:
  `ev_0b8a4ef53e4e5578fda6dccb563dce1bb64b8ba4d625594ae3cd5a884cca2082`.
- Getter wrapper raw bytes:
  `ev_556c90e12b72d5e8abb51c2c3cbd7d7988ca1c5e127d0ef24e511a04fdc484ac`.
- Individual wrapper instructions:
  `ev_021b87891dc250afea53bdfd4495a4cf0c7e39242a74200deff61b9983d324e9`,
  `ev_845dc4b4fbfa3b0697ecac7dac5cf50de57a07b76d932307240d7a6c015cc2a7`,
  `ev_e3471910070a49f3679726c244f0cee027288befd75406ac5f1f4e1f82e50c62`,
  `ev_ec0bf5f475a8b99dd2225126a88968ba91f19ed81025057c6f2769595dc90263`.

Ghidra did not create function bodies for some tiny wrappers; their failed
decompilations are **not** evidence of absent functionality. Exact decoded
instructions/raw bytes were used instead.

## Packed PC script evidence

Read the installed `Data\09.vol` without extraction or writes, using
`tools/inspect-mission-timer.py::unpack`. Decoded A/B lengths matched their
headers. Used historical `ref/tools/strats.py` read-only for instruction
decoding; each selected Ninja, NinjaSword, PauseMenu, ShowGrades and Cheats
code block decoded with **zero instruction/branch-boundary errors**.
PS2 names/notes were leads only; offsets and native IDs below came from PC bytes.

| Member | Member SHA-256 | Decompressed A SHA-256 |
|---|---|---|
| `9-95.LCS` | `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65` | `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd` |
| `9-96.LCS` | `7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4` | `127243447e13d1bc29960008952c6f6033446b03a67fbe36b0bfebdab13f9edc` |

### Earned grades and belt

`9-96.LCS`, `PauseMenu_ShowGrades` entry `A+0xA8148`:

- `0xA8168`: argument **19**; `0xA816C`: native `1/0x3E` returns total into `r12`.
- `0xA8188`: argument **23**; `0xA818C`: same getter returns belt tier.
- `0xA8194..0xA8220`: belt switch 0..5, default visual tier 6.
  Per-tier grade capacity / subtracted cumulative count:
  **(3,0), (6,3), (8,9), (10,17), (11,27), (25,38)**.
- `0xA8410..0xA843C` prints `"belt col: "` and `"     belt max: "`,
  with literal **63**. This supports interpreting index 19 as the earned
  grade/rank total, not currency. It does not independently prove award/save
  side effects or all legal combinations.
- `9-95.LCS` Ninja `0x4B76A4..0x4B779C` separately reads integer 23 and
  switches 0..6 to set appearance colors.

**Feasibility:** the backing integers are located, but grade count and belt
are distinct state. Changing one is not demonstrated to recalculate the
other, unlock missions, replay award notifications, or refresh the Ninja's
current appearance. Do not silently derive/update belt from the count.
No verified belt-refresh event or award routine was established here.

### Sword

`9-95.LCS`:

- Ninja `0x4B8670..0x4B8704`: reads integer **17**, switches 0..5 and
  selects `global[97]` model references.
- Ninja event **35** at `0x4B8708` calls this model-selection subroutine.
- NinjaSword `0x4C347C..0x4C356C`: reads integer 17 and selects tier colors.
- NinjaSword `0x4C4008..0x4C4020`: reads integer 17, converts to float
  (`native 1/1`), calculates **`r8 = 1 + 0.5*tier`**. This is a concrete
  equipment-dependent scale; this pass did not trace every damage application.

Crucially, the game already has a sword-edit path in `9-96.LCS`,
`PauseMenu_Cheats`:

```text
A+0xA9418  r31 = 17
A+0xA941C  r32 = 5
A+0xA9420  r34 = 17
A+0xA9424  r34 = native 1/0x3E(...)
A+0xA942C  r33 = r34 + r30
A+0xA9430  native 1/0x16(&r32)
A+0xA9438  native 0/0xB0(&r31)    ; setter arguments index 17, result in r32
A+0xA9440  r31 = "Ninja"
A+0xA9448  r32 = 35
A+0xA944C  native 0/0x11D(&r31)
A+0xA9454  r31 = "NinjaSword"
A+0xA945C  r32 = 54
A+0xA9460  native 0/0x11D(&r31)
```

`0/0xB0` table entry `0x4C8130` is `0x414F40` (also an incoming data
reference observed by REA instruction Evidence
`ev_c579163feac2d27e127cacd3a25f2c67ca5abaed55b9dbe8e3f500969e6768c5`).
NinjaSword event **54**, `A+0x4C3F9C`, calls `0x4C3FEC`, whose body includes
the tier-dependent scale calculation above.

**Candidate implementation:** integer 17 / `P+0x78`, range **0..5**, followed
by the same two refresh notifications on the normal script/engine thread.
This is stronger than freezing the raw field: existing actors cache derived
equipment state. Native `1/0x16` and notification delivery were not fully
traced in REA, so this is a confirmed *script recipe*, not a ready-to-call
C++ ABI or runtime-tested trainer implementation.

## Native movement: useful boundaries, no fabricated controls

`9-95.LCS`, Ninja class descriptor `A+0x4D15C0`, entry `0x4BA6F0`,
code block `0x4B5F20`:

- `0x4B871C..0x4B8748` reads position property 8, scales `r10/r11/r12`,
  subtracts another vector and writes position property 8. Native property
  getter `0x421680` and setter `0x422E20` map property 8 to the strat's
  `+0x160/+0x164/+0x168` vector (REA
  `ev_a0ee524f29d25bce39cd84a8f9e01f06349cc0dccede42cac1bd00817621b0f9`).
  These are changing movement values, **not demonstrated speed settings**.
- `"Speed "` diagnostic at `0x4BBA84` passes `r10/r12` to native `1/0x2E`
  at `0x4BBA9C`. It is not a named writable multiplier.
- One jump-like animation/impulse path at `0x4B8AA4..0x4B8ADC` selects
  animation 117, scales horizontal values by 0.85 and computes
  `r11 = old_r11*0.85 + r92*r117`.
- Its called subroutine `0x4B820C..0x4B826C` branches on `r94/r81`,
  selecting float constants **0.19 / 0.20 / 0.22**, and incorporates the
  `r10/r12` result and **0.1725**. These are path-dependent calculations,
  not a proven height/cooldown control.
- Numerous other writes to `r11` exist: e.g. `0x4BDC30` sets 0.15,
  `0x4BE000` sets 0.10, `0x4C01D8` sets 0.25, and `0x4C09AC` sets 0.16.
  Their complete state/input/landing semantics were not resolved.

**Do not implement** speed/jump-height/cooldown sliders from these registers,
constants, profile neighbors, or the separate HL controller's tuning values.
Next work needs the selected native locomotion/jump state, all producers of
its impulse/gravity/input latch, and validation across ordinary/double/wall/
traversal jumps. No movement write is approved by this evidence.

## Limits / handoff

Static observations establish storage and script consumers, not runtime
acceptance, persistence, safety, or UI/model refresh timing. No save file was
changed. The packed-script rows above are reproducible byte inspection, not
REA runtime observations. Native Evidence includes provider boundary and
indirect-reference limitations; its string inventory was not treated as an
exhaustive search of packed content. Equipment and ranks are implementation
candidates; movement and belt-refresh behavior remain explicitly unresolved.
