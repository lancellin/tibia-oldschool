function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	if Game.isEmergencyActive() then
		player:sendTextMessage(
			MESSAGE_STATUS_CONSOLE_BLUE,
			"Server access cannot be reopened while emergency mode is active. Use !emergency finish."
		)
		return false
	end

	Game.setGameState(GAME_STATE_NORMAL)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Server is now open.")
	return false
end
