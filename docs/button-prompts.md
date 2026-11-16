# SDK button-prompt support

The SDK supplies **game font hooks, texture decoding, layout integration and
mod ownership**. It contains no controller brand, artwork or platform mapping.
No prompt pack is active until a mod requests one.

## Supply your own mod

Create `EthhhSDK/mods/MyPrompts/`:

```text
MyPrompts/
  mod.json
  mod.lua
  buttons.tga
```

Example `mod.json`:

```json
{
  "id": "my-prompts",
  "name": "My Controller Prompts",
  "version": "1.0.0",
  "type": "lua",
  "entry": "mod.lua",
  "assets": ["buttons.tga"],
  "api": 1,
  "enabled": true
}
```

Example `mod.lua` (artwork rectangles and advances are entirely your choice):

```lua
local ok, reason = game.set_button_prompts("buttons.tga", {
    { code = 1,  x = 0,  y = 0, width = 24, height = 24, advance = 26 },
    { code = 15, x = 0,  y = 0, width = 24, height = 24, advance = 26 },
    { code = 3,  x = 24, y = 0, width = 24, height = 24, advance = 26 },
    { code = 16, x = 24, y = 0, width = 24, height = 24, advance = 26 }
})
if not ok then error(reason) end
```

You can distribute this folder independently of the SDK. Users can replace it
with another platform's pack without recompiling the SDK. Disable the old pack
before enabling another one. The included [Xbox mod](../mods/XboxPrompts/) is
only an example implementation.

## API contract

`game.set_button_prompts(relativeTexturePath, regions)` returns `true` on
acceptance or `false, reason`. It copies the texture and region data; the Lua
table does not need to remain alive. Calling it again replaces the same mod's
pack atomically, invalidating cached font atlases. Invalid replacement requests
leave the previous pack active.

`game.set_button_prompts(nil)` releases **only the calling mod's** pack and
restores native prompts. Unloading, failed script loading, and automatic
error-disable also release ownership and texture resources. Another mod cannot
overwrite or release the active owner's pack.

- Texture: uncompressed TGA, truecolor RGB24/RGBA32 or indexed8 with a
  24/32-bit palette. Standard TGA origins are normalized; supply upright art.
  Use straight alpha. No PNG decoder or embedded platform art is in the SDK.
- File path: relative to the calling mod's directory; canonical paths must stay
  inside it (including symlink resolution). Absolute paths, alternate streams,
  missing files and files over 18 MiB are rejected.
- Texture dimensions: 1..2048 pixels per axis.
- Regions: a dense Lua array of 1..30 records, all six integer fields required.
  Rectangles use top-left pixel coordinates in the supplied texture.
- `code`: unique native text code 1..30. NUL (0) and newline (31) are reserved.
  Different codes may share the same image rectangle.
- `width`, `height`: 1..128 pixels; rectangle must fit in the source texture.
- `advance`: 1..256 native horizontal units. This controls spacing independently
  from the image width. Native font scaling and minimum control-icon scaling
  still apply; icons are not screen-pixel overlays.
- Omitted codes retain native behavior. There is no automatic input remapping,
  controller detection or last-used-device switching.
- Success means the pack was accepted, not that all active font layouts support
  it. Fonts with unsupported format/rotation or insufficient atlas space retain
  native text and log a warning. Generated atlases are capped at 4096 height.

### I-Ninja native text codes

These describe the **game's actions**, not controller-specific button numbers.
They are not DirectInput IDs. Codes without an established role should normally
be omitted.

| Code | Game meaning |
|---|---|
| 1 | Jump |
| 2 | Throw shuriken |
| 3 | Spin attack |
| 4 | Attack |
| 5, 6, 7, 8 | Movement up, down, left, right |
| 9 | First-person camera |
| 10 | Pause |
| 11 | Chain |
| 12 | Tutorial |
| 14 | Directional controls |
| 15 | Confirm |
| 16 | Cancel |
| 17 | Berserka rage |
| 18 | Shuriken rage |
| 19 | I-Ninja rage |
| 20 | Revive rage |

## Packaging

`tools/stage-lua-mods.ps1` discovers Lua manifests and stages each entry plus
its explicitly declared `assets`. `build.ps1` calls it; no mod-folder names are
hardcoded in that pipeline. `install.ps1` copies and backs up declared payloads,
including assets, and preserves existing live manifests. Payload traversal,
missing files, alternate streams and linked paths are refused.

For an independently supplied mod, no SDK build is necessary: copy the complete
folder directly into the installed SDK's `mods` directory.

## Implementation and checks

The existing verified font hooks remain in `src/core/button_prompts.cpp`.
Mod regions are packed into padded shelves above a clone of each native font
texture. Rendering and measurement borrow matching glyphs/metrics/substitution
tables; font reinitialization and pack replacement invalidate caches.
Managed D3D9 textures survive Reset. Native archives are never edited.

Tests cover TGA decoding, malformed regions, production pack loading,
ownership/release, atomic failure/replacement, path confinement, custom-mod
staging and removal of the former embedded Xbox resource. These are offline
checks, **not live visual acceptance**. See [Xbox research notes](xbox-prompts.md)
for the existing native-hook evidence and the prior broad-suite layout failure.
