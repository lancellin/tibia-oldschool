local trapPosition = Position(32497, 31888, 7)

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	creature:addHealth(-200)
	trapPosition:sendMagicEffect(CONST_ME_MORTAREA)
	return true
end
