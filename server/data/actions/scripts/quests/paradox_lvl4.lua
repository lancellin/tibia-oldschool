local foodRequirements = {
	[Position(32476, 31900, 4)] = 2682,
	[Position(32477, 31900, 4)] = 2676,
	[Position(32478, 31900, 4)] = 2679,
	[Position(32479, 31900, 4)] = 2674,
	[Position(32480, 31900, 4)] = 2681,
	[Position(32481, 31900, 4)] = 2678
}

local ladderPosition = Position(32478, 31904, 4)

local function getRequiredFoods()
	local foods = {}
	for position, itemId in pairs(foodRequirements) do
		local tile = Tile(position)
		local topItem = tile and tile:getTopDownItem()
		if not topItem or topItem:getId() ~= itemId then
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

		for _, food in ipairs(foods) do
			food.item:remove()
			food.position:sendMagicEffect(CONST_ME_MAGIC_BLUE)
		end

		item:transform(1946)
		return true
	end

	if item:getId() == 1946 then
		local tile = Tile(ladderPosition)
		local ladder = tile and tile:getItemById(1386)
		if ladder then
			ladder:remove()
		end
		item:transform(1945)
	end

	return true
end
