function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	if creature:isPremium() then
		return true
	end

	local failPosition = Position(position.x + 3, position.y, position.z)
	creature:teleportTo(failPosition, true)
	failPosition:sendMagicEffect(CONST_ME_MAGIC_BLUE)
	return true
end
