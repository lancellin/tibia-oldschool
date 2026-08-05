local summonPositions = {
	{"Ghost", Position(32277, 31902, 13)},
	{"Ghost", Position(32277, 31903, 13)},
	{"Demon Skeleton", Position(32277, 31904, 13)}
}
local destination = Position(32266, 31849, 15)
local hiddenSealStorage = 20001

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	if creature:getStorageValue(hiddenSealStorage) ~= 1 then
		for _, summon in ipairs(summonPositions) do
			local monster = Game.createMonster(summon[1], summon[2], false, true)
			if monster then
				summon[2]:sendMagicEffect(CONST_ME_MAGIC_RED)
			end
		end
		creature:setStorageValue(hiddenSealStorage, 1)
		creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Hidden Seal was broken.")
	end

	creature:teleportTo(destination, false)
	creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
