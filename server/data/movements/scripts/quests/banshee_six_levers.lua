local leverRequirements = {
	[Position(32310, 31975, 13)] = 1946,
	[Position(32312, 31975, 13)] = 1946,
	[Position(32314, 31975, 13)] = 1945,
	[Position(32310, 31976, 13)] = 1946,
	[Position(32312, 31976, 13)] = 1946,
	[Position(32314, 31976, 13)] = 1945
}

local destination = Position(32261, 31856, 15)

local function matchesLevers()
	for leverPosition, expectedItemId in pairs(leverRequirements) do
		local tile = Tile(leverPosition)
		local lever = tile and (tile:getItemById(1945) or tile:getItemById(1946))
		if not lever or lever:getId() ~= expectedItemId then
			return false
		end
	end
	return true
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	if matchesLevers() then
		creature:teleportTo(destination, false)
		if creature:getStorageValue(20006) ~= 1 then
			creature:setStorageValue(20006, 1)
			creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Seal of Logic was broken.")
		end
		creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
		return true
	end

	local fallbackPosition = Position(position.x, position.y - 2, position.z)
	creature:teleportTo(fallbackPosition, false)
	fallbackPosition:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
