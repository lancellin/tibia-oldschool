local mailboxPosition = Position(32013, 31562, 4)
local postmanStorage = 228

local function hasCrowbar(item, target)
	return (item and item:isItem() and item:getId() == 2416) or (target and target:isItem() and target:getId() == 2416)
end

local function hasQuestTarget(item, target)
	return (item and item:isItem() and item:getActionId() == 50018) or (target and target:isItem() and target:getActionId() == 50018)
end

function onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not hasCrowbar(item, target) or not hasQuestTarget(item, target) then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	if player:getStorageValue(postmanStorage) ~= 1 then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	local mailbox = Tile(mailboxPosition):getItemById(2593)
	if not mailbox then
		player:sendCancelMessage("Sorry, not possible.")
		return true
	end

	mailboxPosition:sendMagicEffect(CONST_ME_MAGIC_GREEN)
	player:setStorageValue(postmanStorage, 2)
	return true
end
