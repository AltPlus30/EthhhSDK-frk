# Stop mission timer

The trainer has a session-only **Stop mission timer** checkbox. It requires the
updated DLL as well as `mods/trainer/mod.lua`. Lua API:
`game.stop_mission_timer(true|false)` returns whether the request was accepted.
Acceptance is not proof a timer is currently loaded. Each mod owns its request;
unload, failed loading and error-disable release it automatically.

## What the game does

This is **not** the engine's float game clock at `0051AB44`, nor its frame/tick
counters. The timed-mission HUD is the ASL class **MissionTimer**, shipped in
`Data/09.vol`, member `9-96.LCS`.

For a live `MissionTimer` strat `s` (all pointers are 32-bit):

| Address | Meaning |
|---|---|
| `[s+1A8]` | script class |
| `[[s+1A8]+4]` | NUL-terminated class name |
| `[s+1B4]` | script register block |
| `[s+1B4]+4` | register 1: running, **4-byte integer** |
| `[s+1B4]+8` | register 2: remaining ticks, **float** |
| `005119A8` | float time scale, normally 1 |

Each running timer update reads ASL property `33`, the time scale, and performs:

```text
remainingTicks -= timeScale
if remainingTicks <= 60:
    remainingTicks = 0
    running = 0
    register15 = 1
```

The script derives minutes from ticks/3600 and seconds from ticks/60.
Thus nominally 60 logic updates make one second, rather than wall-clock timing.
The explicit final-second clamp above is why merely suppressing the subtraction
would not reliably freeze a timer already in its final second. Start, stop,
initial-time and add/subtract-time message handlers are separate script code.

## Implementation

The SDK hooks the script interpreter at **00424BA0**. While requested, it checks
the exact class name and an eight-word script fingerprint. Only for that VM call
it replaces the first instruction of the countdown block with an ASL jump past
subtraction **and** expiration. A `__finally` restores the original word, including
when the VM raises an exception. No heap pointer or patch is retained between
calls, avoiding writes to a previous level's freed script memory.

### Freeze-path performance

The initial implementation called `SafeRead` while identifying every strat
executed by the VM. Each of those reads performs `VirtualQuery`: even rejecting
an ordinary non-timer cost three system calls per invocation. Its generic
`SafeWrite` also queried memory and could flush the native instruction cache
when the game's script allocation had executable protection.

The hot path now uses **SEH-guarded direct reads and writes**, with no memory
queries, protection changes or instruction-cache flushes. ASL bytecode is
interpreted data, not x86 code. Read-only storage fails closed rather than
changing its protection. Exact class/fingerprint checks and scoped restoration
remain; neither positive nor negative heap identities are cached across calls.

The synthetic Win32 Release benchmark for 200,000 non-timer identifications
measured **191.492 ms before / 0.775 ms after** (about 247x for identification,
**not** a claim about game FPS). Tests also require zero `SafeRead`/`SafeWrite`
calls on the enabled detour path, and cover invalid pointers, read-only script
storage, same-address class changes and restoration after exceptions.

`class = [s+1A8]`, `code = [class+C]`, countdown site = `code+298`:

```text
Original words:
42251033 0C020225 F4FF0200 42700000
00000008 86020100 320F0001 7A010012

Replacement first word: 78FF0006
Jump destination: site + 4 + 6*4 = code+2B4
```

These are **VM opcodes, not x86 instructions**. Byte order is little-endian.
The running flag is not forced off, and time scale is not changed. Movement,
other script timers, fade animations and mission messages keep their native
behavior. A message can still reset/change the timer; this is intentionally a
countdown stop, not a lock against every possible scripted write.

The DLL refuses the hook unless the executable SHA-256 is:
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.
The separate `I-Ninja_widescreen.exe` has **not** been established as compatible.
An unknown script fingerprint is left untouched.

## Cheat Engine

Attach to the supported `I-Ninja.exe` **with a timed mission loaded**.

### Inspecting the value

Walk the strat linked list: first pointer at **00511A38**, next at `s+198`.
Find the strat whose class name (pointer chain above) is exactly `MissionTimer`.
Add a pointer record with base address **s+1B4**, offset **8**, type **Float**.
The displayed seconds are approximately that value divided by 60. `s` is a
session-local address, not a reusable static pointer.

Freezing this float with CE's periodic writer is less robust: the game may
evaluate expiration between CE writes. Freezing the running integer to zero
also changes the script's start/stop state. Prefer the bytecode patch below.

### Equivalent Auto Assembler script

This scans **all process memory**, not just the executable, because ASL code is
in a loaded data allocation. Enable only after the timer script has loaded:

```asm
[ENABLE]
aobscan(MissionTimerCountdown,33 10 25 42 25 02 02 0C 00 02 FF F4 00 00 70 42 08 00 00 00 00 01 02 86 01 00 0F 32 12 00 01 7A)
assert(MissionTimerCountdown,33 10 25 42)
registersymbol(MissionTimerCountdown)
MissionTimerCountdown:
  db 06 00 FF 78

[DISABLE]
assert(MissionTimerCountdown,06 00 FF 78)
MissionTimerCountdown:
  db 33 10 25 42
unregistersymbol(MissionTimerCountdown)
```

**CE limitations:** this is a suggested table entry, not a live-tested table.
Check that the AOB has exactly one match and that it equals `[class+C]+298`
for the actual `MissionTimer` class. Disable it **before** restarting/unloading
the level or detaching; re-scan after a load. Unlike the SDK's scoped patch,
this persistent CE entry cannot safely track a freed/reused allocation.
Do not enable the CE patch and SDK checkbox together.

## Evidence and verification

Static Ghidra/REA evidence:

- `ev_784bf58b2418f0e9e6b096c8a5f0878874eeb1a2fcabb59149e7afc7834eaa31`:
  decoder `00444EE0` used to recover the packed scripts.
- `ev_33905bc612bf6799191d38f86522040a6578d3afb6ea9bd44c476e71949272e5`:
  getter `00421680`, property `33` reads `005119A8`.
- `ev_5bd0b2815cf7871500ea412cfcc2bf0b60547ba89e1bf12ed659f872abb7c382`:
  interpreter `00424BA0`: register pointer at `s+1B4`, float subtraction,
  branch semantics and native call behavior.
- Packed-script audit: `python tools/inspect-mission-timer.py --game E:\Games\I-Ninja`.
  This reads files only and prints identities and exact opcode locations.
  Decompressed `9-96.LCS` SHA-256:
  `127243447e13d1bc29960008952c6f6033446b03a67fbe36b0bfebdab13f9edc`.
  Class offset `AA874`, code `9F658`, countdown `9F8F0`.
  These are offsets **in the decompressed asset**, not process addresses.

The Win32 DLL build and synthetic production-detour tests passed (opt-in,
multiple owners, exact class/fingerprint refusal, jump destination, normal and
exceptional restoration). The user confirmed that the original freeze works
in-game, but reported a significant enabled-state frame-rate regression.
The optimized path above has passed synthetic tests; its in-game performance
still needs confirmation. The Cheat Engine script remains **unverified live**.
Manual check: load a timed challenge, enable, play for ten seconds, verify the
countdown stays fixed while movement works, disable and verify it resumes.
Repeat near expiration, across restart/loading, and when unloading the trainer.
