dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am a mystic."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am known as Maealil."})
keywordHandler:addKeyword({'spell'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I teach 'Light Healing', 'Antidote', 'Antidote Rune', 'Intense Healing', 'Intense Healing Rune', 'Ultimate Healing' and 'Ultimate Healing Rune'."})
keywordHandler:addKeyword({'magic'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I can heal you or teach you some spells of healing."})

local pendingSpell = {}
local spellOffers = {
	['light healing'] = {price = 170, vocations = {1, 2, 3, 4, 5, 6, 7, 8}, magicLevel = 1},
	['antidote'] = {price = 150, vocations = {1, 2, 3, 4, 5, 6, 7, 8}, magicLevel = 2},
	['intense healing'] = {price = 350, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 2},
	['antidote rune'] = {price = 600, vocations = {2, 6}, magicLevel = 5},
	['intense healing rune'] = {price = 600, vocations = {2, 6}, magicLevel = 4},
	['ultimate healing'] = {price = 1000, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 8},
	['ultimate healing rune'] = {price = 1500, vocations = {2, 6}, magicLevel = 11}
}

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	if msg == 'heal' then
		if getCreatureHealth(cid) <= 39 then
			npcHandler:say('You are looking really bad. Let me heal your wounds.', cid)
			doCreatureAddHealth(cid, -getCreatureHealth(cid) + 40)
			doSendMagicEffect(getPlayerPosition(cid), 12)
		else
			npcHandler:say("You aren't looking really bad. Sorry, I can't help you.", cid)
		end
		return true
	end

	local offer = spellOffers[msg]
	if offer then
		if not isInArray(offer.vocations, getPlayerVocation(cid)) then
			npcHandler:say("I am sorry but this spell is not available for your vocation.", cid)
			return true
		end
		pendingSpell[cid] = {name = msg, price = offer.price, magicLevel = offer.magicLevel}
		npcHandler.topic[cid] = 1
		npcHandler:say("Do you want to learn the spell '" .. msg .. "' for " .. offer.price .. " gold?", cid)
		return true
	elseif npcHandler.topic[cid] == 1 then
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
