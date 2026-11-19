# Native movement tuning

2026-11-19. The initial investigation used static artifact evidence and synthetic
production-code tests, without launching the game or changing installed files.
**Follow-up:** the user confirmed the prior movement/equipment controls work
well in-game. The enable toggle has been removed, but Apply is still explicit.
The newly expanded factors above 2× remain unverified live; no measured apex
claim is made.

## What is implemented

`src/core/trainer_movement.{h,cpp}` supplies two session-only, default-off
requests for the existing native Ninja interpreter:

* **Native locomotion factor**: scales six input/state-dependent speed-cap
  literals and six horizontal acceleration literals in the shared locomotion
  recipe. It does not multiply world-position deltas.
* **Base jump impulse factor**: changes the three base impulses in the shared
  jump helper, accounting for its fixed `-0.1` offset. This is **not a jump-apex
  multiplier**, cooldown removal, gravity change, or universal jump override.

Both factors must be finite in **[0.5, 5]**; `(1,1)` releases the request.
The previous 2× cap was editor safety policy, not an architectural constraint.
The 5× cap is policy too; the UI warns above 2× about untested high-factor
collision, camera and out-of-bounds behavior.
Only one nonempty owner can request tuning; another owner's request is refused,
not multiplied or silently substituted. Changes are always calculated from
checked original literals, never from previously scaled data.

The new files install no hook, write no profile/save data, and do not use the
HL controller. Native timing, collision calls, support corrections, friction,
gravity and specialized movement recipes remain unmodified.

## Integration

`mission_timer.cpp`'s existing `0x424BA0` detour owns executable
hash/prologue validation. The module is integrated with Lua/UI bindings and CMake.
The two movement calls surround
the **same** original VM invocation, with `AfterVm` in its `__finally`:

```cpp
trainer_movement::VmPatch movement; // count starts at zero; unused words need no initialization
if (trainer_movement::Active())
    movement = trainer_movement::BeforeVm(
        reinterpret_cast<uintptr_t>(self), freshValidatedExactPlayerPin,
        hlCurrentlyOwnsMovement);
__try {
    if (original) original(self);
}
__finally {
    trainer_movement::AfterVm(movement);
    // Existing mission-timer restoration also remains here.
}
```

The pin comes from `PlayerTracker::IsPinnedVmNinja`'s current validated explicit
player-pin consent, class/self-handle generation and manager registry, not a
class-name search or a saved heap address. `BeforeVm` additionally requires
`self == pin`, exact class `"Ninja"`, the expected entry/code relationship and
all listed fingerprints. The detour yields while native keys or the camera
are externally owned. Normal invocations of other Ninja clones are not patched.
The detour checks `Active()` before resolving pin/ownership so the disabled hot path is just
an atomic load and branch, with no class lookup, system call or allocation.

Only integrate this into the already hash-verified native VM hook for executable
SHA-256 `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
The standalone movement module does not rehash the executable per invocation.

The UI exposes explicit Apply/Stop without an experimental enable toggle; editing drafts or loading config
must not activate requests. On mod unload/error-disable call `Release(owner)`;
on SDK shutdown call `Shutdown()`. Synchronize hook shutdown with the engine
thread; each in-flight token still needs its `AfterVm`. A request released
during a VM call does not erase that call's restoration record.

Every patch address is local to one VM invocation and is discarded on exit.
All changed words are checked writable before the first store. No
`VirtualProtect`, executable patch, or installed-file write is used.
Partial-write failure rolls back already-written operands. Restoration is
compare-before-write and does not overwrite a foreign intervening change.
An unreadable/freed word cannot safely be restored; SEH refuses access instead
of retrying against a saved heap pointer on a later frame.

## Shipped-artifact evidence

This extends, rather than retroactively changes, the earlier unresolved
`trainer-movement-equipment-research.md`.

Read-only unpack of `Data/09.vol`, member `9-95.LCS`, reproduced:

* Member SHA-256:
  `390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65`.
* Decompressed A SHA-256:
  `68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd`.
* Ninja descriptor `A+4D15C0`, entry `A+4BA6F0`, unit `A+4B5F20`.
  Header words `20,34C8`; end `A+4C3240`.

All following script addresses are **package A offsets, not executable VAs**.
Production stores offsets relative to the freshly resolved class code pointer.
The exact 15 op/literal pairs were checked against this decoded artifact;
independent synthetic fixture copies are in `tests/TrainerMovementTests.cpp`.
Native link pointer words are deliberately not compared to package link IDs:
the loader rewrites them to process addresses.

### Locomotion

`4BCA24` calls `4B92B0`; within that recipe:

1. `4B92B0` calls `4B9040` for horizontal acceleration.
2. `4B92DC..4B9320` combines state/input `global[32]` with those results
   through interpolation helper `4B8CF4`, then scales the horizontal direction.
3. `4B9364..4B937C` multiplies by native time scale property `0x33` and adds
   the result to `r10/r12` (with separate vertical calculation).
4. `4B9388` calls `4B8F04`; `4B9394..4B93D8` uses its input-scaled result
   when limiting horizontal magnitude. `4B93EC..4B9470` handles braking.

`4B8CF4` returns `r117 + r118*(r116-r117)` (or `r117` for zero input);
scaling both endpoint literals scales that recipe's result.

| Helper | Literal A offsets | Original values |
|---|---|---|
| cap `4B8F04` | `4B8F50,4B8F58` | `.115,.69` |
| cap | `4B8FC0,4B8FC8` | `.01725,.0345` |
| cap | `4B9020,4B9028` | `.115,.23` |
| acceleration `4B9040` | `4B9054,4B905C` | `.0048,.0036` |
| acceleration | `4B9090,4B9098` | `.096,.072` |
| acceleration | `4B90BC,4B90C4` | `.024,.018` |

The helpers branch on `r94 == 15`, and `r94 == 16` with `r81 <= 0`.
This pass does not attach unproved gameplay names to those states.
The existing horizontal magnitude native `1/2E` resolves to VA `41FB10`;
offline Capstone shows two input floats squared/added, `fsqrt`, and output store.
No Ghidra function body was discovered for that tiny wrapper.

### Jump helper, not arbitrary r11 writes

`4B820C` selects `.19/.20/.22` at `4B8220/4B8234/4B8240` using the same
state branches, subtracts a speed-dependent correction, adds native property
`A8`, then subtracts `.1` at `4B8280`.

In pseudocode, its returned `r117` is:

```
base - clamp(0, (horizontalMagnitude(r10,r12) - .1725) * .25, .08)
     + property[A8] - .1
```

The implementation replaces only `base` with
`.1 + (base-.1)*jumpFactor`. Thus the nominal net base impulse scales, but
speed/support corrections and actual apex do not scale exactly.

At `4B8AA4` the script selects animation **117**, damps horizontal motion by
`.85`, calls the helper at `4B8ACC`, then computes
`r11 = old_r11*.85 + r92*r117` at `4B8ADC`.
Another animation-117 launch calls it at `4B8BA0` and assigns `r11=r117`
at `4B8BA8`. This establishes a meaningful launch consumer, unlike merely
selecting one of the many unrelated writes to `r11`.

A bounded aligned call-pattern scan within the Ninja unit found seven helper
call sites: `4B8ACC,4B8BA0,4BEAE0,4BF3E4,4C0A88,4C135C,4C1C50`.
The helper is shared by more than the two animation-117 recipes. Some other
special jumps bypass it entirely. Ordinary/double/wall/traversal behavior is
not exhaustively classified or runtime-validated.

### Native REA evidence and equipment assist

REA/Ghidra 12.1.4, same admitted executable SHA-256:

* VM decompilation and ASL float-literal/register operation semantics:
  `ev_be30a226bce7f088d32d44165e378f898922efa022cad840df79e18febbb71c9`.
* Native clamp wrapper `420010` (`1/38`):
  `ev_9e10c99ffa6f3faf34efa43a8cf2cccfbbe777b84f6a458c9006384babb6003f`.
  This also records the unavailable tiny magnitude-wrapper decompilation.
* Event broadcast `423560`:
  `ev_5f9abcb1d11ec2f292f06cd288f7c5dd39614c02d5b8b0c17bb48f01c1a86166`.
* Per-target event lookup/queue `4130B0`:
  `ev_f0b81a1f6ed120f5551fdcd80a9e6837865974d3eac3f158287d6bb11efdc5c7`.
* Event node allocation/copy `4235E0`:
  `ev_833044a933fafcd9bd70a7d708187090760d4b9fbd87a5a38adc0d9045d88716`.

* Cosmetic copy helper `457BF0` (additional parent research assist):
  `ev_404e144edab890913ad4311351c1a6e633045a9bc8729e01a68b9c86d80a32e4`.
  The loop visits render-instance slots `+128/+12C`, clones with `454240`
  and releases with `454150`. Its decompiled ownership-bit test references
  the first slot; do not infer independently checked flags for both slots.
  Tiny tint wrapper `458330` has no discovered Ghidra function body.

Complete six-record REA bundle retained in
[`trainer-movement-experimental-evidence.json`](trainer-movement-experimental-evidence.json)
(205325 bytes), copied from the original temporary export without deleting it.
The native analysis session was then closed. Evidence is static,
and decompiler output is not original source or runtime observation.

Equipment wrapper `417380` is cdecl
`(senderStrat, args{namePtr,event})`, forwarding to thiscall
`423560(manager51019C,sender,name,event,0,0)`.
REA confirms that it walks manager `+189C`, strat-next `+198`,
case-insensitively matches class names, then calls `4130B0`.
The latter checks strat flag `+190 & 40`, `412EF0`, and type-8 event-table
entries. It queues through `4235E0`, not synchronous script execution.
The queue copies sender `+1A0` identity (or zero), event, handler offset and
payloads, not the transient name/argument pointers. Its manager `+1894`
free-list allocation has no null check; do not spam it or call from a UI thread.
No equipment implementation is owned by these movement files.

## Validation and remaining risks

Standalone Win32/MSVC `/std:c++17 /EHsc /O2` build and execution of
`TrainerMovementTests.cpp` passed. Tests invoke production logic on synthetic
strat/class/script objects: factors/bounds, ownership conflict, fresh pin and
HL gating, speed-only/jump-only operands, repeated/nested non-accumulation,
unknown-script refusal, read-only refusal, release while active, compare-safe
restoration, and SEH-finally restoration. These are not gameplay tests.

The class's ASL allocation is shared, like the existing timer patch. Scope is
the synchronous exact-player VM invocation, not a per-instance bytecode copy.
A native callback recursively running another Ninja with the same class
allocation during that invocation could observe the temporary literals; this
reentrancy possibility has not been ruled out by static evidence. A recursive
`BeforeVm` refuses already-patched fingerprints, which prevents cumulative
scaling but does not isolate that shared allocation. Do not advertise clone
isolation or concurrent-VM safety beyond ordinary non-nested invocations.

Restoring literals does not reset already-generated velocity or cached script
register values; native braking/landing resolves existing momentum. Specialized
states, animations and camera reactions can differ. Movement remains off until
the user deliberately applies it. The prior controls are user-accepted, but
measured live speed/apex, factors above 2×, collision edge cases and exhaustive
level-transition/special-state coverage remain unverified.
