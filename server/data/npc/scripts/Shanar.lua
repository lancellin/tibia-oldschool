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

shopModule:addSellableItem({'coat'}, 2651, 1)
shopModule:addSellableItem({'jacket'}, 2650, 1)
shopModule:addSellableItem({'knight armor'}, 2476, 875)
shopModule:addSellableItem({'golden armor'}, 2466, 1500)
shopModule:addSellableItem({'leather armor'}, 2467, 12)
shopModule:addSellableItem({'chain armor'}, 2464, 70)
shopModule:addSellableItem({'brass armor'}, 2465, 150)
shopModule:addSellableItem({'plate armor'}, 2463, 400)
shopModule:addSellableItem({'leather helmet'}, 2461, 4)
shopModule:addSellableItem({'chain helmet'}, 2458, 17)
shopModule:addSellableItem({'steel helmet'}, 2457, 190)
shopModule:addSellableItem({'brass helmet'}, 2460, 30)
shopModule:addSellableItem({'viking helmet'}, 2473, 66)
shopModule:addSellableItem({'iron helmet'}, 2459, 145)
shopModule:addSellableItem({'devil helmet'}, 2462, 450)
shopModule:addSellableItem({'warrior helmet'}, 2475, 696)
shopModule:addSellableItem({'leather legs'}, 2649, 1)
shopModule:addSellableItem({'chain legs'}, 2648, 20)
shopModule:addSellableItem({'brass legs'}, 2478, 49)
shopModule:addSellableItem({'plate legs'}, 2647, 115)
shopModule:addSellableItem({'knight legs'}, 2477, 375)
shopModule:addSellableItem({'wooden shield'}, 2512, 5)
shopModule:addSellableItem({'battleshield'}, 2513, 95)
shopModule:addSellableItem({'steel shield'}, 2509, 80)
shopModule:addSellableItem({'brass shield'}, 2511, 16)
shopModule:addSellableItem({'plate shield'}, 2510, 31)
shopModule:addSellableItem({'guardian shield'}, 2515, 180)
shopModule:addSellableItem({'dragon shield'}, 2516, 360)
shopModule:addSellableItem({'two handed sword'}, 2377, 450)
shopModule:addSellableItem({'longsword'}, 2397, 51)
shopModule:addSellableItem({'dagger'}, 2379, 2)
shopModule:addSellableItem({'club'}, 2382, 1)
shopModule:addSellableItem({'rapier'}, 2384, 5)
shopModule:addSellableItem({'sabre'}, 2385, 12)
shopModule:addSellableItem({'spear'}, 2389, 3)
shopModule:addSellableItem({'short sword'}, 2406, 10)
shopModule:addSellableItem({'spike sword'}, 2383, 225)
shopModule:addSellableItem({'fire sword'}, 2392, 1000)
shopModule:addSellableItem({'sword'}, 2376, 25)
shopModule:addSellableItem({'mace'}, 2398, 23)

shopModule:addBuyableItem({'dagger'}, 2379, 5)
shopModule:addBuyableItem({'spear'}, 2389, 10)
shopModule:addBuyableItem({'rapier'}, 2384, 15)
shopModule:addBuyableItem({'sabre'}, 2385, 35)
shopModule:addBuyableItem({'staff'}, 2401, 40)
shopModule:addBuyableItem({'longsword'}, 2397, 160)
shopModule:addBuyableItem({'sword'}, 2376, 85)
shopModule:addBuyableItem({'machete'}, 2420, 35)
shopModule:addBuyableItem({'throwing knife'}, 2410, 25)
shopModule:addBuyableItem({'chain armor'}, 2464, 200)
shopModule:addBuyableItem({'brass armor'}, 2465, 450)
shopModule:addBuyableItem({'leather armor'}, 2467, 35)
shopModule:addBuyableItem({'chain helmet'}, 2458, 52)
shopModule:addBuyableItem({'leather helmet'}, 2461, 12)
shopModule:addBuyableItem({'steel shield'}, 2509, 240)
shopModule:addBuyableItem({'wooden shield'}, 2512, 15)
shopModule:addBuyableItem({'chain legs'}, 2648, 80)

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell weapons, shields and armor, and I teach protective spells."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am Shanar Ethkal."})
keywordHandler:addKeyword({'offer'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I deal in weapons, shields, armor and a handful of protective spells."})
keywordHandler:addKeyword({'weapon'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I have spears, swords, rapiers, daggers, longswords, machetes, staffs and sabres."})
keywordHandler:addKeyword({'armor'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am selling leather, chain and brass armor, as well as helmets, shields and chain legs."})
keywordHandler:addKeyword({'spell'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I teach 'Poison Field', 'Fire Field', 'Energy Field', 'Poison Wall', 'Fire Wall' and 'Energy Wall'."})

local spellOffers = {
	['poison field'] = {price = 300, vocations = {1, 2, 5, 6}, magicLevel = 2},
	['fire field'] = {price = 500, vocations = {1, 2, 5, 6}, magicLevel = 3},
	['energy field'] = {price = 700, vocations = {1, 2, 5, 6}, magicLevel = 5},
	['poison wall'] = {price = 1600, vocations = {1, 2, 5, 6}, magicLevel = 11},
	['fire wall'] = {price = 2000, vocations = {1, 2, 5, 6}, magicLevel = 13},
	['energy wall'] = {price = 2500, vocations = {1, 2, 5, 6}, magicLevel = 18}
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
			npcHandler:say("I am sorry but this spell is only for sorcerers and druids.", cid)
			return true
		end

		pendingSpell[cid] = {name = msg, price = offer.price, magicLevel = offer.magicLevel, vocations = offer.vocations}
		npcHandler.topic[cid] = 1
		npcHandler:say("Do you want to learn the spell '" .. msg .. "' for " .. offer.price .. " gold?", cid)
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
