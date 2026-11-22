local callbacks, buttons, seen = {}, {}, {}
local loadCount, originalCount, paletteCount, bloodCount, bloodStops = 0, 0, 0, 0, 0
local useSecondary = false
local lastPaths, lastSecondary, lastPalette, lastBlood, lastRgb, lastGlow
local glowSwitches = {}
imgui = {
    text = function() end, text_disabled = function() end, separator = function() end,
    same_line = function() end,
    input_text = function(_, value) return value end,
    color_edit3 = function(label,r,g,b)
        if label=="Costume RGB" or label=="Blood RGB" then return 1,0,0 end
        return r,g,b
    end,
    checkbox = function(label,value)
        seen[label]=true
        if label:find("secondary body atlas",1,true) then return useSecondary end
        if label=="head: head" then return true end
        return value
    end,
    button = function(label)
        seen[label]=true
        local value=buttons[label]; buttons[label]=nil; return value or false
    end,
}
hook = {add = function(event,callback) callbacks[event]=callback end}
game = {
    cosmetic_materials = function()
        return {
            {name="blue",part="body",rgb=0xFFFFFF},
            {name="blue_double",part="body",rgb=0xFFFFFF},
            {name="sash white",part="head",rgb=0xFFFFFF},
            {name="sash knot white",part="head",rgb=0xFFFFFF},
            {name="head",part="head",rgb=0xFFFFFF},
        }, "Ready", false, {heads=1,textures=false,texture_passes=0,texture_refusals=0}
    end,
    set_cosmetic_textures = function(paths,secondary)
        loadCount=loadCount+1; lastPaths=paths; lastSecondary=secondary; return true,""
    end,
    clear_cosmetic_textures = function() originalCount=originalCount+1 end,
    set_cosmetic_colors = function(palette)
        paletteCount=paletteCount+1; lastPalette=palette; return true
    end,
    clear_cosmetic_colors = function() end,
    set_cosmetic_blood = function(paths,rgb,glow)
        bloodCount=bloodCount+1; lastBlood=paths; lastRgb=rgb; lastGlow=glow; return true,""
    end,
    set_cosmetic_glow_enabled = function(value) glowSwitches[#glowSwitches+1]=value; return true,"" end,
    clear_cosmetic_blood = function() bloodStops=bloodStops+1 end,
}
assert(loadfile(trainerSource))()
callbacks.gui()
assert(loadCount==0 and paletteCount==0 and bloodCount==0 and #glowSwitches==0,"no implicit arming")
for _,label in ipairs({
    "Apply costume group", "Restore costume group", "Apply whole head color",
    "Load head cloth selection", "body: blue_double", "head: sash white",
    "head: sash knot white",
}) do assert(not seen[label],"removed control: "..label) end
assert(seen["head: head"],"headgear remains an ordinary selectable part")
buttons["Use supplied monochrome costume textures"]=true
callbacks.gui()
assert(loadCount==1 and #lastPaths==3 and lastSecondary==false)
assert(lastPaths[1]=="textures/ninja-blue-original-A00063740.tga")
assert(lastPaths[3]=="textures/ninja-head-original-A001b0720.tga")
buttons["Apply color to selected parts"]=true
callbacks.gui()
assert(paletteCount==1 and #lastPalette==2)
local parts={}
for _,request in ipairs(lastPalette) do
    assert(request.rgb==255)
    parts[request.part.."/"..request.name]=true
end
assert(parts["body/blue"] and parts["head/head"])
useSecondary=true
buttons["Use supplied monochrome costume textures"]=true
callbacks.gui()
assert(loadCount==2 and lastSecondary==true)
buttons["Apply blood color"]=true
callbacks.gui()
assert(bloodCount==1 and #lastBlood==5 and lastRgb==255 and lastGlow==true)
assert(lastBlood[5]=="blood/green-blood-pool-original-A001c7de0.tga")
buttons["Restore original glow only"]=true
callbacks.gui()
assert(glowSwitches[#glowSwitches]==false and bloodStops==0,"glow rollback is independent of blood/interiors")
buttons["Apply blood color"]=true
callbacks.gui()
assert(bloodCount==2 and lastGlow==false,"explicit glow rollback survives later blood color changes")
buttons["Restore original blood"]=true
callbacks.gui()
assert(bloodStops==1)
buttons["Use original textures"]=true
callbacks.gui()
assert(originalCount==1)
buttons["Stop and restore all colors"]=true
callbacks.gui()
assert(bloodStops==2,"global color stop includes the independent blood channel")
print("PASS cosmetic UI: explicit textures/blood, whole head in parts, filtered list and removed redundant controls")
