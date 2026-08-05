dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

function greetCallback(cid)
	if getPlayerSex(cid) == 1 then
		npcHandler:setMessage(MESSAGE_GREET, 'Welcome on board, Sir ' .. getPlayerName(cid) .. '.')
	else
		npcHandler:setMessage(MESSAGE_GREET, 'Welcome on board, Madam ' .. getPlayerName(cid) .. '.')
	end
	return true
end

npcHandler:setCallback(CALLBACK_GREET, greetCallback)

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is Captain Seagull from the Royal Tibia Line."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am the captain of this sailing ship."})
keywordHandler:addKeyword({'passage'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Where do you want to go? To Thais, Carlin, Venore or Edron?"})
keywordHandler:addKeyword({'trip'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Where do you want to go? To Thais, Carlin, Venore or Edron?"})
keywordHandler:addKeyword({'darashia'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I'm not sailing there. This route is afflicted by a ghost ship! However I've heard that Captain Fearless from Venore sails there."})

local function addTravelKeyword(keyword, cost, destination)
	local travelKeyword = keywordHandler:addKeyword({keyword}, StdModule.say, {
		npcHandler = npcHandler,
		text = 'Do you seek a passage to ' .. titleCase(keyword) .. ' for ' .. cost .. ' gold?',
		cost = cost,
		discount = 'postman'
	})
	travelKeyword:addChildKeyword({'yes'}, StdModule.travel, {
		npcHandler = npcHandler,
		premium = true,
		level = 0,
		cost = cost,
		discount = 'postman',
		destination = destination
	})
	travelKeyword:addChildKeyword({'no'}, StdModule.say, {
		npcHandler = npcHandler,
		text = 'We would like to serve you some time.',
		reset = true
	})
end

addTravelKeyword('carlin', 80, BOATPOS_CARLIN)
addTravelKeyword('edron', 70, BOATPOS_EDRON)
addTravelKeyword('thais', 130, BOATPOS_THAIS)
addTravelKeyword('venore', 90, BOATPOS_VENORE)

npcHandler:addModule(FocusModule:new())
