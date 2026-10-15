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
function methods:SetTexture(texture) self.texture = texture end
function methods:SetOwner() end
function methods:SetHyperlink() end
function methods:AddMessage(message) self.message = message end
function CreateFrame(_, name)
    local f = object(name)
    frames[#frames + 1] = f
    if name then _G[name] = f end
    return f
end
function UnitName() return 'Tester' end
function GetRealmName() return 'Test Realm' end
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
local function emit(prefix, message, sender, channel)
    events.scripts.OnEvent(events, 'CHAT_MSG_ADDON', prefix, message, channel or 'WHISPER', sender or 'Tester')
end
local function tick(elapsed) events.scripts.OnUpdate(events, elapsed or 2) end
-- A forged server status from another player must not open the UI.
emit('SpellChoiceStatus', 'active', 'Attacker')
emit('SpellChoiceDrafts', '1', 'Attacker')
emit('SpellChoice', '100,200,300', 'Attacker')
emit('SpellChoiceStatus', 'active', 'Tester-OtherRealm')
emit('SpellChoiceStatus', 'active', 'Tester', 'PARTY')
tick()
assert(not SpellChoiceFrame.visible)
-- A session can receive inactive first and then become active after enrolling at the NPC.
emit('SpellChoiceStatus', 'inactive')
emit('SpellChoiceStatus', 'active')
emit('SpellChoiceDrafts', '1')
emit('SpellChoiceRerolls', '2')
emit('SpellChoiceBansLeft', '1')
emit('SpellChoiceBansReset', '')
emit('SpellChoiceBans', '') -- previously table.getn(string) crashed here.
emit('SpellChoice', '100,200,300')
emit('SpellChoiceRarities', '0,1,2')
tick()
assert(SpellChoiceFrame.visible)
assert(SpellChoiceButton1.id == 100)
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'RightButton')
assert(sent[#sent] == 'SC_BAN:100')
assert(not SpellChoiceButton1.enabled and not SpellChoiceRerollButton.enabled)
local pendingCount = #sent
-- Balances and ban acknowledgement precede the final offer: keep the action locked.
emit('SpellChoiceRerolls', '2')
emit('SpellChoiceBanAccepted', '100')
SpellChoiceButton2.scripts.OnClick(SpellChoiceButton2, 'LeftButton')
assert(#sent == pendingCount)
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

local function offer(ids, rarity)
    emit('SpellChoiceStatus', 'active', 'Tester-TestRealm')
    emit('SpellChoiceDrafts', '3')
    emit('SpellChoiceRerolls', '2')
    emit('SpellChoiceBansLeft', '1')
    emit('SpellChoiceBansReset', '')
    emit('SpellChoice', ids or '100,200,300')
    if rarity then emit('SpellChoiceRarities', rarity) end
    tick(0.2)
end

-- A rarity packet can arrive after the delayed initial draw and must repaint it.
offer('100,200,300')
assert(SpellChoiceFrame.visible and not SpellChoiceButton1Rarity.visible)
emit('SpellChoiceRarities', '5,3,1')
assert(SpellChoiceButton1Rarity.visible)
assert(SpellChoiceButton1Rarity.texture:match('BROK.tga$'))

-- Malformed protocol values cannot replace the valid current snapshot.
emit('SpellChoice', '400,400,600')
emit('SpellChoice', '400,500,600,')
emit('SpellChoice', '400,invalid500,600')
emit('SpellChoice', '0,500,600')
emit('SpellChoice', '4294967296,500,600')
emit('SpellChoiceRarities', '6,0,0')
emit('SpellChoiceDrafts', '-1')
emit('SpellChoiceRerolls', '1.5')
tick(0.2)
assert(SpellChoiceButton1.id == 100 and SpellChoiceButton1Rarity.texture:match('BROK.tga$'))
assert(SpellChoiceTitle.text == '3 elecciones pendientes')
assert(SpellChoiceRerollButton.text == 'Cambiar (2)')

-- Large ban lists are split across addon packets; resetting must clear the old list.
emit('SpellChoiceBans', '100')
emit('SpellChoiceBans', '200,100')
assert(not SpellChoiceButton1.enabled and not SpellChoiceButton2.enabled)
offer('100,200,300', '0,1,2')
assert(SpellChoiceButton1.enabled and SpellChoiceButton2.enabled)

emit('SpellChoiceBansLeft', '0')
local noBanCount = #sent
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'RightButton')
assert(#sent == noBanCount and UIErrorsFrame.message == 'No te quedan bloqueos.')

-- A previous action's recovery timer must not unlock the next action.
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'LeftButton')
tick(0.5)
emit('SpellChoice', '400,500,600')
emit('SpellChoiceRarities', '0,0,0')
SpellChoiceRerollButton.scripts.OnClick()
assert(sent[#sent] == 'SC_REROLL')
local nextCount = #sent
tick(0.5)
assert(#sent == nextCount and not SpellChoiceButton1.enabled)
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'LeftButton')
assert(#sent == nextCount)
tick(0.6)
assert(#sent == nextCount + 1 and sent[#sent] == 'SC_CHECK')

-- Denials release the UI without changing balances; completion cancels recovery work.
SpellChoiceRerollButton.scripts.OnClick()
emit('SpellChoiceRerollDenied', '0')
assert(SpellChoiceButton1.enabled and SpellChoiceRerollButton.text == 'Cambiar (2)')
SpellChoiceButton1.scripts.OnClick(SpellChoiceButton1, 'LeftButton')
local closeCount = #sent
emit('SpellChoiceStatus', 'inactive')
emit('SpellChoiceClose', '')
tick()
assert(#sent == closeCount)
assert(not SpellChoiceFrame.visible and not SpellChoiceDismissButton.visible)
assert(not SpellChoiceRerollButton.enabled)

-- The next enrollment starts with a visible offer rather than an old minimized state.
offer('700,800,900', '0,1,2')
SpellChoiceDismissButton.scripts.OnClick()
assert(not SpellChoiceFrame.visible)
emit('SpellChoiceStatus', 'inactive')
offer('1000,1100,1200', '0,1,2')
assert(SpellChoiceFrame.visible and SpellChoiceButton1.id == 1000)
print('PASS: sender validation, parsing, snapshot controls, split bans, late rarities, request timers and completion')
