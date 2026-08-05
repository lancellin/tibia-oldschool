local doorPosition = Position(32177, 32148, 11)
local relocatePosition = Position(32178, 32148, 11)

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		item:transform(1946)
	elseif item:getId() == 1946 then
		item:transform(1945)
	end

	local tile = Tile(doorPosition)
	if not tile then
		return true
	end

	local doorItem = tile:getItemById(1211) or tile:getItemById(1209) or tile:getItemById(1210)
	if not doorItem then
		return true
	end

	if doorItem:getId() == 1211 then
		doorItem:transform(1209)
	elseif doorItem:getId() == 1209 then
		doRelocate(doorPosition, relocatePosition)
		doorItem:transform(1211)
	else
		doorItem:transform(doorItem:getId() + 1)
	end

	doorItem:setAttribute(ITEM_ATTRIBUTE_ACTIONID, 11013)
	return true
end
