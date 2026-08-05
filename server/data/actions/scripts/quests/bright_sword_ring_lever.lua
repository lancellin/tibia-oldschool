local ringPosition = Position(32594, 32214, 9)
local ringItemId = 2166

local wallPosition0 = Position(32593, 32216, 9)
local nextTile0 = Position(32593, 32217, 9)
local wallPosition1 = Position(32594, 32216, 9)
local nextTile1 = Position(32594, 32217, 9)
local wallPosition8 = Position(32601, 32216, 9)
local wallPosition9 = Position(32602, 32216, 9)
local wallPosition10 = Position(32603, 32216, 9)
local nextTile10 = Position(32603, 32217, 9)
local wallPosition11 = Position(32604, 32216, 9)
local nextTile11 = Position(32604, 32217, 9)
local wallPosition13 = Position(32606, 32216, 9)
local wallPosition14 = Position(32607, 32216, 9)

local function getItemById(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

local function getTopItem(position)
	local tile = Tile(position)
	if not tile then
		return nil
	end

	local thing = tile:getTopVisibleThing()
	return thing and thing:isItem() and thing or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local ringItem = getItemById(ringPosition, ringItemId)
	if not ringItem then
		ringPosition:sendMagicEffect(CONST_ME_POFF)
		return true
	end

	ringPosition:sendMagicEffect(CONST_ME_GREEN_RINGS)
	ringItem:remove()

	if item:getId() == 1945 then
		doRelocate(wallPosition0, nextTile0)
		Game.createItem(1026, 1, wallPosition0)
		doRelocate(wallPosition1, nextTile1)
		Game.createItem(1026, 1, wallPosition1)

		local wallItem8 = getTopItem(wallPosition8)
		if wallItem8 then
			wallItem8:transform(1207)
		end

		local wallItem9 = getTopItem(wallPosition9)
		if wallItem9 then
			wallItem9:transform(1208)
		end

		doRelocate(wallPosition10, nextTile10)
		local wallItem10 = getTopItem(wallPosition10)
		if wallItem10 then
			wallItem10:transform(1026)
		end

		doRelocate(wallPosition11, nextTile11)
		local wallItem11 = getTopItem(wallPosition11)
		if wallItem11 then
			wallItem11:transform(1026)
		end

		local wallItem13 = getItemById(wallPosition13, 1026)
		if wallItem13 then
			wallItem13:remove()
		end

		local wallItem14 = getItemById(wallPosition14, 1026)
		if wallItem14 then
			wallItem14:remove()
		end

		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		local wallItem0 = getTopItem(wallPosition0)
		if wallItem0 then
			wallItem0:remove()
		end

		local wallItem1 = getTopItem(wallPosition1)
		if wallItem1 then
			wallItem1:remove()
		end

		local wallItem8 = getTopItem(wallPosition8)
		if wallItem8 then
			wallItem8:transform(1026)
		end

		local wallItem9 = getTopItem(wallPosition9)
		if wallItem9 then
			wallItem9:transform(1026)
		end

		local wallItem10 = getTopItem(wallPosition10)
		if wallItem10 then
			wallItem10:transform(1207)
		end

		local wallItem11 = getTopItem(wallPosition11)
		if wallItem11 then
			wallItem11:transform(1208)
		end

		Game.createItem(1026, 1, wallPosition13)
		Game.createItem(1026, 1, wallPosition14)
		item:transform(1945)
		return true
	end

	ringPosition:sendMagicEffect(CONST_ME_POFF)
	return true
end
