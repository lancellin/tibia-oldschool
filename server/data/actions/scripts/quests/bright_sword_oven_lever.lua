local ovenPosition = Position(32623, 32188, 9)
local nextTilePosition = Position(32623, 32189, 9)
local nextTile2Position = Position(32623, 32190, 9)

local function getItemById(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local ovenItem = Tile(ovenPosition):getTopVisibleThing(player)
		if ovenItem and ovenItem:isItem() then
			ovenItem:remove()
		end

		doRelocate(nextTilePosition, nextTile2Position)
		Game.createItem(1787, 1, nextTilePosition)
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		local ovenItem = Tile(nextTilePosition):getTopVisibleThing(player)
		if ovenItem and ovenItem:isItem() then
			ovenItem:remove()
		end

		doRelocate(ovenPosition, nextTilePosition)
		Game.createItem(1787, 1, ovenPosition)
		item:transform(1945)
		return true
	end

	player:sendCancelMessage("Sorry not possible.")
	return true
end
