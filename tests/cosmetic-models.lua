-- Legacy selection mock: unlike the real SDK, this mock supplies one-based
-- combo indices. It tests API request shaping, NOT real selection correctness.
-- See docs/cosmetic-model-noop-investigation.md; repairs are explicitly deferred.
local callbacks, buttons, requests = {}, {}, {}
local refreshes, stops = 0, 0
local chosenTier, filter = 1, ""
imgui = {
    text = function() end, text_disabled = function() end, separator = function() end,
    same_line = function() end,
    color_edit3 = function(_, r, g, b) return r,g,b end,
    checkbox = function(_,value) return value end,
    input_text = function() return filter end,
    combo = function(label, current)
        return label == "Built-in sword appearance" and chosenTier or current
    end,
    button = function(label) local value = buttons[label]; buttons[label] = nil; return value or false end,
}
hook = {add = function(event, cb) callbacks[event] = cb end}
game = {
    cosmetic_materials = function() return {}, "", false end,
    set_cosmetic_colors = function() error("model controls must not alter tint") end,
    clear_cosmetic_colors = function() error("model stop must not alter tint") end,
    cosmetic_models = function(refresh)
        if refresh then refreshes = refreshes+1 end
        return {
            {name="Ranx",addr=0x10000}, {name="NinjaSword",addr=0x20000},
            {name="Crate",addr=0x30000}, {name="RanxBlade",addr=0x40000},
        }, "Ready", {captured=1,substituted=1,waiting=0,refused=0}
    end,
    set_cosmetic_model = function(channel, recipient, donor)
        requests[#requests+1] = {channel,recipient,donor}; return true
    end,
    set_cosmetic_sword = function(recipient,tier)
        requests[#requests+1] = {"builtin",recipient,tier}; return true
    end,
    clear_cosmetic_models = function() stops=stops+1 end,
}
assert(loadfile(trainerSource))()
callbacks.gui()
assert(refreshes == 1 and #requests == 0 and stops == 0)
callbacks.gui()
assert(refreshes == 1, "do not snapshot loaded actors every GUI frame")
buttons["Apply donor to pinned Ninja"] = true
callbacks.gui()
assert(requests[1][1] == "player" and requests[1][2] == 0 and requests[1][3] == 0x10000)
chosenTier = 6
buttons["Apply built-in sword appearance"] = true
callbacks.gui()
assert(requests[2][1] == "builtin" and requests[2][2] == 0x20000 and requests[2][3] == 5)
filter = "ranxblade"
callbacks.gui()
buttons["Apply donor to selected sword"] = true
callbacks.gui()
assert(requests[3][1] == "sword" and requests[3][2] == 0x20000 and requests[3][3] == 0x40000)
buttons["Stop all model / sword overrides"] = true
callbacks.gui()
assert(stops == 1 and #requests == 3)
buttons["Refresh loaded model list"] = true
callbacks.gui()
callbacks.gui()
assert(refreshes == 2)
print("PASS legacy model request-shaping mock; real SDK combo-index bug remains deferred")
