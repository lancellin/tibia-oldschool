function onSay(player, words, param)
	param = param:trim()
	if param == "" then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Use !bestiary <monster name>.")
		return false
	end

	local resultId = db.storeQuery("SELECT `creature_id`, `name`, `kills_stage_1`, `kills_stage_2`, `kills_stage_3`, `charm_points` FROM `bestiary_monsters` WHERE `name` = " .. db.escapeString(param) .. " AND `enabled` = 1")
	if not resultId then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Monster not found in bestiary: " .. param .. ".")
		return false
	end

	local creatureId = result.getNumber(resultId, "creature_id")
	local creatureName = result.getString(resultId, "name")
	local stage1 = result.getNumber(resultId, "kills_stage_1")
	local stage2 = result.getNumber(resultId, "kills_stage_2")
	local stage3 = result.getNumber(resultId, "kills_stage_3")
	local charmPoints = result.getNumber(resultId, "charm_points")
	result.free(resultId)

	local kills = 0
	local stageReached = 0
	resultId = db.storeQuery("SELECT `kills`, `last_stage_reached` FROM `player_bestiary_progress` WHERE `player_id` = " .. player:getGuid() .. " AND `creature_id` = " .. creatureId)
	if resultId then
		kills = result.getNumber(resultId, "kills")
		stageReached = result.getNumber(resultId, "last_stage_reached")
		result.free(resultId)
	end

	player:sendTextMessage(
		MESSAGE_STATUS_CONSOLE_BLUE,
		string.format(
			"Bestiary %s [id %04d]: kills=%d, stage=%d, thresholds=%d/%d/%d, charm=%d.",
			creatureName,
			creatureId,
			kills,
			stageReached,
			stage1,
			stage2,
			stage3,
			charmPoints
		)
	)
	return false
end
