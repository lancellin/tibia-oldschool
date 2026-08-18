-- Elite Creatures GM test tool (mirrors the loottest.lua pattern).
--
-- /elitetest elite, demon, 3      -> spawns a tier-3 elite demon next to
--                                    the GM so crystal drops and outfit
--                                    darkening can be tested deterministically.
-- /elitetest portal, rat, 2       -> spawns a normal rat whose death roll is
--                                    forced to tier 2, so the portal color and
--                                    the delayed elite spawn can be verified.
-- /elitetest portal, rat, 2, 5    -> wave mode: spawns 5 rats around the GM
--                                    and forces the next 5 death rolls to
--                                    tier 2; kill them all with one AoE to
--                                    verify multiple simultaneous portals.

local eliteTest = TalkAction("/elitetest")

local function collectSpawnPositions(centerPosition, needed)
	local positions = {}
	for radius = 1, 4 do
		for dx = -radius, radius do
			for dy = -radius, radius do
				if dx ~= 0 or dy ~= 0 then
					local position = Position(centerPosition.x + dx, centerPosition.y + dy, centerPosition.z)
					local tile = Tile(position)
					if tile and tile:getGround() and not tile:getTopCreature() then
						table.insert(positions, position)
						if #positions >= needed then
							return positions
						end
					end
				end
			end
		end
	end
	return positions
end

function eliteTest.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		player:sendCancelMessage("Comando restrito a membros da equipe.")
		return false
	end

	local mode, monsterName, tierText, countText = param:match("^%s*(%a+)%s*,%s*(.-)%s*,%s*(%d+)%s*,%s*(%d+)%s*$")
	if not mode then
		mode, monsterName, tierText = param:match("^%s*(%a+)%s*,%s*(.-)%s*,%s*(%d+)%s*$")
		countText = "1"
	end

	if not mode then
		player:sendCancelMessage("Use: /elitetest elite, demon, 3  ou  /elitetest portal, rat, 2[, 5]")
		return false
	end

	local tier = tonumber(tierText)
	if not tier or tier < 1 or tier > 3 then
		player:sendCancelMessage("Tier deve ser 1, 2 ou 3.")
		return false
	end

	local count = math.max(1, math.min(20, tonumber(countText) or 1))

	if mode ~= "elite" and mode ~= "portal" then
		player:sendCancelMessage("Modo invalido. Use: elite ou portal.")
		return false
	end

	if mode == "elite" and count > 1 then
		player:sendCancelMessage("O modo elite cria uma unica criatura (use o modo portal para ondas).")
		return false
	end

	local monsterType = MonsterType(monsterName)
	if not monsterType then
		player:sendCancelMessage("Monstro nao encontrado: " .. monsterName)
		return false
	end

	local positions = collectSpawnPositions(player:getPosition(), count)
	if #positions < count then
		player:sendCancelMessage("Sem espaco livre suficiente por perto (" .. #positions .. " de " .. count .. ").")
		return false
	end

	if mode == "elite" then
		local monster = Game.createMonster(monsterType:name(), positions[1], false, true)
		if not monster then
			player:sendCancelMessage("Falha ao criar o monstro.")
			return false
		end

		monster:setEliteTier(tier)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("Elite '%s' (tier %d) criado. Mate-o para testar o drop de cristais.", monsterType:name(), tier))
	else
		local spawned = 0
		for _, position in ipairs(positions) do
			if Game.createMonster(monsterType:name(), position, false, true) then
				spawned = spawned + 1
			end
		end

		if spawned == 0 then
			player:sendCancelMessage("Falha ao criar os monstros.")
			return false
		end

		Game.setForcedEliteTier(tier, spawned)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("%d '%s' criados: os proximos %d rolls de morte serao portais tier %d. Mate todos num unico turno para testar portais simultaneos.",
				spawned, monsterType:name(), spawned, tier))
	end

	return false
end

eliteTest:separator(" ")
eliteTest:register()
