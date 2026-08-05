local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local topic = {
	GAMEL_REBEL = 1,
	GAMEL_PLANS = 2,
	MAGIC_CRYSTAL = 3
}

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	if msgcontains(msg, "gamel rebel") or msgcontains(msg, "gamel is rebel") or msgcontains(msg, "gamel is a rebel") then
		npcHandler:say("Are you saying that Gamel is a member of the rebellion?", cid)
		npcHandler.topic[cid] = topic.GAMEL_REBEL
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.GAMEL_REBEL then
		npcHandler:say("Do you know what his plans are about?", cid)
		npcHandler.topic[cid] = topic.GAMEL_PLANS
	elseif msgcontains(msg, "no") and npcHandler.topic[cid] == topic.GAMEL_REBEL then
		npcHandler:say("Then don't bother me with that. I am a busy man.", cid)
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, "magic crystal") and npcHandler.topic[cid] == topic.GAMEL_PLANS then
		npcHandler:say("That is terrible! Will you give me the crystal?", cid)
		npcHandler.topic[cid] = topic.MAGIC_CRYSTAL
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.MAGIC_CRYSTAL then
		if player:removeItem(2177, 1) then
			player:addItem(2168, 1)
			npcHandler:say("Thank you! Take this ring. If you ever need a healing, come, bring the scroll, and ask me to 'heal'.", cid)
			npcHandler.topic[cid] = 0
		else
			npcHandler:say("Sorry, you have none.", cid)
		end
	elseif msgcontains(msg, "no") and npcHandler.topic[cid] == topic.MAGIC_CRYSTAL then
		npcHandler:say("Traitor!", cid)
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
