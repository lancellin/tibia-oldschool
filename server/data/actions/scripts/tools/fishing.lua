local waterIds = {493, 4608, 4609, 4610, 4611, 4612, 4613, 4614, 4615, 4616, 4617, 4618, 4619, 4620, 4621, 4622, 4623, 4624, 4625, 7236, 10499, 15401, 15402}
local lootTrash = {2234, 2238, 2376, 2509, 2667}
local lootCommon = {2152, 2167, 2168, 2669, 7588, 7589}
local lootRare = {2143, 2146, 2149, 7158, 7159}
local lootVeryRare = {7632, 7633, 10220}
local fishItemId = 2667
local fishingExhaust = {}
local fishingExhaustMs = 500
local exhaustedFishingSpots = {}
local exhaustedFishingSpotMs = 10 * 60 * 1000

local function getFishingSpotKey(position)
	return string.format("%d:%d:%d", position.x, position.y, position.z)
end

local function isFishingSpotExhausted(position, now)
	local exhaustedUntil = exhaustedFishingSpots[getFishingSpotKey(position)]
	return exhaustedUntil and exhaustedUntil > now
end

local function exhaustFishingSpot(position, now)
	exhaustedFishingSpots[getFishingSpotKey(position)] = now + exhaustedFishingSpotMs
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local targetId = target.itemid
	if not table.contains(waterIds, target.itemid) then
		return false
	end

	local playerId = player:getGuid()
	local now = os.mtime()
	if fishingExhaust[playerId] and fishingExhaust[playerId] > now then
		player:sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED)
		return true
	end
	fishingExhaust[playerId] = now + fishingExhaustMs

	local trackFishingSpot = targetId ~= 493 and targetId ~= 7236 and targetId ~= 10499 and targetId ~= 15401 and targetId ~= 15402
	if trackFishingSpot and isFishingSpotExhausted(toPosition, now) then
		toPosition:sendMagicEffect(CONST_ME_LOSEENERGY)
		return true
	end

	if targetId == 10499 then
		local owner = target:getAttribute(ITEM_ATTRIBUTE_CORPSEOWNER)
		if owner ~= 0 and owner ~= player:getId() then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You are not the owner.")
			return true
		end

		toPosition:sendMagicEffect(CONST_ME_WATERSPLASH)
		target:remove()

		local rareChance = math.random(1, 100)
		if rareChance == 1 then
			player:addItem(lootVeryRare[math.random(#lootVeryRare)], 1)
		elseif rareChance <= 3 then
			player:addItem(lootRare[math.random(#lootRare)], 1)
		elseif rareChance <= 10 then
			player:addItem(lootCommon[math.random(#lootCommon)], 1)
		else
			player:addItem(lootTrash[math.random(#lootTrash)], 1)
		end
		return true
	end

	if targetId ~= 7236 then
		toPosition:sendMagicEffect(CONST_ME_LOSEENERGY)
	end

	if targetId == 493 or targetId == 15402 then
		return true
	end

	player:addSkillTries(SKILL_FISHING, 1)
	if math.random(1, 100) <= math.min(math.max(10 + (player:getEffectiveSkillLevel(SKILL_FISHING) - 10) * 0.597, 10), 50) then
		if targetId == 15401 then
			target:transform(targetId + 1)
			target:decay()

			if math.random(1, 100) >= 97 then
				player:addItem(15405, 1)
				return true
			end
		elseif targetId == 7236 then
			target:transform(targetId + 1)
			target:decay()

			local rareChance = math.random(1, 100)
			if rareChance == 1 then
				player:addItem(7158, 1)
				return true
			elseif rareChance <= 4 then
				player:addItem(2669, 1)
				return true
			elseif rareChance <= 10 then
				player:addItem(7159, 1)
				return true
			end
		end
		player:addItem(fishItemId, 1)
		if trackFishingSpot then
			exhaustFishingSpot(toPosition, now)
		end
	end
	return true
end
