local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local prisonerKeyStorage = 6669

local topic = {
	MATH_START = 1,
	MATH_COLOR = 2,
	MATH_FINISH = 3,
	RIDDLE_APPLES = 4,
	RIDDLE_CONFIRM_1 = 5,
	RIDDLE_CONFIRM_2 = 6,
	RIDDLE_CONFIRM_3 = 7,
	SELL_RUNE = 8
}

local function getFarewellMessage(player)
	if player and player:getStorageValue(prisonerKeyStorage) >= 1 then
		return "Good bye! Don't forget about the secrets of mathemagics."
	end
	return "Next time we should talk about my surreal numbers."
end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	if msgcontains(msg, "numbers") or msgcontains(msg, "mathemagic") then
		npcHandler:say("My surreal numbers are based on astonishing facts. Are you interested in learning the secret of mathemagics?", cid)
		npcHandler.topic[cid] = topic.MATH_START
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.MATH_START then
		npcHandler:say("But first tell me your favourite colour please!", cid)
		npcHandler.topic[cid] = topic.MATH_COLOR
	elseif msgcontains(msg, "green") and npcHandler.topic[cid] == topic.MATH_COLOR then
		npcHandler:say("Very interesting. So are you ready to proceed in you lesson in mathemagics?", cid)
		npcHandler.topic[cid] = topic.MATH_FINISH
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.MATH_FINISH then
		npcHandler:say("So know that everthing is based on the simple fact that 1 + 1 = 2!", cid)
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, "key") then
		npcHandler:say("Sure I have the key! Hehehe! Perhaps I will give it to you. IF you can solve my riddle.", cid)
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, "sell rune") then
		npcHandler:say("You want to sell me blank runes! I will give you 50000 gold for each rune! Interested?", cid)
		npcHandler.topic[cid] = topic.SELL_RUNE
	elseif msgcontains(msg, "riddle") then
		if player:getStorageValue(prisonerKeyStorage) >= 1 then
			npcHandler:say("You already have my key. Now go and get happy - or die, hehe.", cid)
		else
			npcHandler:say("Great riddle, isn´t it? If you can tell me the correct answer, I will give you something. Hehehe!", cid)
			npcHandler.topic[cid] = 0
		end
	elseif msgcontains(msg, "pd-d-ks-p-pd") then
		if player:getStorageValue(prisonerKeyStorage) >= 1 then
			npcHandler:say("You already have my key. Now go and get happy - or die, hehe.", cid)
		else
			npcHandler:say("Hurray! For that I will give you my key for - hmm - let´s say ... some apples. Interested?", cid)
			npcHandler.topic[cid] = topic.RIDDLE_APPLES
		end
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.RIDDLE_APPLES then
		if not player:removeItem(2674, 7) then
			npcHandler:say("No apples, no key. I need seven red apples. Mnjam!", cid)
			npcHandler.topic[cid] = 0
			return true
		end

		npcHandler:say("Mnjam - excellent apples. Now - about that key. You are sure want it?", cid)
		npcHandler.topic[cid] = topic.RIDDLE_CONFIRM_1
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.RIDDLE_CONFIRM_1 then
		npcHandler:say("Really, really?", cid)
		npcHandler.topic[cid] = topic.RIDDLE_CONFIRM_2
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.RIDDLE_CONFIRM_2 then
		npcHandler:say("Really, really, really, really?", cid)
		npcHandler.topic[cid] = topic.RIDDLE_CONFIRM_3
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.RIDDLE_CONFIRM_3 then
		npcHandler:say("Then take it and get happy - or die, hehe.", cid)
		local keyItem = player:addItem(2088, 1)
		if keyItem then
			keyItem:setActionId(3666)
			player:setStorageValue(prisonerKeyStorage, 1)
		end
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, "yes") and npcHandler.topic[cid] == topic.SELL_RUNE then
		if player:removeItem(2260, 1) then
			player:addMoney(10)
			npcHandler:say("Ok. Take my money. I can summon new money anytime - hehehe.", cid)
		else
			npcHandler:say("Hey! You don't even have a blank rune. Hehehe.", cid)
		end
		npcHandler.topic[cid] = 0
	elseif msgcontains(msg, "bye") and npcHandler.topic[cid] ~= 0 then
		npcHandler:say(getFarewellMessage(player), cid)
		npcHandler.topic[cid] = 0
	end
	return true
end

local function farewellCallback(cid)
	local player = Player(cid)
	npcHandler:setMessage(MESSAGE_FAREWELL, getFarewellMessage(player))
	return true
end

keywordHandler:addKeyword({"capture"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Yes, they capture people. I guess that's their job."
})
keywordHandler:addKeyword({"job"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Job? JOB? Hey man - I am in prison! But you know - once upon a time - I was a powerful mage! A mage ... come to think of it .., what is that - a mage?"
})
keywordHandler:addKeyword({"time"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Better save time than comitting a crime. I am a poet and I know it!"
})
keywordHandler:addKeyword({"name"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "My name is - um... hang on? I knew it yesterday, didn't I? Doesn't matter!"
})
keywordHandler:addKeyword({"monster"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Yeah! There are many monsters guarding my home. Only the bravest hero will be able to slay them!"
})
keywordHandler:addKeyword({"home"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Yeah! There are many monsters guarding my home. Only the bravest hero will be able to slay them!"
})
keywordHandler:addKeyword({"conjure"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Yeah! There are many monsters guarding my home. Only the bravest hero will be able to slay them!"
})
keywordHandler:addKeyword({"mino"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "They are trying to capture me! Or hang on! Haven't they already captured me? Hmmm - I will have to think about this."
})
keywordHandler:addKeyword({"sorcerer"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I am the mightiest sorcerer from here to there! Yeah!"
})
keywordHandler:addKeyword({"mad mage"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "Hey! That's me! You got it! Thanks mate - now I remember my name!"
})
keywordHandler:addKeyword({"books"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "I have many books in my home. But only powerful people can read them. I bet you will only see three dots after the headline! Hehehe! Hahaha! Excellent!"
})
keywordHandler:addKeyword({"escape"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "How could I escape? They only give me rotten food here. I can´t regain my powers because I have no mana!"
})
keywordHandler:addKeyword({"labyrinth"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "It´s easy to find your way through it! Just follow the pools of mud. Hehe - useful hint, isn´t it?"
})
keywordHandler:addKeyword({"way"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "It´s easy to find your way through it! Just follow the pools of mud. Hehe - useful hint, isn´t it?"
})
keywordHandler:addKeyword({"something"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "No! I won't tell you. Shame coz it would be useful for you - hehehe."
})
keywordHandler:addKeyword({"palkar"}, StdModule.say, {
	npcHandler = npcHandler,
	text = "He is the leader of the outcasts. I hope he will never conquer the city of Mintwallin. That would be the end of me!"
})

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:setCallback(CALLBACK_FAREWELL, farewellCallback)
npcHandler:addModule(FocusModule:new())
