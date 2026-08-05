local holes = {468, 481, 483}
local sandExhaust = {}
local sandExhaustMs = 300
local TILE_SAND = 231
local ITEM_SCARAB_COIN = 2159
local TOMB_ENTRANCE_AID = 25001
local SCARAB_TILE_AID = 25002
local OPEN_SAND_HOLE = 489

local function resetScarabTile(position)
	local tile = Tile(position)
	if not tile then
		return
	end

	local ground = tile:getGround()
	if not ground or ground:getId() ~= TILE_SAND then
		return
	end

	if ground:getActionId() == 101 then
		ground:setActionId(SCARAB_TILE_AID)
	end
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local tile = Tile(toPosition)
	if not tile then
		return false
	end

	local ground = tile:getGround()
	if not ground then
		return false
	end

	local groundId = ground:getId()
	if table.contains(holes, groundId) then
		ground:transform(groundId + 1)
		ground:decay()

		toPosition.z = toPosition.z + 1
		tile:relocateTo(toPosition)
	elseif groundId == TILE_SAND then
		local playerId = player:getGuid()
		local now = os.mtime()
		if sandExhaust[playerId] and sandExhaust[playerId] > now then
			player:sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED)
			return true
		end
		sandExhaust[playerId] = now + sandExhaustMs

		local groundActionId = ground:getActionId()
		if groundActionId == TOMB_ENTRANCE_AID then
			if math.random(1, 5) == 1 then
				ground:transform(OPEN_SAND_HOLE)
				ground:decay()
			end
			toPosition:sendMagicEffect(CONST_ME_POFF)
			return true
		end

		if groundActionId == SCARAB_TILE_AID then
			ground:setActionId(101)
			addEvent(resetScarabTile, 30 * 60 * 1000, Position(toPosition))
			if math.random(1, 20) == 1 then
				Game.createItem(ITEM_SCARAB_COIN, 1, toPosition)
			else
				Game.createMonster("Scarab", toPosition)
			end
			toPosition:sendMagicEffect(CONST_ME_POFF)
			return true
		end

		local randomValue = math.random(1, 100)
		if randomValue == 1 then
			Game.createItem(ITEM_SCARAB_COIN, 1, toPosition)
		elseif randomValue > 95 then
			Game.createMonster("Scarab", toPosition)
		end
		toPosition:sendMagicEffect(CONST_ME_POFF)
	else
		return false
	end

	return true
end
