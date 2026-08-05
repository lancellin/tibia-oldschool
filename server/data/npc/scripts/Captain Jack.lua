dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I'm ol' Captain Jack."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I work as a kind of ferryman. I transport wares and travellers for the monks."})
keywordHandler:addKeyword({'captain'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I work as a kind of ferryman. I transport wares and travellers for the monks."})
keywordHandler:addKeyword({'ship'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "All right she's small. But she's a real beauty, don't you think?"})
keywordHandler:addKeyword({'ferry'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "All right she's small. But she's a real beauty, don't you think?"})
keywordHandler:addKeyword({'wares'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "They always need provisions from the cities, and they sell their wine there."})
keywordHandler:addKeyword({'traveller'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Sometimes pilgrims come to this place. And now and then a monk leaves the monastery for some time."})
keywordHandler:addKeyword({'trip'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Where do you want to go today?"})
keywordHandler:addKeyword({'passage'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "If you have the abbot's permission, I can take you to the home of a local fisherman on the continent Tibia. We can't sail to Carlin though."})
keywordHandler:addKeyword({'carlin'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "For some obscure political reason the monks never sail to Carlin or Thais directly."})
keywordHandler:addKeyword({'island'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This is the isle of the kings. All the great Tibian leaders have found their final rest here under the monastery."})
keywordHandler:addKeyword({'monastery'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The white raven monastery is a place of wisdom and contemplation, or so the monks say. Sounds like a pretty boring place to me! HAR HAR!"})
keywordHandler:addKeyword({'white raven'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The order of the white raven."})

local function addTravelKeyword(keyword)
	local travelNode = keywordHandler:addKeyword({keyword}, StdModule.say, {
		npcHandler = npcHandler,
		onlyFocus = true,
		text = 'Friends of Dalbrect are my friends too! So you are looking for a passage to the continent for 20 gold?'
	})
	travelNode:addChildKeyword({'yes'}, StdModule.travel, {npcHandler = npcHandler, premium = false, level = 0, cost = 20, destination = FERRYPOS_TIBIA})
	travelNode:addChildKeyword({'no'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = 'We would like to serve you some time.', reset = true})
end

addTravelKeyword('continent')
addTravelKeyword('tibia')

npcHandler:addModule(FocusModule:new())
