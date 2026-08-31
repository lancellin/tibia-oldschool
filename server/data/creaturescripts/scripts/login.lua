function onLogin(player)
	-- Sealed room: only reachable through this manager teleport.
	local managerPosition = Position(32096, 32219, 5)
	local playerName = player:getName()
	local isCharacterManager = playerName:sub(1, #"Character Manager ") == "Character Manager "

	local wormCount = player:getItemCount(3976)
	if wormCount > 0 then
		player:removeItem(3976, wormCount, -1, true)
	end

	local serverName = configManager.getString(configKeys.SERVER_NAME)
	local loginStr = "Welcome to " .. serverName .. "!"
	if player:getLastLoginSaved() <= 0 then
		loginStr = loginStr .. " Please choose your outfit."
		player:sendOutfitWindow()
	else
		if loginStr ~= "" then
			player:sendTextMessage(MESSAGE_STATUS_DEFAULT, loginStr)
		end

		loginStr = string.format("Your last visit in %s: %s.", serverName, os.date("%d %b %Y %X", player:getLastLoginSaved()))
	end
	player:sendTextMessage(MESSAGE_STATUS_DEFAULT, loginStr)

	-- Promotion
	local vocation = player:getVocation()
	local promotion = vocation:getPromotion()
	if player:isPremium() then
		local value = player:getStorageValue(PlayerStorageKeys.promotion)
		if value == 1 then
			player:setVocation(promotion)
		end
	elseif not promotion then
		player:setVocation(vocation:getDemotion())
	end

	-- Events
	player:registerEvent("PlayerDeath")
	player:registerEvent("DropLoot")

	if playerName == "Account Manager" or isCharacterManager then
		player:setTown(Town(11))
		player:teleportTo(managerPosition)
		managerPosition:sendMagicEffect(CONST_ME_TELEPORT)
		if playerName == "Account Manager" then
			player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "Say hi to the account clerk next to you to create a test account and a fresh character.")
		else
			player:sendTextMessage(MESSAGE_STATUS_DEFAULT, "Say hi to the account clerk next to you to create another character for this account.")
		end
	end
	return true
end
