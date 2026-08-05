local exitPosition = Position(32566, 31963, 1)

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	creature:teleportTo(exitPosition)
	exitPosition:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
