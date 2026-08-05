local cratePosition = Position(32479, 31900, 5)

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		if not Tile(cratePosition):getItemById(1739) then
			Game.createItem(1739, 1, cratePosition)
		end
		item:transform(1946)
		return true
	end

	player:sendCancelMessage("The switch seems to be stuck.")
	return true
end
