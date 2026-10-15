-- WoW 3.3.5a / Lua 5.1. All decisions and balances are authoritative on the server.
local frame = SpellChoiceFrame
local buttons = {SpellChoiceButton1, SpellChoiceButton2, SpellChoiceButton3}
local textures = {"COMM", "UNCO", "RARE", "EPIC", "LEGE", "BROK"}
local active, dismissed = false, false
local drafts, rerolls, bans = 0, 0, 0
local choices, rarities, banned = {}, {}, {}
local tasks, generation = {}, 0
local eventFrame = CreateFrame("Frame")
local requestPending = false
local Render

local function Later(seconds, callback)
    table.insert(tasks, {remaining = seconds, callback = callback})
end

eventFrame:SetScript("OnUpdate", function(_, elapsed)
    -- Remove before invoking: callbacks may schedule more work.
    for i = #tasks, 1, -1 do
        local task = tasks[i]
        task.remaining = task.remaining - elapsed
        if task.remaining <= 0 then
            table.remove(tasks, i)
            task.callback()
        end
    end
end)

local function Request(message)
    local name = UnitName("player")
    if name then
        SendChatMessage(message, "WHISPER", nil, name)
    end
end

local function ParseNumbers(message)
    local result = {}
    for value in string.gmatch(message or "", "%d+") do
        table.insert(result, tonumber(value))
    end
    return result
end

local function SelfSender(sender)
    if not sender then return false end
    -- Server sends these packets as a whisper from the player to themself.
    local name = UnitName("player")
    return name and (sender == name or sender:match("^([^%-]+)") == name)
end

local function RefreshControls()
    SpellChoiceTitle:SetText(drafts .. " elecciones pendientes")
    SpellChoiceRerollButton:SetText("Reroll (" .. rerolls .. ")")
    SpellChoiceBanButton:SetText("Bloqueos: " .. bans)
    if active and drafts > 0 and rerolls > 0 and not requestPending then
        SpellChoiceRerollButton:Enable()
    else
        SpellChoiceRerollButton:Disable()
    end
    SpellChoiceDismissButton:SetText(dismissed and (drafts .. " pendientes - abrir") or "Ocultar")
end

local function Choose(self, mouseButton)
    local id = self:GetID()
    if not active or requestPending or not id or id <= 0 or banned[id] then return end
    requestPending = true
    local command = mouseButton == "RightButton" and "SC_BAN:" or "SC:"
    Request(command .. id)
    RefreshControls()
    -- Recover from a lost response without optimistically spending a balance.
    Later(1, function()
        requestPending = false
        if active then Request("SC_CHECK") end
        RefreshControls()
    end)
end

SpellChoiceBanButton = CreateFrame("Button", "SpellChoiceBanButton", frame, "UIPanelButtonTemplate")
SpellChoiceBanButton:SetSize(180, 22)
SpellChoiceBanButton:SetPoint("LEFT", SpellChoiceRerollButton, "RIGHT", 10, 0)
SpellChoiceBanButton:SetScript("OnClick", function()
    UIErrorsFrame:AddMessage("Clic derecho en una carta para bloquearla.", 1, 0.82, 0)
end)

Render = function()
    RefreshControls()
    if not active or drafts == 0 or #choices ~= 3 then
        frame:Hide()
        SpellChoiceDismissButton:Hide()
        return
    end
    if dismissed then
        -- Keep a small independent button available to reopen the choice window.
        SpellChoiceDismissButton:SetParent(UIParent)
        SpellChoiceDismissButton:ClearAllPoints()
        SpellChoiceDismissButton:SetPoint("CENTER", UIParent, "CENTER", 0, -180)
        frame:Hide()
        SpellChoiceDismissButton:Show()
        return
    end
    SpellChoiceDismissButton:SetParent(frame)
    SpellChoiceDismissButton:ClearAllPoints()
    SpellChoiceDismissButton:SetPoint("CENTER", SpellChoiceTitle, "TOP", 0, -290)
    for i, button in ipairs(buttons) do
        local id = choices[i]
        local name, _, icon = GetSpellInfo(id)
        button:SetID(id)
        _G[button:GetName() .. "Name"]:SetText(name or ("Hechizo " .. id))
        _G[button:GetName() .. "Icon"]:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
        _G[button:GetName() .. "LevelReq"]:SetText("Izquierdo: elegir | Derecho: bloquear")
        local rarity = _G[button:GetName() .. "Rarity"]
        local texture = textures[(rarities[i] or -1) + 1]
        if texture then
            rarity:SetTexture("Interface\\AddOns\\HabilidadesRandom\\Textures\\" .. texture .. ".tga")
            rarity:Show()
        else
            rarity:Hide()
        end
        if banned[id] or requestPending then button:Disable() else button:Enable() end
        button:Show()
    end
    SpellChoiceDismissButton:Show()
    frame:Show()
end

for _, button in ipairs(buttons) do
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button:SetScript("OnClick", Choose)
    button:SetScript("OnEnter", function(self)
        if self:GetID() > 0 then
            GameTooltip:SetOwner(self, "ANCHOR_CURSOR")
            GameTooltip:SetHyperlink("spell:" .. self:GetID())
            GameTooltip:Show()
        end
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

SpellChoiceRerollButton:SetScript("OnClick", function()
    if not active or drafts == 0 or rerolls <= 0 or requestPending then return end
    requestPending = true
    Request("SC_REROLL")
    RefreshControls()
    Later(1, function()
        requestPending = false
        if active then Request("SC_CHECK") end
        RefreshControls()
    end)
end)

SpellChoiceDismissButton:SetScript("OnClick", function()
    dismissed = not dismissed
    Render()
end)

local function HideChoices()
    generation = generation + 1
    choices, rarities = {}, {}
    requestPending = false
    frame:Hide()
    SpellChoiceDismissButton:Hide()
end

eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")
eventFrame:SetScript("OnEvent", function(_, event, prefix, message, channel, sender)
    if event == "PLAYER_LOGIN" then
        Later(1, function() Request("SC_CHECK") end)
        return
    end
    if not SelfSender(sender) or channel ~= "WHISPER" then return end
    if prefix == "SpellChoiceStatus" then
        active = message == "active"
        if not active then
            drafts, rerolls, bans = 0, 0, 0
            banned = {}
            HideChoices()
        end
    elseif prefix == "SpellChoiceClose" then
        HideChoices()
    elseif prefix == "SpellChoiceDrafts" then
        drafts = tonumber(message) or 0
        Render()
    elseif prefix == "SpellChoiceRerolls" then
        rerolls = tonumber(message) or 0
        requestPending = false
        RefreshControls()
    elseif prefix == "SpellChoiceBansLeft" then
        bans = tonumber(message) or 0
        RefreshControls()
    elseif prefix == "SpellChoiceBans" then
        banned = {}
        for _, id in ipairs(ParseNumbers(message)) do banned[id] = true end
    elseif prefix == "SpellChoice" and active then
        choices = ParseNumbers(message)
        requestPending = false
        generation = generation + 1
        local expected = generation
        -- Delay display until the following rarity packet has been received.
        Later(0.1, function() if active and expected == generation then Render() end end)
    elseif prefix == "SpellChoiceRarities" then
        rarities = ParseNumbers(message)
    elseif prefix == "SpellChoiceBanAccepted" then
        requestPending = false
    elseif prefix == "SpellChoiceBanDenied" or prefix == "SpellChoiceRerollDenied" then
        requestPending = false
        UIErrorsFrame:AddMessage("No se pudo cambiar la oferta. No se consumió saldo.", 1, 0.2, 0.2)
        Render()
    end
end)

ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", function(_, _, message)
    return message == "SC_CHECK" or message == "SC_REROLL" or message:match("^SC:%d+$") or
        message:match("^SC_BAN:%d+$")
end)
frame:Hide()
