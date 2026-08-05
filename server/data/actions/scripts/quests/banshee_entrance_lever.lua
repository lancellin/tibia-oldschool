local magicWallPosition = Position(32266, 31860, 11)
local floorPosition = Position(32266, 31860, 11)

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local tile = Tile(floorPosition)
	if not tile then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	local magicWall = tile:getItemById(1498)
	local closedFloor = tile:getItemById(407)
	local stair = tile:getItemById(410)

	if item:getActionId() == 50019 and item:getId() == 1945 and magicWall and closedFloor then
		magicWall:remove(1)
		item:transform(1946)
		Game.createItem(410, 1, floorPosition)
		return true
	end

	if item:getActionId() == 50019 and item:getId() == 1946 and not magicWall and stair then
		stair:remove(1)
		Game.createItem(407, 1, floorPosition)
		Game.createItem(1498, 1, magicWallPosition)
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
