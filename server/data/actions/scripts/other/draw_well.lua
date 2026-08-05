function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local destination
	if item.itemid == 1368 then
		destination = Position(fromPosition.x + 1, fromPosition.y, fromPosition.z + 1)
	elseif item.itemid == 1369 then
		destination = Position(fromPosition.x, fromPosition.y, fromPosition.z + 1)
	else
		return false
	end

	player:teleportTo(destination, false)
	return true
end
