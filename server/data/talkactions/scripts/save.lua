function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	logCommand(player, words, param)
	saveServer()
	Persistence.clearHouseItemsDirty()
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Server saved.")
	return false
end
