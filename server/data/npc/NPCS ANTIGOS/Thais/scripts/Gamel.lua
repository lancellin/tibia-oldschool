local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am selling some... things."
})
keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Names don't matter."
})
keywordHandler:addKeyword({"gamel"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Oh, you know my name. Please don't tell it to the others."
})

npcHandler:addModule(FocusModule:new())
