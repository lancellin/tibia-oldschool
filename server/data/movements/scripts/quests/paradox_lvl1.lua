local treePosition = Position(32478, 31902, 7)
local grassPosition = Position(32478, 31906, 7)

local function restoreTree()
	local tile = Tile(treePosition)
	local item = tile and tile:getItemById(1385)
	if item then
		item:transform(1304)
	end
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local hasGrass = Tile(grassPosition):getItemById(2782)
	local tree = Tile(treePosition):getItemById(1304)
	if hasGrass and tree then
		tree:transform(1385)
		addEvent(restoreTree, 45000)
	else
		creature:sendCancelMessage(" ")
	end
	return true
end
