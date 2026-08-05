function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if param == "" then
		local position = player:getPosition()
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Your current position is: " .. position.x .. ", " .. position.y .. ", " .. position.z .. ".")
		return false
	end

	local x, y, z = param:match("^%s*(%d+)%s*[, ]%s*(%d+)%s*[, ]%s*(%d+)%s*$")
	x, y, z = tonumber(x), tonumber(y), tonumber(z)
	if not x or not y or not z then
		player:sendCancelMessage("Usage: /pos x,y,z or /pos x y z.")
		return false
	end

	player:teleportTo(Position(x, y, z))
	player:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	return false
end
