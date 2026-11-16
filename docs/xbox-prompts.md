# Xbox Button Prompts

Refactored on branch `rewire`. Enable **Xbox Button Prompts** in the SDK mod
manager. It uses the fixed controller layout supplied with this request, including
when keyboard input is used; it does not remap input.

## Build / install

The mod supplies `buttons.tga` and its layout in `mod.lua`; no Xbox artwork or
mapping is compiled into the SDK. Rebuilding does not require Pillow or access
to the original image folder. To regenerate the mod's texture:

```powershell
python mods/XboxPrompts/convert-images.py "D:\AI\opencode\temp\i-ninja prompts\img"
.\build.ps1 -Reconfigure
```

Outputs: `build/dist/EthhhSDK.dll` and `build/dist/mods/XboxPrompts/`.
With the game closed, install using:

```powershell
.\install.ps1 -GameDir 'E:\Games\I-Ninja'
```

The installer backs up replaced SDK files and preserves existing manifests.
Toggle the mod off in the manager to restore native prompts. Native font data
is borrowed only during text calls, so unloading does not leave modified game
pointers behind. Game archives are not modified.

## Coverage and mapping

This replaces native **embedded prompt codes** in tutorials, dialogue and HUD/menu
text. Plain labels in the controller-binding configuration screen (for example
`Button 1`) are separate strings and are not rewritten.

| Native code | Meaning | Xbox image |
|---|---|---|
| 1 / 15 | Jump / confirm | A |
| 2 | Shuriken | Y |
| 3 / 16 | Spin attack / cancel | B |
| 4 | Attack | X |
| 5–8 | Movement up/down/left/right | Left stick directions |
| 9 | First person | LB |
| 10 | Pause | Start |
| 11 | Chain | RB |
| 12 | Tutorial | Back |
| 14 | Directional controls | Left stick |
| 17 | Berserka | D-pad up |
| 18 | Shuriken rage | D-pad down |
| 19 | I-Ninja rage | D-pad left |
| 20 | Revive | D-pad right |

Code 31 remains a newline. Unmapped codes retain native behavior. All 22 supplied
images were converted; right-stick images are retained in the mod's texture but no
verified right-stick prompt token was found in this font's substitution table.

The mod calls the generic `game.set_button_prompts(path, regions)` API.
See [SDK button-prompt support](button-prompts.md) to supply a different button
set, dimensions or mappings. The former `game.xbox_prompts(boolean)` prototype
was removed: use the generic API rather than asking the SDK for branded assets.

## Implementation / evidence

Supported executable SHA-256:
`49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0`.

REA/Ghidra observations:

- `0x40D0D0`: resolves control tokens to keyboard descriptions or localized
  action labels. Evidence `ev_43f31830709ca0eead609386aa60c3e04ae447635b599a33f71578eeab8d0a69`.
- `0x40D3A0`: initializes a font from its relocated descriptor, including glyph,
  texture and 32-entry substitution table pointers.
  Evidence `ev_ee313341f8d6df5771c6ee62d66935fe4142bc1abf50628b8979fc15e880ff7a`.
- `0x40D630`: expands tokens before enqueueing text; `-1` table entries pass the
  original glyph codes through. Evidence
  `ev_4a586e27129faeaebab483eda8e50f784ea8de1ebeff9001dfe999a8397da25f`.
- `0x455B50`: TGA texture upload, evidence
  `ev_9888db2946bda6f57804fb99508223438fe7a18d0f2dbe00c07b226e8b8a9bb7`.

Matching historical decompilation guided `0x40D470` (text measurement) and
`0x448670` (rendering). Direct Capstone decoding of this executable corroborated
entry bytes, `ret 0x0c` / `ret 0x24` stack cleanup, and the renderer's **cdecl
integer pass argument** (`[esp+0x8c]` after 0x88 bytes of frame/pushes). Ghidra's
renderer pseudocode omitted that argument; it must be forwarded.

The actual `09.vol / 9-98.LCS` font descriptors establish the mapping above.
Most action glyphs have zero dimensions in the PC asset. The runtime clones
glyphs, supplies nonempty metrics, disables only their substitutions, and builds
an expanded `D3DFMT_A8R8G8B8` managed texture from the original TGA plus converted
icons. Original glyph UVs are rescaled without changing their on-screen size.
The upside-down native font texture convention is preserved. The renderer
borrows the original texture-binding vtable; the SDK owns the new COM texture.
Font reinitialization invalidates the cache. Managed textures survive Reset.

REA export: `build/xbox-prompts-rea-evidence.json` (local research artifact).
Static analysis does not establish live appearance. No in-game visual acceptance
has been performed; check tutorials, dialogue, pause prompts, enable/disable,
level reload and display Reset in the installed game.

## Original implementation checks (before modularization)

- Release SDK and mod staging: passed; latest DLL includes the corrected icon
  orientation.
- `eth_xbox_prompt_tests`: passed (short palettes, alpha, image origins,
  truncation, invalid palette indices, RGB24 and control mappings).
- Production decoder against the extracted 256×256 native font TGA: passed.
- `tests/run-tests.ps1`: new prompt checks and preceding native/managed checks
  passed, then the suite stopped at the manager CPU layout test:
  `page 2 tab 0`, `1728x1056`, scale `1.50`, horizontal overflow `54.0`.
  Consequently the full regression suite is **not** reported as passing.
- `git diff --check`: passed.

## Modularization checks

- Release SDK and generic Lua-mod staging: passed.
- `eth_button_prompt_tests`: passed, including the mod's external 256×96 TGA.
- `eth_button_prompt_runtime_tests`: production loading, ownership, path
  confinement, atomic replacement failure and cache/resource release passed.
- `tests/test_button_prompt_packaging.py`: custom user mod/nested asset staging,
  invalid asset rejection, and absence of embedded Xbox resources passed.
- `tests/test_notice_packaging.py`: 3 failures in untouched branch content
  (expects 9 notices but the branch supplies 6; missing FreeType caveat;
  Valve license hash mismatch). These were not changed as part of this work.

Artifacts are staged, not installed. Branch remains `rewire`. No new live
visual verification was performed.
