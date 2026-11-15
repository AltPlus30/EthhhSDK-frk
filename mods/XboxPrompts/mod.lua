-- Native font rendering and measurements share the same Xbox glyphs.
-- The SDK releases this request automatically when the mod is unloaded.
if not game.xbox_prompts(true) then
    error("Xbox prompt hooks are unavailable; check the SDK log and executable version.")
end
