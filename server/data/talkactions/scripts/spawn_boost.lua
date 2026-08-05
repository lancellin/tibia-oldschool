local BOOST_DURATION_SECONDS = 15 * 60

local function formatRemaining(seconds)
	local totalMinutes = math.ceil(seconds / 60)
	local hours = math.floor(totalMinutes / 60)
	local minutes = totalMinutes % 60
	if hours > 0 then
		return string.format("%dh %02dmin", hours, minutes)
	end
	return string.format("%dmin", minutes)
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	logCommand(player, words, param)
	if not Game.addSpawnRateBoostDuration(BOOST_DURATION_SECONDS) then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			"Nao foi possivel persistir o boost global de respawn.")
		return false
	end

	Game.broadcastMessage(string.format(
		"%s comprou boost global de respawn por 15 minutos.", player:getName()
	), MESSAGE_STATUS_WARNING)

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Tempo total acumulado do boost global: %s.",
		formatRemaining(Game.getSpawnRateBoostRemainingSeconds())
	))
	return false
end
