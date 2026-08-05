local function boolText(value)
	return value and "yes" or "no"
end

local function sendLine(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local function parseRequest(player, param)
	local tokens = {}
	for token in param:gmatch("[^,%s]+") do
		tokens[#tokens + 1] = token:lower()
	end

	local assign = false
	if tokens[1] == "assign" then
		assign = true
		table.remove(tokens, 1)
	end

	if #tokens == 0 or (#tokens == 1 and tokens[1] == "front") then
		local position = player:getPosition()
		position:getNextPosition(player:getDirection())
		return position, assign
	end

	if #tokens == 1 and tokens[1] == "here" then
		return player:getPosition(), assign
	end

	if #tokens ~= 3 then
		return nil, assign
	end

	local x, y, z = tonumber(tokens[1]), tonumber(tokens[2]), tonumber(tokens[3])
	if not x or not y or not z then
		return nil, assign
	end
	return Position(x, y, z), assign
end

local function inspectItem(player, item, tile, path, context, assign, totals)
	local classification = FloorPersistence.classifyItem(item, tile, context)
	if not classification then
		return
	end

	totals.items = totals.items + 1
	totals.states[classification.state] = (totals.states[classification.state] or 0) + 1

	local instanceId = item:getFloorPersistenceInstanceId()
	local rawInstanceId = item:getCustomAttribute(FloorPersistence.INSTANCE_ID_ATTRIBUTE)
	if assign and classification.state == FloorPersistence.STATE.PERSIST_ALWAYS then
		if instanceId then
			totals.identitiesExisting = totals.identitiesExisting + 1
		else
			instanceId = item:ensureFloorPersistenceInstanceId()
			if instanceId then
				totals.identitiesCreated = totals.identitiesCreated + 1
			elseif rawInstanceId ~= nil then
				totals.identitiesInvalid = totals.identitiesInvalid + 1
			end
		end
	end

	local identityText = instanceId or (rawInstanceId ~= nil and "INVALID" or "-")
	sendLine(player, string.format(
		"[%s] id=%d name=%s count=%d movable=%s stackable=%s food=%s otbm=%s state=%s instance=%s",
		path,
		item:getId(),
		item:getName(),
		item:getCount(),
		boolText(classification.movable),
		boolText(classification.stackable),
		boolText(classification.food),
		boolText(classification.loadedFromMap or context.otbmBase == true),
		classification.state,
		identityText
	))

	if not item:isContainer() then
		return
	end

	local childContext = FloorPersistence.childContext(context, classification)
	for index, child in ipairs(item:getItems()) do
		inspectItem(player, child, tile, path .. "." .. index, childContext, assign, totals)
	end
end

local function summarizeStates(states)
	local parts = {}
	for _, state in pairs(FloorPersistence.STATE) do
		if states[state] then
			parts[#parts + 1] = state .. "=" .. states[state]
		end
	end
	table.sort(parts)
	return #parts > 0 and table.concat(parts, ", ") or "none"
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	local position, assign = parseRequest(player, param)
	if not position then
		sendLine(player, "Use /floorinspect [front|here|x,y,z] or /floorinspect assign[,x,y,z].")
		return false
	end

	local tile = Tile(position)
	if not tile then
		sendLine(player, string.format("No tile exists at %d,%d,%d.", position.x, position.y, position.z))
		return false
	end

	local items = tile:getItems() or {}
	local totals = {
		items = 0,
		states = {},
		identitiesCreated = 0,
		identitiesExisting = 0,
		identitiesInvalid = 0
	}

	sendLine(player, string.format(
		"Floor persistence stage=%d tile=%d,%d,%d city=%s house=%s mode=%s (inspection only; snapshots may be active, replay is disabled).",
		FloorPersistence.STAGE,
		position.x,
		position.y,
		position.z,
		boolText(FloorPersistence.isCityPosition(position)),
		boolText(tile:getHouse() ~= nil),
		assign and "assign" or "read-only"
	))

	for index, item in ipairs(items) do
		inspectItem(player, item, tile, tostring(index), {}, assign, totals)
	end

	sendLine(player, string.format(
		"Summary: items=%d; %s; instance_created=%d existing=%d invalid=%d.",
		totals.items,
		summarizeStates(totals.states),
		totals.identitiesCreated,
		totals.identitiesExisting,
		totals.identitiesInvalid
	))
	return false
end
