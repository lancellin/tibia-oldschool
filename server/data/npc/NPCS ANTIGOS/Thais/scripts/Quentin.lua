local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid)              npcHandler:onCreatureAppear(cid)            end
function onCreatureDisappear(cid)           npcHandler:onCreatureDisappear(cid)         end
function onCreatureSay(cid, type, msg)      npcHandler:onCreatureSay(cid, type, msg)    end
function onThink()                          npcHandler:onThink()                        end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	if msgcontains(msg, "heal") then
		local player = Player(cid)
		if player then
			local currentHealth = player:getHealth()

			if player:getCondition(CONDITION_FIRE) then
				npcHandler:say("You are burning. I will help you.", cid)
				player:removeCondition(CONDITION_FIRE)
				player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
			elseif player:getCondition(CONDITION_POISON) then
				npcHandler:say("You are poisoned. I will help you.", cid)
				player:removeCondition(CONDITION_POISON)
				player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
			elseif currentHealth < 65 then
				npcHandler:say("You are looking really bad. Let me heal your wounds.", cid)
				player:addHealth(65 - currentHealth)
				player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
			else
				npcHandler:say("You aren't looking that bad. Sorry, I can't help you.", cid)
			end
		end
		return true
	end

	return true
end

keywordHandler:addKeyword({"help"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "First you should try to get some gold to buy better {equipment}."
})
keywordHandler:addKeyword({"gold", "money"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "If you need {money} you should slay monsters and take their gold. Look for {spiders} and {rats}."
})
keywordHandler:addKeyword({"equipment"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "First you should buy a bag or backpack. That way your hands will be free to hold a weapon and a shield."
})
keywordHandler:addKeyword({"spiders", "spider"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "There are {spiders}' nests beyond our city near {Gorn}'s shop and at the McRonalds' farm in the east."
})
keywordHandler:addKeyword({"rats"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "There are sewers underneath the city. They say these sewers are brimming with {rats}."
})
keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "My name is Quentin."
})
keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Job? I have no job. I just live for the gods of Tibia."
})
keywordHandler:addKeyword({"gods"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "They created {Tibia} and all life on it."
})
keywordHandler:addKeyword({"tibia"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "That is where we are. The world of {Tibia}. Admire its beauty."
})
keywordHandler:addKeyword({"monsters", "monster"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "There are really too many of them in {Tibia}. But who am I to challenge the wisdom of the {gods}?"
})

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
