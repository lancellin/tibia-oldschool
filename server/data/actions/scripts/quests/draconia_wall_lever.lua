local configByUniqueId = {
	[1654] = {gatePosition = Position(32792, 31581, 7), gateItemId = 1037},
	[1657] = {gatePosition = Position(32790, 31594, 7), gateItemId = 1285},
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local config = configByUniqueId[item.uid]
	if not config then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	local tile = Tile(config.gatePosition)
	local gateItem = tile and tile:getItemById(config.gateItemId) or nil
	if item:getId() == 1945 and gateItem then
		gateItem:remove()
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 and not gateItem then
		Game.createItem(config.gateItemId, 1, config.gatePosition)
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
