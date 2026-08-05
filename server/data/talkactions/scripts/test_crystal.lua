local CRYSTAL_COIN_ID = 2160
local CRYSTAL_COIN_AMOUNT = 100

function onSay(player, words, param)
	local item = player:addItem(CRYSTAL_COIN_ID, CRYSTAL_COIN_AMOUNT)
	if not item then
		player:sendCancelMessage("You need enough capacity and free slots to receive the crystal coins.")
		return false
	end

	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You received 100 crystal coins.")
	return false
end
