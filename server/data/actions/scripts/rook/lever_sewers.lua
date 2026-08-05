local BRIDGE_ITEM_ID = 1284
local FLOOR_ITEM_ID = 493

local bridgePositions = {
	Position(32100, 32205, 8),
	Position(32101, 32205, 8),
}

local fallbackPosition = Position(32102, 32205, 8)
local switchPositions = {
	Position(32098, 32204, 8),
	Position(32104, 32204, 8),
}

local closedDecorations = {
	[32100] = 4799,
	[32101] = 4797,
}

local function getItemById(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

local function removeItemById(position, itemId)
	local item = getItemById(position, itemId)
	while item do
		item:remove()
		item = getItemById(position, itemId)
	end
end

local function ensureItem(position, itemId)
	if not getItemById(position, itemId) then
		Game.createItem(itemId, 1, position)
	end
end

local function syncLevers(activeItemId)
	for _, position in ipairs(switchPositions) do
		local lever = getItemById(position, 1945) or getItemById(position, 1946)
		if lever and lever:getId() ~= activeItemId then
			lever:transform(activeItemId)
		end
	end
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		for _, position in ipairs(bridgePositions) do
			removeItemById(position, FLOOR_ITEM_ID)
			local decorationId = closedDecorations[position.x]
			if decorationId then
				removeItemById(position, decorationId)
			end
			ensureItem(position, BRIDGE_ITEM_ID)
		end
		syncLevers(1946)
		return true
	end

	if item:getId() == 1946 then
		for _, position in ipairs(bridgePositions) do
			doRelocate(position, fallbackPosition)
			removeItemById(position, BRIDGE_ITEM_ID)
			ensureItem(position, FLOOR_ITEM_ID)
			local decorationId = closedDecorations[position.x]
			if decorationId then
				ensureItem(position, decorationId)
			end
		end
		syncLevers(1945)
		return true
	end

	return true
end
