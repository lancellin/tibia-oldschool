local removals = {
	{itemId = 3474, position = Position(32864, 32556, 11)},
	{itemId = 3475, position = Position(32865, 32556, 11)},
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		for _, removal in ipairs(removals) do
			local tile = Tile(removal.position)
			local gate = tile and tile:getItemById(removal.itemId) or nil
			if gate then
				gate:remove()
			end
		end
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		for _, removal in ipairs(removals) do
			local tile = Tile(removal.position)
			local gate = tile and tile:getItemById(removal.itemId) or nil
			if not gate then
				Game.createItem(removal.itemId, 1, removal.position)
			end
		end
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry, not possible.")
	return true
end
