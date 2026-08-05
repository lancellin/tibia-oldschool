local gateMap = {
	[1300] = Position(32604, 31904, 3),
	[1301] = Position(32605, 31904, 3),
	[1302] = Position(32604, 31905, 3),
	[1303] = Position(32605, 31905, 3),
}

local function getItemById(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

local function gatesAreClosed()
	for itemId, position in pairs(gateMap) do
		if not getItemById(position, itemId) then
			return false
		end
	end
	return true
end

local function gatesAreOpen()
	for itemId, position in pairs(gateMap) do
		if getItemById(position, itemId) then
			return false
		end
	end
	return true
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 and gatesAreClosed() then
		for itemId, position in pairs(gateMap) do
			local gateItem = getItemById(position, itemId)
			if gateItem then
				gateItem:remove()
			end
		end
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 and gatesAreOpen() then
		for itemId, position in pairs(gateMap) do
			Game.createItem(itemId, 1, position)
		end
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
