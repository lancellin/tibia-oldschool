local gatePosition = Position(32094, 32149, 10)
local relocatePosition = Position(32094, 32150, 10)
local gateItemId = 1037

local function getGateItem()
	return Tile(gatePosition):getItemById(gateItemId)
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local gateItem = getGateItem()
		if not gateItem then
			player:sendCancelMessage("Sorry not possible.")
			return true
		end

		gateItem:remove()
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		doRelocate(gatePosition, relocatePosition)
		if not getGateItem() then
			Game.createItem(gateItemId, 1, gatePosition)
		end
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry not possible.")
	return true
end
