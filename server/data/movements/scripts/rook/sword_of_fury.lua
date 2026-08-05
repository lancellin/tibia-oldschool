local swordFuryStorage = 11003

local firePositions = {
	Position(32100, 32084, 7),
	Position(32101, 32084, 7),
	Position(32102, 32084, 7),
	Position(32100, 32085, 7),
	Position(32100, 32086, 7),
	Position(32101, 32086, 7),
	Position(32102, 32086, 7),
	Position(32102, 32085, 7),
}

local swordPosition = Position(32101, 32085, 7)
local actionRequirements = {
	[11003] = -1,
	[11004] = 0,
}

local fireTransforms = {
	[1487] = 1489,
	[1488] = 1489,
	[1492] = 1489,
}

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local requiredValue = actionRequirements[item.actionid]
	if requiredValue == nil then
		return true
	end

	local storageValue = getGlobalStorageValue(swordFuryStorage)
	if storageValue ~= requiredValue then
		return true
	end

	setGlobalStorageValue(swordFuryStorage, storageValue + 1)
	if storageValue ~= 0 then
		return true
	end

	for _, firePosition in ipairs(firePositions) do
		local tile = Tile(firePosition)
		if tile then
			for fromId, toId in pairs(fireTransforms) do
				local fireItem = tile:getItemById(fromId)
				if fireItem then
					fireItem:transform(toId)
					break
				end
			end
		end
	end

	local swordTile = Tile(swordPosition)
	local swordItem = swordTile and swordTile:getTopDownItem()
	if swordItem then
		swordItem:remove()
		swordPosition:sendMagicEffect(CONST_ME_MAGIC_RED)
	end

	return true
end
