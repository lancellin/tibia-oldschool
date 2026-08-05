local watcherPositions = {
	Position(31949, 31711, 6),
	Position(31949, 31712, 6),
	Position(31948, 31712, 6)
}

function onAddItem(moveitem, tileitem, position)
	if tileitem:getActionId() ~= 50015 or moveitem:getId() ~= 2333 then
		return true
	end

	local mailbox = Tile(Position(31948, 31711, 6)):getItemById(2334)
	if not mailbox then
		return true
	end

	local updated = false
	for _, watcherPosition in ipairs(watcherPositions) do
		local creature = Tile(watcherPosition):getTopCreature()
		if creature and creature:isPlayer() then
			creature:setStorageValue(244, 2)
			updated = true
		end
	end

	if updated then
		moveitem:remove(1)
		mailbox:getPosition():sendMagicEffect(CONST_ME_SOUND_GREEN)
	end
	return true
end
