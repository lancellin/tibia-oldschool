function Container.isContainer(self)
	return true
end

local LOOT_DIFFICULTY_ATTRIBUTE = "loot_difficulty"

local function getLootDifficulty(chance)
	if chance >= 20000 then
		return 0 -- Common
	elseif chance >= 7100 then
		return 1 -- Uncommon
	elseif chance >= 2000 then
		return 2 -- Semi-Rare
	elseif chance >= 500 then
		return 3 -- Rare
	elseif chance >= 100 then
		return 4 -- Very Rare
	end
	return 5 -- Extremely Rare
end

function Container.createLootItem(self, item)
	if self:getEmptySlots() == 0 then
		return true
	end

	local itemCount = 0
	local randvalue = getLootRandom()
	local itemType = ItemType(item.itemId)
	
	if randvalue < item.chance then
		if itemType:isStackable() then
			itemCount = randvalue % item.maxCount + 1
		else
			itemCount = 1
		end
	end

	while itemCount > 0 do
		local count = math.min(100, itemCount)
		
		local subType = count
		if itemType:isFluidContainer() then
			subType = math.max(0, item.subType)
		end
		
		local tmpItem = Game.createItem(item.itemId, subType)
		if not tmpItem then
			return false
		end

		tmpItem:setCustomAttribute(LOOT_DIFFICULTY_ATTRIBUTE, getLootDifficulty(item.chance))

		if tmpItem:isContainer() then
			for i = 1, #item.childLoot do
				if not tmpItem:createLootItem(item.childLoot[i]) then
					tmpItem:remove()
					return false
				end
			end

			if #item.childLoot > 0 and tmpItem:getSize() == 0 then
				tmpItem:remove()
				return true
			end
		end

		if item.subType ~= -1 then
			tmpItem:setAttribute(ITEM_ATTRIBUTE_CHARGES, item.subType)
		end

		if item.actionId ~= -1 then
			tmpItem:setActionId(item.actionId)
		end

		if item.text and item.text ~= "" then
			tmpItem:setText(item.text)
		end

		local ret = self:addItemEx(tmpItem)
		if ret ~= RETURNVALUE_NOERROR then
			tmpItem:remove()
		end

		itemCount = itemCount - count
	end
	return true
end
