local boxId = 1739
local ladderPosition = Position(32478, 31904, 5)

local function ensureLadder()
	if not Tile(ladderPosition):getItemById(1386) then
		Game.createItem(1386, 1, ladderPosition)
	end
end

local function removeLadder()
	local tile = Tile(ladderPosition)
	local ladder = tile and tile:getItemById(1386)
	if ladder then
		ladder:remove()
	end
end

function onAddItem(moveitem, tileitem, position)
	if moveitem:getId() == boxId then
		ensureLadder()
	end
	return true
end

function onRemoveItem(moveitem, tileitem, position)
	if moveitem:getId() == boxId then
		removeLadder()
	end
	return true
end
