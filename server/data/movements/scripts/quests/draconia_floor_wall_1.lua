local wallPosition = Position(32795, 31593, 5)
local wallItemId = 1026

function onStepIn(creature, item, position, fromPosition)
	if item.uid == 5678 and item.itemid == 426 then
		local tile = Tile(wallPosition)
		local wall = tile and tile:getItemById(wallItemId) or nil
		if wall then
			wall:remove()
		end
		item:transform(425)
	end
	return true
end

function onStepOut(creature, item, position, fromPosition)
	if item.uid == 5678 and item.itemid == 425 then
		if not Tile(wallPosition):getItemById(wallItemId) then
			Game.createItem(wallItemId, 1, wallPosition)
		end
		item:transform(426)
	end
	return true
end
