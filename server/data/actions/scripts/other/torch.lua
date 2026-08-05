local TORCH_REMAINING_ATTR = "__torchRemainingDuration"

local torches = {
	[2050] = {transformTo = 2051, shouldDecay = true},
	[2051] = {transformTo = 2050, shouldDecay = false},
	[2052] = {transformTo = 2053, shouldDecay = true},
	[2053] = {transformTo = 2052, shouldDecay = false},
	[2054] = {transformTo = 2055, shouldDecay = true},
	[2055] = {transformTo = 2054, shouldDecay = false},
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local torch = torches[item.itemid]
	if not torch then
		return false
	end

	if not torch.shouldDecay then
		local remainingDuration = item:getDuration()
		if remainingDuration and remainingDuration > 0 then
			item:setCustomAttribute(TORCH_REMAINING_ATTR, remainingDuration)
		else
			item:removeCustomAttribute(TORCH_REMAINING_ATTR)
		end
	end

	item:transform(torch.transformTo)
	if torch.shouldDecay then
		local remainingDuration = item:getCustomAttribute(TORCH_REMAINING_ATTR)
		if remainingDuration and remainingDuration > 0 then
			item:setAttribute(ITEM_ATTRIBUTE_DURATION, remainingDuration)
			item:removeCustomAttribute(TORCH_REMAINING_ATTR)
		end
		item:decay()
	else
		item:removeAttribute(ITEM_ATTRIBUTE_DURATION)
	end
	return true
end
