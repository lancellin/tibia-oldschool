local wallPositions = {
	Position(32186, 31626, 8),
	Position(32187, 31626, 8),
	Position(32188, 31626, 8),
	Position(32189, 31626, 8)
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	for _, wallPosition in ipairs(wallPositions) do
		local tile = Tile(wallPosition)
		local wallItem = tile and tile:getTopVisibleThing(player)
		if wallItem and wallItem:isItem() then
			wallPosition:sendMagicEffect(CONST_ME_POFF)
			wallItem:remove(1)
		end
	end

	fromPosition:sendMagicEffect(CONST_ME_POFF)
	item:remove()
	return true
end
