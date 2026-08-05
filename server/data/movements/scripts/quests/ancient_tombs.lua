local poison = Condition(CONDITION_POISON)
poison:setParameter(CONDITION_PARAM_DELAYED, true)
poison:setParameter(CONDITION_PARAM_MINVALUE, -50)
poison:setParameter(CONDITION_PARAM_MAXVALUE, -120)
poison:setParameter(CONDITION_PARAM_STARTVALUE, -5)
poison:setParameter(CONDITION_PARAM_TICKINTERVAL, 4000)
poison:setParameter(CONDITION_PARAM_FORCEUPDATE, true)

local coalBasinConfig = {
	[5999] = {returnDestination = Position(33097, 32815, 13)},
	[6000] = {coin = Position(33098, 32816, 13), destination = Position(33093, 32824, 13)},
	[6003] = {coin = Position(33293, 32741, 13), destination = Position(33299, 32742, 13)},
	[6004] = {returnDestination = Position(33292, 32742, 13)},
	[6005] = {coin = Position(33073, 32589, 13), destination = Position(33079, 32589, 13)},
	[6006] = {returnDestination = Position(33072, 32590, 13)},
	[6007] = {coin = Position(33240, 32855, 13), destination = Position(33246, 32850, 13)},
	[6008] = {returnDestination = Position(33239, 32856, 13)},
	[6009] = {coin = Position(33276, 32552, 14), destination = Position(33271, 32553, 14)},
	[6011] = {coin = Position(33135, 32682, 12), destination = Position(33130, 32683, 12)},
	[6012] = {returnDestination = Position(33136, 32683, 12)},
	[6013] = {coin = Position(33161, 32831, 10), destination = Position(33156, 32832, 10)},
	[6014] = {returnDestination = Position(33162, 32832, 10)},
	[6016] = {coin = Position(33233, 32692, 13), destination = Position(33234, 32687, 13)},
	[6017] = {returnDestination = Position(33235, 32694, 13)},
	[6018] = {returnDestination = Position(33277, 32553, 14)},
}

local leverTeleports = {
	[13000] = {
		leverPositions = {
			Position(33399, 32794, 14),
			Position(33382, 32786, 14),
			Position(33368, 32763, 14),
			Position(33357, 32749, 14),
			Position(33305, 32734, 14),
			Position(33338, 32702, 14),
			Position(33358, 32701, 14),
			Position(33349, 32680, 14),
			Position(33320, 32682, 14),
		},
		destination = Position(33366, 32806, 14),
	},
	[13001] = {
		leverPositions = {
			Position(33176, 32880, 11),
			Position(33182, 32880, 11),
			Position(33175, 32884, 11),
			Position(33183, 32884, 11),
			Position(33176, 32889, 11),
			Position(33181, 32889, 11),
		},
		destination = Position(33198, 32885, 11),
	},
}

local helmetTilePosition = Position(33198, 32876, 11)

local function teleportCreature(creature, destination, effect)
	creature:teleportTo(destination, false)
	destination:sendMagicEffect(effect or CONST_ME_MAGIC_BLUE)
end

local function handleCoalBasin(creature, actionId)
	local config = coalBasinConfig[actionId]
	if not config then
		return false
	end

	if config.coin then
		local coinTile = Tile(config.coin)
		local coinItem = coinTile and coinTile:getItemById(2159) or nil
		if coinItem then
			teleportCreature(creature, config.destination)
			coinItem:remove()
			config.coin:sendMagicEffect(CONST_ME_MAGIC_RED)
		end
		return true
	end

	if config.returnDestination then
		teleportCreature(creature, config.returnDestination)
		return true
	end

	return false
end

local function handleLeverTeleport(creature, item, position)
	local config = leverTeleports[item.actionid]
	if not config then
		return false
	end

	for _, leverPosition in ipairs(config.leverPositions) do
		local leverTile = Tile(leverPosition)
		local lever = leverTile and leverTile:getTopVisibleThing() or nil
		if not lever or not lever:isItem() or lever:getId() ~= 1946 then
			local fallback = Position(position)
			fallback.y = fallback.y - 2
			creature:teleportTo(fallback, false)
			fallback:sendMagicEffect(CONST_ME_TELEPORT)
			creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You need to flip all levers to pass on teleport.")
			return true
		end
	end

	teleportCreature(creature, config.destination)
	return true
end

local function handleHelmetTile(creature)
	local tile = Tile(helmetTilePosition)
	if not tile then
		return true
	end

	for itemId = 2335, 2341 do
		local helmet = tile:getItemById(itemId)
		if not helmet then
			return true
		end
		helmet:remove()
	end

	Game.createItem(2342, 1, helmetTilePosition)
	helmetTilePosition:sendMagicEffect(CONST_ME_FIREAREA)
	return true
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local actionId = item.actionid
	if actionId == 6001 then
		teleportCreature(creature, Position(33205, 32956, 14), CONST_ME_MAGIC_RED)
		return true
	end

	if actionId == 6015 then
		creature:addCondition(poison)
		position:sendMagicEffect(CONST_ME_POISONAREA)
		creature:setStorageValue(10051, 1)
		return true
	end

	if actionId == 13002 then
		return handleHelmetTile(creature)
	end

	if handleLeverTeleport(creature, item, position) then
		return true
	end

	if handleCoalBasin(creature, actionId) then
		return true
	end

	return true
end
