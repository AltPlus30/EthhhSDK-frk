# Wavy sash: separate NinjaHead-owned ribbon

2026-11-20. Static executable/packed-script inspection and synthetic x86 tests
only. No game launch, attachment, installation or installed-file modification.
No REA session was opened; the other investigator's session was not touched.

## Finding

The moving head sash is **not another named entry of Ninja's body model**.
`NinjaHead` creates a separate ribbon/effect allocation. Its own belt-color helper
tints both named head entries **and this effect**. The normal Ninja model refresh
passes zero as its optional effect handle; copying that body-only recipe omits
the moving section.

The verified allocation is owned by `NinjaHead`, not directly by `Ninja`:

| Value | Location |
|---|---|
| Exact selected Ninja generation | existing strat identity/registry validation |
| Head's creator full strat handle | `NinjaHead+0x238` |
| Head VM register block | pointer at `NinjaHead+0x1B4` |
| Ribbon full effects handle | head register **2**, block+8 |
| Effects registry slot | `0x519A60 + (handle & 0x1FF)*8` |
| Slot fields | full handle at +0, object pointer at +4 |
| Ribbon vtable | rebased `0x4C8CF8` |
| Ribbon creator | object+`0x0C`, raw **NinjaHead pointer** |
| Ribbon self full handle | object+`0x10` |
| Ribbon packed color | object+`0x2898` |

Never use the strat registry `0x51019C` to resolve the ribbon. Neither the
creator pointer alone nor a handle's low nine bits establishes current ownership.
The editor must still tint `sash white` and `sash knot white` on both validated
body/head instances for a full belt+sash control.

## Integration API

New files: `src/core/cosmetic_sash.h/.cpp`. No editor, runtime, Lua or module
files were edited by this investigation.

```cpp
#include "cosmetic_sash.h"

// Preferred hot path: reuse the editor's CURRENT validated root/LinkedHeads
// from its complete live-snapshot evidence. Do not supply stale cached heads.
auto result = eth::cosmetic::sash::ApplyKnown(ninjaIdentity, packedRGB, heads);

// On release, removal of the belt group, disable or selected-player change,
// while the old world is still valid, on that same engine thread:
eth::cosmetic::sash::RestoreKnown(ninjaIdentity, heads);

// Mandatory at level/session teardown or loss of world validity:
eth::cosmetic::sash::Invalidate(); // forget only; no stores/native calls
```

`ApplyKnown` is immediate **engine-thread work**, not a UI-thread queue. Queue it
through the editor's existing request mechanism and call outside VM execution
and rendering, after native ticks. Keep one active editor owner. Reissue while
the belt group is enabled to detect ribbon recreation and ordinary tier refresh.
Do not infer success from the body material alone: report `Waiting` when the
exact linked ribbon is absent/unreadable/ambiguous; `Refused` means unsupported
image/native signature or invalid RGB/native failure. RGB is
`R | G<<8 | B<<16`, the same input the shipped belt helper uses.

The hot path performs **zero `SnapshotStrats` / `ReadStratIdentity` calls**.
The caller supplies fresh complete live-list membership evidence; the module
independently rechecks both root/head full strat registry handles and resolved
addresses, self handles, class pointers, exact class names (`Ninja`/`NinjaHead`),
roles, head ownership metadata and the actual raw owner handle. It then checks
head register 2, full effects-slot agreement, exact vtable, exact creator pointer
and effect self-handle. Supplied lists are bounded to 16 heads. More than one
matching linked ribbon is refused. Identity/effect reads use small direct SEH
copies, not per-field VirtualQuery/full identity or live-list traversals.
The supported-image/native signature gate retains three bounded `SafeRead`
operations; no game memory pointers or identity validity are cached to avoid them.
No
global resource/model/texture writes, registry edits, effect allocation/freeing,
or model rebinds occur.

`Apply(ninjaIdentity,packedRGB)` and `Restore()` remain standalone audit
fallbacks that take their own complete snapshot/full identities. Rare release
may use `Restore()` when fresh head evidence is unavailable and the world is
still valid. The hot API **never implicitly falls back to this scan**, even on
root change: restore the previous selection using its fresh heads before
switching roots, or intentionally invalidate/forget its baseline.

The native setter is called only for a changed color. Restoration snapshots the
exact original dword, including alpha (the setter always forces alpha FF).
Restore revalidates the same Ninja/head/register-block/effect generations and
only writes if our last color is still present. A game tier refresh observed
before reapplication becomes the new restoration baseline. Another writer's
different current color is never overwritten on release. Both restore entry
points use one direct SEH-guarded expected-color comparison and data store,
**never `SafeWriteBytes`, VirtualProtect or writable-code/resource patches**.
Deleted/replaced/
reparented objects are deliberately not restored. `Invalidate` is mandatory:
generation checks alone cannot exclude identity reuse across whole-world resets.

The `.cpp` joins the existing `src/core/*.cpp` CMake glob after regeneration.
The main integration owner must wire the calls above and add the new standalone
test to their build/test targets; this worker does not own `CMakeLists.txt`.

## Reproducible static chain

All native coordinates are preferred-image VAs, rebased before runtime use.
All **A+** coordinates below are offsets in decompressed `Data/09.vol` member
`9-95.LCS`, never callable addresses.

Artifact identity:

- EXE: `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`
- archive: `0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669`
- member: `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65`
- A pool: `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd`

1. Exact `NinjaHead` class A+`4D1778`, code A+`4C4D40`, entry A+`4C5254`
   (head owner attribution is independently documented in
   [head/blood research](cosmetic-head-blood-research.md)).
2. Helper A+`4C4ECC` attaches to the owner node via native `0/0x106`.
   A+`4C4F44..4C4F64` stages `[3,10,A+1C5020,scaled-width]` and invokes
   **1/0xBE**, destination register 10. A+`4C4F70` copies result to register 11.
3. Entry A+`4C527C..4C5284` calls that helper and copies register 11 into
   persistent register **2**. The parallel reinitialization path
   A+`4C51CC..4C51D4` does the same. A+`4C5188..4C518C` submits register 2 to
   native **0/0x100** for deletion.
4. A+`4C51D8..4C51F0`, `4C51F8..4C5210` and `4C5234..4C524C` stage the two
   sash material names, copy register 2 to register 11 and call the head's
   color helper A+`4C4D48`.
5. That helper packs the belt-tier RGB through **1/0x33**, tints the two named
   entries through **0/0x184**, checks register 11 through **1/0x10A** at
   A+`4C4E84..4C4E88`, then submits `[r11,r10]` to **0/0x1D8** at
   A+`4C4E90..4C4E9C`. This positively connects the created effect to the same
   sash-color operation; it is not an inference from object adjacency.
6. Native **1/0xBE** table entry is `0x41D0C0`. It calls manager
   `0x42B6D0` with the invoking strat and reads the result object's +`0x10`
   handle. The manager allocates `0x295C` bytes and calls `0x4275F0`.
   That constructor calls base `0x41D3E0`, then installs vtable `0x4C8CF8`.
7. Base `0x41D3E0` writes creator pointer at +`0x0C`, registers the allocation
   through `0x41D360`, and writes its returned full handle at +`0x10`.
   `0x41D410` clears both registry slot words and self handle on destruction.
   Ribbon destructor `0x45B290` calls this unregister path at `0x45B30F`.
8. **1/0x10A** -> `0x41EEA0` checks the full effects registry handle and returns
   its object pointer. **0/0x1D8** -> `0x41DB90` resolves the same registry,
   then calls **thiscall `0x45A900(object,packedRGB)`**, callee pops four bytes.
   The setter does only `object->color2898 = packedRGB | 0xFF000000`.
   The script wrapper itself can call the setter with null on stale input;
   the SDK intentionally does not invoke that unsafe wrapper.
9. Ribbon setup `0x45C613..0x45C623` initializes this color from its model data.
   Geometry generation at `0x45B4F8..0x45B50F` reads its low 24 bits and combines
   them with computed alpha before storing into the generated mesh entry+`0x14`.
   Therefore the private field feeds the moving geometry; no shared mesh tint
   needs to be patched. **Timing correction:** this copy is reached during render
   flush, not merely the next native simulation tick; see below.

VM block layout at strat+`0x1B4`, four bytes per register, is also established
by the existing [mission timer interpreter research](mission-timer.md).

Native bytes were decoded offline with installed pefile and Capstone 5.0.7,
not executed and not assigned new REA Evidence IDs. Reproduce selected ranges:

```powershell
python -B tools/inspect-cosmetic-research.py --range 0x4c4d40 0x4c5340
python -B tools/inspect-cosmetic-research.py --native 0x41d0c0 0x40
python -B tools/inspect-cosmetic-research.py --native 0x42b6d0 0x75
python -B tools/inspect-cosmetic-research.py --native 0x4275f0 0x260
python -B tools/inspect-cosmetic-research.py --native 0x41d3e0 0x60
python -B tools/inspect-cosmetic-research.py --native 0x41eea0 0x30
python -B tools/inspect-cosmetic-research.py --native 0x41db90 0x38
python -B tools/inspect-cosmetic-research.py --native 0x45a900 0x12
python -B tools/inspect-cosmetic-research.py --native 0x45b290 0x90
python -B tools/inspect-cosmetic-research.py --native 0x45b4f8 0x20
python -B tools/inspect-cosmetic-research.py --native 0x45c613 0x16
```

## Post-tick timing audit: no additional hook indicated

Follow-up static inspection specifically checked whether normal head script
execution resets the color every tick before simulation, preventing a post-tick
field override from ever reaching generated geometry.

**Result:** the ordinary head loop does not unconditionally tint every tick.
More importantly, the mesh consumes the field at **render flush**, after the
post-tick override. Keep the integrated `ApplyKnown` call post-native-tick;
no setter interception, new MinHook detour, draw-scoped field mutation, or
public API change is justified by this bounded path.

### Head script control flow

The exact class record A+`4D1778` declares four events; its table at
A+`4D13D0..4D13EF` is:

| Event type / ID | Handler A offset | Relevant effect |
|---|---:|---|
| 8 / `0x44E` | `4C5188` | deletes register-2 effect |
| 8 / `0x36` (54) | `4C519C` | deletes/recreates effect, then colors at `4C51D8..4C51EC` |
| 8 / `0x8C` (140) | `4C51F8` | colors existing effect at `4C51F8..4C520C` |
| 8 / `0x41` (65) | `4C5218` | sets r3=0; no direct color call |

Thus A+`4C51D8` and A+`4C51F8` are **event-handler bodies**, not sequential
fallthrough of the main loop. Their terminators at A+`4C51F4`, `4C5214` and
`4C521C` are opcode `0x90`, whose interpreter handler at `0x42619E` unwinds the
event execution/return stack.

The main entry initializes remembered descriptor **r0=0** at A+`4C5288`,
selects requested descriptor **r35** at A+`4C529C`, then calls helper
**A+`4C5220`** at A+`4C52A0`. The loop begins A+`4C52D8`; its two state branches
call this same helper at A+`4C52F4` / `4C5304`. The loop's yield at
A+`4C535C` and backward jump at A+`4C5360` return to A+`4C52D8`.

Crucially, the refresh helper is:

```text
A+4C5220  C8FF0023 00000028    if r0 == r35, branch to A+4C5250
A+4C5228  0A000023             r0 = r35
A+4C522C  80FF2300 00290012    native 0/29 with requested descriptor
A+4C5234..4C5244               stage sash names and register-2 effect handle
A+4C5248  7EFF0000 FFFFFAF8    call belt color helper A+4C4D48
A+4C5250  88000000             return
```

This is not a guessed branch meaning: native interpreter dispatch table
`0x426370 + 0xC8*4` points to `0x425B1C`, whose decoded instructions compare
the two registers and apply the inline byte displacement **only on equality**.
`0x88` dispatches to ordinary helper return at `0x426182`. Opcode `0x78`
handler `0x425FAF` uses signed dword-relative displacement; `0x47` handler
`0x426315` stores a wait count and exits the interpreter.

Consequently initial binding and **descriptor changes** execute this helper's
color operation; subsequent unchanged-descriptor ticks skip it. The bounded
head code's direct relative calls to the actual color helper A+`4C4D48` occur
only at A+`4C51EC`, `4C520C`, `4C5248`. This was an aligned-dword relative-call
search with these three control-flow contexts inspected, not a whole-game CFG.

**Frequency boundary:** this establishes the head's own loop and declared
event bodies. It does not prove every possible external sender's event frequency.
For example, Ninja stages event 54 for its head handle at A+`4B6540..4B654C`
and A+`4C0378..4C0384`; the complete conditions of all external event senders
were not reconstructed. Even a refresh in a native tick precedes the post-tick
apply and the render-time color copy described next.

### Native ordering and actual RGB consumption

`AppTick` at `0x43E0A0` calls strat manager update `0x4246F0` at `0x43E10F`,
then effects manager update `0x42B580` at `0x43E119`. The latter walks the effect
list and invokes vtable+4. For this exact ribbon vtable `0x4C8CF8`, that method
is `0x427850`, performing simulation and tail-calling `0x427130`.

However, **the RGB-consuming builder is a different function, `0x45B330`:**

```text
render flush 0x44A340
  0x44A353 -> 0x449C00       walk effects list at global 0x51AA64
    0x449C52..0x449C67       check effect's vtable+8 type identity
    0x449C6B -> 0x45B330     build this ribbon's render geometry
      0x45B4F8              read object+0x2898
      0x45B507              preserve low 24 RGB bits
      0x45B50D..0x45B50F    combine alpha; store generated mesh+0x14
```

The type method in this ribbon's vtable is `0x427810`, returning the exact
metadata pointer `0x4C8D04` that `0x449C54` compares. A bounded search of
executable sections found the direct call to `0x45B330` at `0x449C6B`, not in
the ribbon simulation method. The same search found direct setter calls only
at `0x41DBB4` / `0x41DBC2`, within the known script tint wrapper. Separately,
the field-reference scan found constructor initialization (`0x427627`),
setup (`0x45C623`), setter (`0x45A909`) and render-builder read (`0x45B4F8`).
These byte-pattern searches do not exclude arbitrary indirect calls or other
code that computes an equivalent field address.

Therefore the demonstrated sequence is:

**native tick (including any queued head refresh) → SDK post-tick ApplyKnown →
native render flush builds ribbon using the overridden field.**

There is no demonstrated “reset every tick before the only mesh copy” trap.
Multiple simulation ticks before a rendered frame are also compatible when
the integrated callback reapplies after each tick. This does not establish
behavior of third-party callbacks writing the field between apply and flush,
or guarantee that a given frame actually draws this effect.

Additional read-only reproduction:

```powershell
python -B tools/inspect-cosmetic-research.py --range 0x4d13d0 0x4d13f0
python -B tools/inspect-cosmetic-research.py --range 0x4c5188 0x4c536c
python -B tools/inspect-cosmetic-research.py --native 0x425b1c 0x21
python -B tools/inspect-cosmetic-research.py --native 0x426182 0x47
python -B tools/inspect-cosmetic-research.py --native 0x43e100 0x1e
python -B tools/inspect-cosmetic-research.py --native 0x42b580 0x20
python -B tools/inspect-cosmetic-research.py --native 0x427850 0x98
python -B tools/inspect-cosmetic-research.py --native 0x44a340 0x20
python -B tools/inspect-cosmetic-research.py --native 0x449c00 0x7b
```

## Validation and limits

`tests/CosmeticSashTests.cpp` includes the production implementation with
synthetic Win32 identities, effects slots, register block and a native-setter
stub. Compiled with MSVC x86 `/std:c++17 /EHa /W4`; passed:

- full native RGB apply, no redundant native call, color changes;
- exact original alpha/RGB restore, idempotent release;
- new normal tier baseline and another writer's changes;
- stale same-slot effects generation, wrong self handle/creator/vtable;
- stale owner generation, wrong class, incomplete list, ambiguous head;
- unreadable registers, null effect, stale Ninja/head generation, missing world;
- no stale restores on generation change/reparent/teardown;
- unsupported executable/signature and out-of-range RGB refusal.
- hot apply/reapply/restore with zero snapshot/full-identity calls and zero
  `SafeWriteBytes` calls, including changed-root and failed-restore branches;
- independent hot-path root/head strat generation, address, self, class, role
  and ownership checks, plus an actual inaccessible-register SEH fault;
- hot-path new game-tier baseline, other-writer preservation, duplicate/absent
  heads, stale effects generation and teardown.

These checks do not observe live rendering. The follow-up timing claims above
come from static script/interpreter/render-call inspection, not those synthetic
tests or live frame observation. Runtime visuals, presentation latency,
all alternate costumes/levels and editor integration remain untested. This
bounded attribution covers the normal `9-95.LCS` NinjaHead path, not arbitrary
cloth/effects or every game's script resource variant.
