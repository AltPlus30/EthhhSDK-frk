# EthhhSDK third-party notices

Local experimental Windows x86 SDK. This notice is not a blanket license for
EthhhSDK, the game, or supplied assets, and is not distribution approval.

| Component | Attribution / terms | Source notice | Staged notice under `build/dist` |
| --- | --- | --- | --- |
| Dear ImGui 1.91.8 | Omar Cornut and contributors; MIT | `src/third_party/imgui/LICENSE.txt` | `licenses/Dear-ImGui.txt` |
| MinHook | Tsuda Kageyu and contributors; BSD 2-Clause and included third-party terms | `src/third_party/minhook/LICENSE.txt` | `licenses/MinHook.txt` |
| Lua 5.4 | Copyright (C) 1994-2024 Lua.org, PUC-Rio; MIT | `src/third_party/lua/LICENSE.txt` (notice from `lua.h`) | `licenses/Lua.txt` |
| Half-Life movement reference | Half Life 1 SDK Copyright(c) Valve Corp.; Valve HLSDK agreement, not MIT | `mods/HalfLifeMovement/LICENSE` | `licenses/Valve-HLSDK.txt` and `mods/HalfLifeMovement/LICENSE` |

ImGui's bundled stb code/default-font notices remain in its vendored sources.

## Valve reference

Only `halflife-steam_legacy/pm_shared/pm_shared.c` was used as the HL movement
reference. No BunnymodXT reference is used. This is a C# reimplementation of
selected movement math, not the HL engine or a verified native hull-trace port.
The full agreement from `halflife-steam_legacy/LICENSE` accompanies the mod.
Its free-distribution and modified-Valve-game language needs review before any
public release of this non-Valve-game adaptation. Copying the text does not
resolve applicability or grant permission. Do not claim distribution clearance.

## Supplied audio and game assets

The five WAV files in `sounds to be used` were supplied by the user and retained
unchanged. Their embedded PCM16 adaptation does not establish ownership or
redistribution rights. No external audio license was supplied; review those
rights before distributing binaries that contain the sounds.

I-Ninja is (c) 2003 Namco Hometek Inc. / Argonaut Games plc. The SDK does not
redistribute the original executable, Data archives, movies or saves. It is a
fan project, not affiliated with those owners or with Valve.
