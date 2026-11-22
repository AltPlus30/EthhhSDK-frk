local callbacks, clicked = {}, {}
local requests, released = {}, 0
local colors = {}
imgui = {
    text = function() end, text_disabled = function() end,
    separator = function() end, same_line = function() end,
    button = function(label) local hit = clicked[label]; clicked[label] = nil; return hit or false end,
    checkbox = function(label, value) return value end,
    color_edit3 = function(label, r, g, b)
        if colors[label] then return table.unpack(colors[label]) end
        return r, g, b
    end,
}
hook = {add = function(name, callback) callbacks[name] = callback end}
game = {
    cosmetic_materials = function()
        return {{name="blue", rgb=0x102030}, {name="sash white", rgb=0xFFFFFF}}, "Ready", false
    end,
    set_cosmetic_colors = function(value) requests[#requests+1] = value; return true end,
    clear_cosmetic_colors = function() released = released+1 end,
}
assert(loadfile(trainerSource))()
assert(callbacks.gui)
callbacks.gui()
assert(#requests == 0 and released == 0, "loading/drawing must not arm an override")
colors["Belt RGB"] = {1, 0.5, 0}
clicked["Apply belt color"] = true
callbacks.gui()
assert(#requests == 1 and #requests[1] == 1)
for _, request in ipairs(requests[1]) do
    assert(request.name == "belt" and request.part == "group")
    assert(request.rgb == 0x0080FF, "native RGB packing/rounding")
end
imgui.checkbox = function(label, value) return label == "body: blue" end
callbacks.gui() -- choose the blue costume entry; no implicit apply
assert(#requests == 1)
colors["Costume RGB"] = {0, 0, 1}
clicked["Apply color to selected parts"] = true
callbacks.gui()
assert(#requests == 2 and #requests[2] == 2)
local blue
for _, request in ipairs(requests[2]) do if request.name == "blue" then blue = request.rgb end end
assert(blue == 0xFF0000)
clicked["Restore selected parts"] = true
callbacks.gui()
assert(#requests[3] == 1, "restore only selected costume part, retain belt group")
clicked["Stop and restore all colors"] = true
callbacks.gui()
assert(released == 1 and #requests == 3)
clicked["Apply color to selected parts"] = true
callbacks.gui()
assert(#requests == 4 and #requests[4] == 1)
assert(requests[4][1].part == "body" and requests[4][1].name == "blue")
game.cosmetic_materials = nil
callbacks.gui() -- graceful older-SDK fallback
print("PASS cosmetic Lua: standby, explicit Apply, native RGB, selection, stop and old-SDK fallback")
