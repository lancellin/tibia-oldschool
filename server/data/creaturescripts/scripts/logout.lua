function onLogout(player)
	Persistence.saveIfHouseItemsDirty("player logout")

	local playerId = player:getId()
	if nextUseStaminaTime[playerId] then
		nextUseStaminaTime[playerId] = nil
	end
	return true
end
