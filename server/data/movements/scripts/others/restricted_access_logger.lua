local WATCH_ACTION_ID = 65000
local LOG_FILE = "data/logs/restricted_access_tiles.log"

local function appendRestrictedAccessLog(player, item, position, fromPosition)
	local playerName = player:getName()
	local line = string.format(
		"[%s] player=%s itemid=%d aid=%d uid=%d tile=%d,%d,%d from=%d,%d,%d\n",
		os.date("%Y-%m-%d %H:%M:%S"),
		playerName,
		item:getId(),
		item:getActionId(),
		item:getUniqueId(),
		position.x,
		position.y,
		position.z,
		fromPosition.x,
		fromPosition.y,
		fromPosition.z
	)

	print("[RestrictedAccess] " .. line:gsub("\n", ""))

	local file, err = io.open(LOG_FILE, "a")
	if not file then
		print(string.format("[RestrictedAccess] failed to open log file '%s': %s", LOG_FILE, err or "unknown error"))
		return
	end

	file:write(line)
	file:close()
end

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() or item:getActionId() ~= WATCH_ACTION_ID then
		return true
	end

	appendRestrictedAccessLog(creature, item, position, fromPosition)
	return true
end
