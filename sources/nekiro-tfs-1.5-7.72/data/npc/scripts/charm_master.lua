local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

local CRITICAL_CHARM_ID = 1
local ACTIVATION_GOLD = 10000
local ACTIVATION_ITEM_ID = 2160
local ACTIVATION_ITEM_COUNT = 1

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if msgcontains(msg, "charm") or msgcontains(msg, "critical") or msgcontains(msg, "activate") then
		local state = player:getCharmState(CRITICAL_CHARM_ID)
		if state == 0 then
			npcHandler:say("You must first unlock Savage Blow with Charm Points in the Cyclopedia.", cid)
			npcHandler.topic[cid] = 0
		elseif state == 2 then
			npcHandler:say("Savage Blow is already active.", cid)
			npcHandler.topic[cid] = 0
		else
			npcHandler:say(
				"Activation requires " .. ACTIVATION_GOLD .. " gold and " .. ACTIVATION_ITEM_COUNT ..
				" crystal coin. Do you want to activate Savage Blow?",
				cid
			)
			npcHandler.topic[cid] = 1
		end
		return true
	end

	if npcHandler.topic[cid] == 1 then
		if msgcontains(msg, "yes") then
			if player:getMoney() < ACTIVATION_GOLD then
				npcHandler:say("You do not have enough gold.", cid)
			elseif player:getItemCount(ACTIVATION_ITEM_ID) < ACTIVATION_ITEM_COUNT then
				npcHandler:say("You do not have the required crystal coin.", cid)
			elseif not player:removeMoney(ACTIVATION_GOLD) then
				npcHandler:say("I could not collect the activation gold.", cid)
			elseif not player:removeItem(ACTIVATION_ITEM_ID, ACTIVATION_ITEM_COUNT) then
				player:addMoney(ACTIVATION_GOLD)
				npcHandler:say("I could not collect the required item.", cid)
			elseif player:activateCharm(CRITICAL_CHARM_ID) then
				npcHandler:say("Savage Blow is now active. Its critical strikes affect PvE direct hits only.", cid)
			else
				player:addMoney(ACTIVATION_GOLD)
				player:addItem(ACTIVATION_ITEM_ID, ACTIVATION_ITEM_COUNT)
				npcHandler:say("The charm could not be activated. Your payment has been returned.", cid)
			end
		else
			npcHandler:say("Come back when you are ready.", cid)
		end
		npcHandler.topic[cid] = 0
		return true
	end

	return false
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
