local foodPositions = {
	Position(32478, 31903, 3),
	Position(32479, 31903, 3)
}

local requiredItems = {
	[1] = 2628,
	[2] = 2634
}

local ladderPosition = Position(32479, 31904, 3)

local function removeTimedLadder()
	local tile = Tile(ladderPosition)
	local ladder = tile and tile:getItemById(1386)
	if ladder then
		ladder:remove()
	end
end

local function getRequiredFoods()
	local foods = {}
	for index, position in ipairs(foodPositions) do
		local tile = Tile(position)
		local topItem = tile and tile:getTopDownItem()
		if not topItem or topItem:getId() ~= requiredItems[index] then
			return nil
		end
		foods[#foods + 1] = {item = topItem, position = position}
	end
	return foods
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item:getId() == 1945 then
		local foods = getRequiredFoods()
		if not foods then
			return true
		end

		if not Tile(ladderPosition):getItemById(1386) then
			Game.createItem(1386, 1, ladderPosition)
		end
		addEvent(removeTimedLadder, 45000)

		for _, food in ipairs(foods) do
			food.item:remove()
			food.position:sendMagicEffect(CONST_ME_POFF)
		end

		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		item:transform(1945)
	end

	return true
end
