local function beginEmergencyCleanSave()
	if not Game.beginFloorPersistenceCleanSave(false) then
		print("[Emergency] The authorized coordinated clean save failed to start. Server access remains closed.")
	end
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() or player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	local action = param:match("^%s*(.-)%s*$"):lower()
	if action == "" or action == "start" then
		if Game.isEmergencyActive() then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Emergency mode is already active.")
			return false
		end

		if not Game.activateEmergency(player:getGuid(), player:getName()) then
			player:sendTextMessage(
				MESSAGE_STATUS_CONSOLE_BLUE,
				"Emergency mode could not be activated during recovery, clean save or shutdown."
			)
			return false
		end

		player:sendTextMessage(
			MESSAGE_STATUS_CONSOLE_BLUE,
			"Emergency mode activated. Ordinary login and server saves are blocked; decay is paused."
		)
		return false
	end

	if action == "finish" then
		if not Game.isEmergencyActive() then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Emergency mode is not active.")
			return false
		end

		if not Game.finishEmergency(player:getGuid(), player:getName()) then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Emergency mode could not be finished.")
			return false
		end

		player:sendTextMessage(
			MESSAGE_STATUS_CONSOLE_BLUE,
			"Emergency finish authorized. Player corpses received 50 extra minutes; starting coordinated clean save."
		)
		addEvent(beginEmergencyCleanSave, 100)
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Use !emergency or !emergency finish.")
	return false
end
