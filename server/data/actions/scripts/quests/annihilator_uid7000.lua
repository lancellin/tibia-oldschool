local PLAYERS_ONLY = true
local REQUIRED_LEVEL = 100
local QUEST_STORAGE = 30015

local room = {
	from = Position(33219, 31657, 13),
	to = Position(33222, 31661, 13),
}

local monsterPositions = {
	{position = Position(33219, 31657, 13), name = "Demon"},
	{position = Position(33221, 31657, 13), name = "Demon"},
	{position = Position(33220, 31661, 13), name = "Demon"},
	{position = Position(33222, 31661, 13), name = "Demon"},
	{position = Position(33223, 31659, 13), name = "Demon"},
	{position = Position(33224, 31659, 13), name = "Demon"},
}

local playerPositions = {
	Position(33222, 31671, 13),
	Position(33223, 31671, 13),
	Position(33224, 31671, 13),
	Position(33225, 31671, 13),
}

local destinationPositions = {
	Position(33219, 31659, 13),
	Position(33220, 31659, 13),
	Position(33221, 31659, 13),
	Position(33222, 31659, 13),
}

local function getPlayerAt(position)
	local tile = Tile(position)
	if not tile then
		return nil
	end

	local creature = tile:getTopCreature()
	return creature and creature:isPlayer() and creature or nil
end

local function getCreaturesInRoom()
	local creatures = {}
	for x = room.from.x, room.to.x do
		for y = room.from.y, room.to.y do
			local tile = Tile(Position(x, y, room.from.z))
			if tile then
				local creature = tile:getTopCreature()
				if creature then
					creatures[#creatures + 1] = creature
				end
			end
		end
	end
	return creatures
end

local function roomHasPlayers()
	for _, creature in ipairs(getCreaturesInRoom()) do
		if creature:isPlayer() then
			return true
		end
	end
	return false
end

local function clearRoomCreatures()
	for _, creature in ipairs(getCreaturesInRoom()) do
		if creature:isMonster() then
			creature:remove()
		end
	end
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getUniqueId() ~= 7000 then
		return false
	end

	if item:getId() == 1946 then
		local participants = {}
		for index, position in ipairs(playerPositions) do
			local targetPlayer = getPlayerAt(position)
			if not targetPlayer then
				player:sendCancelMessage("You need 4 players to do this quest.")
				return true
			end

			if targetPlayer:getLevel() < REQUIRED_LEVEL then
				player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "All players have to be level " .. REQUIRED_LEVEL .. " to do this quest.")
				return true
			end

			if targetPlayer:getStorageValue(QUEST_STORAGE) ~= -1 then
				player:sendCancelMessage("Sorry, not possible.")
				return true
			end

			participants[index] = targetPlayer
		end

		if PLAYERS_ONLY then
			for _, position in ipairs(playerPositions) do
				local tile = Tile(position)
				local creature = tile and tile:getTopCreature() or nil
				if creature and not creature:isPlayer() then
					player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Only players can do this quest.")
					return true
				end
			end
		end

		for _, monster in ipairs(monsterPositions) do
			Game.createMonster(monster.name, monster.position)
		end

		for index, targetPlayer in ipairs(participants) do
			playerPositions[index]:sendMagicEffect(CONST_ME_POFF)
			targetPlayer:teleportTo(destinationPositions[index], false)
			targetPlayer:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
		end

		item:transform(1945)
		return true
	end

	if item:getId() == 1945 then
		if roomHasPlayers() then
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "There is already a team in the quest room.")
			return true
		end

		clearRoomCreatures()
		item:transform(1946)
		return true
	end

	return false
end
