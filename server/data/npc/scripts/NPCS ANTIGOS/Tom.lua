local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

shopModule:addSellableItem({'dead rabbit'}, 2992, 2, 'dead rabbit')
shopModule:addSellableItem({'dead rat'}, 2813, 2, 'dead rat')
shopModule:addSellableItem({'fresh dead rat'}, 3073, 2, 'fresh dead rat')
shopModule:addSellableItem({'dead wolf'}, 2826, 5, 'dead wolf')

npcHandler:addModule(FocusModule:new())
