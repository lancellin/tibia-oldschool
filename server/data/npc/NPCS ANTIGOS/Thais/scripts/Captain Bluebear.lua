local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local destinations = {
	{keyword = "carlin", name = "Carlin", cost = 110, destination = Position(32387, 31820, 6)},
	{keyword = "ab'dendriel", name = "Ab'Dendriel", cost = 130, destination = Position(32734, 31668, 6)},
	{keyword = "edron", name = "Edron", cost = 160, destination = Position(33175, 31764, 6)},
	{keyword = "venore", name = "Venore", cost = 170, destination = Position(32954, 32022, 6)}
}

local function addTravelKeyword(entry)
	local text = string.format("Do you seek a passage to %s for %d gold?", entry.name, entry.cost)
	local travelKeyword = keywordHandler:addKeyword({entry.keyword}, StdModule.say, {
		npcHandler = npcHandler,
		text = text
	})
	travelKeyword:addChildKeyword({"yes"}, StdModule.travel, {
		npcHandler = npcHandler,
		premium = true,
		cost = entry.cost,
		destination = entry.destination
	})
	travelKeyword:addChildKeyword({"no"}, StdModule.say, {
		npcHandler = npcHandler,
		text = "We would like to serve you some time.",
		reset = true
	})
end

for _, entry in ipairs(destinations) do
	addTravelKeyword(entry)
end

keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "My name is Captain Bluebear from the Royal Tibia Line."
})
keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am the captain of this sailing-ship."
})
keywordHandler:addKeyword({"captain"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am the captain of this sailing-ship."
})
keywordHandler:addKeyword({"ship"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Royal Tibia Line connects all seaside towns of Tibia."
})
keywordHandler:addKeyword({"line"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Royal Tibia Line connects all seaside towns of Tibia."
})
keywordHandler:addKeyword({"company"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Royal Tibia Line connects all seaside towns of Tibia."
})
keywordHandler:addKeyword({"route"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Royal Tibia Line connects all seaside towns of Tibia."
})
keywordHandler:addKeyword({"tibia"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "The Royal Tibia Line connects all seaside towns of Tibia."
})
keywordHandler:addKeyword({"good"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "We can transport everything you want."
})
keywordHandler:addKeyword({"passanger"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "We would like to welcome you on board."
})
keywordHandler:addKeyword({"passenger"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "We would like to welcome you on board."
})
keywordHandler:addKeyword({"trip"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"passage"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"town"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"destination"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"sail"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"go"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Where do you want to go? To {Carlin}, {Ab'Dendriel}, {Venore} or {Edron}?"
})
keywordHandler:addKeyword({"ice"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm sorry, but we don't serve the routes to the Ice Islands."
})
keywordHandler:addKeyword({"senja"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm sorry, but we don't serve the routes to the Ice Islands."
})
keywordHandler:addKeyword({"folda"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm sorry, but we don't serve the routes to the Ice Islands."
})
keywordHandler:addKeyword({"vega"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm sorry, but we don't serve the routes to the Ice Islands."
})
keywordHandler:addKeyword({"darashia"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm not sailing there. This route is afflicted by a ghost ship! However I've heard that Captain Fearless from Venore sails there."
})
keywordHandler:addKeyword({"darama"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I'm not sailing there. This route is afflicted by a ghost ship! However I've heard that Captain Fearless from Venore sails there."
})
keywordHandler:addKeyword({"ghost"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Many people who sailed to Darashia never returned because they were attacked by a ghostship! I'll never sail there!"
})
keywordHandler:addKeyword({"thais"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "This is Thais. Where do you want to go?"
})

npcHandler:setMessage(MESSAGE_GREET, "Welcome on board, |PLAYERNAME|.")
npcHandler:setMessage(MESSAGE_FAREWELL, "Good bye. Recommend us if you were satisfied with our service.")
npcHandler:setMessage(MESSAGE_WALKAWAY, "Good bye. Recommend us if you were satisfied with our service.")

npcHandler:addModule(FocusModule:new())
