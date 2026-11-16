-- Platform artwork, token mapping and metrics belong to the mod, not the SDK.
-- Change the texture/regions here to create a PlayStation, Nintendo or custom pack.
local icons = {
    a = 0, y = 1, b = 2, x = 3,
    left_up = 4, left_down = 5, left_left = 6, left_right = 7,
    lb = 8, start = 9, rb = 10, back = 11, left_stick = 12,
    dpad_up = 13, dpad_right = 14, dpad_down = 15, dpad_left = 16,
}
local controls = {
    [1] = "a", [2] = "y", [3] = "b", [4] = "x",
    [5] = "left_up", [6] = "left_down", [7] = "left_left", [8] = "left_right",
    [9] = "lb", [10] = "start", [11] = "rb", [12] = "back",
    [14] = "left_stick", [15] = "a", [16] = "b",
    [17] = "dpad_up", [18] = "dpad_down", [19] = "dpad_left", [20] = "dpad_right",
}
local regions = {}
for code, name in pairs(controls) do
    local index = icons[name]
    regions[#regions + 1] = {
        code = code, x = (index % 8) * 32, y = math.floor(index / 8) * 32,
        width = 32, height = 32, advance = 34,
    }
end
local ok, reason = game.set_button_prompts("buttons.tga", regions)
if not ok then error(reason) end
