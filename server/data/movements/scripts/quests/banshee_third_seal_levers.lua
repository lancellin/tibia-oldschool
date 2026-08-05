local leverPositions = {
	Position(32220, 31842, 15),
	Position(32220, 31843, 15),
	Position(32220, 31844, 15),
	Position(32220, 31845, 15),
	Position(32220, 31846, 15)
}
local destination = Position(32272, 31849, 15)
local demonrageStorage = 20003

local function resetLevers()
	for _, leverPosition in ipairs(leverPositions) do
		local tile = Tile(leverPosition)
		local lever = tile and tile:getItemById(1946)
		if lever then
			lever:transform(1945)
		end
	end
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	for _, leverPosition in ipairs(leverPositions) do
		local lever = Tile(leverPosition):getItemById(1946)
		if not lever then
			local fallbackPosition = Position(position.x, position.y - 2, position.z)
			creature:teleportTo(fallbackPosition, false)
			fallbackPosition:sendMagicEffect(CONST_ME_TELEPORT)
			return true
		end
	end

	creature:teleportTo(destination, false)
	if creature:getStorageValue(demonrageStorage) ~= 1 then
		creature:setStorageValue(demonrageStorage, 1)
		creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Seal of Demonrage was broken.")
	end
	resetLevers()
	creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
