local gatePosition = Position(32483, 31633, 9)
local relocatePosition = Position(32483, 31633, 10)
local openTileIds = {354, 355}
local closedGateId = 392

local function getTopMovableThing(position)
	local tile = Tile(position)
	return tile and tile:getTopVisibleThing() or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local gateItem = getTopMovableThing(gatePosition)
		if gateItem and gateItem:isItem() and table.contains(openTileIds, gateItem:getId()) then
			doRelocate(gatePosition, relocatePosition)
			gateItem:transform(closedGateId)
			gateItem:decay()
			item:transform(1946)
			return true
		end
	elseif item:getId() == 1946 then
		item:transform(1945)
		return true
	end

	item:transform(item:getId() == 1945 and 1946 or 1945)
	return true
end
