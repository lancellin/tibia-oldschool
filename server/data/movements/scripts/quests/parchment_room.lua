local demonPositions = {
	Position(33060, 31623, 15),
	Position(33066, 31623, 15),
	Position(33060, 31627, 15),
	Position(33066, 31627, 15),
}

function onRemoveItem(item, tile, position)
	item:setActionId(0)

	for _, demonPosition in ipairs(demonPositions) do
		Game.createMonster("Demon", demonPosition)
	end

	return true
end
