local draw, time, clicks
local materialCalls, modelCalls, statusCalls, widgetCalls, applies = 0, 0, 0, 0, 0
time, clicks = 0, {}
imgui = {
    get_time = function() return time end,
    text = function() end, text_disabled = function() end,
    separator = function() end, same_line = function() end,
    input_text = function(_, value) return value end,
    color_edit3 = function(_,r,g,b) widgetCalls=widgetCalls+1; return r,g,b end,
    checkbox = function(_,value) return value end,
    button = function(label) local hit=clicks[label]; clicks[label]=nil; return hit or false end,
}
hook = {add=function(_,callback) draw=callback end}
game = {
    cosmetic_materials=function()
        materialCalls=materialCalls+1
        return {{name="blue",part="body",rgb=0xFFFFFF}},"Ready",false,
            {heads=0,texture_passes=100,texture_uploads=3,tick_ms=0.05,inspection_ms=0.06,peak_tick_ms=0.1}
    end,
    cosmetic_models=function()
        modelCalls=modelCalls+1
        return {},"",{captured=0,substituted=0,waiting=0,refused=0}
    end,
    cosmetic_blood_status=function() statusCalls=statusCalls+1; return "Ready" end,
    set_cosmetic_textures=function() return true,"" end,
    set_cosmetic_blood=function() return true,"" end,
    set_cosmetic_colors=function() applies=applies+1; return true end,
    clear_cosmetic_colors=function() end, clear_cosmetic_blood=function() end,
}
assert(loadfile(trainerSource))()
for frame=0,1000 do time=frame/1000; draw() end
assert(materialCalls<=6 and modelCalls<=6 and statusCalls<=6,"readouts poll at 5 Hz, not GUI frame rate")
assert(widgetCalls>=3000 and applies==0,"widgets stay responsive without automatic Apply")
local before=materialCalls
clicks["Apply color to selected parts"]=true
draw()
draw()
assert(applies==1 and materialCalls==before+1,"explicit requests immediately refresh the view")
imgui.collapsing_header=function() return false end
local beforeModels=modelCalls
for frame=1,10 do time=2+frame; draw() end
assert(modelCalls==beforeModels,"closed experimental model panel does not rebuild donor tables")
print("PASS cosmetic GUI: 1001 frames use <=6 status/material/model snapshots; widgets and Apply remain immediate")
