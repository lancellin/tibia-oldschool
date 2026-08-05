local effectPositions = {
	Position(32243, 31891, 14),
	Position(32242, 31891, 14),
	Position(32242, 31892, 14),
	Position(32242, 31893, 14),
	Position(32243, 31893, 14)
}

function onAddItem(moveitem, tileitem, position)
	if moveitem:getId() ~= 2016 then
		return true
	end

	for _, effectPosition in ipairs(effectPositions) do
		effectPosition:sendMagicEffect(CONST_ME_NONE)
	end
	return true
end
