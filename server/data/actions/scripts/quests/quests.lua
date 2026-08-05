local annihilatorReward = {1990, 2400, 2431, 2494}
local CONTAINER_QUEST_ACTION_ID = 2000
local MAPPED_QUEST_ACTION_ID = 2001
local MAX_MAP_UNIQUE_ID = 0xFFFF
local legacyQuestStorageOverrides = {
	[20002] = 20001, -- Annihilator
	[20003] = 20001, -- Annihilator
	[20004] = 20001, -- Annihilator
	[20069] = 243 -- Postman
}

local mappedQuestRewards = {
	[10007] = {items = {{itemId = 2485}}, name = "Doublet Quest"},
	[10009] = {items = {{itemId = 2103}}, name = "Honey Flower Quest"},
	[10018] = {items = {{itemId = 1955, text = "Hardek *\nBozo *\nSam ****\nOswald\nPartos ***\nQuentin *\nTark ***\nHarsky ***\nStutch *\nFerumbras *\nFrodo **\nNoodles ****"}}, name = "Amber Notebook Quest"},
	[10019] = {items = {{itemId = 2676}}, name = "Banana Quest"},
	[10020] = {items = {{itemId = 2676}}, name = "Banana Quest"},
	[10024] = {items = {{itemId = 2559}}, name = "Small Axe Quest"},
	[20040] = {items = {{itemId = 2091, actionId = 3980}}, name = "Deeper Fibula Key Quest"},
	[20085] = {items = {{itemId = 2463}}, name = "Plate Armor Quest"},
	[20086] = {
		items = {
			{itemId = 2091, actionId = 6010},
			{itemId = 1948},
			{itemId = 2229},
			{itemId = 2230},
			{itemId = 2151, count = 2},
			{itemId = 2165}
		},
		name = "Demon Quest Bag"
	},
	[20087] = {items = {{itemId = 2487}}, name = "Crown Armor Quest"},
	[20088] = {items = {{itemId = 2519}}, name = "Crown Shield Quest"},
	[20089] = {items = {{itemId = 2798}}, name = "Blood Herb Quest"},
	[20091] = {items = {{itemId = 2432}}, name = "Fire Axe Quest"},
	[20092] = {items = {{itemId = 2089, actionId = 3899}}, name = "Mintwallin Key Quest"},
	[20093] = {items = {{itemId = 2088, actionId = 5010}}, name = "Black Knight Key Quest"},
	[20104] = {items = {{itemId = 2089, actionId = 3301}}, name = "Bright Sword Copper Key Quest"},
	[20105] = {items = {{itemId = 2088, actionId = 3302}}, name = "Bright Sword Silver Key Quest"},
	[20106] = {items = {{itemId = 2089, actionId = 3303}}, name = "Bright Sword Copper Key Quest"}
}

local function resolveQuestStorageId(uniqueId)
	return legacyQuestStorageOverrides[uniqueId] or uniqueId
end

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

local function buildConfiguredRewardItem(entry)
	local count = entry.count or entry.type or 1
	local rewardItem = Game.createItem(entry.itemId, count)
	if not rewardItem then
		return nil
	end

	if entry.actionId then
		rewardItem:setActionId(entry.actionId)
	end

	if entry.text then
		rewardItem:setAttribute(ITEM_ATTRIBUTE_TEXT, entry.text)
	end

	return rewardItem
end

local function buildMappedQuestReward(rewardConfig)
	local items = rewardConfig.items
	if not items or #items == 0 then
		return nil
	end

	if #items == 1 then
		return buildConfiguredRewardItem(items[1])
	end

	local containerId = #items > 8 and 1988 or 1987
	local container = Game.createItem(containerId, 1)
	if not container then
		return nil
	end

	for _, entry in ipairs(items) do
		local rewardItem = buildConfiguredRewardItem(entry)
		if not rewardItem then
			container:remove()
			return nil
		end

		local result = container:addItemEx(rewardItem)
		if result ~= RETURNVALUE_NOERROR then
			rewardItem:remove()
			container:remove()
			return nil
		end
	end

	return container
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
	local storageId = resolveQuestStorageId(uniqueId)
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
	elseif player:getStorageValue(storageId) == -1 then
		if playerCap >= itemWeight then
			local rewardItem = player:addItem(uniqueId, 1, false)
			if not rewardItem then
				player:sendCancelMessage(RETURNVALUE_NOTENOUGHROOM)
				return true
			end
			if not identifyQuestReward(rewardItem, storageId) then
				rewardItem:remove()
				player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
				return true
			end

			player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a ' .. itemType:getName() .. '.')
			completeQuest(player, storageId)
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
	local storageId = resolveQuestStorageId(uniqueId)

	if not item:isContainer() or item:getSize() == 0 then
		return false
	end

	if player:getStorageValue(storageId) ~= -1 then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "It is empty.")
		return true
	end

	local rewards, totalWeight = createContainerRewards(item)
	if not rewards or #rewards == 0 then
		return false
	end

	if player:getFreeCapacity() < totalWeight then
		local rewardText = #rewards == 1 and getQuestRewardText(rewards[1]) or "a reward"
		player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found ' .. rewardText .. ' weighing ' .. totalWeight .. ' oz it\'s too heavy.')
		return true
	end

	if not giveContainerRewards(player, rewards, storageId) then
		return true
	end

	local rewardText = #rewards == 1 and getQuestRewardText(rewards[1]) or "a reward"
	player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found ' .. rewardText .. '.')
	completeQuest(player, storageId)
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

	local rewardItem = buildMappedQuestReward(reward)
	if not rewardItem then
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This quest reward is not configured.")
		print("[Warning - quests.lua] Invalid mapped quest reward for uid " .. uniqueId .. ".")
		return true
	end

	local itemWeight = rewardItem:getWeight()
	if player:getFreeCapacity() < itemWeight then
		player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found a reward weighing ' .. itemWeight .. ' oz it\'s too heavy.')
		rewardItem:remove()
		return true
	end

	local result = player:addItemEx(rewardItem)
	if result ~= RETURNVALUE_NOERROR then
		rewardItem:remove()
		player:sendCancelMessage(result)
		return true
	end
	if not identifyQuestReward(rewardItem, uniqueId) then
		rewardItem:remove()
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return true
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, 'You have found ' .. getQuestRewardText(rewardItem) .. '.')
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
