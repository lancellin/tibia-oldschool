dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am the overseer of the mines."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am called Shirith Blooddancer."})
keywordHandler:addKeyword({'mines'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "We hardly get the ore we need. The worthless trolls are lazy workers. I keep them locked up the whole time."})
keywordHandler:addKeyword({'locked'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I keep the keys to the mines."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "It is |TIME|."})

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)
	if msgcontains(msg, 'key') or msgcontains(msg, 'keys') then
		npcHandler:say('You are not one of my usual helpers. But if you insist, do you want to buy the key to the mines for 1000 gold?', cid)
		npcHandler.topic[cid] = 1
	elseif npcHandler.topic[cid] == 1 then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveMoney(cid, 1000) then
				local key = doPlayerAddItem(cid, 2088, 1)
				if key then
					doSetItemActionId(key, 3010)
				end
				npcHandler:say('Here you are. Do not lose it.', cid)
			else
				npcHandler:say("You don't have enough money.", cid)
			end
		else
			npcHandler:say('Then not.', cid)
		end
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
