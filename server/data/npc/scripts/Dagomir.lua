dofile(getDataDir() .. 'npc/scripts/lib/greeting.lua')

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink()
	npcHandler:onThink()
	if getWorldUpTime() == 5 then
		doMoveCreature(getNpcCid(), 0)
	elseif getWorldUpTime() == 10 then
		doMoveCreature(getNpcCid(), 2)
	end
end

function creatureSayCallback(cid, type, msg)
	if not npcHandler:isFocused(cid) then
		return false
	end

	if msgcontains(msg, 'hi') or msgcontains(msg, 'hello') then
		npcHandler:say('Wha... what?? HOW DARE YOU!!?? LEAVE ME ALONE ON MY TOILET AT ONCE!', cid)
	end
	return true
end

npcHandler:setCallback(CALLBACK_MESSAGE_DEFAULT, creatureSayCallback)
npcHandler:addModule(FocusModule:new())
