FloorPersistence = rawget(_G, "FloorPersistence") or {}

FloorPersistence.STAGE = 3
FloorPersistence.INSTANCE_ID_ATTRIBUTE = "floor_persistence_instance_id"
FloorPersistence.DEATH_BUNDLE_ATTRIBUTE = "floor_persistence_death_bundle"
FloorPersistence.PLAYER_CORPSE_ATTRIBUTE = "floor_persistence_player_corpse"
FloorPersistence.CREATURE_CORPSE_ATTRIBUTE = "floor_persistence_creature_corpse"

FloorPersistence.STATE = {
	HOUSE_OWNED = "HOUSE_OWNED",
	PERSIST_DEATH_BUNDLE = "PERSIST_DEATH_BUNDLE",
	CREATURE_CORPSE_EXCLUDED = "CREATURE_CORPSE_EXCLUDED",
	CITY_EXCLUDED = "CITY_EXCLUDED",
	OTBM_BASE = "OTBM_BASE",
	DO_NOT_PERSIST = "DO_NOT_PERSIST",
	PERSIST_FOOD = "PERSIST_FOOD",
	PERSIST_CLEAN_ONLY = "PERSIST_CLEAN_ONLY",
	PERSIST_ALWAYS = "PERSIST_ALWAYS"
}

FloorPersistence.CITY_POSITIONS = {
	{x = 32339, y = 32213, z = 7},
	{x = 32340, y = 32213, z = 7},
	{x = 32341, y = 32213, z = 7}
}

FloorPersistence.DIRTY_REASON = {
	{mask = 1, name = "ITEM_ADD"},
	{mask = 2, name = "ITEM_REMOVE"},
	{mask = 4, name = "ITEM_UPDATE"},
	{mask = 8, name = "ITEM_REPLACE"},
	{mask = 16, name = "CONTAINER_ADD"},
	{mask = 32, name = "CONTAINER_REMOVE"},
	{mask = 64, name = "CONTAINER_UPDATE"},
	{mask = 128, name = "ATTRIBUTE_UPDATE"},
	{mask = 256, name = "DEATH_BUNDLE"}
}

FloorPersistence.DIRTY_ORIGIN = {
	{mask = 1, name = "PLAYER_MOVE"},
	{mask = 2, name = "PLAYER_DEATH"},
	{mask = 4, name = "EXPLICIT"}
}

local cityTiles = {}
for _, position in ipairs(FloorPersistence.CITY_POSITIONS) do
	cityTiles[string.format("%d:%d:%d", position.x, position.y, position.z)] = true
end

local function positionKey(position)
	return string.format("%d:%d:%d", position.x, position.y, position.z)
end

function FloorPersistence.isCityPosition(position)
	if Game and Game.isFloorPersistenceCityPosition then
		return Game.isFloorPersistenceCityPosition(position)
	end
	return position and cityTiles[positionKey(position)] == true
end

function FloorPersistence.getDirtyReasonName(reason)
	for _, definition in ipairs(FloorPersistence.DIRTY_REASON) do
		if definition.mask == reason then
			return definition.name
		end
	end
	return "UNKNOWN"
end

function FloorPersistence.getDirtyReasonNames(reasonMask)
	local names = {}
	for _, definition in ipairs(FloorPersistence.DIRTY_REASON) do
		if bit.band(reasonMask, definition.mask) ~= 0 then
			names[#names + 1] = definition.name
		end
	end
	return names
end

function FloorPersistence.getDirtyOriginName(origin)
	for _, definition in ipairs(FloorPersistence.DIRTY_ORIGIN) do
		if definition.mask == origin then
			return definition.name
		end
	end
	return "SYSTEM"
end

function FloorPersistence.getDirtyOriginNames(originMask)
	local names = {}
	for _, definition in ipairs(FloorPersistence.DIRTY_ORIGIN) do
		if bit.band(originMask, definition.mask) ~= 0 then
			names[#names + 1] = definition.name
		end
	end
	return names
end

function FloorPersistence.isFoodId(itemId)
	return (itemId >= 2666 and itemId <= 2691)
		or itemId == 2695
		or itemId == 2696
		or (itemId >= 2787 and itemId <= 2796)
end

function FloorPersistence.classifyItem(item, tile, context)
	if not item or not tile then
		return nil
	end

	context = context or {}
	local itemType = ItemType(item:getId())
	local result = {
		state = nil,
		itemId = item:getId(),
		movable = itemType:isMovable(),
		stackable = itemType:isStackable(),
		food = FloorPersistence.isFoodId(item:getId()),
		loadedFromMap = item:isLoadedFromMap(),
		city = FloorPersistence.isCityPosition(tile:getPosition()),
		house = tile:getHouse() ~= nil,
		playerCorpse = item:getCustomAttribute(FloorPersistence.PLAYER_CORPSE_ATTRIBUTE) ~= nil,
		creatureCorpse = context.creatureCorpse == true
			or item:getCustomAttribute(FloorPersistence.CREATURE_CORPSE_ATTRIBUTE) ~= nil
			or item:getCustomAttribute("loot_monster_name") ~= nil,
		deathBundle = context.deathBundle == true
			or item:getCustomAttribute(FloorPersistence.DEATH_BUNDLE_ATTRIBUTE) ~= nil
	}
	result.cityException = context.cityException == true or result.playerCorpse

	if result.house then
		result.state = FloorPersistence.STATE.HOUSE_OWNED
	elseif result.deathBundle then
		result.state = FloorPersistence.STATE.PERSIST_DEATH_BUNDLE
	elseif result.creatureCorpse then
		result.state = FloorPersistence.STATE.CREATURE_CORPSE_EXCLUDED
	elseif context.otbmBase == true or result.loadedFromMap then
		result.state = FloorPersistence.STATE.OTBM_BASE
	elseif not result.movable then
		result.state = FloorPersistence.STATE.DO_NOT_PERSIST
	elseif result.stackable and result.food then
		result.state = FloorPersistence.STATE.PERSIST_FOOD
	elseif result.stackable then
		result.state = FloorPersistence.STATE.PERSIST_CLEAN_ONLY
	else
		result.state = FloorPersistence.STATE.PERSIST_ALWAYS
	end

	return result
end

function FloorPersistence.childContext(parentContext, classification)
	parentContext = parentContext or {}
	return {
		deathBundle = parentContext.deathBundle == true
			or classification.state == FloorPersistence.STATE.PERSIST_DEATH_BUNDLE,
		cityException = parentContext.cityException == true
			or classification.cityException == true,
		creatureCorpse = parentContext.creatureCorpse == true
			or classification.creatureCorpse == true,
		otbmBase = parentContext.otbmBase == true
			or classification.state == FloorPersistence.STATE.OTBM_BASE
	}
end

if Game and Game.setFloorPersistenceCityPosition then
	Game.clearFloorPersistenceCityPositions()
	for _, position in ipairs(FloorPersistence.CITY_POSITIONS) do
		Game.setFloorPersistenceCityPosition(Position(position.x, position.y, position.z), true)
	end
	Game.setFloorDirtyTrackingEnabled(true)
end
