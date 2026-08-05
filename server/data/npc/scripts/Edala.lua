dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am a mystic of the suns. I provide protective blessings for those in need."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is Edala, pilgrim."})
keywordHandler:addKeyword({'bless'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "There are five different blessings available in five sacred places."})
keywordHandler:addKeyword({'phoenix'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The spark of the phoenix is given by the dwarven priests in Kazordoon."})
keywordHandler:addKeyword({'embrace'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The druids north of Carlin will provide you with the embrace of Tibia."})
keywordHandler:addKeyword({'wisdom'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Talk to the hermit Eremo on the isle of Cormaya about this blessing."})

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

	if msgcontains(msg, 'fire') or msgcontains(msg, 'sun') then
		npcHandler:say('Do you wish to receive the blessing of the two suns? It will cost you 10000 gold, pilgrim.', cid)
		npcHandler.topic[cid] = 1
	elseif npcHandler.topic[cid] == 1 then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveMoney(cid, 10000) then
				if doPlayerAddBless(cid, 3) then
					doSendMagicEffect(getPlayerPosition(cid), 13)
					setPlayerStorageValue(cid, 30006, 1)
					npcHandler:say('Kneel down and receive the warmth of sunfire, pilgrim.', cid)
				else
					doPlayerAddMoney(cid, 10000)
					npcHandler:say('You already possess this blessing.', cid)
				end
			else
				npcHandler:say('Oh. You do not have enough money.', cid)
			end
		else
			npcHandler:say('Ok. Suits me.', cid)
		end
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
