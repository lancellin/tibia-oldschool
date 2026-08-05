local CRITICAL_CHARM_ID = 1
local VAMPIRIC_EMBRACE_CHARM_ID = 2
local VOIDS_CALL_CHARM_ID = 3
local DODGE_CHARM_ID = 4

local aliases = {
	["1"] = CRITICAL_CHARM_ID,
	["critical"] = CRITICAL_CHARM_ID,
	["savage"] = CRITICAL_CHARM_ID,
	["savage blow"] = CRITICAL_CHARM_ID,
	["savageblow"] = CRITICAL_CHARM_ID,
	["2"] = VAMPIRIC_EMBRACE_CHARM_ID,
	["vampiric"] = VAMPIRIC_EMBRACE_CHARM_ID,
	["vampiric embrace"] = VAMPIRIC_EMBRACE_CHARM_ID,
	["vampiricembrace"] = VAMPIRIC_EMBRACE_CHARM_ID,
	["3"] = VOIDS_CALL_CHARM_ID,
	["void"] = VOIDS_CALL_CHARM_ID,
	["voids call"] = VOIDS_CALL_CHARM_ID,
	["void's call"] = VOIDS_CALL_CHARM_ID,
	["voidscall"] = VOIDS_CALL_CHARM_ID,
	["4"] = DODGE_CHARM_ID,
	["dodge"] = DODGE_CHARM_ID,
}

function onSay(player, words, param)
	local key = param:lower():gsub("^%s+", ""):gsub("%s+$", "")
	if key == "" then
		player:sendCancelMessage("Use !activatecharm savage blow, vampiric embrace, void's call, or dodge")
		return false
	end

	local charmId = aliases[key]
	if not charmId then
		player:sendCancelMessage("Unknown charm.")
		return false
	end

	local state = player:getCharmState(charmId)
	if state == 0 then
		player:sendCancelMessage("You must unlock this charm first.")
		return false
	end

	if state == 2 then
		player:sendCancelMessage("This charm is already active.")
		return false
	end

	if player:activateCharm(charmId) then
		if player:save() then
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Charm activated for testing and player state saved.")
		else
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Charm activated for testing, but player save failed.")
		end
	else
		player:sendCancelMessage("Could not activate charm.")
	end
	return false
end
