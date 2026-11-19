-- Real movement/equipment UI: controls are visible without an enable toggle,
-- but drafts and opening/reloading the panel must never apply writes.
local hooks, actions, requests = {}, {}, {}
local id, cancels, movementClears = nil, 0, 0
local movementRequests = {}
local messages, ranges = {}, {}
local values = {sword = 1, belt = 2, ranks = 9}
local addresses = {sword = 0x10078, belt = 0x10090, ranks = 0x10080}
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
    trainer_supported = function() return true end,
    trainer_value = function(key) return values[key], values[key] and addresses[key] end,
    set_trainer_equipment = function(key, value, address)
        assert(address == addresses[key], "expected current field token")
        requests[#requests+1] = {key, value, address}
        return true
    end,
    trainer_equipment_status = function() return "fixture status" end,
    cancel_trainer_equipment = function() cancels = cancels + 1 end,
    set_trainer_movement = function(speed, jump)
        assert(speed >= 0.5 and speed <= 5 and jump >= 0.5 and jump <= 5)
        movementRequests[#movementRequests+1] = {speed, jump}
        return true
    end,
    clear_trainer_movement = function() movementClears = movementClears + 1 end,
}
local function action(kind, label)
    local a = actions[kind]
    if a and a.id == id and a.label == label then actions[kind] = nil; return a end
end
imgui = setmetatable({
    hex = function() return 0 end,
    collapsing_header = function(label) return label == "Movement / equipment" end,
    text_colored = function(_, text) messages[#messages+1] = text end,
    text_disabled = function(text) messages[#messages+1] = text end,
    push_id = function(value) id = value end,
    pop_id = function() id = nil end,
    button = function(label) return action("button", label) ~= nil end,
    checkbox = function(label, value)
        assert(label ~= "Enable experimental controls this session", "obsolete enable toggle must not be rendered")
        local a = action("checkbox", label)
        return a and not value or (not a and value), a ~= nil
    end,
    slider_int = function(label, value, minimum, maximum)
        ranges[label] = {minimum, maximum}
        local a = action("slider", label)
        return a and a.value or value, a ~= nil
    end,
}, {__index = function() return function() end end})
imgui.slider_float = imgui.slider_int
local function draw(kind, targetId, label, value)
    if kind then actions[kind] = {id = targetId, label = label, value = value} end
    hooks.gui()
    if kind then assert(not actions[kind], "action not consumed: " .. label) end
end
assert(loadfile(trainerSource))()
draw()
hooks.tick(1, 1/60)
assert(#requests == 0 and cancels == 0 and #movementRequests == 0 and movementClears == 0, "opening controls must not write")
for _, label in ipairs({"Native locomotion factor", "Base jump impulse factor"}) do
    assert(ranges[label][1] == 0.5 and ranges[label][2] == 5, "both movement sliders expose 0.5-5x")
end
local function saw(text)
    for _, message in ipairs(messages) do if message:find(text, 1, true) then return true end end
    return false
end
assert(saw("Back up your saves") and saw("attack once"), "save backup and equipment refresh reminders")
draw("slider", nil, "Native locomotion factor", 1.5)
draw("slider", nil, "Base jump impulse factor", 1.2)
assert(#movementRequests == 0, "movement drafts must not apply silently")
draw("button", nil, "Apply native movement tuning")
assert(#movementRequests == 1 and movementRequests[1][1] == 1.5 and movementRequests[1][2] == 1.2)
draw("button", nil, "Reset movement draft")
assert(#movementRequests == 1, "resetting a draft must not change active tuning")
draw("button", nil, "Stop native movement tuning")
assert(movementClears == 1)
draw("slider", "equipment_sword", "Target", 4)
hooks.tick(1, 1/60)
assert(#requests == 0, "draft changes must not write")
messages = {}
draw("button", "equipment_sword", "Apply")
assert(#requests == 1 and requests[1][1] == "sword" and requests[1][2] == 4)
draw()
assert(not saw("Request rejected"), "a successfully queued equipment edit must not show rejection")
draw()
hooks.tick(1, 1/60)
assert(#requests == 1, "equipment is never frozen/requeued")
draw("button", "equipment_belt", "Apply")
assert(#requests == 2 and requests[2][1] == "belt" and requests[2][2] == 2)
draw("button", "equipment_ranks", "Apply")
assert(#requests == 3 and requests[3][1] == "ranks" and requests[3][2] == 9)
addresses.sword = addresses.sword + 0x100
values.sword = 2
draw("button", "equipment_sword", "Apply")
assert(#requests == 4 and requests[4][2] == 2, "new allocation resets draft")
draw("button", nil, "Cancel pending equipment edit")
assert(cancels == 1)
draw("slider", nil, "Native locomotion factor", 5)
draw("slider", nil, "Base jump impulse factor", 5)
assert(#movementRequests == 1, "5x drafts still require Apply")
assert(saw("Above 2x is not yet live-tested"), "high-factor risk disclaimer")
draw("button", nil, "Apply native movement tuning")
assert(#movementRequests == 2 and movementRequests[2][1] == 5 and movementRequests[2][2] == 5)
messages = {}
draw("button", nil, "Reset movement draft")
draw()
assert(saw("Above 2x is not yet live-tested"), "warning stays visible for applied high factors, even after resetting drafts")
draw("button", nil, "Stop native movement tuning")
assert(movementClears == 2, "explicit stop remains available without master toggle")
draw()
assert(#requests == 4)
assert(loadfile(trainerSource))()
draw()
hooks.tick(1, 1/60)
assert(#requests == 4, "reload begins unarmed")
assert(#movementRequests == 2, "reload must not restore movement tuning")
-- Old DLL path must show an explanation instead of calling missing functions.
game.set_trainer_equipment = nil
draw()
print("PASS movement/equipment Lua: no enable toggle, 5x sliders, warnings, drafts, one-shot Apply, pointer reset, cancel and reload")
