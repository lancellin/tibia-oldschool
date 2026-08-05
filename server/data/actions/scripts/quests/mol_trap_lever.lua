local fieldPositions = {
	Position(32487, 31628, 13),
	Position(32487, 31629, 13),
	Position(32488, 31629, 13),
	Position(32487, 31627, 13),
	Position(32486, 31627, 13),
	Position(32486, 31628, 13),
	Position(32486, 31629, 13),
	Position(32486, 31630, 13),
	Position(32487, 31630, 13),
	Position(32488, 31630, 13),
	Position(32486, 31626, 13),
	Position(32487, 31626, 13),
	Position(32488, 31626, 13)
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	for _, fieldPosition in ipairs(fieldPositions) do
		Game.createItem(1491, 1, fieldPosition)
	end

	fromPosition:sendMagicEffect(CONST_ME_POFF)
	item:remove()
	return true
end
