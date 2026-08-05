dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

keywordHandler:addKeyword({'name'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am known as the riddler. That is all you need to know."})
keywordHandler:addKeyword({'job'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am the guardian of the paradox tower."})
keywordHandler:addKeyword({'time'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "It is the age of the talon."})
keywordHandler:addKeyword({'tower'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This tower, of course, silly one. It holds my master's treasure."})
keywordHandler:addKeyword({'paradox'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "This tower, of course, silly one. It holds my master's treasure."})
keywordHandler:addKeyword({'master'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "His name is none of your business."})
keywordHandler:addKeyword({'guard'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am guarding the treasures of the tower. Only those who pass the test of the three sigils may pass."})
keywordHandler:addKeyword({'treasure'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "I am guarding the treasures of the tower. Only those who pass the test of the three sigils may pass."})
keywordHandler:addKeyword({'key'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The key of this tower! You will never find it! A malicious plant spirit is guarding it!"})
keywordHandler:addKeyword({'door'}, StdModule.say, {npcHandler = npcHandler, onlyFocus = true, text = "The key of this tower! You will never find it! A malicious plant spirit is guarding it!"})

local passPosition = Position(32478, 31905, 1)
local failPosition = Position(32725, 31589, 12)
local prompt = "Death awaits those who fail the test of the three seals! Do you really want me to test you?"
local abandoned = "Better for you..."
local wrong = "NO! HAHA! YOU FAILED!"

local questions = {
	[1] = {answer = "yes", reply = "FOOL! Now you're doomed! But well ... So be it! Let's start out with the Seal of Knowledge and the first question: What name did the necromant king choose for himself?"},
	[2] = {answer = "goshnar", reply = "HOHO! You have learned your lesson well. Question number two then: Who or what is the feared Hugo?"},
	[3] = {answer = "demonbunny", reply = "HOHO! Right again. All right. The final question of the first seal: Who was the first warrior to follow the path of the Mooh'Tah?"},
	[4] = {answer = "tha'kull", reply = "HOHO! Lucky you. You have passed the first seal! So ... would you like to continue with the Seal of the Mind?"},
	[5] = {answer = "yes", reply = "As you wish, foolish one! Here is my first question: Its lighter then a feather but no living creature can hold it for ten minutes?"},
	[6] = {answer = "breath", reply = "That was an easy one. Let's try the second: If you name it, you break it."},
	[7] = {answer = "silence", reply = "Hm. I bet you think you're smart. All right. How about this: What does everybody want to become but nobody to be?"},
	[8] = {answer = "old", reply = "ARGH! You did it again! Well all right. Do you wish to break the Seal of Madness?"},
	[9] = {answer = "yes", reply = "GOOD! So I will get you at last. Answer this: What is my favourite colour?"},
	[10] = {answer = "green", reply = "UHM UH OH ... How could you guess that? Are you mad??? All right. Question number two: What is the opposite?"},
	[11] = {answer = "none", reply = "NO! NO! NO! That can't be true. You're not only mad, you are a complete idiot! Ah well. Here is the last question: What is 1 plus 1?"},
	[12] = {answer = "__fail_number__"},
	[13] = {answer = "yes", reply = "FOOL! Now you're doomed! But well ... So be it! Let's start out with the Seal of Knowledge and the first question: What name did the necromant king choose for himself?"},
	[14] = {answer = "goshnar", reply = "HOHO! You have learned your lesson well. Question number two then: Who or what is the feared Hugo?"},
	[15] = {answer = "demonbunny", reply = "HOHO! Right again. All right. The final question of the first seal: Who was the first warrior to follow the path of the Mooh'Tah?"},
	[16] = {answer = "tha'kull", reply = "HOHO! Lucky you. You have passed the first seal! So ... would you like to continue with the Seal of the Mind?"},
	[17] = {answer = "yes", reply = "As you wish, foolish one! Here is my first question: Its lighter then a feather but no living creature can hold it for ten minutes?"},
	[18] = {answer = "breath", reply = "That was an easy one. Let's try the second: If you name it, you break it."},
	[19] = {answer = "silence", reply = "Hm. I bet you think you're smart. All right. How about this: What does everybody want to become but nobody to be?"},
	[20] = {answer = "old", reply = "ARGH! You did it again! Well all right. Do you wish to break the Seal of Madness?"},
	[21] = {answer = "yes", reply = "GOOD! So I will get you at last. Answer this: What is my favourite colour?"},
	[22] = {answer = "green", reply = "UHM UH OH ... How could you guess that? Are you mad??? All right. Question number two: What is the opposite?"},
	[23] = {answer = "none", reply = "NO! NO! NO! That can't be true. You're not only mad, you are a complete idiot! Ah well. Here is the last question: What is 1 plus 1?"},
	[24] = {answer = "2", reply = "RIGHT!"}
}

local function failPlayer(player)
	npcHandler:say(wrong, player:getId())
	npcHandler.topic[player:getId()] = 0
	player:teleportTo(failPosition)
	failPosition:sendMagicEffect(CONST_ME_TELEPORT)
end

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	local text = msg:lower()
	local topic = npcHandler.topic[cid] or 0
	local queststate1 = getPlayerStorageValue(cid, 6667)
	local queststate2 = getPlayerStorageValue(cid, 6668)

	if msgcontains(text, 'test') and queststate2 == 1 then
		npcHandler:say(prompt, cid)
		npcHandler.topic[cid] = 13
		return true
	elseif msgcontains(text, 'test') and queststate1 == 1 then
		npcHandler:say(prompt, cid)
		npcHandler.topic[cid] = 1
		return true
	end

	if (topic == 1 or topic == 13) and msgcontains(text, 'no') then
		npcHandler:say(abandoned, cid)
		npcHandler.topic[cid] = 0
		return true
	end

	local current = questions[topic]
	if not current then
		return true
	end

	if current.answer == "__fail_number__" then
		if tonumber(text) and tonumber(text) >= 1 then
			npcHandler:say("WRONG!", cid)
			player:teleportTo(failPosition)
			failPosition:sendMagicEffect(CONST_ME_TELEPORT)
			npcHandler.topic[cid] = 0
		else
			failPlayer(player)
		end
		return true
	end

	if text ~= current.answer then
		failPlayer(player)
		return true
	end

	if topic == 24 then
		player:teleportTo(passPosition)
		passPosition:sendMagicEffect(CONST_ME_TELEPORT)
		npcHandler:say(current.reply, cid)
		npcHandler.topic[cid] = 0
		return true
	end

	npcHandler:say(current.reply, cid)
	npcHandler.topic[cid] = topic + 1
	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
