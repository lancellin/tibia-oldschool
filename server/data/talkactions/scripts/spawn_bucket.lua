local function normalize(value)
	return value:lower():gsub("^%s+", ""):gsub("%s+$", "")
end

local function sendStatus(player)
	local mode = Game.isSpawnPlayerBucketOverridden() and "fixo" or "automatico"
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Bucket de respawn: %d (%s). Jogadores online: %d.",
		Game.getSpawnPlayerBucket(), mode, Game.getPlayerCount()
	))
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	logCommand(player, words, param)
	param = normalize(param)

	if param == "" or param == "status" then
		sendStatus(player)
		return false
	end

	if param == "auto" or param == "reset" then
		Game.clearSpawnPlayerBucketOverride()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			"Bucket de respawn voltou ao modo automatico. A alteracao vale para novos timers de respawn.")
		sendStatus(player)
		return false
	end

	local bucket = tonumber(param)
	if not bucket or bucket ~= math.floor(bucket) or bucket < 0 or bucket > 600 or bucket % 50 ~= 0 then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			"Uso: /spawnbucket 0|50|100|...|600, /spawnbucket auto ou /spawnbucket status.")
		return false
	end

	if not Game.setSpawnPlayerBucketOverride(bucket) then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Nao foi possivel fixar o bucket informado.")
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Bucket de respawn fixado em %d. A alteracao vale para novos timers de respawn; timers existentes nao sao recalculados.",
		bucket
	))
	return false
end
