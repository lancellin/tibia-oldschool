local leverRequirements = {
	[Position(32476, 31900, 6)] = 1946,
	[Position(32477, 31900, 6)] = 1946,
	[Position(32478, 31900, 6)] = 1945,
	[Position(32479, 31900, 6)] = 1945,
	[Position(32480, 31900, 6)] = 1946,
	[Position(32481, 31900, 6)] = 1945
}

local ladderPosition = Position(32477, 31905, 6)

local function matchesLevers()
	for leverPosition, expectedId in pairs(leverRequirements) do
		local tile = Tile(leverPosition)
		local lever = tile and (tile:getItemById(1945) or tile:getItemById(1946))
		if not lever or lever:getId() ~= expectedId then
			return false
		end
	end
	return true
end

local function ensureItem(itemId, position)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or Game.createItem(itemId, 1, position)
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 and matchesLevers() then
		ensureItem(1386, ladderPosition)
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		local tile = Tile(ladderPosition)
		local ladder = tile and tile:getItemById(1386)
		if ladder then
			ladder:remove()
		end
		item:transform(1945)
		return true
	end

	return false
end
