local annihilatorReward = {1990, 2400, 2431, 2494}
local CONTAINER_QUEST_ACTION_ID = 2000
local MAPPED_QUEST_ACTION_ID = 2001
local MAX_MAP_UNIQUE_ID = 0xFFFF

local mappedQuestRewards = {
	[10007] = {itemId = 2485, count = 1, name = "Doublet Quest"},
	[10009] = {itemId = 2103, count = 1, name = "Honey Flower Quest"}
}

local function completeQuest(player, storageId)
	player:setStorageValue(storageId, 1)
	if not player:save() then
		print("[Warning - quests.lua] Failed to save player after quest storage " .. storageId .. ".")
	end
end

local function formatPosition(position)
	if not position then
		return "unknown"
	end
	return position.x .. "," .. position.y .. "," .. position.z
end

local function getQuestRewardText(reward)
	local article = reward:getArticle()
	if article ~= "" then
		return article .. " " .. reward:getName()
	end
	return reward:getName()
end

local function identifyQuestReward(reward, storageId)
	if reward and reward:ensureFloorPersistenceSubtreeIdentified() then
		return true
	end

	print("[Warning - quests.lua] Failed to identify delivered quest reward for storage " .. storageId .. ".")
	return false
end

local function createContainerRewards(container)
	local rewards = {}
	local totalWeight = 0

	for index = 0, container:getSize() - 1 do
		local reward = container:getItem(index)
		if reward then
			local clone = reward:clone()
			if not clone then
				return nil, 0
			end

			totalWeight = totalWeight + clone:getWeight()
			rewards[#rewards + 1] = clone
		end
	end

	return rewards, totalWeight
end

local function giveContainerRewards(player, rewards, storageId)
	local addedRewards = {}

	for _, reward in ipairs(rewards) do
		local result = player:addItemEx(reward)
		if result ~= RETURNVALUE_NOERROR then
			for _, addedReward in ipairs(addedRewards) do
				addedReward:remove()
			end

			player:sendCancelMessage(result)
			return false
		end

		addedRewards[#addedRewards + 1] = reward
	end

	for _, addedReward in ipairs(addedRewards) do
		if not identifyQuestReward(addedReward, storageId) then
			for _, rollbackReward in ipairs(addedRewards) do
				rollbackReward:remove()
			end

			player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
			return false
		end
	end

	return true
end

local function handleLegacyQuestChest(player, uniqueId)
	local itemType = ItemType(uniqueId)
	if itemType:getId() == 0 then
		return false
	end

	local itemWeight = itemType:getWeight()
	local playerCap = player:getFreeCapacity()
	if table.contains(annihilatorReward, uniqueId) then
		if player:getStorageValue(30015) == -1 then
			if playerCap >= itemWeight then
				local rewardItem
				if uniqueId == 1990 then
					rewardItem = player:addItem(1990, 1, false)
					if rewardItem and not rewardItem:addItem(2326, 1) then
						rewardItem:remove()
						rewardItem = nil
					end
				else
					rewardItem = player:addItem(uniqueId, 1, false)
				end

				if not rewardItem then
					player:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
					return true
				end
				if not identifyQuestReward(rewardItem, 30015) then
					rewardItem:remove()
					player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
					return true
				end

				player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. '.')
				completeQuest(player, 30015)
			else
				player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. ' weighing ' .. itemWeight .. ' oz it\'s too heavy.')
			end
		else
			player:sendTextMessage(MESSAGE_INFO_DESCR, "It is empty.")
		end
	elseif player:getStorageValue(uniqueId) == -1 then
		if playerCap >= itemWeight then
			local rewardItem = player:addItem(uniqueId, 1, false)
			if not rewardItem then
				player:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
				return true
			end
			if not identifyQuestReward(rewardItem, uniqueId) then
				rewardItem:remove()
				player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
				return true
			end

			player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. '.')
			completeQuest(player, uniqueId)
		else
			player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. ' weighing ' .. itemWeight .. ' oz it\'s too heavy.')
		end
	else
		player:sendTextMessage(MESSAGE_INFO_DESCR, "It is empty.")
	end
	return true
end

local function handleMappedContainerQuestChest(player, item, uniqueId)
	if item:getActionId() ~= CONTAINER_QUEST_ACTION_ID or uniqueId > MAX_MAP_UNIQUE_ID then
		return false
	end

	local container = Container(uniqueId)
	if not container or container:getSize() == 0 then
		return false
	end

	if player:getStorageValue(uniqueId) ~= -1 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "It is empty.")
		return true
	end

	local rewards, totalWeight = createContainerRewards(container)
	if not rewards or #rewards == 0 then
		return false
	end

	if player:getFreeCapacity() < totalWeight then
		local rewardText = #rewards == 1 and getQuestRewardText(rewards[1]) or "a reward"
		player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found ' .. rewardText .. ' weighing ' .. totalWeight .. ' oz it\'s too heavy.')
		return true
	end

	if not giveContainerRewards(player, rewards, uniqueId) then
		return true
	end

	local rewardText = #rewards == 1 and getQuestRewardText(rewards[1]) or "a reward"
	player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found ' .. rewardText .. '.')
	completeQuest(player, uniqueId)
	return true
end

local function handleMappedNonContainerQuest(player, item, uniqueId, position)
	if item:getActionId() ~= MAPPED_QUEST_ACTION_ID or uniqueId == 0 or uniqueId > MAX_MAP_UNIQUE_ID then
		return false
	end

	if player:getStorageValue(uniqueId) ~= -1 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "It is empty.")
		return true
	end

	local reward = mappedQuestRewards[uniqueId]
	if not reward then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This quest reward is not configured.")
		print("[Warning - quests.lua] Missing mapped quest reward for uid " .. uniqueId .. ", actionid " .. item:getActionId() .. ", itemid " .. item:getId() .. ", position " .. formatPosition(position) .. ".")
		return true
	end

	local itemType = ItemType(reward.itemId)
	if itemType:getId() == 0 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This quest reward is not configured.")
		print("[Warning - quests.lua] Invalid mapped quest reward item " .. reward.itemId .. " for uid " .. uniqueId .. ".")
		return true
	end

	local count = reward.count or 1
	local itemWeight = itemType:getWeight(count)
	if player:getFreeCapacity() < itemWeight then
		player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. ' weighing ' .. itemWeight .. ' oz it\'s too heavy.')
		return true
	end

	local rewardItem = player:addItem(reward.itemId, count, false)
	if not rewardItem then
		player:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
		return true
	end
	if not identifyQuestReward(rewardItem, uniqueId) then
		rewardItem:remove()
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return true
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. '.')
	completeQuest(player, uniqueId)
	return true
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local uniqueId = item:getAttribute(ITEM_ATTRIBUTE_UNIQUEID) or 0
	if uniqueId == 0 then
		return false
	end

	if item:getActionId() == MAPPED_QUEST_ACTION_ID then
		return handleMappedNonContainerQuest(player, item, uniqueId, fromPosition)
	end

	if handleLegacyQuestChest(player, uniqueId) then
		return true
	end

	if handleMappedContainerQuestChest(player, item, uniqueId) then
		return true
	end

	return handleMappedNonContainerQuest(player, item, uniqueId, fromPosition)
end
