local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)
NpcSystem.parseParameters(npcHandler)

function onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end
function onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end
function onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end
function onThink() npcHandler:onThink() end

function greetCallback(cid)
	if getPlayerStorageValue(cid, 3058) == -1 then
		selfSay('Arrrrgh! A dirty paleskin! Kill them my guards!')
		local pos = getCreaturePosition(getNpcCid())
		doSummonCreature('Orc Warlord', {x = pos.x - 1, y = pos.y - 1, z = pos.z})
		doSummonCreature('Orc Warlord', {x = pos.x - 1, y = pos.y, z = pos.z})
		doSummonCreature('Orc Leader', {x = pos.x - 1, y = pos.y + 1, z = pos.z})
		doSummonCreature('Orc Leader', {x = pos.x, y = pos.y + 1, z = pos.z})
		doSummonCreature('Orc Leader', {x = pos.x + 1, y = pos.y + 1, z = pos.z})
		doSummonCreature('Slime', {x = pos.x + 1, y = pos.y, z = pos.z})
		doSummonCreature('Slime', {x = pos.x + 1, y = pos.y - 1, z = pos.z})
		doSummonCreature('Slime', {x = pos.x, y = pos.y - 1, z = pos.z})
		setPlayerStorageValue(cid, 3058, 1)
	end
	return true
end

npcHandler:setCallback(CALLBACK_GREET, greetCallback)
npcHandler:setMessage(MESSAGE_GREET, 'Arrrgh! Again?! What do you want?')
npcHandler:addModule(FocusModule:new())
