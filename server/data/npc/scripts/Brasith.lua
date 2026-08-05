dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

shopModule:addBuyableItem({'corncob'}, 2686, 3)
shopModule:addBuyableItem({'cherry'}, 2679, 1)
shopModule:addBuyableItem({'grape'}, 2681, 3)
shopModule:addBuyableItem({'melon'}, 2682, 8)
shopModule:addBuyableItem({'banana'}, 2676, 2)
shopModule:addBuyableItem({'strawberry'}, 2680, 1)
shopModule:addBuyableItem({'carrot'}, 2684, 3)
shopModule:addBuyableItem({'pumpkin'}, 2683, 10)
shopModule:addBuyableItem({'bugmilk', 'milk'}, 2007, 15, 6, 'bottle of bugmilk')

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am Brasith Seedsinger."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "You may buy all the things we grow or gather at this place."})
keywordHandler:addKeyword({'offer'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell corncobs, cherries, grapes, melons, pumpkins, bananas, strawberries, carrots and bugmilk."})
keywordHandler:addKeyword({'food'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell corncobs, cherries, grapes, melons, pumpkins, bananas, strawberries, carrots and bugmilk."})

npcHandler:addModule(FocusModule:new())
