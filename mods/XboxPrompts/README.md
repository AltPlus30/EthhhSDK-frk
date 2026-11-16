# Xbox Button Prompts

An independently loadable Lua mod for the SDK's generic button-prompt API.

- `mod.lua`: Xbox image layout, native control mapping, and icon metrics.
- `buttons.tga`: supplied artwork as an uncompressed RGBA atlas.
- `mod.json`: manifest and declared texture asset.
- `convert-images.py`: optional PNG-to-atlas generator (requires Pillow).

Copy this folder to `EthhhSDK/mods/XboxPrompts` and enable it in the mod manager.
Disable other prompt packs first. No SDK rebuild is needed to edit or replace
the art/layout. Turn the mod off to restore native prompts.

To create a different controller pack, copy the folder, change the manifest ID
and name, and supply your own atlas and regions in `mod.lua`.
See [the generic API](../../docs/button-prompts.md) for supported formats,
region limits, error behavior and the native control-code reference.

This is a fixed visual layout, not an input remapper. In-game visual verification
is still pending. Supplied artwork's redistribution rights have not been
established; the mod is prepared for local use, not public distribution approval.
