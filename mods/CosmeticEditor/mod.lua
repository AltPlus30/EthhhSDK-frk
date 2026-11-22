-- No memory writes, profile edits, native calls or automatic saved overrides.
local ui = imgui
local belt = {1, 1, 1}
local costume = {1, 1, 1}
local useSecondaryAtlas = false
local blood = {1, 0, 0}
local sharedGlow = true
local selected, drafts = {["body/blue"] = true}, {}
local message
local modelRefresh = true
local donorIndex, swordIndex, bladeDonorIndex, swordTier = 1, 1, 1, 1
local filter = ""
local snapshotAt, snapshot = 0, nil
local modelsAt, modelSnapshot = 0, nil
local bloodAt, bloodStatus = 0, ""
local donorSnapshot, donorFilter, donorLists
local previousPasses, previousTime, passesPerSecond = 0, nil, 0
local function now()
    return ui.get_time and ui.get_time() or nil
end
local function refresh()
    snapshot, modelSnapshot = nil, nil
    bloodAt = 0
end
local function inspect()
    local time = now()
    if not snapshot or not time or time >= snapshotAt then
        snapshot = {game.cosmetic_materials()}
        snapshotAt = (time or 0) + 0.2
        local coverage = snapshot[4]
        if time and coverage then
            local count = coverage.texture_passes or 0
            if previousTime and time > previousTime then
                passesPerSecond = math.max(0, count - previousPasses) / (time - previousTime)
            end
            previousPasses, previousTime = count, time
        end
    end
    return table.unpack(snapshot)
end

local function pack(c)
    local function byte(n) return math.floor(math.max(0, math.min(1, n)) * 255 + 0.5) end
    return byte(c[1]) | (byte(c[2]) << 8) | (byte(c[3]) << 16)
end
local function unpack_rgb(rgb)
    return {(rgb & 255) / 255, ((rgb >> 8) & 255) / 255, ((rgb >> 16) & 255) / 255}
end
local function color(label, c)
    c[1], c[2], c[3] = ui.color_edit3(label, c[1], c[2], c[3])
end
local function selectable(material)
    return material.name ~= "blue_double" and not string.find(string.lower(material.name), "sash", 1, true)
end
local function apply()
    local colors = {}
    for _, request in pairs(drafts) do colors[#colors + 1] = request end
    message = game.set_cosmetic_colors(colors) and
        "Queued for the game thread." or
        "Rejected: check the Ninja pin, supported SDK/executable and native-code compatibility; stop any other owner."
    refresh()
end

hook.add("gui", function()
    ui.text("COSMETIC EDITOR")
    ui.text_disabled("Experimental: requires this mod's updated EthhhSDK. Load a level; pin Ninja with Auto+ or Manual.")
    ui.text_disabled("Apply arms a session palette that follows level changes. Stop/disable cancels it; reload starts unarmed.")
    ui.text_disabled("No changes to belt rank, sword damage or saved progression.")
    if not game.cosmetic_materials then
        ui.text("This SDK does not expose the Cosmetic Editor API.")
        return
    end
    local materials, status, active, coverage = inspect()
    ui.text(status ~= "" and status or "Waiting for the next game tick to inspect named render entries.")
    if active then ui.text("Tint override is active.") end
    if coverage then ui.text_disabled(string.format("Linked head actors: %d", coverage.heads or 0)) end
    if ui.button("Stop and restore all colors") then
        game.clear_cosmetic_colors()
        if game.clear_cosmetic_blood then game.clear_cosmetic_blood() end
        drafts = {}
        refresh()
        message = "Restore queued. Paused gameplay must tick once to complete restoration."
    end
    if message then ui.text_disabled(message) end
    ui.separator()
    ui.text("SUPPLIED MONOCHROME TEXTURES")
    if game.set_cosmetic_textures then
        useSecondaryAtlas = ui.checkbox("Also replace secondary body atlas (may include skin/leather)", useSecondaryAtlas)
        if ui.button("Use supplied monochrome costume textures") then
            local ok, error = game.set_cosmetic_textures({
                "textures/ninja-blue-original-A00063740.tga",
                "textures/ninja-body-secondary-original-A00073a80.tga",
                "textures/ninja-head-original-A001b0720.tga",
            }, useSecondaryAtlas)
            message = ok and "Monochrome pack loaded; preparing draw-time overrides next tick." or error
            refresh()
        end
        ui.same_line()
        if ui.button("Use original textures") then game.clear_cosmetic_textures(); refresh() end
        ui.text_disabled("The supplied headgear texture is included; recolor it through the head part below.")
        ui.text_disabled("Main blue atlas is enabled by default; secondary mixed body regions require the explicit option above.")
        if coverage then
            ui.text(coverage.textures and "Monochrome pack armed." or "Native textures in use.")
            if coverage.texture_status and coverage.texture_status ~= "" then ui.text_disabled(coverage.texture_status) end
            ui.text_disabled(string.format("Texture passes: %d applied / %d refused", coverage.texture_passes or 0,
                coverage.texture_refusals or 0))
            ui.text_disabled(string.format("%.0f render passes/s (not uploads); GPU uploads: %d",
                passesPerSecond, coverage.texture_uploads or 0))
            if coverage.tick_ms then
                ui.text_disabled(string.format("Costume SDK tick work: %.3f ms; full check %.3f; peak %.3f (includes cold setup)",
                    coverage.tick_ms, coverage.inspection_ms or 0, coverage.peak_tick_ms or 0))
            end
        end
    else ui.text_disabled("Monochrome binding requires the updated SDK.") end
    ui.separator()
    ui.text("BELT + SASH / KNOT")
    if coverage and coverage.sash_status then ui.text_disabled(coverage.sash_status) end
    color("Belt RGB", belt)
    if ui.button("Apply belt color") then
        drafts["group/belt"] = {name="belt", part="group", rgb=pack(belt)}
        apply()
    end
    ui.same_line()
    if ui.button("Restore belt color") then
        drafts["group/belt"] = nil
        apply()
    end
    ui.separator()
    ui.text("COSTUME")
    ui.text_disabled("Tint multiplies the original texture; saturated/dark pixels cannot become arbitrary bright colors.")
    ui.text_disabled("Select costume/headgear parts. Belt and all sash segments use the separate belt controls.")
    color("Costume RGB", costume)
    ui.text("PARTS (BODY / HEADGEAR)")
    if ui.button("Apply color to selected parts") then
        for _, material in ipairs(materials) do
            local part = material.part or "body"
            local key = part .. "/" .. material.name
            if selectable(material) and selected[key] then
                drafts[key] = {name=material.name, part=part, rgb=pack(costume)}
            end
        end
        apply()
    end
    ui.same_line()
    if ui.button("Restore selected parts") then
        for key, enabled in pairs(selected) do if enabled then drafts[key] = nil end end
        apply()
    end
    for _, material in ipairs(materials) do
        if selectable(material) then
            local part = material.part or "body"
            local key = part .. "/" .. material.name
            selected[key] = ui.checkbox(part .. ": " .. material.name, selected[key] or false)
            ui.same_line()
            local c = unpack_rgb(material.rgb)
            ui.text(string.format("#%02X%02X%02X", math.floor(c[1]*255+0.5),
                math.floor(c[2]*255+0.5), math.floor(c[3]*255+0.5)))
        end
    end
    if #materials == 0 then ui.text_disabled("No named materials found yet.") end
    ui.separator()
    ui.text("BLOOD / POOLS / SPLIT INTERIORS / CUT GLOW")
    ui.text_disabled("One RGB covers splatters, three pool variants and the separate Ranx interior atlas.")
    ui.text_disabled("Glow targets the exact shared green asset, including any reused effects. Orange sparks are left native.")
    color("Blood RGB", blood)
    if game.set_cosmetic_blood then
        local glowChoice = ui.checkbox("Include shared green cut glow", sharedGlow)
        if glowChoice ~= sharedGlow then
            sharedGlow = glowChoice
            if game.set_cosmetic_glow_enabled then game.set_cosmetic_glow_enabled(sharedGlow) end
            bloodAt = 0
        end
        if ui.button("Apply blood color") then
            local ok, error = game.set_cosmetic_blood({
                "blood/blood-splatter-0-original-A001e0060.tga",
                "blood/blood-splatter-1-original-A001dfc20.tga",
                "blood/blood-splatter-2-original-A001df7e0.tga",
                "blood/blood-splatter-3-original-A001df3a0.tga",
                "blood/green-blood-pool-original-A001c7de0.tga",
            }, pack(blood), sharedGlow)
            message = ok and (error ~= "" and error or "Blood/interior/glow color request accepted.") or error
            bloodAt = 0
        end
        ui.same_line()
        if ui.button("Restore original blood") then game.clear_cosmetic_blood(); bloodAt = 0 end
        if game.set_cosmetic_glow_enabled and ui.button("Restore original glow only") then
            game.set_cosmetic_glow_enabled(false)
            sharedGlow, bloodAt = false, 0
            message = "Original glow restored; blood, pool and interior colors are unchanged."
        end
        if game.cosmetic_blood_status then
            local time = now()
            if not time or time >= bloodAt then
                bloodStatus = game.cosmetic_blood_status()
                bloodAt = (time or 0) + 0.2
            end
            ui.text_disabled(bloodStatus)
        end
    else ui.text_disabled("Blood recoloring requires the updated SDK.") end
    ui.separator()
    if ui.collapsing_header and not ui.collapsing_header("Character / prop / sword models (experimental)", false) then
        return
    end
    ui.text("CHARACTER / PROP / SWORD MODELS")
    ui.text_disabled("Known UI index/order issues: model swap repairs are deferred. See docs/cosmetic-model-noop-investigation.md.")
    if not game.cosmetic_models then
        ui.text_disabled("Model controls require a newer SDK build.")
        return
    end
    local time = now()
    if not modelSnapshot or modelRefresh or not time or time >= modelsAt then
        modelSnapshot = {game.cosmetic_models(modelRefresh)}
        modelRefresh = false
        modelsAt = (time or 0) + 0.2
    end
    local candidates, modelStatus, stats = table.unpack(modelSnapshot)
    if ui.button("Refresh loaded model list") then modelRefresh = true end
    ui.same_line()
    if ui.button("Stop all model / sword overrides") then
        game.clear_cosmetic_models()
        modelSnapshot = nil
        message = "Model stop/eligible sword restoration queued for the next tick."
    end
    if modelStatus ~= "" then ui.text(modelStatus) end
    ui.text_disabled(string.format("Last frame: %d donor captures, %d swaps, %d waiting, %d refused",
        stats.captured, stats.substituted, stats.waiting, stats.refused))
    ui.text_disabled("Experimental donor pose, NOT Ninja animation retargeting. Donor must be visible and render first.")
    ui.text_disabled("Off-screen/later/complex donors keep the native appearance. Larger costumes can clip; equipment stays attached.")
    filter = ui.input_text("Donor name filter", filter, 64)
    if donorSnapshot ~= modelSnapshot or donorFilter ~= filter then
        local donors, labels, swords, swordLabels = {}, {}, {}, {}
        for _, candidate in ipairs(candidates) do
            local label = string.format("%s @ %08X", candidate.name, candidate.addr)
            if filter == "" or string.find(string.lower(candidate.name), string.lower(filter), 1, true) then
                donors[#donors+1], labels[#labels+1] = candidate, label
            end
            if candidate.name == "NinjaSword" then
                swords[#swords+1], swordLabels[#swordLabels+1] = candidate, label
            end
        end
        donorSnapshot, donorFilter = modelSnapshot, filter
        donorLists = {donors, labels, swords, swordLabels}
    end
    local donors, labels, swords, swordLabels = table.unpack(donorLists)
    if #donors > 0 then
        donorIndex = math.max(1, math.min(donorIndex, #donors))
        donorIndex = ui.combo("Ninja appearance donor", donorIndex, labels)
        if ui.button("Apply donor to pinned Ninja") then
            message = game.set_cosmetic_model("player", 0, donors[donorIndex].addr) and
                "Player donor queued." or "Donor request rejected; check the exact Ninja pin."
        end
    else ui.text_disabled("No matching loaded donors. Refresh while a level is loaded.") end
    ui.separator()
    ui.text("COSMETIC SWORD (REAL TIER / DAMAGE UNCHANGED)")
    ui.text_disabled("Choose the exact NinjaSword explicitly: its ownership link is not yet verified. Stop before switching recipient.")
    if #swords > 0 then
        swordIndex = math.max(1, math.min(swordIndex, #swords))
        swordIndex = ui.combo("Sword recipient", swordIndex, swordLabels)
        swordTier = ui.combo("Built-in sword appearance", swordTier,
            {"Tier 0 model", "Tier 1 model", "Tier 2 model", "Tier 3 model", "Tier 4 model", "Tier 5 / black model"})
        if ui.button("Apply built-in sword appearance") then
            message = game.set_cosmetic_sword(swords[swordIndex].addr, swordTier - 1) and
                "Sword appearance queued." or "Sword request rejected; check pin/recipient and stop other overrides."
        end
        if #donors > 0 then
            bladeDonorIndex = math.max(1, math.min(bladeDonorIndex, #donors))
            bladeDonorIndex = ui.combo("Sword appearance donor (e.g. loaded Ranx weapon)", bladeDonorIndex, labels)
            if ui.button("Apply donor to selected sword") then
                message = game.set_cosmetic_model("sword", swords[swordIndex].addr, donors[bladeDonorIndex].addr) and
                    "Sword donor queued." or "Sword donor request rejected."
            end
        end
    else ui.text_disabled("No loaded NinjaSword found; refresh with the sword visible.") end
end)
