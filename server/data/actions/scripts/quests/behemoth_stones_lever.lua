local stonePositions = {
	Position(33295, 31677, 15),
	Position(33296, 31677, 15),
	Position(33297, 31677, 15),
	Position(33298, 31677, 15),
	Position(33299, 31677, 15),
}

local stoneItemId = 1304

local function getStone(position)
	local tile = Tile(position)
	return tile and tile:getItemById(stoneItemId) or nil
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local removed = false
		for _, position in ipairs(stonePositions) do
			local stone = getStone(position)
			if stone then
				stone:remove()
				position:sendMagicEffect(CONST_ME_POFF)
				removed = true
			end
		end

		if not removed then
			player:sendCancelMessage("Sorry, not possible.")
			return true
		end

		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		for _, position in ipairs(stonePositions) do
			local tile = Tile(position)
			if tile then
				tile:relocateTo(Position(position.x, position.y + 1, position.z))
			end

			if not getStone(position) then
				Game.createItem(stoneItemId, 1, position)
				position:sendMagicEffect(CONST_ME_POFF)
			end
		end

		item:transform(1945)
		return true
	end

	return false
end
