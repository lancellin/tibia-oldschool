local summonStorage = 63636
local summonConfig = {
	{"Warlock", Position(32215, 31841, 15)},
	{"Warlock", Position(32215, 31833, 15)}
}

function onStepIn(creature, item, position, fromPosition)
	if not creature:isPlayer() then
		return true
	end

	if item:getId() == 426 then
		item:transform(425)
	end

	if creature:getStorageValue(summonStorage) == 1 then
		creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_RED)
		return true
	end

	for _, summon in ipairs(summonConfig) do
		Game.createMonster(summon[1], summon[2], false, true)
	end
	creature:setStorageValue(summonStorage, 1)
	return true
end
