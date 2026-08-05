local effectsByActionId = {
	[2551] = {
		Position(32217, 31845, 14),
		Position(32218, 31845, 14),
		Position(32219, 31845, 14),
		Position(32220, 31845, 14),
		Position(32217, 31843, 14),
		Position(32218, 31842, 14),
		Position(32219, 31841, 14)
	},
	[2552] = {
		Position(32217, 31844, 14),
		Position(32218, 31844, 14),
		Position(32219, 31843, 14),
		Position(32220, 31845, 14),
		Position(32219, 31845, 14)
	},
	[2553] = {
		Position(32217, 31842, 14),
		Position(32219, 31843, 14),
		Position(32219, 31845, 14),
		Position(32218, 31844, 14),
		Position(32217, 31844, 14),
		Position(32217, 31845, 14)
	},
	[2554] = {
		Position(32217, 31845, 14),
		Position(32218, 31846, 14),
		Position(32218, 31844, 14),
		Position(32219, 31845, 14),
		Position(32220, 31846, 14)
	},
	[2555] = {
		Position(32219, 31841, 14),
		Position(32219, 31842, 14),
		Position(32219, 31846, 14),
		Position(32217, 31843, 14),
		Position(32217, 31844, 14),
		Position(32217, 31845, 14),
		Position(32218, 31843, 14),
		Position(32218, 31845, 14)
	}
}

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local actionId = item:getActionId()
	local effectPositions = effectsByActionId[actionId]
	if not effectPositions then
		return true
	end

	if item:getId() == 1945 then
		for _, effectPosition in ipairs(effectPositions) do
			effectPosition:sendMagicEffect(CONST_ME_HITBYFIRE)
		end
		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		item:transform(1945)
		return true
	end

	return true
end
