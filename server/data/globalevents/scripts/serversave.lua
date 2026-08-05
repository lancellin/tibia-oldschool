local INTERNAL_SHUTDOWN_DELAY = 10 * 60 * 1000

local function FinalizeServerSaveShutdown()
	Game.setGameState(GAME_STATE_SHUTDOWN)
end

local function ServerSave()
	if Game.isEmergencyActive() then
		print("[Emergency] Automatic server save cancelled while emergency mode is active.")
		return
	end

	if configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN) then
		-- Public access stops now, but the process remains alive while the
		-- coordinated player/floor checkpoint is verified. The actual process
		-- shutdown happens ten minutes later; reopening remains manual.
		if Game.beginFloorPersistenceCleanSave() then
			addEvent(FinalizeServerSaveShutdown, INTERNAL_SHUTDOWN_DELAY)
		else
			print("[Error - ServerSave] Coordinated player/floor checkpoint failed. Automatic shutdown was cancelled.")
		end
	else
		local closeAtServerSave = configManager.getBoolean(configKeys.SERVER_SAVE_CLOSE)
		if closeAtServerSave then
			Game.setGameState(GAME_STATE_CLOSED)
		end

		saveServer()
		Persistence.clearHouseItemsDirty()

		if configManager.getBoolean(configKeys.SERVER_SAVE_CLEAN_MAP) then
			cleanMap()
		end

		if closeAtServerSave then
			Game.setGameState(GAME_STATE_NORMAL)
		end
	end
end

local function ServerSaveWarning(time)
	if Game.isEmergencyActive() then
		print("[Emergency] Pending server save warning chain cancelled while emergency mode is active.")
		return
	end

	local remaningTime = tonumber(time) - 60000

	if configManager.getBoolean(configKeys.SERVER_SAVE_NOTIFY_MESSAGE) then
		Game.broadcastMessage("Server is saving game in " .. (remaningTime/60000) .."  minute(s). Please logout.", MESSAGE_STATUS_WARNING)
	end

	if remaningTime > 60000 then
		addEvent(ServerSaveWarning, 60000, remaningTime)
	else
		addEvent(ServerSave, 60000)
	end
end

function onTime(interval)
	if Game.isEmergencyActive() then
		print("[Emergency] Scheduled server save cancelled while emergency mode is active.")
		return true
	end

	local remaningTime = configManager.getNumber(configKeys.SERVER_SAVE_NOTIFY_DURATION) * 60000
	if configManager.getBoolean(configKeys.SERVER_SAVE_NOTIFY_MESSAGE) then
		Game.broadcastMessage("Server is saving game in " .. (remaningTime/60000) .."  minute(s). Please logout.", MESSAGE_STATUS_WARNING)
	end

	addEvent(ServerSaveWarning, 60000, remaningTime)
	return not configManager.getBoolean(configKeys.SERVER_SAVE_SHUTDOWN)
end
