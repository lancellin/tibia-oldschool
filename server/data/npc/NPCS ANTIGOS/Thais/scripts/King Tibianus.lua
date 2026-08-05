local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am your king, Tibianus."
})
keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am King Tibianus."
})
keywordHandler:addKeyword({"king"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am King Tibianus, ruler of Thais."
})
keywordHandler:addKeyword({"thais"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Thais is the heart of my kingdom."
})

local promoteNode = keywordHandler:addKeyword({"promot"}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = "I can promote you for 20000 gold coins. Do you want me to promote you?"
})
promoteNode:addChildKeyword({"yes"}, StdModule.promotePlayer, {
	npcHandler = npcHandler,
	cost = 20000,
	level = 20,
	text = "Congratulations! You are now promoted."
})
promoteNode:addChildKeyword({"no"}, StdModule.say, {
	npcHandler = npcHandler,
	onlyFocus = true,
	text = "Alright then, come back when you are ready.",
	reset = true
})

npcHandler:addModule(FocusModule:new())
