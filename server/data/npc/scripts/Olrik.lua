dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am working here at the post office. If you have questions about the Royal Tibia Mail System or the depots ask me."})
keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "My name is Olrik."})
keywordHandler:addKeyword({'depot'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The depots are easy to use. Just step in front of them and you will find your items in them."})
keywordHandler:addKeyword({'mail'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "Our mail system is unique! Everyone can use it."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "It's |TIME|."})

local rollByPlayer = {}

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	msg = string.lower(msg)

	if msgcontains(msg, 'parcel') then
		npcHandler:say("Would you like to buy a parcel for 10 gold?", cid)
		npcHandler.topic[cid] = 1
	elseif msgcontains(msg, 'letter') then
		npcHandler:say("Would you like to buy a letter for 5 gold?", cid)
		npcHandler.topic[cid] = 2
	elseif npcHandler.topic[cid] == 1 then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveMoney(cid, 10) then
				doPlayerAddItem(cid, 2595, 1)
				doPlayerAddItem(cid, 2599, 1)
				npcHandler:say("Here you are. Don't forget to write the name and address of the receiver on the label.", cid)
			else
				npcHandler:say("Oh, you do not have enough gold to buy a parcel.", cid)
			end
		else
			npcHandler:say("Ok.", cid)
		end
		npcHandler.topic[cid] = 0
	elseif npcHandler.topic[cid] == 2 then
		if msgcontains(msg, 'yes') then
			if doPlayerRemoveMoney(cid, 5) then
				doPlayerAddItem(cid, 2597, 1)
				npcHandler:say("Here it is. Don't forget the receiver's name on the first line and the address on the second one.", cid)
			else
				npcHandler:say("Oh, you do not have enough gold to buy a letter.", cid)
			end
		else
			npcHandler:say("Ok.", cid)
		end
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, 'mail') then
		npcHandler:say("The Tibia Mail System enables you to send and receive letters and parcels. You can buy them here if you want.", cid)
	elseif msgcontains(msg, 'measurements') and getPlayerStorageValue(cid, 234) > 0 and getPlayerStorageValue(cid, 240) < 1 then
		npcHandler:say("Let's make this a bit more exciting. I will roll a die. If I roll a 6 you win and I'll tell you what you need to know. Otherwise I win and get 5 gold. Deal?", cid)
		rollByPlayer[cid] = math.random(1, 6)
		npcHandler.topic[cid] = 3
	elseif npcHandler.topic[cid] == 3 then
		if msgcontains(msg, 'yes') then
			if getPlayerMoney(cid) < 5 then
				npcHandler:say("I am sorry, but you don't have so much money.", cid)
			elseif rollByPlayer[cid] == 6 then
				npcHandler:say("Ok, here we go ... 6! You have won! So listen ... <tells you what you need to know>", cid)
				setPlayerStorageValue(cid, 240, 1)
				setPlayerStorageValue(cid, 234, getPlayerStorageValue(cid, 234) + 1)
			else
				doPlayerRemoveMoney(cid, 5)
				npcHandler:say("Ok, and it's not 6. You have lost. Another game?", cid)
				rollByPlayer[cid] = math.random(1, 6)
				npcHandler.topic[cid] = 4
				return true
			end
		else
			npcHandler:say("This way you'll never get my measurements.", cid)
		end
		npcHandler.topic[cid] = 0
	elseif npcHandler.topic[cid] == 4 then
		if msgcontains(msg, 'yes') then
			if getPlayerMoney(cid) < 5 then
				npcHandler:say("I am sorry, but you don't have so much money.", cid)
			elseif rollByPlayer[cid] == 6 then
				npcHandler:say("Ok, here we go ... 6! You have won! So listen ... <tells you what you need to know>", cid)
				setPlayerStorageValue(cid, 240, 1)
				setPlayerStorageValue(cid, 234, getPlayerStorageValue(cid, 234) + 1)
			else
				doPlayerRemoveMoney(cid, 5)
				npcHandler:say("Ok, and it's not 6. You have lost.", cid)
			end
		else
			npcHandler:say("This way you'll never get my measurements.", cid)
		end
		npcHandler.topic[cid] = 0
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
