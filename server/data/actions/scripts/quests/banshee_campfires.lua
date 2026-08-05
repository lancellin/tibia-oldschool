local campfires = {
	[2451] = Position(32309, 31975, 13),
	[2452] = Position(32309, 31976, 13),
	[2453] = Position(32311, 31975, 13),
	[2454] = Position(32311, 31976, 13),
	[2455] = Position(32313, 31975, 13),
	[2456] = Position(32313, 31976, 13)
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local campfirePosition = campfires[item:getActionId()]
	if not campfirePosition then
		return true
	end

	local tile = Tile(campfirePosition)
	if not tile then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	local litCampfire = tile:getItemById(1423)
	local burntCampfire = tile:getItemById(1421)

	if item:getId() == 1945 and litCampfire then
		litCampfire:remove(1)
		Game.createItem(1421, 1, campfirePosition)
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 and burntCampfire then
		burntCampfire:remove(1)
		Game.createItem(1423, 1, campfirePosition)
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
