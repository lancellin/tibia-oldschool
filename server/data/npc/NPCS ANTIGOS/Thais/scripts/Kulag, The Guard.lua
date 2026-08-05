local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({"job"}, StdModule.say, {npcHandler = npcHandler, text = "I am a guard of Thais."})
keywordHandler:addKeyword({"king"}, StdModule.say, {npcHandler = npcHandler, text = "LONG LIVE KING TIBIANUS!"})
keywordHandler:addKeyword({"city"}, StdModule.say, {npcHandler = npcHandler, text = "The city is safe while we are watching."})
keywordHandler:addKeyword({"castle"}, StdModule.say, {npcHandler = npcHandler, text = "The castle is under constant guard."})
keywordHandler:addKeyword({"thais"}, StdModule.say, {npcHandler = npcHandler, text = "Welcome to Thais!"})

npcHandler:addModule(FocusModule:new())
