dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Not that I like to talk to you, but I am Elathriel Shadowslayer."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am the leader of the Kuridai and the Az'irel of Ab'Dendriel."})
keywordHandler:addKeyword({'key'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "If you are that curious, I may sell you a key to the Hellgate."})
keywordHandler:addKeyword({'spell'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell 'Light Magic Missile', 'Heavy Magic Missile', 'Fireball', 'Great Fireball', 'Firebomb' and 'Explosion'."})

local pendingSpell = {}
local spellOffers = {
	['light magic missile'] = {price = 200, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 1},
	['heavy magic missile'] = {price = 600, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 3},
	['fireball'] = {price = 800, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 5},
	['great fireball'] = {price = 1200, vocations = {1, 2, 5, 6}, magicLevel = 9},
	['firebomb'] = {price = 1500, vocations = {1, 2, 5, 6}, magicLevel = 9},
	['explosion'] = {price = 1800, vocations = {1, 2, 5, 6}, magicLevel = 12}
}

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	if msgcontains(msg, 'key') then
		npcHandler:say("If you are that curious, do you want to buy a key for 5000 gold? Don't blame me if you get sucked in.", cid)
		npcHandler.topic[cid] = 1
		return true
	elseif npcHandler.topic[cid] == 1 then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveMoney(cid, 5000) then
				local key = doPlayerAddItem(cid, 2088, 1)
				if key then
					doSetItemActionId(key, 3012)
				end
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say('Come back when you have enough money.', cid)
			end
		else
			npcHandler:say("Believe me, it's better for you that way.", cid)
		end
		npcHandler.topic[cid] = 0
		return true
	end

	local offer = spellOffers[msg]
	if offer then
		if not isInArray(offer.vocations, getPlayerVocation(cid)) then
			npcHandler:say("I am sorry but this spell is not available for your vocation.", cid)
			return true
		end

		pendingSpell[cid] = {name = msg, price = offer.price, magicLevel = offer.magicLevel}
		npcHandler.topic[cid] = 2
		npcHandler:say("Do you want to learn the spell '" .. msg .. "' for " .. offer.price .. " gold?", cid)
		return true
	elseif npcHandler.topic[cid] == 2 then
		local spell = pendingSpell[cid]
		if msgcontains(msg, 'yes') and spell then
			if getPlayerMagLevel(cid) < spell.magicLevel then
				npcHandler:say("You must have magic level " .. spell.magicLevel .. " or better to learn this spell!", cid)
			elseif getPlayerLearnedInstantSpell(cid, spell.name) then
				npcHandler:say("You already know how to cast this spell.", cid)
			elseif doPlayerRemoveMoney(cid, spell.price) then
				playerLearnInstantSpell(cid, spell.name)
				doSendMagicEffect(getPlayerPosition(cid), 14)
				npcHandler:say("Here you are. Look in your spellbook for the pronounciation of this spell.", cid)
			else
				npcHandler:say("Oh. You do not have enough money.", cid)
			end
		else
			npcHandler:say("Maybe next time.", cid)
		end
		pendingSpell[cid] = nil
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
