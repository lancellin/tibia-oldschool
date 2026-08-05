local splashPosition = Position(32243, 31892, 14)
local destination = Position(32261, 31849, 15)

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local splash = Tile(splashPosition):getItemById(2016)
	if splash then
		creature:teleportTo(destination, false)
		if creature:getStorageValue(20004) ~= 1 then
			creature:setStorageValue(20004, 1)
			creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Seal of Sacrifice was broken.")
		end
		splash:remove(1)
		splashPosition:sendMagicEffect(CONST_ME_MAGIC_RED)
		creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
		return true
	end

	local fallbackPosition = Position(position.x - 2, position.y, position.z)
	creature:teleportTo(fallbackPosition, false)
	fallbackPosition:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end
