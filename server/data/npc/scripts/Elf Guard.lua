dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is unimportant, only my duty does matter."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am a guardian of this town. I have no time to chat!"})
keywordHandler:addKeyword({'town'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This is the elven town of Ab'Dendriel."})
keywordHandler:addKeyword({"ab'dendriel"}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This is the elven town of Ab'Dendriel."})
keywordHandler:addKeyword({'cenath'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The Cenath are magic users. Look for them on the upper levels of the town."})
keywordHandler:addKeyword({'kuridai'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The Kuridai are the smiths and craftsmen. Look for them in the underground parts of the town."})
keywordHandler:addKeyword({'deraisim'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The Deraisim are scouts and hunters. You may find them on the ground level of the town."})
keywordHandler:addKeyword({"tha'shi ab'dendriel"}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "In the crude human language you would translate it with 'My life for Ab'Dendriel'."})
keywordHandler:addKeyword({"bahaha aka"}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This means 'Take your punishment, defiler'."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "It's |TIME|."})

npcHandler:addModule(FocusModule:new())
