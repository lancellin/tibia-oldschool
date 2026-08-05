local whitePearlPosition = Position(32173, 31871, 15)
local blackPearlPosition = Position(32180, 31871, 15)
local destinationByActionId = {
	[1335] = Position(32176, 31863, 15),
	[1336] = Position(32177, 31863, 15)
}

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local destination = destinationByActionId[item:getActionId()]
	if not destination then
		return true
	end

	local whitePearl = Tile(whitePearlPosition):getItemById(2143)
	local blackPearl = Tile(blackPearlPosition):getItemById(2144)
	if whitePearl and blackPearl then
		creature:teleportTo(destination, false)
		whitePearl:remove(1)
		blackPearl:remove(1)
		whitePearlPosition:sendMagicEffect(CONST_ME_POFF)
		blackPearlPosition:sendMagicEffect(CONST_ME_POFF)
		creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
		return true
	end

	local fallbackPosition = Position(position.x, position.y + 2, position.z)
	creature:teleportTo(fallbackPosition, false)
	fallbackPosition:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
