function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	if creature:getLevel() >= 2 then
		return true
	end

	local failPosition = Position(position.x, position.y + 2, position.z)
	creature:teleportTo(failPosition, true)
	failPosition:sendMagicEffect(CONST_ME_MAGIC_BLUE)
	creature:sendTextMessage(MESSAGE_INFO_DESCR, "You need to be at least Level 2 in order to pass.")
	return true
end
