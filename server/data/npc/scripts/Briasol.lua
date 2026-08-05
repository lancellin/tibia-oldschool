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

shopModule:addSellableItem({'white pearl'}, 2143, 160)
shopModule:addSellableItem({'black pearl'}, 2144, 280)
shopModule:addSellableItem({'small diamond'}, 2145, 300)
shopModule:addSellableItem({'small sapphire'}, 2146, 250)
shopModule:addSellableItem({'small ruby'}, 2147, 250)
shopModule:addSellableItem({'small emerald'}, 2149, 250)
shopModule:addSellableItem({'small amethyst'}, 2150, 200)

shopModule:addBuyableItem({'wedding ring'}, 2121, 990)
shopModule:addBuyableItem({'golden amulet'}, 2130, 6600)
shopModule:addBuyableItem({'ruby necklace'}, 2133, 3560)
shopModule:addBuyableItem({'white pearl'}, 2143, 320)
shopModule:addBuyableItem({'black pearl'}, 2144, 560)
shopModule:addBuyableItem({'small diamond'}, 2145, 600)
shopModule:addBuyableItem({'small sapphire'}, 2146, 500)
shopModule:addBuyableItem({'small ruby'}, 2147, 500)
shopModule:addBuyableItem({'small emerald'}, 2149, 500)
shopModule:addBuyableItem({'small amethyst'}, 2150, 400)

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am a jeweller and exchange money."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am Briasol Crithanath."})
keywordHandler:addKeyword({'offer'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I can sell gems, pearls, and jewels. I also exchange gold, platinum and crystal coins."})
keywordHandler:addKeyword({'gem'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "You can buy and sell small diamonds, sapphires, rubies, emeralds, and amethysts."})
keywordHandler:addKeyword({'pearl'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I have white and black pearls for sale, but you also can sell me some."})
keywordHandler:addKeyword({'jewel'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "You can purchase wedding rings, golden amulets and ruby necklaces."})

local exchangeState = {}

local function resetExchange(cid)
	exchangeState[cid] = nil
	npcHandler.topic[cid] = 0
end

local function promptExchange(cid, mode, prompt)
	exchangeState[cid] = {mode = mode}
	npcHandler.topic[cid] = 1
	npcHandler:say(prompt, cid)
end

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	local state = exchangeState[cid]

	if msgcontains(msg, 'change gold') then
		promptExchange(cid, 'gold_to_platinum', 'How many platinum coins do you want to get?')
		return true
	elseif msgcontains(msg, 'change platinum') then
		promptExchange(cid, 'platinum_choose', 'Do you want to change your platinum coins to gold or crystal?')
		return true
	elseif msgcontains(msg, 'change crystal') then
		promptExchange(cid, 'crystal_to_platinum', 'How many crystal coins do you want to change to platinum?')
		return true
	end

	if not state then
		return true
	end

	if state.mode == 'gold_to_platinum' then
		local amount = getMoneyCount(msg)
		if amount <= 0 or amount >= 999 then
			npcHandler:say('Well, can I help you with something else?', cid)
			resetExchange(cid)
			return true
		end

		state.amount = amount
		state.cost = amount * 100
		state.mode = 'gold_to_platinum_confirm'
		npcHandler:say('So I should change ' .. state.cost .. ' of your gold coins to ' .. amount .. ' platinum coins for you?', cid)
		return true
	elseif state.mode == 'gold_to_platinum_confirm' then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveItem(cid, 2148, state.cost) then
				doPlayerAddItem(cid, 2152, state.amount)
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say("You don't have money.", cid)
			end
		else
			npcHandler:say('Ok. We cancel.', cid)
		end
		resetExchange(cid)
		return true
	elseif state.mode == 'platinum_choose' then
		if msgcontains(msg, 'gold') then
			state.mode = 'platinum_to_gold'
			npcHandler:say('How many platinum coins do you want to change to gold?', cid)
		elseif msgcontains(msg, 'crystal') then
			state.mode = 'platinum_to_crystal'
			npcHandler:say('How many crystal coins do you want to get?', cid)
		else
			npcHandler:say('Do you want to change your platinum coins to gold or crystal?', cid)
		end
		return true
	elseif state.mode == 'platinum_to_gold' then
		local amount = getMoneyCount(msg)
		if amount <= 0 or amount >= 999 then
			npcHandler:say('Well, can I help you with something else?', cid)
			resetExchange(cid)
			return true
		end
		state.amount = amount
		state.cost = amount * 100
		state.mode = 'platinum_to_gold_confirm'
		npcHandler:say('So I should change ' .. amount .. ' of your platinum coins to ' .. state.cost .. ' gold coins for you?', cid)
		return true
	elseif state.mode == 'platinum_to_gold_confirm' then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveItem(cid, 2152, state.amount) then
				doPlayerAddItem(cid, 2148, state.cost)
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say("You don't have money.", cid)
			end
		else
			npcHandler:say('Ok. We cancel.', cid)
		end
		resetExchange(cid)
		return true
	elseif state.mode == 'platinum_to_crystal' then
		local amount = getMoneyCount(msg)
		if amount <= 0 or amount >= 999 then
			npcHandler:say('Well, can I help you with something else?', cid)
			resetExchange(cid)
			return true
		end
		state.amount = amount
		state.cost = amount * 100
		state.mode = 'platinum_to_crystal_confirm'
		npcHandler:say('So I should change ' .. state.cost .. ' of your platinum coins to ' .. amount .. ' crystal coins for you?', cid)
		return true
	elseif state.mode == 'platinum_to_crystal_confirm' then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveItem(cid, 2152, state.cost) then
				doPlayerAddItem(cid, 2160, state.amount)
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say("You don't have money.", cid)
			end
		else
			npcHandler:say('Ok. We cancel.', cid)
		end
		resetExchange(cid)
		return true
	elseif state.mode == 'crystal_to_platinum' then
		local amount = getMoneyCount(msg)
		if amount <= 0 or amount >= 999 then
			npcHandler:say('Well, can I help you with something else?', cid)
			resetExchange(cid)
			return true
		end
		state.amount = amount
		state.cost = amount * 100
		state.mode = 'crystal_to_platinum_confirm'
		npcHandler:say('So I should change ' .. amount .. ' of your crystal coins to ' .. state.cost .. ' platinum coins for you?', cid)
		return true
	elseif state.mode == 'crystal_to_platinum_confirm' then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveItem(cid, 2160, state.amount) then
				doPlayerAddItem(cid, 2152, state.cost)
				npcHandler:say('Here you are.', cid)
			else
				npcHandler:say("You don't have money.", cid)
			end
		else
			npcHandler:say('Ok. We cancel.', cid)
		end
		resetExchange(cid)
		return true
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
