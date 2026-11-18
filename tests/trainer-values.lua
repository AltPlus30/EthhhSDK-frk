-- Execute the actual mod with mocked public APIs, including its real tick/gui hooks.
local hooks, actions, values, addresses, writes = {}, {}, {}, {}, {}
local activeId, failures, supported = nil, {}, true
local keys = {"health", "rage", "coins", "score", "multiplier", "lives", "darts", "shurikens", "ranx"}
for i, key in ipairs(keys) do values[key] = i; addresses[key] = 0x10000 + i * 4 end
settings = {
    get_bool = function(_, default) return default end,
    get_float = function(_, default) return default end,
    get = function(_, default) return default end,
}
log = {info = function() end, warn = function() end}
input = {key = function() return false end, overlay_open = function() return false end}
hook = {add = function(name, fn) hooks[name] = fn end}
game = {
    camera = function() return nil end, player_addr = function() return 0 end,
    trainer_supported = function() return supported end,
    trainer_value = function(key) return values[key], values[key] and addresses[key] end,
    set_trainer_value = function(key, value, address)
        if failures[key] or not values[key] or address ~= addresses[key] then return false end
        writes[#writes+1] = {key, value, address}
        values[key] = value
        return true
    end,
}
local function consume(kind, label)
    local action = actions[kind]
    if action and action.id == activeId and action.label == label then
        actions[kind] = nil
        return action
    end
end
imgui = setmetatable({
    hex = function() return 0 end,
    collapsing_header = function(label) return label == "Values / inventory" end,
    push_id = function(id) activeId = id end,
    pop_id = function() activeId = nil end,
    button = function(label) return consume("button", label) ~= nil end,
    checkbox = function(label, value)
        local action = consume("checkbox", label)
        if action then return not value, true end
        return value, false
    end,
    input_int = function(label, value)
        local action = consume("input", label)
        return action and action.value or value, action ~= nil
    end,
}, {__index = function() return function() end end})
imgui.input_float = imgui.input_int
local function draw(kind, id, label, value)
    if kind then actions[kind] = {id = id, label = label, value = value} end
    hooks.gui()
    if kind then assert(not actions[kind], "GUI action not consumed: " .. label) end
end
local function tick() hooks.tick(1, 1/60) end
local function count() return #writes end
assert(loadfile(trainerSource))()
tick()
draw()
assert(count() == 0, "loading/drawing must not write")
draw("input", "health", "Target", 12)
tick()
assert(count() == 0, "editing draft must not write")
draw("button", "health", "Apply")
assert(count() == 1 and values.health == 12)
tick()
assert(count() == 1, "Apply must be one-shot")
draw("checkbox", "health", "Freeze")
local n = count()
values.health = 1
tick()
assert(count() == n+1 and values.health == 12, "freeze applies target on tick")
draw("input", "health", "Target", 9)
n = count()
tick()
assert(count() == n, "editing must disarm freeze")
draw("checkbox", "health", "Freeze")
draw("checkbox", "health", "Freeze")
n = count()
tick()
assert(count() == n and values.health == 9, "unfreeze stops writes without reverting")
draw("checkbox", "health", "Freeze")
addresses.health = addresses.health + 0x100
n = count()
tick()
assert(count() == n, "old freeze must not write new allocation")
draw()
tick()
assert(count() == n, "GUI must not re-arm")
for _, key in ipairs(keys) do draw("checkbox", key, "Freeze") end
draw("button", nil, "Release all value freezes")
n = count()
tick()
assert(count() == n, "release all freezes")
draw("checkbox", "coins", "Freeze")
values.coins = nil
n = count()
tick()
values.coins = 100
tick()
assert(count() == n, "unavailable must release instead of resuming")
draw("checkbox", "rage", "Freeze")
draw("checkbox", "darts", "Freeze")
failures.rage = true
tick()
failures.rage = false
n = count()
tick()
assert(count() == n+1 and writes[#writes][1] == "darts",
    "one failed freeze disarms without stopping another field")
draw("button", nil, "Release all value freezes")
supported = false
draw()
-- Reload starts entirely unarmed.
assert(loadfile(trainerSource))()
n = count()
tick()
assert(count() == n, "reload must start unarmed")
print("PASS actual trainer Lua: draft/apply/freeze/release/failure/pointer change/reload")
