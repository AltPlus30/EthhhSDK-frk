# Shared cut-glow recolor and rollback (v0.5)

## Scope approved by the user

On 2026-11-22 the user approved recoloring **every supported use of the exact
shared green glow asset**, even if a non-enemy effect reuses it, provided there
is a revert plan. This replaces the earlier proposed strict live-ancestor
policy. It is not authorization to recolor arbitrary green artwork, skin,
armor, or orange sparks.

The supplied screenshots in `D:\AI\opencode\temp\ininja-glow-insides` show the
anatomical cut surface and the separate green rays/orange sparks. Those images
are visual observations, not runtime call traces.

Green glow original:
`fbde93538ad88d9648c1994fb3c78b43f448a2ef94e745ee9fa31c0619f77185`,
64x64 indexed BGRA TGA, 5138 bytes. The orange flare hash `fa4baac8...` is **not**
admitted. The separate interior atlas `b940a1d4...` uses the ordinary blood
binder, not this path.

## Complete texture + vertex path

`src/core/cosmetic_glow.cpp` handles both baked green texture RGB and baked
vertex diffuse. Merely replacing the texture would multiply red by green.

1. Native mesh setup `4545D0`, no-argument thiscall, observes a bounded candidate
   key and always runs its original once. It does no hashing, allocation or
   upload. Only FVF `142`, 24-byte XYZ/diffuse/UV, no bone-node assignment,
   neutral material RGB, one-stage animated parents with an ordinary child,
   and the supported native buffer layouts qualify.
2. Post-native-tick cold work verifies the **full exact TGA digest**, validates
   triangle-strip ranges/absolute uint16 indices, and copies original CPU
   vertices/indices into SDK-owned proof storage. Source sizes/counts are bounded.
   It builds managed private streams and a private RGB-baked texture.
3. Only referenced vertices' RGB is neutralized; positions, UVs, per-vertex
   alpha, indices, and unreferenced vertices remain unchanged. Native CPU
   arrays, meshes, descriptors and native GPU buffers are never edited.
4. Native primitive `453E60`, no-argument thiscall, only submits
   `DrawIndexedPrimitive`: BaseVertexIndex=0, min vertex at +20, start index +24,
   type/counts at +0/+4/+8. It does **not** rebind the stream. Wrapping this
   exact call therefore avoids material-cache leakage.
5. Immediately before that draw, actual device stream/index/texture handles,
   offset/stride, FVF, absence of custom shaders and supported fixed-function
   color operations must match. Private stream/texture are borrowed only around
   the one native primitive continuation, then **both exact original bindings
   are restored in `__finally`**, including native SEH. Get-returned COM
   references are balanced.
6. Multiply uses neutral diffuse and RGB-baked texture (one color factor).
   Linear add/alpha lerp uses RGB-baked diffuse and texture (both terms share
   the hue); texture-only/diffuse-only selection is supported. Unknown nonlinear,
   constant/complement or custom-shader recipes stay native. Alpha operations,
   blend state, transforms, material state and depth state remain native.
7. A restoration failure latches the glow path off until explicit reconfiguration.
   It never retries the native draw. Nested/unknown mesh scopes cannot inherit
   another mesh's draw admission.

Native ABI is from the supported executable SHA
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Relevant retained REA Evidence and geometry/source provenance are listed in
`cosmetic-blood-coverage-v05.md`. Reproduce native boundaries with:

```
python -B tools/inspect-cosmetic-research.py --native 0x453e60 0x2e
python -B tools/inspect-cosmetic-research.py --native 0x454d00 0x36
python -B tools/inspect-cosmetic-research.py --native 0x4545d0 0x138
```

## Performance and lifetime

There are 32 candidate slots and at most four cold preparation attempts per
native tick. Negative attempts back off 30 ticks. Inactive glow candidates
retire after 120 ticks. First-ever/cold effects can draw natively until the next
safe tick. Vertex buffers are limited to 512 KiB and index sources to 128 KiB.

Every native tick suspends drawing; fresh metadata and **all original source,
vertex and index bytes** must match the SDK proof before reuse. Warm proof uses
guarded copies/comparison, not crypto or OS memory queries. Opacity, UV animation,
reference counts and primitive bounds are left native and do not force uploads.
Device reset/change drops prepared GPU state. Reentrant reconfiguration waits
until scopes/preparation finish. Parallel resource destruction by other mods is
unsupported.

These are supported fixed-function **mesh** uses of this image, not a claim to
inventory every possible particle, custom shader or future asset variant.

## Verification

`eth_cosmetic_glow_tests.exe build/blood-v05-split-verified` uses production
code and fake Win32 COM resources. Checks include:

- exact art admission, source mutation refusal, RGB and original alpha;
- original geometry/UVs/indices/native GPU data unchanged;
- multiply versus additive color inputs;
- original stream/texture restored and COM references balanced;
- exactly-once continuations, real `RaiseException` unwind, unknown-recipe
  fallback, creation/lock/bind failures and restore-failure latching;
- 10,000 warmed draws without allocations, source reads or uploads;
- 1,000 native tick/draw cycles with animated opacity/UV/refcounts/bounds and
  no repeated allocations, SHA or uploads;
- independently disabling glow restores native draws while leaving other blood
  channels available.

No game launch, attachment, installation, live GPU timing or visual acceptance
was performed. The new F9 counters show glow draws/refusals/uploads and the
last observed color operation/arguments for live checking.

## Revert plan

1. **Glow only:** click **Restore original glow only**, or uncheck
   **Include shared green cut glow**. Blood, pools and split-interior colors
   stay armed. Native resources were never modified, so no repair pass is needed.
2. **All colors:** Stop/restore or disable Cosmetic Editor. Costume material
   restoration still needs its normal next native tick.
3. **Whole update:** close the game manually, then copy the backed-up DLL and
   CosmeticEditor payload over their installed counterparts. Do not overwrite
   saves or unrelated SDK configuration.

Read-only snapshots created before installing anything:

| Payload | Folder | DLL SHA256 |
|---|---|---|
| User's installed v0.4 | `build/rollback/installed-before-v05-20261122-031038` | `9352D52DAAF972EEA1AEBF54E62C4783FC98A2510F4973E22E8325D739865A92` |
| New performance/pools/interior build, before glow | `build/rollback/CosmeticEditor-pre-glow-20261122-024613` | `68118F62F59E8BCECB888751C26B6CEAE3FC53BB07B6274880CFEB471F09C5AF` |

Each contains `EthhhSDK.dll` and `CosmeticEditor/`. The original installed game
files were only read for backup, never changed.
