local function sendLine(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	local instanceId = param:match("^%s*(.-)%s*$"):lower()
	local result = Game.inspectFloorInstanceId(instanceId)
	if not result.validFormat then
		sendLine(player, "Use /instancecheck <32 lowercase hexadecimal characters>.")
		if result.error ~= "" then
			sendLine(player, "Check error: " .. result.error)
		end
		return false
	end

	if not result.databaseAvailable or result.error ~= "" then
		sendLine(player, string.format(
			"Instance %s: INCONCLUSIVE. Do not recreate the item.",
			instanceId
		))
		sendLine(player, "Check error: " .. (result.error ~= "" and result.error or "database unavailable"))
		return false
	end

	if result.safeToRecreate then
		sendLine(player, string.format(
			"Instance %s: ABSENT from current durable storage and live unsaved state.",
			instanceId
		))
		sendLine(player,
			"Manual compensation is allowed only as a newly created item with a new instance_id; never reuse the old ID.")
		return false
	end

	sendLine(player, string.format(
		"Instance %s: PRESENT/RESERVED. Do not recreate. database_matches=%.0f live_matches=%.0f.",
		instanceId, result.databaseMatches, result.liveMatches
	))
	if result.firstDatabaseLocation ~= "" then
		sendLine(player, "First durable match: " .. result.firstDatabaseLocation)
	end
	if result.firstLiveLocation ~= "" then
		sendLine(player, "First live match: " .. result.firstLiveLocation)
	end
	return false
end
