local barrelPosition = Position(32614, 32209, 10)
local nextTile1 = Position(32613, 32209, 10)
local nextTile2 = Position(32614, 32208, 10)
local ringPosition = Position(32613, 32220, 10)

local function getItemById(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

local function removeTopItem(position)
	local tile = Tile(position)
	if not tile then
		return
	end

	local thing = tile:getTopVisibleThing()
	if thing and thing:isItem() then
		thing:remove()
	end
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() ~= 1945 then
		fromPosition:sendMagicEffect(CONST_ME_POFF)
		player:sendCancelMessage("Sorry not possible.")
		return true
	end

	local barrelItem = getItemById(barrelPosition, 1774)
	if barrelItem then
		barrelItem:remove()
		removeTopItem(Position(32614, 32208, 10))
		doRelocate(barrelPosition, nextTile2)
		Game.createItem(1774, 1, Position(32614, 32208, 10))

		removeTopItem(Position(32614, 32206, 10))
		removeTopItem(Position(32614, 32205, 10))
		Game.createItem(1323, 1, Position(32614, 32204, 10))
		Game.createItem(1336, 1, Position(32614, 32205, 10))
		Game.createItem(1025, 1, Position(32614, 32206, 10))
		Position(32615, 32224, 10):sendMagicEffect(CONST_ME_HITBYFIRE)
		Position(32614, 32224, 10):sendMagicEffect(CONST_ME_HITBYFIRE)
	else
		doRelocate(barrelPosition, nextTile1)
	end

	removeTopItem(Position(32614, 32221, 10))
	removeTopItem(Position(32615, 32223, 10))
	Game.createItem(1309, 1, Position(32615, 32223, 10))
	Game.createItem(1487, 1, Position(32615, 32221, 10))
	Game.createItem(1488, 1, Position(32615, 32223, 10))
	Game.createItem(1487, 1, ringPosition)
	ringPosition:sendMagicEffect(CONST_ME_GREEN_RINGS)
	Game.createItem(1025, 1, barrelPosition)

	local ringItem = getItemById(ringPosition, 2166)
	if ringItem then
		ringItem:remove()
	end

	item:transform(1946)
	return true
end
