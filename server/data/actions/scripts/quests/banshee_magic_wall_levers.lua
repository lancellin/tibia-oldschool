local leftLeverPosition = Position(32315, 31910, 12)
local rightLeverPosition = Position(32212, 31888, 12)
local leftWallPosition = Position(32259, 31891, 10)
local rightWallPosition = Position(32259, 31890, 10)
local leftRespawnMs = 180 * 1000
local rightRespawnMs = 120 * 1000

local function ensureWall(position)
	local tile = Tile(position)
	local wall = tile and tile:getItemById(1498)
	if not wall then
		Game.createItem(1498, 1, position)
	end
end

local function restoreBansheeEntrance()
	ensureWall(leftWallPosition)
	ensureWall(rightWallPosition)

	local leftLever = Tile(leftLeverPosition):getItemById(1946)
	if leftLever then
		leftLever:transform(1945)
	end

	local rightLever = Tile(rightLeverPosition):getItemById(1946)
	if rightLever then
		rightLever:transform(1945)
	end
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getActionId() == 50017 then
		if item:getId() == 1945 then
			local wall = Tile(rightWallPosition):getItemById(1498)
			if wall then
				wall:remove(1)
			end
			item:transform(1946)
			addEvent(ensureWall, rightRespawnMs, rightWallPosition)
			return true
		end

		if item:getId() == 1946 then
			ensureWall(rightWallPosition)
			item:transform(1945)
			return true
		end

		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	if item:getActionId() ~= 50016 then
		return true
	end

	local auxiliaryLever = Tile(rightLeverPosition):getItemById(1946)
	local wall = Tile(leftWallPosition):getItemById(1498)

	if item:getId() == 1945 and auxiliaryLever and wall then
		wall:remove(1)
		item:transform(1946)
		addEvent(ensureWall, leftRespawnMs, leftWallPosition)
		return true
	end

	if item:getId() == 1946 then
		ensureWall(leftWallPosition)
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
