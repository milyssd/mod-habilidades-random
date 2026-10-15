-- WoW 3.3.5a / Lua 5.1. El servidor decide las ofertas y los saldos.
local frame = SpellChoiceFrame
local buttons = {SpellChoiceButton1, SpellChoiceButton2, SpellChoiceButton3}
local textures = {"COMM", "UNCO", "RARE", "EPIC", "LEGE", "BROK"}
local active, dismissed = false, false
local drafts, rerolls, bans = 0, 0, 0
local choices, rarities, banned = {}, {}, {}
local tasks, generation = {}, 0
local eventFrame = CreateFrame("Frame")
local requestPending = false
local requestSerial = 0
local Render

local function Later(seconds, callback)
    table.insert(tasks, {remaining = seconds, callback = callback})
end

eventFrame:SetScript("OnUpdate", function(_, elapsed)
    -- Quitar antes de ejecutar: una tarea puede programar otra.
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

local function ParseInteger(value)
    if type(value) ~= "string" or not value:match("^%d+$") then return nil end
    local number = tonumber(value)
    if not number or number > 4294967295 then return nil end
    return number
end

local function ParseNumbers(message)
    local result = {}
    if message == "" then return result end
    if type(message) ~= "string" or message:sub(-1) == "," then return nil end
    for value in string.gmatch(message .. ",", "(.-),") do
        local number = ParseInteger(value)
        if not number then return nil end
        table.insert(result, number)
    end
    return result
end

local function SelfSender(sender)
    if not sender then return false end
    -- El core envía los datos como un susurro del personaje a sí mismo.
    local name = UnitName("player")
    if not name then return false end
    if sender == name then return true end
    local realm = GetRealmName and GetRealmName()
    return realm and sender == name .. "-" .. realm:gsub("%s", "")
end

local function RefreshControls()
    SpellChoiceTitle:SetText(requestPending and "Esperando al servidor..." or
        (drafts .. (drafts == 1 and " elección pendiente" or " elecciones pendientes")))
    SpellChoiceRerollButton:SetText("Cambiar (" .. rerolls .. ")")
    SpellChoiceBanButton:SetText("Bloqueos: " .. bans)
    if active and drafts > 0 and #choices == 3 and rerolls > 0 and not requestPending then
        SpellChoiceRerollButton:Enable()
    else
        SpellChoiceRerollButton:Disable()
    end
    SpellChoiceDismissButton:SetText(dismissed and (drafts .. " pendientes - abrir") or "Ocultar")
end

local function FinishRequest()
    requestSerial = requestSerial + 1
    requestPending = false
end

local function SendAction(message)
    requestSerial = requestSerial + 1
    local expected = requestSerial
    requestPending = true
    Request(message)
    Render()
    -- Recuperar el estado sin descontar saldos en el cliente. Una respuesta o
    -- un cierre invalida esta tarea para que no afecte a una solicitud posterior.
    Later(1, function()
        if not active or expected ~= requestSerial then return end
        FinishRequest()
        Request("SC_CHECK")
        Render()
    end)
end

local function Choose(self, mouseButton)
    local id = self:GetID()
    if not active or drafts == 0 or #choices ~= 3 or requestPending or not id or id <= 0 or banned[id] then return end
    if mouseButton == "RightButton" and bans == 0 then
        UIErrorsFrame:AddMessage("No te quedan bloqueos.", 1, 0.82, 0)
        return
    end
    local command = mouseButton == "RightButton" and "SC_BAN:" or "SC:"
    SendAction(command .. id)
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
        -- Mantener un botón independiente para volver a abrir la oferta.
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
        _G[button:GetName() .. "LevelReq"]:SetText("Clic izquierdo: elegir\nClic derecho: bloquear")
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
    if not active or drafts == 0 or #choices ~= 3 or rerolls <= 0 or requestPending then return end
    SendAction("SC_REROLL")
end)

SpellChoiceDismissButton:SetScript("OnClick", function()
    dismissed = not dismissed
    Render()
end)

local function HideChoices()
    generation = generation + 1
    choices, rarities = {}, {}
    FinishRequest()
    RefreshControls()
    for _, button in ipairs(buttons) do button:Disable() end
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
            dismissed = false
            banned = {}
            HideChoices()
        end
    elseif prefix == "SpellChoiceClose" then
        HideChoices()
    elseif prefix == "SpellChoiceDrafts" then
        local value = ParseInteger(message)
        if not value then return end
        drafts = value
        Render()
    elseif prefix == "SpellChoiceRerolls" then
        local value = ParseInteger(message)
        if not value then return end
        rerolls = value
        RefreshControls()
    elseif prefix == "SpellChoiceBansLeft" then
        local value = ParseInteger(message)
        if not value then return end
        bans = value
        RefreshControls()
    elseif prefix == "SpellChoiceBansReset" then
        banned = {}
    elseif prefix == "SpellChoiceBans" then
        local values = ParseNumbers(message)
        if not values then return end
        for _, id in ipairs(values) do banned[id] = true end
        Render()
    elseif prefix == "SpellChoice" and active then
        local values = ParseNumbers(message)
        if not values or #values ~= 3 or values[1] == 0 or values[2] == 0 or values[3] == 0 or
            values[1] == values[2] or values[1] == values[3] or values[2] == values[3] then return end
        choices, rarities = values, {}
        FinishRequest()
        generation = generation + 1
        local expected = generation
        -- Dejar que llegue también el siguiente paquete con las rarezas.
        Later(0.1, function() if active and expected == generation then Render() end end)
    elseif prefix == "SpellChoiceRarities" then
        local values = ParseNumbers(message)
        if not values or #values ~= 3 or values[1] > 5 or values[2] > 5 or values[3] > 5 then return end
        rarities = values
        Render()
    elseif prefix == "SpellChoiceBanDenied" or prefix == "SpellChoiceRerollDenied" then
        FinishRequest()
        UIErrorsFrame:AddMessage("No se pudo cambiar la oferta. No se consumió saldo.", 1, 0.2, 0.2)
        Render()
    end
end)

ChatFrame_AddMessageEventFilter("CHAT_MSG_WHISPER_INFORM", function(_, _, message)
    return message == "SC_CHECK" or message == "SC_REROLL" or message:match("^SC:%d+$") or
        message:match("^SC_BAN:%d+$")
end)
frame:Hide()
