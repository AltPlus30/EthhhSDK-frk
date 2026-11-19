# Trainer value editing and freezing

Open F9 → Mods → I-Ninja Trainer → **Values / inventory**. Requires the updated
`EthhhSDK.dll` and `mods/trainer/mod.lua`.

Each row shows the current value and a separate target:

- **Use current** copies the current value into the target.
- **Apply** writes the target once.
- **Freeze** writes it immediately and after each native game tick.
- Editing the target releases its freeze; re-enable explicitly.
- **Release all value freezes** stops every repeated value write.

All value freezes start **off**, including after reload/re-enable. Disabling or
unloading the Lua mod stops its tick writes; there is no native background writer.
Releasing a freeze does **not** undo the last written value. The existing mission
timer control is independent of these value freezes.

**Back up your game saves first.** The game may persist edited profile values.
No save file is directly edited by this implementation.

## Imported CE mapping

Source: the user's `%USERPROFILE%\Documents\My Cheat Tables\I-Ninja.CT`
(Cheat Engine table version 52). The original table was read, not modified.
These names/types are supplied by that table; importing them does not constitute
new live verification of their gameplay behavior.

Let `P = [exe + 0x11AA88]` (32-bit pointer). Offsets are hexadecimal:

| Field / Lua key | Address | Type |
|---|---|---|
| Health / `health` | `P+14` | float32 |
| Rage / `rage` | `P+98` | int32 |
| Coin bank / `coins` | `P+94` | int32 |
| Score / `score` | `P+AC` | int32 |
| Lives / `lives` | `P+B4` | int32 |
| Darts / `darts` | `P+8C` | int32 |
| Shurikens / `shurikens` | `P+84` | int32 |
| Ranx bank / `ranx` | `P+A0` | int32 |

**Ranx currency is not earned ranks/grades.** The normal value panel does not
expose arbitrary inventory unlocks or mission completion. Separate opt-in
[equipment/rank controls](trainer-experimental.md) use their own
one-shot tick queue, not the value-freeze UI.

For `multiplier`, start at `exe+0xED8A0`. At each step, dereference a 32-bit
pointer and add the next offset: `0, 4, 24, 44, 0, D8, 138`. Read/write the
final address as int32. CE's XML lists these offsets in the reverse order.
The final field is not an additional pointer.

The table describes health as base 4 (+1 per belt), rage as base 256
(+256 per skill), and multiplier as 1–10. Editor limits are health 0–64,
rage 0–65536, multiplier 1–10, other fields 0–2147483647. These are
**editor policy**, not confirmed engine capacities; extreme values may still
break game logic. There is no automatic max-health/rage calculation.

## API and safeguards

```lua
if game.trainer_supported() then
    local value, address = game.trainer_value("coins")
    if value ~= nil then
        local ok = game.set_trainer_value("coins", 1000, address)
    end
end
```

- `trainer_supported()` hashes the on-disk executable once per SDK load;
  only SHA-256
  `49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`
  is accepted. This is not a fingerprint of every live executable byte.
- `trainer_value(name)` returns `value, address`, or `nil`.
- `set_trainer_value(name, value, expectedAddress)` returns a boolean.
  The address is an identity token from a read, **not** an arbitrary write address.
  The native setter resolves the named field again and refuses a mismatch.
- Unknown names, non-finite/fractional integer targets, out-of-policy values,
  null/misaligned/overflowing pointers, access faults and read-only storage fail
  closed. Stores touch four bytes and do not change page protection.
- Lua freezes retain only the target and expected address. They disarm after
  a failed write or an observed pointer change/unavailability. Re-enable manually.
  No pointer chain is cached in native code.

An unchanged numerical address cannot establish allocation lifetime. Free/reuse
at the same address between observations is not detected. Values can also remain
available outside gameplay; release freezes before loading or starting another
profile. This is a periodic value lock, **not invulnerability**: damage, spending,
death or other scripts can act between writes. The multiplier chain's ownership
and stability beyond the supplied table remain unverified.

## Verification

- Win32 Release DLL and production native resolver/writer tests built.
- Synthetic tests cover all nine fields, four-byte-only writes, reversed CE
  pointer order, null intermediate links, changed identity, invalid targets,
  unsupported executable, freed memory and read-only refusal.
- Actual trainer Lua runs under mocked public APIs to test no writes on load,
  separate draft/Apply, repeated freeze, release-all, pointer changes, failed
  writes, unavailable fields and unarmed reload.
- ABI unchanged: 219 exports / 217 managed bindings.
- Broader `tests/run-tests.ps1` run passed the native/managed/UI suites through
  the entity-performance tests, then stopped at the legacy evidence tool's
  missing hard-coded `C:\Users\Administrator\Desktop\ininja\I-Ninja.exe`.
  An in-memory path override to the actual installation reached a separate
  `general.vol` fingerprint mismatch; fingerprint requirements were not relaxed.
  Separately, PestControl evidence (with an in-memory EXE path override) and
  all 31 hot-path guards passed. The existing notice suite has three failures
  (old nine-file expectation vs six staged files, removed notice text, and a
  Valve-license hash mismatch). Those unrelated sources/tests were not changed.

These are offline checks, **not live game acceptance**. Test each field first
with a modest one-shot edit, then freeze/unfreeze. Check health/damage, rage
consumption, spending, score awards/multiplier decay and ammo use; repeat around
respawn/loading and trainer disable/re-enable. Avoid CE freezing the same field
at the same time.

**2026-11-19 update:** the user confirmed that the value, movement and equipment
controls work well in-game. Sword/belt appearance can lag until an attack.
The newly expanded movement factors above 2× are not covered by that acceptance;
the panel warns about their risks.

Movement and equipment findings are tracked separately in
[`trainer-movement-equipment-research.md`](trainer-movement-equipment-research.md).
