local REQUIRED_LEVEL = 20
local LEVER_ITEMID = 1945
local LEVER_USED_ITEMID = 1946

local basinRequirements = {
	{position = Position(32673, 32094, 8), itemId = 2376},
	{position = Position(32673, 32083, 8), itemId = 2455},
	{position = Position(32667, 32089, 8), itemId = 2674},
	{position = Position(32679, 32089, 8), itemId = 2175}
}

local teamRequirements = {
	{position = Position(32673, 32093, 8), destinations = Position(32672, 32070, 8), vocations = {[4] = true, [8] = true}},
	{position = Position(32673, 32085, 8), destinations = Position(32671, 32070, 8), vocations = {[3] = true, [7] = true}},
	{position = Position(32669, 32089, 8), destinations = Position(32672, 32069, 8), vocations = {[2] = true, [6] = true}},
	{position = Position(32677, 32089, 8), destinations = Position(32671, 32069, 8), vocations = {[1] = true, [5] = true}}
}

local function getPlayerAt(position)
	local tile = Tile(position)
	if not tile then
		return nil
	end

	local creature = tile:getTopCreature()
	return creature and creature:isPlayer() and creature or nil
end

local function getRequiredItem(position, itemId)
	local tile = Tile(position)
	return tile and tile:getItemById(itemId) or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getUniqueId() ~= 8890 then
		return false
	end

	if item:getId() == LEVER_USED_ITEMID then
		item:transform(LEVER_ITEMID)
		return true
	end

	local players = {}
	for index, requirement in ipairs(teamRequirements) do
		local targetPlayer = getPlayerAt(requirement.position)
		if not targetPlayer then
			player:sendCancelMessage("Sorry, all 4 players must be on right positions.")
			return true
		end

		if targetPlayer:getLevel() < REQUIRED_LEVEL then
			player:sendCancelMessage("Sorry, all players in your team must to be level " .. REQUIRED_LEVEL .. ".")
			return true
		end

		if not requirement.vocations[targetPlayer:getVocation():getId()] then
			player:sendCancelMessage("Sorry, all players in your team must have the correct vocation.")
			return true
		end

		players[index] = targetPlayer
	end

	local basinItems = {}
	for index, requirement in ipairs(basinRequirements) do
		local basinItem = getRequiredItem(requirement.position, requirement.itemId)
		if not basinItem then
			player:sendCancelMessage("Sorry, you need to put the correct stuffs at the correct basins.")
			return true
		end
		basinItems[index] = basinItem
	end

	for index, targetPlayer in ipairs(players) do
		local sourcePosition = teamRequirements[index].position
		sourcePosition:sendMagicEffect(CONST_ME_POFF)
		targetPlayer:teleportTo(teamRequirements[index].destinations, false)
		targetPlayer:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
		basinItems[index]:remove()
	end

	item:transform(LEVER_USED_ITEMID)
	return true
end
