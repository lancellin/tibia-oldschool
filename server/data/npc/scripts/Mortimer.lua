local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'My name is Mortimer.'})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'I coordinate research efforts for fellow explorers and scholars.'})
keywordHandler:addKeyword({'mission'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Research never ends. Bring useful information to the right people and progress will follow.'})
keywordHandler:addKeyword({'research'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'With the right reports and samples, our researchers will make steady progress.'})
keywordHandler:addKeyword({'report'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Accurate reports are often more valuable than brute force.'})
keywordHandler:addKeyword({'explorer'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'Explorers who survive long enough usually learn that preparation matters more than courage.'})

npcHandler:addModule(FocusModule:new())
