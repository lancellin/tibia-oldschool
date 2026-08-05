local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

local responses = {
	["how are you"] = "Thank you very much. How kind of you to care about me. I am fine, thank you.",
	["sell"] = "Sorry, I have nothing to sell.",
	["job"] = "I do some work now and then. Nothing unusual, though.",
	["name"] = "I am a little sad, that you seem to have forgotten me, handsome. I am Aruda.",
	["aruda"] = "Oh, I like it, how you say my name.",
	["time"] = "Please don't be so rude to look for the time if you are talking to me.",
	["help"] = "I am deeply sorry, I can't help you."
}

local function stealGold(player)
	player:removeMoney(5)
end

local function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	local player = Player(cid)
	if not player then
		return false
	end

	for keyword, response in pairs(responses) do
		if msgcontains(msg, keyword) then
			npcHandler:say(response, cid)
			stealGold(player)
			return true
		end
	end

	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
