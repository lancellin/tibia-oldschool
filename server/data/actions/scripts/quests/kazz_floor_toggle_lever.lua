local floorPosition = Position(32605, 31902, 4)
local closedFloorId = 431
local openFloorId = 408

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local tile = Tile(floorPosition)
	local ground = tile and tile:getGround()

	if not ground then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	local groundId = ground:getId()
	if groundId == closedFloorId then
		ground:transform(openFloorId)
	elseif groundId == openFloorId then
		ground:transform(closedFloorId)
	else
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	item:transform(item:getId() == 1945 and 1946 or 1945)
	return true
end
