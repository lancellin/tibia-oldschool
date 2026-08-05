dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local shopModule = ShopModule:new()
npcHandler:addModule(shopModule)

shopModule:addBuyableItem({'crossbow'}, 2455, 450)
shopModule:addBuyableItem({'bow'}, 2456, 350)
shopModule:addBuyableItem({'bolt'}, 2543, 3)

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell bows, arrows, crossbows and bolts. I also teach some spells."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am known as Irea."})
keywordHandler:addKeyword({'spell'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I teach 'Conjure Arrow', 'Poison Arrow' and 'Explosive Arrow'."})
keywordHandler:addKeyword({'magic'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I teach spells to create enchanted arrows."})

local pendingSpell = {}
local pendingArrow = {}
local spellOffers = {
	['conjure arrow'] = {price = 450, vocations = {3, 7}, magicLevel = 2},
	['poison arrow'] = {price = 700, vocations = {3, 7}, magicLevel = 5},
	['explosive arrow'] = {price = 1000, vocations = {3, 7}, magicLevel = 10}
}

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	local offer = spellOffers[msg]
	if offer then
		if not isInArray(offer.vocations, getPlayerVocation(cid)) then
			npcHandler:say("I am sorry but this spell is only for paladins.", cid)
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
		return true
	end

	if msgcontains(msg, 'arrow') then
		local amount = getCount(msg)
		if amount < 1 then
			amount = 1
		elseif amount > 100 then
			amount = 100
		end
		pendingArrow[cid] = amount
		npcHandler.topic[cid] = 2
		npcHandler:say('Would you like to buy ' .. amount .. ' ' .. (amount == 1 and 'arrow' or 'arrows') .. ' for ' .. (amount * 2) .. ' gold?', cid)
		return true
	elseif npcHandler.topic[cid] == 2 then
		if msgcontains(msg, 'yes') then
			local amount = pendingArrow[cid] or 1
			if doPlayerRemoveMoney(cid, amount * 2) then
				doPlayerAddItem(cid, 2544, amount)
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say("Sorry, you don't have enough money.", cid)
			end
		else
			npcHandler:say('Then not.', cid)
		end
		pendingArrow[cid] = nil
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
