-- Minimal WoW API double: execute the real addon and drive its event/click handlers.
local frames, sent = {}, {}
local methods = {}
local function object(name)
    local o = {name = name, scripts = {}, visible = false, id = 0, enabled = true}
    return setmetatable(o, {__index = methods})
end
function methods:SetScript(event, handler) self.scripts[event] = handler end
function methods:RegisterEvent(event) self[event] = true end
function methods:RegisterForClicks() end
function methods:GetName() return self.name end
function methods:SetID(id) self.id = id end
function methods:GetID() return self.id end
function methods:SetText(text) self.text = text end
function methods:Show() self.visible = true end
function methods:Hide() self.visible = false end
function methods:Enable() self.enabled = true end
function methods:Disable() self.enabled = false end
function methods:SetParent(parent) self.parent = parent end
function methods:SetSize() end
function methods:SetPoint() end
function methods:ClearAllPoints() end
function methods:SetTexture() end
function methods:SetOwner() end
function methods:SetHyperlink() end
function methods:AddMessage() end
function CreateFrame(_, name)
    local f = object(name)
    frames[#frames + 1] = f
    if name then _G[name] = f end
    return f
end
function UnitName() return 'Tester' end
function SendChatMessage(msg) sent[#sent + 1] = msg end
function GetSpellInfo(id) return 'Spell ' .. id, nil, 'Interface\\Icons\\Test' end
function ChatFrame_AddMessageEventFilter() end
UIParent = object('UIParent')
GameTooltip = object('GameTooltip')
UIErrorsFrame = object('UIErrorsFrame')
for _, name in ipairs({'SpellChoiceFrame','SpellChoiceTitle','SpellChoiceRerollButton','SpellChoiceDismissButton'}) do
    _G[name] = object(name)
end
for i = 1, 3 do
    local name = 'SpellChoiceButton' .. i
    _G[name] = object(name)
    for _, suffix in ipairs({'Name','Icon','LevelReq','Rarity'}) do _G[name .. suffix] = object(name .. suffix) end
end
assert(loadfile('client/addon/HabilidadesRandom/SpellChoice.lua'))()
local events
for _, f in ipairs(frames) do if f.CHAT_MSG_ADDON then events = f end end
assert(events)
local function emit(prefix, message, sender)
    events.scripts.OnEvent(events, 'CHAT_MSG_ADDON', prefix, message, 'WHISPER', sender or 'Tester')
end
local function tick() events.scripts.OnUpdate(events, 2) end
-- A forged server status from another player must not open the UI.
emit('SpellChoiceStatus', 'active', 'Attacker')
emit('SpellChoiceDrafts', '1', 'Attacker')
emit('SpellChoice', '100,200,300', 'Attacker')
tick()
assert(not SpellChoiceFrame.visible)
-- A session can receive inactive first and then become active after enrolling at the NPC.
emit('SpellChoiceStatus', 'inactive')
emit('SpellChoiceStatus', 'active')
emit('SpellChoiceDrafts', '1')
emit('SpellChoiceRerolls', '2')
emit('SpellChoiceBansLeft', '1')
emit('SpellChoiceBans', '') -- previously table.getn(string) crashed here.
emit('SpellChoice', '100,200,300')
emit('SpellChoiceRarities', '0,1,2')
tick()
assert(SpellChoiceFrame.visible)
assert(SpellChoiceButton1.id == 100)
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'RightButton')
assert(sent[#sent] == 'SC_BAN:100')
-- Replacement and same-list refresh must be rendered, not lost to stale deduplication.
emit('SpellChoiceBans', '100')
emit('SpellChoice', '400,200,300')
tick()
assert(SpellChoiceButton1.id == 400)
SpellChoiceDismissButton.scripts.OnClick()
assert(not SpellChoiceFrame.visible and SpellChoiceDismissButton.visible)
SpellChoiceDismissButton.scripts.OnClick()
assert(SpellChoiceFrame.visible)
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'LeftButton')
assert(sent[#sent] == 'SC:400')
-- A delayed display cannot reopen the event after completion at 80.
emit('SpellChoice', '400,200,300')
emit('SpellChoiceStatus', 'inactive')
emit('SpellChoiceClose', '')
tick()
assert(not SpellChoiceFrame.visible and not SpellChoiceDismissButton.visible)
assert(not SpellChoiceRerollButton.enabled)
print('PASS: sender validation, enrollment, empty bans, choices, ban, reopen and level-80 close')
