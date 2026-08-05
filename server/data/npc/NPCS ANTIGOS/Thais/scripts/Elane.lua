local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I teach the ways of the paladins."
})
keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am Elane."
})
keywordHandler:addKeyword({"spell"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I can teach you {light healing}, {find person}, {great light}, {magic rope}, {conjure arrow}, {conjure bolt}, {poison arrow}, and {explosive arrow}."
})
keywordHandler:addKeyword({"offer"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I can teach you {light healing}, {find person}, {great light}, {magic rope}, {conjure arrow}, {conjure bolt}, {poison arrow}, and {explosive arrow}."
})

local spells = {
	{{"light healing"}, "light healing", 170},
	{{"find person"}, "find person", 80},
	{{"great light"}, "great light", 500},
	{{"magic rope"}, "magic rope", 450},
	{{"conjure arrow"}, "conjure arrow", 450},
	{{"conjure bolt"}, "conjure bolt", 600},
	{{"poison arrow"}, "poison arrow", 700},
	{{"explosive arrow"}, "explosive arrow", 1200}
}

for _, entry in ipairs(spells) do
	local node = keywordHandler:addKeyword(entry[1], StdModule.say, {
		npcHandler = npcHandler,
		text = "I can teach you " .. entry[2] .. " for " .. entry[3] .. " gold. Do you want to learn it?"
	})
	node:addChildKeyword({"yes"}, StdModule.learnSpell, {
		npcHandler = npcHandler,
		spellName = entry[2],
		price = entry[3],
		premium = false
	})
	node:addChildKeyword({"no"}, StdModule.say, {
		npcHandler = npcHandler,
		text = "Very well.",
		reset = true
	})
end

npcHandler:addModule(FocusModule:new())
