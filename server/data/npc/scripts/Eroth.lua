dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am the leader of the Cenath caste."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is Eroth Ramathi."})
keywordHandler:addKeyword({'spell'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I can teach 'Magic Shield', 'Invisible', 'Summon Creature', 'Convince Creature', 'Creature Illusion', 'Chameleon' and 'Destroy Field'."})
keywordHandler:addKeyword({'magic'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Magic is the domain of the Cenath."})

local spellOffers = {
	['magic shield'] = {price = 450, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 4},
	['invisible'] = {price = 1000, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 15, spellName = 'invisible'},
	['summon creature'] = {price = 2000, vocations = {1, 2, 5, 6}, magicLevel = 16},
	['convince creature'] = {price = 1500, vocations = {2, 6}, magicLevel = 10},
	['creature illusion'] = {price = 1000, vocations = {1, 2, 5, 6}, magicLevel = 10},
	['chameleon'] = {price = 1300, vocations = {2, 6}, magicLevel = 11},
	['destroy field'] = {price = 350, vocations = {1, 2, 3, 5, 6, 7}, magicLevel = 6}
}

local pendingSpell = {}

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	local offer = spellOffers[msg]
	if offer then
		if not isInArray(offer.vocations, getPlayerVocation(cid)) then
			npcHandler:say("I am sorry but this spell is not available for your vocation.", cid)
			return true
		end

		pendingSpell[cid] = {name = offer.spellName or msg, price = offer.price, magicLevel = offer.magicLevel}
		npcHandler.topic[cid] = 1
		npcHandler:say("Do you want to learn the spell '" .. (offer.spellName or msg) .. "' for " .. offer.price .. " gold?", cid)
		return true
	end

	if npcHandler.topic[cid] == 1 then
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
