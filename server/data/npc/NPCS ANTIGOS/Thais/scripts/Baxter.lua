local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local topic = {
	RAT_BOUNTY = 1
}

local insultingWords = {
	"fuck",
	"idiot",
	"ass",
	"stupid",
	"tyrant",
	"shit"
}

local fireCondition = Condition(CONDITION_FIRE)
fireCondition:setParameter(CONDITION_PARAM_DELAYED, true)
fireCondition:addDamage(3, 1000, -10)

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	if msgcontains(msg, "rat") then
		npcHandler:say("Do you bring a freshly killed rat for a bounty of 1 gold?.", cid)
		npcHandler.topic[cid] = topic.RAT_BOUNTY
		return true
	end

	if msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.RAT_BOUNTY then
		if player:removeItem(2813, 1) then
			player:addMoney(1)
			npcHandler:say("Here is your reward. You will become a great warrior some day.", cid)
		else
			npcHandler:say("Look like it wasn't as dead as you thought ... it's gone.", cid)
		end
		npcHandler.topic[cid] = 0
		return true
	end

	if msgcontains(msg, "no") and npcHandler.topic[cid] == topic.RAT_BOUNTY then
		npcHandler:say("Come on. Don't waste my time with your jests.", cid)
		npcHandler.topic[cid] = 0
		return true
	end

	for _, word in ipairs(insultingWords) do
		if msgcontains(msg, word) then
			npcHandler:say("Take this!", cid)
			player:addCondition(fireCondition)
			npcHandler.topic[cid] = 0
			return true
		end
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
