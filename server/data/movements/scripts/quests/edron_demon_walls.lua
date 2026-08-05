local pressurePositions = {
	Position(33190, 31629, 13),
	Position(33191, 31629, 13),
}

local wallPositions = {
	Position(33210, 31630, 13),
	Position(33211, 31630, 13),
	Position(33212, 31630, 13),
}

local wallItemId = 1050

local function hasCreature(position)
	local tile = Tile(position)
	local creature = tile and tile:getTopCreature() or nil
	return creature and creature:isCreature() or false
end

local function bothPressureTilesOccupied()
	for _, position in ipairs(pressurePositions) do
		if not hasCreature(position) then
			return false
		end
	end
	return true
end

function onStepIn(creature, item, position, fromPosition)
	item:transform(item:getId() - 1)

	if not bothPressureTilesOccupied() then
		return true
	end

	for _, wallPosition in ipairs(wallPositions) do
		local tile = Tile(wallPosition)
		local wall = tile and tile:getItemById(wallItemId) or nil
		if wall then
			wall:remove()
			wallPosition:sendMagicEffect(CONST_ME_POFF)
		end
	end
	return true
end

function onStepOut(creature, item, position, fromPosition)
	item:transform(item:getId() + 1)

	for _, wallPosition in ipairs(wallPositions) do
		if not Tile(wallPosition):getItemById(wallItemId) then
			local wallTile = Tile(wallPosition)
			if wallTile then
				wallTile:relocateTo(Position(wallPosition.x, wallPosition.y + 1, wallPosition.z))
			end

			Game.createItem(wallItemId, 1, wallPosition)
			wallPosition:sendMagicEffect(CONST_ME_POFF)
		end
	end
	return true
end
