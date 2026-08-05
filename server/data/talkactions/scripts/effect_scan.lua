function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local split = param:split(",")
	local startEffect = tonumber(split[1])
	local endEffect = tonumber(split[2] or split[1])

	if not startEffect or not endEffect then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Use !fxscan start,end")
		return false
	end

	if startEffect > endEffect then
		startEffect, endEffect = endEffect, startEffect
	end

	local perRow = 8
	local startX = player:getPosition().x - math.floor(perRow / 2)
	local startY = player:getPosition().y + 2
	local z = player:getPosition().z
	local row = {}
	local rowIndex = 0
	local column = 0

	for effect = startEffect, endEffect do
		local position = Position(startX + column, startY + rowIndex, z)
		position:sendMagicEffect(effect)
		row[#row + 1] = effect
		column = column + 1

		if column == perRow or effect == endEffect then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "fx row " .. (rowIndex + 1) .. " left->right: " .. table.concat(row, ", "))
			row = {}
			rowIndex = rowIndex + 1
			column = 0
		end
	end

	return false
end
