local bridgeId = 1284
local closedGroundId = 493

local bridgePositions = {
	Position(32100, 32205, 8),
	Position(32101, 32205, 8)
}

local leverPositions = {
	Position(32098, 32204, 8),
	Position(32104, 32204, 8)
}

local function getTileItem(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

local function ensureItem(position, itemId)
	if not getTileItem(position, itemId) then
		Game.createItem(itemId, 1, position)
	end
end

local function removeItems(position, itemId)
	local item = getTileItem(position, itemId)
	while item do
		item:remove()
		item = getTileItem(position, itemId)
	end
end

local function setLevers(itemId)
	for _, position in ipairs(leverPositions) do
		local lever = getTileItem(position, 1945) or getTileItem(position, 1946)
		if lever then
			lever:transform(itemId)
		end
	end
end

local function openBridge()
	removeItems(bridgePositions[1], closedGroundId)
	removeItems(bridgePositions[2], closedGroundId)
	ensureItem(bridgePositions[1], bridgeId)
	ensureItem(bridgePositions[2], bridgeId)
	setLevers(1946)
end

local function closeBridge()
	for _, position in ipairs(bridgePositions) do
		removeItems(position, bridgeId)
	end

	ensureItem(bridgePositions[1], closedGroundId)
	ensureItem(bridgePositions[2], closedGroundId)
	setLevers(1945)
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		openBridge()
	elseif item:getId() == 1946 then
		closeBridge()
	end

	return true
end
