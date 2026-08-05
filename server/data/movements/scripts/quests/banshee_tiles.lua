local failPosition = Position(32184, 31940, 14)
local flamePosition = Position(32268, 31856, 15)

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local actionId = item:getActionId()
	if actionId == 888 then
		creature:teleportTo(failPosition, false)
		creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_RED)
		return true
	end

	if actionId == 889 then
		creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
		return true
	end

	if actionId == 890 then
		creature:teleportTo(flamePosition, false)
		if creature:getStorageValue(20005) ~= 1 then
			creature:setStorageValue(20005, 1)
			creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The Seal of the True Path was broken.")
		end
		creature:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	end
	return true
end
