local gatePosition = Position(33314, 31592, 15)
local gateRelocatePosition = Position(33315, 31592, 15)
local gateItemId = 1355

local teleportPosition = Position(33316, 31591, 15)
local teleportItemId = 1387
local teleportDestination = Position(33321, 31591, 14)

local function getGate()
	local tile = Tile(gatePosition)
	return tile and tile:getItemById(gateItemId) or nil
end

local function getTeleportItem()
	local tile = Tile(teleportPosition)
	return tile and tile:getItemById(teleportItemId) or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local gate = getGate()
		if not gate then
			player:sendCancelMessage("Sorry, not possible.")
			return true
		end

		gate:remove()
		gatePosition:sendMagicEffect(CONST_ME_POFF)

		if not getTeleportItem() then
			doCreateTeleport(teleportItemId, teleportDestination, teleportPosition)
			teleportPosition:sendMagicEffect(CONST_ME_TELEPORT)
		end

		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		local teleportItem = getTeleportItem()
		if teleportItem then
			teleportItem:remove()
			teleportPosition:sendMagicEffect(CONST_ME_POFF)
		end

		local gateTile = Tile(gatePosition)
		if gateTile then
			gateTile:relocateTo(gateRelocatePosition)
		end

		if not getGate() then
			Game.createItem(gateItemId, 1, gatePosition)
			gatePosition:sendMagicEffect(CONST_ME_POFF)
		end

		item:transform(1945)
		return true
	end

	return false
end
