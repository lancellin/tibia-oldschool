local function sendLine(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local function trim(value)
	return value:match("^%s*(.-)%s*$")
end

local function parsePosition(player, value)
	value = trim(value):lower()
	if value == "" or value == "front" then
		local position = player:getPosition()
		position:getNextPosition(player:getDirection())
		return position
	end

	if value == "here" then
		return player:getPosition()
	end

	local x, y, z = value:match("^(%d+)%s*[, ]%s*(%d+)%s*[, ]%s*(%d+)$")
	if not x then
		return nil
	end
	return Position(tonumber(x), tonumber(y), tonumber(z))
end

local function reasonText(record)
	local names = FloorPersistence.getDirtyReasonNames(record.reasonMask)
	return #names > 0 and table.concat(names, ",") or "NONE"
end

local function originText(record)
	local names = FloorPersistence.getDirtyOriginNames(record.originMask)
	return #names > 0 and table.concat(names, ",") or "NONE"
end

local function timeText(timestamp)
	return os.date("%d/%m/%Y %H:%M:%S", timestamp)
end

local function showStatus(player)
	local stats = Game.getFloorDirtyStats()
	sendLine(player, string.format(
		"Dirty tracking: enabled=%s tiles=%d events=%d sequence=%d ignored_system=%d city_positions=%d. Use /floorsnapshot for stage 3 database status; replay is disabled.",
		stats.enabled and "yes" or "no",
		stats.tileCount,
		stats.totalEvents,
		stats.sequence,
		stats.ignoredSystemEvents,
		stats.cityPositionCount
	))
end

local function showPosition(player, position)
	local tile = Tile(position)
	local city = FloorPersistence.isCityPosition(position)
	local house = tile and tile:getHouse() ~= nil or false
	local record = Game.getFloorDirtyTile(position)

	if not record then
		sendLine(player, string.format(
			"Tile %d,%d,%d dirty=no city=%s house=%s.",
			position.x,
			position.y,
			position.z,
			city and "yes" or "no",
			house and "yes" or "no"
		))
		return
	end

	sendLine(player, string.format(
		"Tile %d,%d,%d dirty=yes events=%d sequence=%d..%d last=%s origin=%s reasons=%s origins=%s.",
		position.x,
		position.y,
		position.z,
		record.eventCount,
		record.firstSequence,
		record.lastSequence,
		FloorPersistence.getDirtyReasonName(record.lastReason),
		FloorPersistence.getDirtyOriginName(record.lastOrigin),
		reasonText(record),
		originText(record)
	))
	sendLine(player, string.format(
		"First=%s last=%s city=no house=no tile_version=%.0f snapshot_in_flight=%s retries=%d error=%s.",
		timeText(record.firstModifiedAt),
		timeText(record.lastModifiedAt),
		record.tileVersion,
		record.snapshotInFlight and "yes" or "no",
		record.snapshotRetryCount,
		record.lastSnapshotError ~= "" and record.lastSnapshotError or "-"
	))
end

local function listDirty(player, limit)
	limit = math.min(100, math.max(1, limit or 20))
	local records = Game.getFloorDirtyTiles(limit)
	sendLine(player, string.format("Most recent dirty tiles: showing=%d limit=%d.", #records, limit))
	for _, record in ipairs(records) do
		sendLine(player, string.format(
			"%d,%d,%d events=%d last_seq=%d last=%s origin=%s reasons=%s origins=%s",
			record.x,
			record.y,
			record.z,
			record.eventCount,
			record.lastSequence,
			FloorPersistence.getDirtyReasonName(record.lastReason),
			FloorPersistence.getDirtyOriginName(record.lastOrigin),
			reasonText(record),
			originText(record)
		))
	end
end

local function clearPosition(player, value)
	local position = parsePosition(player, value)
	if not position then
		sendLine(player, "Use /floordirty clear front, here or x,y,z.")
		return
	end

	local removed = Game.clearFloorDirtyTile(position)
	sendLine(player, string.format(
		"Dirty record %d,%d,%d removed=%s. Floor items were not changed.",
		position.x,
		position.y,
		position.z,
		removed and "yes" or "no"
	))
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	local action = trim(param):lower()
	if action == "" or action == "status" then
		showStatus(player)
		return false
	end

	local listLimit = action:match("^list%s*(%d*)$")
	if listLimit then
		listDirty(player, tonumber(listLimit))
		return false
	end

	if action == "clear all confirm" then
		local count = Game.clearFloorDirtyTiles()
		sendLine(player, string.format(
			"Cleared %d dirty records. Floor items were not changed; tracking remains enabled.",
			count
		))
		return false
	end

	local clearValue = action:match("^clear%s+(.+)$")
	if clearValue then
		clearPosition(player, clearValue)
		return false
	end

	local position = parsePosition(player, action)
	if position then
		showPosition(player, position)
		return false
	end

	sendLine(player, "Use /floordirty status, list [limit], front, here, x,y,z, clear <position> or clear all confirm.")
	return false
end
