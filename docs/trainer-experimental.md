# Movement and equipment trainer

The user confirmed the value, movement and equipment modifications work well
in-game. Sword/belt appearance can occasionally lag until the player attacks
once. This is user-reported live acceptance, not exhaustive engine validation.

Open the trainer's **Movement / equipment** section; there is no experimental
enable toggle. Drafts and movement requests are not persisted, and opening the
panel does not apply anything. **Back up saves before editing equipment or
earned ranks**: the game may save those changes, and stopping the mod does not
undo applied profile edits.

## Native speed / jump tuning

The panel includes **Native locomotion factor** and **Base jump impulse factor**,
each **0.5–5.0**, default 1.0. Editing sliders changes only a draft.
Click **Apply native movement tuning** to request the new factors.
**Stop native movement tuning**, mod
unload/error-disable or SDK shutdown releases the request. Applying `(1,1)` also
releases it. Existing momentum is left to native braking/landing.

The old 2× ceiling was a conservative editor policy, **not an architectural
limit**. The new 5× ceiling is also editor policy. Values above 2× have not yet
been live-accepted and may cause collision/tunneling, camera, animation or
out-of-bounds issues. A warning appears whenever either the draft or applied
factor exceeds 2×. Jump remains an **impulse factor, not an apex multiplier**;
increasing it can increase height much more than proportionally.

- Locomotion scales checked native-ASL speed caps and acceleration endpoints;
  it does not teleport/multiply world-position deltas.
- Jump scales the shared helper's **net base launch impulse**, not measured
  apex height. Speed/support corrections remain native; special jumps may use
  different recipes or bypass this helper. There is no jump-cooldown bypass.
- Changes are temporary for the exact pinned Ninja's VM invocation, restored
  in the existing detour's `__finally`. Unknown fingerprints are left untouched.
  No separate detour, persistent bytecode patch or profile/save write is used.
- An accepted request does not prove a valid pin/recognized script is currently
  loaded. It waits for a matching current pin and remains session-scoped through
  level changes. It yields while native movement keys or the camera are
  externally owned; pure additive-controller combinations remain untested.
- Shared class bytecode can be observed by a recursively executing same-class
  actor during that call. This is **not** a clone-isolation or concurrent-VM
  guarantee. User acceptance covers the previously available controls; factors
  above 2×, all special movement states and collision edge cases remain
  unverified.

Start with modest factors such as 1.1–1.25, with HL disabled. Compare normal
running and standing/running jumps in an open area, then stop tuning and verify
native behavior returns. Test each factor independently before combining.
See the [exact recipe/evidence and limits](trainer-movement-experimental.md).

## Equipment and earned ranks

With a level loaded, select/pin the exact `Ninja` using Auto+ or Manual.
Choose a target and click **Apply**:

| Control | Profile field | Editor range |
|---|---|---|
| Sword tier | `[exe+11AA88]+78`, int32 | 0–5 |
| Belt tier | `[exe+11AA88]+90`, int32 | 0–6 |
| Earned ranks/grades, **not Ranx currency** | `[exe+11AA88]+80`, int32 | 0–63 |

These limits come from the static PC script consumers, not validation of every
possible progression combination.

- Applying queues a **one-shot** operation to the engine tick, never a game
  function call from the render GUI. Requests recheck the profile field address,
  exact pinned Ninja and self handle. One pending request per mod, eight total.
- Sword applies the integer and calls the existing native notification wrapper
  for `Ninja` event **35**, then `NinjaSword` event **54**, following the built-in
  sword cheat. This requests model and tier-dependent scale refresh. The user
  reports the modification works, though appearance can lag until an attack.
- Notifications are the game's **class-wide queued delivery**, not a surgical
  immediate call on just the pinned actor. Matching live `Ninja`/`NinjaSword`
  instances can receive them; queued delivery does not prove a recipient accepted
  or already processed an event.
- Belt changes its profile integer only. **Sword and belt appearance may not
  update until the player presses attack at least once.** If attacking does
  not refresh it, reload the level. Research located Ninja event 20, but it also
  rebinds the normal model and could reset an alternate form, so Apply
  deliberately does not send it.
- Ranks, belt, current/max health, mission unlocks and award side effects remain
  separate. No inferred progression conversion or award replay is performed.
- Equipment is not frozen. Repeated model/notification refreshes could disrupt
  native state.
- Cancelling pending equipment or unloading/error-
  disabling the mod cancels unapplied requests. Already-applied fields are not
  restored, and the game may save them.

The status line distinguishes a queued request, cancellation, completed field
write and notification fault. If a sword refresh faults, the profile field may
already have changed; reload the level. Further sword requests are refused until
SDK restart instead of retrying potentially partial game work.

### API

```lua
local current, address = game.trainer_value("sword")
if current ~= nil then
    local queued = game.set_trainer_equipment("sword", 3, address)
end
local status = game.trainer_equipment_status()
game.cancel_trainer_equipment()
```

Movement API:

```lua
local accepted = game.set_trainer_movement(1.25, 1.1)
game.clear_trainer_movement()
```

Both APIs are mod-owned; one mod cannot clear another mod's requests.
Only one movement owner can be active; a conflicting request is refused.

The original `set_trainer_value` Lua binding refuses the three equipment/rank
names: use the queued API. Raw memory APIs remain low-level tools, not the
equipment refresh contract.

### Native boundary

Supported EXE SHA-256 remains
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
Native `0/0x11D`, table entry VA `4C82E4`, points to `417380`.
The wrapper consumes cdecl arguments `(senderStrat, argumentWords)`, where
`argumentWords = {classNamePointer, event}`. Its complete 31-byte body and table
pointer are checked before queueing and again before applying a sword edit.
The native wrapper calls the manager rather than the SDK inventing a direct
script entry or model pointer assignment.

The prior [movement/equipment research](trainer-movement-equipment-research.md)
records exact PC script consumers, built-in cheat recipe and retained Evidence.
The [cosmetic research](trainer-cosmetic-research.md) records the class-wide
notification queue, the belt model-rebind caveat and research-only tint/model
entry points. There are no cosmetic editor controls.

## Verification and test plan

The production resolver and equipment queue run against synthetic Win32
fixtures. Tests cover allowed/forbidden names/ranges, exact pinned identity,
profile replacement, same-address handle change, queue bounds, one-shot
consumption, owner cancellation, live-wrapper/table mismatches, read-only
storage, cdecl argument order and notification-fault latching. The actual Lua
trainer also runs with mocked APIs to check no enable-toggle dependency, separate drafts,
one-shot Apply, cancellation, pointer-reset drafts and unarmed reload.
Movement tests additionally cover independent literal fingerprints, the 5×
boundary and restoration, rejection above 5×, high-factor UI warnings, range and
owner validation, non-accumulation, read-only refusal, release during a call and
exception restoration. The actual shared detour is tested with nested
Ninja/timer calls, faults, pin loss and external-key/camera ownership gates.
Production tracker tests cover immediate VM generation/registry rejection
without generic memory-query calls.

The focused tests pass. The broader regression run also passed the
native/managed/UI/display/entity tests, then stopped at the unchanged legacy
static-evidence fixture's missing hard-coded
`C:\Users\Administrator\Desktop\ininja\I-Ninja.exe`. No fixture paths,
fingerprints or unrelated notice tests were relaxed.

Run the focused suite without the unrelated legacy asset fixtures:
`powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-trainer-tests.ps1`.

Automated checks did not launch the game or modify installed game files.
The user has live-tested the prior controls and confirmed they work.
For further checks, test a modest sword change, verify appearance and attacks,
then reload/respawn. Attack once if a sword/belt change is not yet visible.
Test belt separately across a level reload and re-open the
pause progress screen. Check that rank editing does not silently change belt
or unlock state. Test cancellation with a paused overlay, pin loss, level
loading, and trainer disable/re-enable. Restore your save backup if progression
becomes inconsistent.
