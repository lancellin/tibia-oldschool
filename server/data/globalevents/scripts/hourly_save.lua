function onThink(interval)
	print("[Persistence] Hourly save started.")
	saveServer()
	Persistence.clearHouseItemsDirty()
	print("[Persistence] Hourly save finished.")
	return true
end
