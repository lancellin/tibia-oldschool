local TEST_RUNES = {
	{itemId = 2273, name = "ultimate healing rune"},
	{itemId = 2304, name = "great fireball rune"},
	{itemId = 2268, name = "sudden death rune"}
}

local RUNE_CHARGES = 100

function onSay(player, words, param)
	for _, rune in ipairs(TEST_RUNES) do
		local item = player:addItem(rune.itemId, RUNE_CHARGES)
		if not item then
			player:sendCancelMessage("You need enough capacity and free slots to receive the test runes.")
			return false
		end
	end

	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "You received one UH, one GFB and one SD rune with 100 charges each.")
	return false
end
