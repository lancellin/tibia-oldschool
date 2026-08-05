local destination = Position(32273, 31856, 15)
local plagueSealStorage = 20002

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	creature:teleportTo(destination, false)
	if creature:getStorageValue(plagueSealStorage) ~= 1 then
		creature:setStorageValue(plagueSealStorage, 1)
		creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Plague Seal was broken.")
	end
	creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
