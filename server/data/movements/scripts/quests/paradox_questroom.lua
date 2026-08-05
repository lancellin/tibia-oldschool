local storageByActionId = {
	[50009] = 20061,
	[50010] = 20062,
	[50011] = 20063,
	[50012] = 20064
}

local effectByActionId = {
	[50009] = Position(32477, 31900, 1),
	[50010] = Position(32478, 31900, 1),
	[50011] = Position(32479, 31900, 1),
	[50012] = Position(32480, 31900, 1)
}

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	local storage = storageByActionId[item:getActionId()]
	if not storage then
		return true
	end

	creature:setStorageValue(storage, 1)

	local effectPosition = effectByActionId[item:getActionId()]
	if effectPosition then
		effectPosition:sendMagicEffect(CONST_ME_MAGIC_RED)
	end
	return true
end
