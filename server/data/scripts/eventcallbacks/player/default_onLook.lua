local ec = EventCallback
local CREATURE_STACK_ATTRIBUTE = "creaturestack"

ec.onLook = function(self, thing, position, distance, description)
	local description = "You see " .. thing:getDescription(distance)
	local accountType = self:getAccountType()
	if thing:isItem() and thing:getId() == ITEM_GOLD_COIN and
			(accountType == ACCOUNT_TYPE_GAMEMASTER or accountType == ACCOUNT_TYPE_GOD) then
		local goldOrigin = thing:getCustomAttribute(CREATURE_STACK_ATTRIBUTE) == true and "creature" or "common"
		description = string.format("%s\nGold origin: %s", description, goldOrigin)
	end

	if thing:isItem() and accountType >= ACCOUNT_TYPE_GOD then
		local instanceId = thing:getFloorPersistenceInstanceId() or "none"
		description = string.format("%s\nFloor instance ID: %s", description, instanceId)
		local lastActorGuid = thing:getLastActorGuid() or 0
		description = string.format("%s\nFloor last actor GUID: %d", description, lastActorGuid)
	end

	if self:getGroup():getAccess() then
		if thing:isItem() then
			description = string.format("%s\nItem ID: %d", description, thing:getId())

			local actionId = thing:getActionId()
			if actionId ~= 0 then
				description = string.format("%s, Action ID: %d", description, actionId)
			end

			local uniqueId = thing:getAttribute(ITEM_ATTRIBUTE_UNIQUEID)
			if uniqueId > 0 and uniqueId < 65536 then
				description = string.format("%s, Unique ID: %d", description, uniqueId)
			end

			local itemType = thing:getType()

			local transformEquipId = itemType:getTransformEquipId()
			local transformDeEquipId = itemType:getTransformDeEquipId()
			if transformEquipId ~= 0 then
				description = string.format("%s\nTransforms to: %d (onEquip)", description, transformEquipId)
			elseif transformDeEquipId ~= 0 then
				description = string.format("%s\nTransforms to: %d (onDeEquip)", description, transformDeEquipId)
			end

			local decayId = itemType:getDecayId()
			if decayId ~= -1 then
				description = string.format("%s\nDecays to: %d", description, decayId)
			end
		elseif thing:isCreature() then
			local str = "%s\nHealth: %d / %d"
			if thing:isPlayer() and thing:getMaxMana() > 0 then
				str = string.format("%s, Mana: %d / %d", str, thing:getMana(), thing:getMaxMana())
			end
			description = string.format(str, description, thing:getHealth(), thing:getMaxHealth()) .. "."
		end

		local position = thing:getPosition()
		description = string.format(
			"%s\nPosition: %d, %d, %d",
			description, position.x, position.y, position.z
		)

		if thing:isCreature() then
			if thing:isPlayer() then
				description = string.format("%s\nIP: %s.", description, Game.convertIpToString(thing:getIp()))
			end
		end
	end
	return description
end

ec:register()
