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

shopModule:addBuyableItem({'life'}, 2006, 60, 10, 'life fluid')
shopModule:addBuyableItem({'mana'}, 2006, 55, 7, 'mana fluid')
shopModule:addBuyableItem({'spellbook'}, 2175, 150)
shopModule:addBuyableItem({'antidote rune'}, 2266, 65, 1, 'antidote rune')
shopModule:addBuyableItem({'blank rune'}, 2260, 10)
shopModule:addBuyableItem({'convince creature rune'}, 2290, 80, 1, 'convince creature rune')
shopModule:addBuyableItem({'destroy field rune'}, 2261, 45, 3, 'destroy field rune')
shopModule:addBuyableItem({'energy field rune'}, 2277, 115, 3, 'energy field rune')
shopModule:addBuyableItem({'energy wall rune'}, 2279, 340, 4, 'energy wall rune')
shopModule:addBuyableItem({'explosion rune', 'explosion'}, 2313, 190, 3, 'explosion rune')
shopModule:addBuyableItem({'fire bomb rune'}, 2305, 235, 2, 'fire bomb rune')
shopModule:addBuyableItem({'fire field rune'}, 2301, 85, 3, 'fire field rune')
shopModule:addBuyableItem({'fire wall rune'}, 2303, 245, 4, 'fire wall rune')
shopModule:addBuyableItem({'great fireball rune', 'gfb'}, 2304, 180, 4, 'great fireball rune')
shopModule:addBuyableItem({'heavy magic missile rune', 'hmm'}, 2311, 120, 5, 'heavy magic missile rune')
shopModule:addBuyableItem({'intense healing rune', 'ih'}, 2265, 95, 1, 'intense healing rune')
shopModule:addBuyableItem({'light magic missile rune', 'lmm'}, 2287, 40, 10, 'light magic missile rune')
shopModule:addBuyableItem({'poison field rune'}, 2285, 65, 3, 'poison field rune')
shopModule:addBuyableItem({'poison wall rune'}, 2289, 210, 4, 'poison wall rune')
shopModule:addBuyableItem({'sudden death rune', 'sd'}, 2268, 325, 1, 'sudden death rune')
shopModule:addBuyableItem({'ultimate healing rune', 'uh'}, 2273, 175, 1, 'ultimate healing rune')
shopModule:addBuyableItem({'wand of cosmic energy'}, 2189, 10000)
shopModule:addBuyableItem({'wand of plague'}, 2188, 5000)
shopModule:addBuyableItem({'wand of dragonbreath'}, 2191, 1000)
shopModule:addBuyableItem({'wand of vortex'}, 2190, 500)
shopModule:addBuyableItem({'moonlight rod'}, 2186, 1000)
shopModule:addBuyableItem({'volcanic rod'}, 2185, 5000)
shopModule:addBuyableItem({'snakebite rod'}, 2182, 500)
shopModule:addBuyableItem({'quagmire rod'}, 2181, 10000)

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell mystic runes, spellbooks, wands, rods and fluids of life or mana."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am Shiriel Sharaziel."})
keywordHandler:addKeyword({'rune'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I sell blank runes and many spell runes."})
keywordHandler:addKeyword({'wand of inferno'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Sorry, this wand contains magic far too powerful and we are afraid to store it here. I heard they have a few of these at the Edron academy though."})
keywordHandler:addKeyword({'tempest rod'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Sorry, this rod contains magic far too powerful and we are afraid to store it here. I heard they have a few of these at the Edron academy though."})

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	if msgcontains(msg, 'vial') or msgcontains(msg, 'deposit') or msgcontains(msg, 'flask') then
		npcHandler:say("I will pay you 5 gold for every empty vial. Ok?", cid)
		npcHandler.topic[cid] = 1
		return true
	elseif npcHandler.topic[cid] == 1 then
		if msgcontains(msg, 'yes') then
			if sellPlayerEmptyVials(cid) then
				npcHandler:say("Here's your money!", cid)
			else
				npcHandler:say("You don't have any empty vials!", cid)
			end
		else
			npcHandler:say("Then not.", cid)
		end
		npcHandler.topic[cid] = 0
		return true
	end

	if getPlayerStorageValue(cid, 999) == -1 and (msgcontains(msg, 'rod') or msgcontains(msg, 'wand')) then
		if getPlayerVocation(cid) == 1 or getPlayerVocation(cid) == 5 then
			doPlayerAddItem(cid, 2190, 1)
			npcHandler:say("Here's your wand!", cid)
			setPlayerStorageValue(cid, 999, 1)
		elseif getPlayerVocation(cid) == 2 or getPlayerVocation(cid) == 6 then
			doPlayerAddItem(cid, 2182, 1)
			npcHandler:say("Here's your rod!", cid)
			setPlayerStorageValue(cid, 999, 1)
		else
			npcHandler:say("I'm sorry, but you're neither sorcerer nor druid!", cid)
		end
		return true
	end

	if msgcontains(msg, 'rod') then
		npcHandler:say("Rods can be wielded by druids only. There are snakebite, moonlight, volcanic, quagmire and tempest rods.", cid)
	elseif msgcontains(msg, 'wand') then
		npcHandler:say("Wands can be wielded by sorcerers only. There are vortex, dragonbreath, plague, cosmic energy and inferno wands.", cid)
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
