local gatePosition = Position(33171, 31897, 8)
local relocatePosition = Position(33171, 31898, 8)
local gateItemId = 1285

local function getGate()
	local tile = Tile(gatePosition)
	return tile and tile:getItemById(gateItemId) or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local gate = getGate()
		if not gate then
			player:sendCancelMessage("Sorry, not possible.")
			return true
		end

		gatePosition:sendMagicEffect(CONST_ME_POFF)
		gate:remove()
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		if getGate() then
			player:sendCancelMessage("Sorry, not possible.")
			return true
		end

		local gateTile = Tile(gatePosition)
		if gateTile then
			gateTile:relocateTo(relocatePosition)
		end

		Game.createItem(gateItemId, 1, gatePosition)
		gatePosition:sendMagicEffect(CONST_ME_POFF)
		item:transform(1945)
		return true
	end

	return false
end
