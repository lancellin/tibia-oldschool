local resetTriggerPositions = {
	["32258,31887,10"] = true,
	["32259,31887,10"] = true
}

local leftLeverPosition = Position(32315, 31910, 12)
local rightLeverPosition = Position(32212, 31888, 12)
local wallPositions = {
	Position(32259, 31890, 10),
	Position(32259, 31891, 10)
}

local function ensureWall(position)
	local tile = Tile(position)
	local wall = tile and tile:getItemById(1498)
	if not wall then
		Game.createItem(1498, 1, position)
	end
end

local function resetLever(position)
	local tile = Tile(position)
	local lever = tile and tile:getItemById(1946)
	if lever then
		lever:transform(1945)
	end
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local key = string.format("%d,%d,%d", position.x, position.y, position.z)
	if not resetTriggerPositions[key] then
		return true
	end

	for _, wallPosition in ipairs(wallPositions) do
		ensureWall(wallPosition)
	end

	resetLever(leftLeverPosition)
	resetLever(rightLeverPosition)
	return true
end
