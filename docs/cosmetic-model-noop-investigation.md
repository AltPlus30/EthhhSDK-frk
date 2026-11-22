# Cosmetic model no-op investigation

2026-11-20. Investigation only; no implementation changes, installation, launch,
attach, or new REA session. Existing modified/untracked user files were inspected
in place and preserved. Findings describe the current working tree, not an
installed DLL whose identity has not been established.

## Outcome and priorities

1. **Confirmed UI coding bug: every model combo uses the wrong index convention.**
   This affects both player and sword selection, and independently breaks built-in
   sword tier selection. Fix the UI and its inaccurate mock before drawing
   conclusions about native swapping.
2. **Donor ordering is an intentional, substantial limitation, not a queued retry.**
   A loaded donor that renders after Ninja can never replace that Ninja draw,
   even over arbitrarily many frames. Visibility alone is insufficient.
3. **Built-in sword refusal is a separate path.** Its packed script fingerprints,
   native getter relocation, binder prologue, frame increment, and primary queue
   layout match available static evidence. Do not “fix” them by comparing linked
   scriptwords against serialized words or removing safety checks.
4. **Diagnostics are insufficient to distinguish these cases.** Requests report
   queued/armed before any successful substitution; aggregate counters omit
   several early exits and all built-in binding failures. Add reason-specific
   diagnostics before broadening model support.

No static observation establishes which native guard was actually encountered
in the reported game session. There is no demonstrated single native coding bug
that necessarily makes both backends no-op on the supported executable.

## 1. Confirmed shared UI defect

`src/core/lua_imgui.cpp:364-390` implements `imgui.combo` using **zero-based**
indices. It clamps outside `[0,n-1]` to zero and returns the chosen zero-based
index as its first result.

In contrast, `mods/CosmeticEditor/mod.lua`:

- Initializes all indices to 1 (`:8`).
- Clamps donor/sword indices to `[1,#items]` (`:113`, `:124`, `:133`).
- Passes those indices straight to `ui.combo`, then uses the result directly
  as a one-based Lua array subscript (`:114-117`, `:125`, `:129`,
  `:134-137`).
- Subtracts 1 from the already-zero-based sword tier (`:126-130`).

Concrete consequences:

| User action/state | Actual result |
|---|---|
| At least two donors, initial index 1 | Combo previews the second donor, but Apply uses `donors[1]`, the first. |
| Select displayed donor index k > 0 and apply | Request uses Lua element k, i.e. the previous displayed actor. |
| Select the first donor and apply in that callback | Returned index 0 makes `donors[0].addr` a nil-index Lua error; no request is sent. |
| Select first entry, apply on a later GUI frame | Pre-combo minimum-1 clamp changes selection again (when more than one entry exists). |
| Only one sword/donor exists | Binding clamps index 1 to 0 each frame; Apply indexes element 0 and errors. |
| Select displayed built-in tier 0 | Request gets tier -1 and Lua API rejects it. |
| Select displayed built-in tier 1..5 | Request gets tier 0..4, respectively. Tier 5 cannot be requested through this UI. |

The exact sword can therefore be wrong even if the user follows the explicit
recipient instructions. Selecting the same actual tier also legitimately produces
no visible change. Lua callback errors are logged at `src/core/lua_host.cpp:115-130`;
eight accumulated errors disable the mod and release its cosmetic ownership.

**Minimal future fix:** keep UI state zero-based, clamp to `[0,#items-1]`,
index Lua arrays with `index+1`, and pass `swordTier` unchanged. Keep this local
to Cosmetic Editor; do not change the established combo binding globally.
Resolve actor identity, rather than only index, when filtered/refreshed lists
change if preserving selection is desired.

**Why tests missed it:** `tests/cosmetic-models.lua:10-12` returns the supplied
index without the real binding's clamping/selection behavior. Its `chosenTier=6`
at `:43-46` is impossible for the real six-item combo, yet is used to assert a
successful tier-5 request. Future tests must exercise real zero-based mock
semantics, all six tiers, first/last/single-item lists, filtering, and the displayed
actor address versus the actual requested address.

## 2. Donor backend: ordering and guard refusals

Source: `src/core/cosmetic_models.cpp:197-254,383-465`.

`Observe` captures only after the selected donor's ordinary native submission
returns. `Choose` substitutes only a recipient submitted **after** that capture.
`BeginFrame` and `EndFrame` discard captures every frame. Thus a recurring
recipient-before-donor ordering causes recurring waiting, not eventual success.
Off-screen, hidden, alternate-path, or second-scene-only donors are also ineligible.
The candidate list only requires a nonnull actor `+0x128` instance
(`src/core/cosmetic_model_host.cpp:147-154`); it is not an eligibility list.

Even an earlier visible donor can be refused because:

- Both actor generations must remain registered/live (`:105-117`, `:225-239`).
- Submission instance must equal actor `+0x128`, and mode `+0x144` must be zero
  (`:146-175`). Alternate-instance modes are deliberately unsupported.
- Mesh lists must have a first chain, bounded links, and safe signed node indices
  (`:125-143`). Nonzero node counts require the existing model/controller,
  allocation-header, alignment and count checks in
  `src/core/ethhhpatch_entity_interpolation.h:207-229`.
- Every copied donor matrix and the recipient root must pass affine decomposition
  (`:162-166`, `:241-242`). Shear/degeneracy can fail even when native drawing works.
- Donor total mesh links cannot exceed recipient total links (`:240`); this is
  an intentional no-extra-matrix-consumption restriction, especially restrictive
  for replacing a small sword with a complex character/prop.
- The original donor queue record must remain unchanged/pending and its instance
  frame marker must match (`:178-188`); matrix/queue capacities must fit
  (`:243-247`).

**Interpret counters:** waiting > 0 means recipient saw no captured donor at
that moment. Captured > 0 and swaps = 0 can mean donor came later, or subsequent
recipient validation failed. Refused > 0 does not identify which check failed.
All zero is ambiguous: no eligible frame, no relevant submissions, stale stats,
or only built-in sword use. Neither “armed” nor “queued” means a draw changed.

**Future solution:** initially expose actual per-frame donor/recipient submission
ordinals and eligibility, then select an earlier, compatible donor. Supporting
later donors requires a separately proven deferred/reordered submission or
resource-lifetime design. Do not retain last-frame borrowed instances as a quick
fix; the current scene-local lifetime guarantee would no longer apply.

## 3. Frame and queue audit: no demonstrated offset/off-by-one bug

`src/core/ethhhpatch_runtime.cpp:987-1003` starts cosmetics immediately before the
outer original AppRender, ends in `__finally`, and handles nested calls without
beginning/ending another cosmetic scope. `cosmetic_models.cpp:391-394` records
native frame count +1.

Offline supported-executable disassembly confirms:

- `0x43E170` loads `[0x51AB3C]`, increments EDX at `0x43E176`, and stores it at
  `0x43E17A`, before submissions. `game.h:23` uses that same frame global.
  The +1 is correct for the current hook location.
- `0x44A490` uses renderer `+0x7C8`, count `+0xAFAC`, stride `0x1C`.
  At `0x44A4DF` it writes record+4 = instance; at `0x44A535` record+0 = owner.
- `0x44A503` writes record+8 = renderer-owned matrix queue address, not the
  supplied world pointer; `0x44A537` writes record+0xC = matrix count.
- `0x44A4E7` writes instance+0x30 from that same native frame global.
- Native matrix storage is renderer `+0xDFB0`, count `+0x45F70`.
- The flush prologue at `0x44A340` matches the installed fence fingerprint.

Accordingly `Pending`/`Observe`'s four-word primary-record interpretation is
consistent with the bytes. Transparency records have a different interpretation;
the code does not use them as primary owner/instance records.

`SubmitEntity` surrounds the full continuation, not just interpolation acceptance
(`ethhhpatch_runtime.cpp:882-947`); overlay-open/interpolation-disabled fallbacks
still pass through cosmetics. `BeforeFlush` disables building at the first flush.
Existing evidence reports a later queue reset/second scene inside AppRender:
do not remove that fence to make more donors appear eligible.

## 4. Built-in sword: serialized versus linked fingerprint

Source: `src/core/cosmetic_models.cpp:256-313,315-339,468-517`;
hook installation: `src/core/cosmetic_model_host.cpp:44-77,173-181`.

Offline observations from the admitted `9-95.LCS` A pool:

```text
A+4B5F20 00000020       A+4B5F24 000034C8
A+4B8670 32740011       A+4B8674 81747400
A+4B8678 103E0028       [serialized native operand, not a runtime pointer]
```

| Tier | Literal operand | Descriptor | Descriptor word 0 |
|---:|---:|---:|---:|
| 0 | A+4B86A8 | A+4B3CC0 | A+4B2380 |
| 1 | A+4B86B8 | A+4B3CD0 | A+4B22D0 |
| 2 | A+4B86C8 | A+4B3CE0 | A+4B2220 |
| 3 | A+4B86D8 | A+4B3CF0 | A+4B2170 |
| 4 | A+4B86E8 | A+4B3D00 | A+4B20C0 |
| 5 | A+4B86F8 | A+4B3D10 | A+4B2010 |

Each literal is preceded by `5A740000` and followed by `43747061`;
each descriptor's remaining three words are zero.

**Important:** `SwordResources` expects selector[2] to equal the **linked native
getter address**, not `0x103E0028`. Existing linker evidence
`ev_5d17b125994919ec38d59b4b99877a63e2283991e0af29e0d34fee7f2d02b210`
decompiles `0x426A20`: nibble 1 selects table `0x4C87C8`, index bits 16..27 select
entry 0x3E; low 16 bits advance the relocation chain.
An offline bounded traversal of the Ninja unit's chain confirms A+4B8678 is
actually included: `(unit A+4B5F20, word 103E0028, table 1, index 62)`.
The executable's table entry at `0x4C88C0` is `0x414F80`.
The comparison at `:292-294` is therefore consistent with linked code.
Comparing runtime memory to the serialized native operand would be a regression.

Likewise serialized A offsets are not VAs. Runtime literal/descriptor relocation
and the contiguous A-pool relation still need runtime confirmation, but are not
disproved by their serialized forms. Header words match offline.

Native binder table entry `0x4C7F14` is `0x416F20`; its full 17-byte prologue
matches `InstallBind` exactly and calls `0x410C40` using the documented cdecl
wrapper. No supported-executable prologue mismatch was found.

Earlier docs' global/property distinction remains important: `43747061` uses
selector 0x70/global97, not actor `+0x2C0`. Existing VM evidence in
`docs/cosmetic-model-swap-research.md:203-254` explains this; the current native
binding implementation does not write either global97 or the real profile tier.

### Separate reasons an armed built-in sword may not visibly change

- UI index bug above selects the wrong exact NinjaSword or tier.
- Immediate apply requires current instance to be one of six canonical instances
  (`:511-514`). Private/COW or special instances can refuse.
- Nonzero mode, nonnull auxiliary `+0x2FC`, incompatible mesh indices, or unequal
  bind flags refuse (`:306-330`). Checking all six resources means even a
  nonselected tier can block selection.
- Native interception changes only requests using one of the six known
  descriptors; hidden/unrelated requests pass through (`:493-497`).
- “Attack once” is advice, not a guaranteed refresh: neither the host nor the
  model module schedules one. If gameplay does not call that native wrapper
  again for the selected exact sword, an armed-only override stays invisible.
- Built-in calls do not increment donor counters; zero donor swaps alone cannot
  diagnose this backend.

Do not force a rebind past the auxiliary/mode/canonical guards. First log the
specific refusal and whether the exact recipient ever reaches BindDetour.

## 5. Other concrete host/diagnostic defects and minimal future changes

- **Pending requests overwrite one another:** `cosmetic_model_host.cpp:107-110`
  has one global `pending`; two accepted requests before the next engine tick
  retain only the latter. Each returns true. Queue independent channels or
  explicitly report supersession; test back-to-back player/sword requests.
- **Failed requests retain ownership:** owner is assigned in `Queue`, but
  generation-cancel and generic refusal paths at `:166-195` do not release it.
  Another owner can remain blocked until Stop/unload. Distinguish ownership of
  existing valid overrides from an unsuccessful initial request.
- **Statistics are lossy/stale:** `Building` rejection and unsuccessful
  `BeginFrame` have no reason counter; stats reset per eligible render, while
  the host copies only at ticks (`:197-200`). The Lua label says “Last frame”
  (`mod.lua:97`) without identifying frame/tick/channel. A fast extra render can
  replace a useful failure sample before UI observation.
- **Generic refusals collapse independent failures:** hook installation,
  fingerprint, state and shape failures share one string (`host :195`).
  Return structured stage/reason, first failing address, expected/actual scalar
  values, recipient/donor identities and selected tier. Avoid per-draw log spam:
  aggregate per channel and log reason transitions.

These defects are established from control flow, but are not evidence that they
occurred in the user's session.

### Bounded diagnostic/acceptance order for the later fix

1. Correct combo indices/mocks. Verify displayed label/address equals request,
   all six tiers produce 0..5, single-item lists do not error. Check SDK Lua logs.
2. Show request serial, accepted/consumed outcome, exact Ninja pin and sword
   generation; confirm a native tick consumes the request (paused GUI is not one).
3. Separate hook-ready, BeginFrame rejection, Building mismatch/fence, donor
   observe, recipient choose, and successful submission counters by channel.
4. For donors record submission ordinals, mode, node/mesh counts, exact snapshot
   rejection and queue identity/capacity reason. Use an earlier compatible donor.
5. For built-ins expose selector header/word checks, relocated literal relation,
   current/canonical instance, mode/auxiliary/flags, immediate-apply outcome,
   native wrapper hits and normal-descriptor match. Confirm the selected sword
   is the visibly equipped one rather than assuming class name implies ownership.
6. Only after those observations consider new ordering/lifetime support or
   additional native refresh routes. Preserve profile, bounds and safety guards.

## Evidence provenance and limits

Read-only offline inspector:
`python -B tools/inspect-cosmetic-research.py --range START END` and
`--native VA SIZE`; bounded in-memory Python inspected the existing relocation
chain, and pefile read the two native table words. No generated inspection files
or target mutations were required.

- Target: `E:\Games\I-Ninja\I-Ninja.exe`
- EXE SHA-256: `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`
- `Data/09.vol`: `0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669`
- `9-95.LCS` member: `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65`
- Decompressed A: `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd`

Reused existing docs and the exported static evidence bundle
`C:\Users\Akira\AppData\Local\Temp\cosmetic-model-swap-evidence-2026-11-20.json`,
specifically the linker record above. Existing frame/queue evidence IDs are
listed in `docs/cosmetic-model-swap-research.md:63-107`; no new REA session was
opened or owned. Native linear disassembly establishes inspected bytes, not
runtime control-flow coverage. Packed words do not establish the live pool's
address/layout. Synthetic tests validate assumed fixtures, not installed game
behavior. Actual donor ordering, runtime guard values, equipped sword ownership,
and installed SDK/mod identity remain unresolved.
