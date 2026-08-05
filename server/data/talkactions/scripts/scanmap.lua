local config = {
	minX = 31800,
	minY = 31500,
	maxX = 33400,
	maxY = 33000,
	startZ = 15,
	endZ = 7,
	step = 14,
	searchRadius = 7,
	delayMs = 80,
	emptyBatchPerTick = 1500,
	progressEvery = 250
}

local scanmapState = rawget(_G, "scanmapState") or {}
_G.scanmapState = scanmapState

local function copyConfig(startZ, endZ)
	local scanConfig = {}
	for key, value in pairs(config) do
		scanConfig[key] = value
	end

	scanConfig.startZ = startZ or config.startZ
	scanConfig.endZ = endZ or config.endZ
	return scanConfig
end

local function totalPoints(scanConfig)
	local columns = math.floor((scanConfig.maxX - scanConfig.minX) / scanConfig.step) + 1
	local rows = math.floor((scanConfig.maxY - scanConfig.minY) / scanConfig.step) + 1
	local floors = scanConfig.startZ - scanConfig.endZ + 1
	return columns * rows * floors
end

local function makePosition(x, y, z)
	return Position(x, y, z)
end

local function canStandOn(player, position)
	local tile = Tile(position)
	if not tile then
		return false
	end

	return tile:queryAdd(player, FLAG_NOLIMIT) == RETURNVALUE_NOERROR
end

local function findNearestStandPosition(player, center, scanConfig)
	if canStandOn(player, center) then
		return center
	end

	for radius = 1, scanConfig.searchRadius do
		for dx = -radius, radius do
			local top = makePosition(center.x + dx, center.y - radius, center.z)
			if canStandOn(player, top) then
				return top
			end

			local bottom = makePosition(center.x + dx, center.y + radius, center.z)
			if canStandOn(player, bottom) then
				return bottom
			end
		end

		for dy = -radius + 1, radius - 1 do
			local left = makePosition(center.x - radius, center.y + dy, center.z)
			if canStandOn(player, left) then
				return left
			end

			local right = makePosition(center.x + radius, center.y + dy, center.z)
			if canStandOn(player, right) then
				return right
			end
		end
	end

	return nil
end

local function advance(cursor, scanConfig)
	cursor.x = cursor.x + scanConfig.step
	if cursor.x <= scanConfig.maxX then
		return
	end

	cursor.x = scanConfig.minX
	cursor.y = cursor.y + scanConfig.step
	if cursor.y <= scanConfig.maxY then
		return
	end

	cursor.y = scanConfig.minY
	cursor.z = cursor.z - 1
end

local function clearState(playerId)
	local state = scanmapState[playerId]
	if state and state.eventId then
		stopEvent(state.eventId)
	end
	scanmapState[playerId] = nil
	return state
end

local function finishScan(playerId, message)
	clearState(playerId)

	local player = Player(playerId)
	if player then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Agora salve no client com salvarMapaGerado() ou g_minimap.saveOtmm('/minimap.otmm').")
	end
end

local function scanStep(playerId)
	local state = scanmapState[playerId]
	if not state or not state.running then
		return
	end
	local scanConfig = state.config

	local player = Player(playerId)
	if not player then
		clearState(playerId)
		return
	end

	local processedThisTick = 0
	while processedThisTick < scanConfig.emptyBatchPerTick do
		if state.cursor.z < scanConfig.endZ then
			finishScan(playerId, string.format(
				"Scanmap concluido. Pontos: %d, teleports: %d, pulados: %d.",
				state.checked,
				state.teleported,
				state.skipped
			))
			return
		end

		local target = makePosition(state.cursor.x, state.cursor.y, state.cursor.z)
		local destination = findNearestStandPosition(player, target, scanConfig)

		state.checked = state.checked + 1
		processedThisTick = processedThisTick + 1

		if destination and player:teleportTo(destination, false) then
			state.teleported = state.teleported + 1
			advance(state.cursor, scanConfig)

			if state.checked % scanConfig.progressEvery == 0 then
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
					"Scanmap: z=%d x=%d y=%d | %d/%d pontos | teleports=%d pulados=%d",
					state.cursor.z,
					state.cursor.x,
					state.cursor.y,
					state.checked,
					state.total,
					state.teleported,
					state.skipped
				))
			end

			state.eventId = addEvent(scanStep, scanConfig.delayMs, playerId)
			return
		end

		state.skipped = state.skipped + 1
		if state.checked % scanConfig.progressEvery == 0 then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
				"Scanmap: z=%d x=%d y=%d | %d/%d pontos | teleports=%d pulados=%d",
				state.cursor.z,
				state.cursor.x,
				state.cursor.y,
				state.checked,
				state.total,
				state.teleported,
				state.skipped
			))
		end

		advance(state.cursor, scanConfig)
	end

	state.eventId = addEvent(scanStep, 1, playerId)
end

local function startScan(player, startZ, endZ)
	local playerId = player:getId()
	if scanmapState[playerId] and scanmapState[playerId].running then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Scanmap ja esta em execucao. Use /scanmap stop para parar.")
		return false
	end

	local scanConfig = copyConfig(startZ, endZ)
	if scanConfig.startZ < scanConfig.endZ then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "O andar inicial precisa ser maior ou igual ao andar final.")
		return false
	end

	scanmapState[playerId] = {
		running = true,
		config = scanConfig,
		cursor = {
			x = scanConfig.minX,
			y = scanConfig.minY,
			z = scanConfig.startZ
		},
		total = totalPoints(scanConfig),
		checked = 0,
		teleported = 0,
		skipped = 0,
		eventId = nil
	}

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Scanmap iniciado: z %d ate %d, x %d..%d, y %d..%d, passo %d.",
		scanConfig.startZ,
		scanConfig.endZ,
		scanConfig.minX,
		scanConfig.maxX,
		scanConfig.minY,
		scanConfig.maxY,
		scanConfig.step
	))
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Comandos: /scanmap status e /scanmap stop.")

	scanStep(playerId)
	return false
end

local function stopScan(player)
	local state = clearState(player:getId())
	if not state then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Nenhum scanmap em execucao.")
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Scanmap parado. Pontos: %d, teleports: %d, pulados: %d.",
		state.checked,
		state.teleported,
		state.skipped
	))
	return false
end

local function showStatus(player)
	local state = scanmapState[player:getId()]
	if not state then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Nenhum scanmap em execucao.")
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Scanmap: z=%d x=%d y=%d | %d/%d pontos | teleports=%d pulados=%d.",
		state.cursor.z,
		state.cursor.x,
		state.cursor.y,
		state.checked,
		state.total,
		state.teleported,
		state.skipped
	))
	return false
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local action = param:lower():trim()
	if action == "" or action == "start" then
		return startScan(player)
	end

	local startZ, endZ = action:match("^start%s+(%d+)%s+(%d+)$")
	if startZ and endZ then
		return startScan(player, tonumber(startZ), tonumber(endZ))
	elseif action == "stop" then
		return stopScan(player)
	elseif action == "status" then
		return showStatus(player)
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Use /scanmap start, /scanmap start 6 1, /scanmap status ou /scanmap stop.")
	return false
end
