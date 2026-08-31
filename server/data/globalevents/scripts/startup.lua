local WORM_ITEM_ID = 3976
local wormItemTables = {
	"player_depotlockeritems",
	"player_depotitems",
	"player_inboxitems",
	"player_storeinboxitems",
	"player_items"
}
local ACCOUNT_MANAGER = {
	accountName = "1",
	password = "1",
	characterName = "Account Manager",
	sex = 1,
	lookType = 128,
	townId = 11,
	-- Sealed room: players can only reach it through the manager login
	-- teleport, so password conversations with the Account Clerk stay private.
	position = {x = 32096, y = 32219, z = 5}
}

local function purgeWormsFromDatabase()
	for _, tableName in ipairs(wormItemTables) do
		db.query("DELETE FROM `" .. tableName .. "` WHERE `itemtype` = " .. WORM_ITEM_ID)
	end
end

local function purgeWormsFromContainer(container)
	local removed = 0
	for _, item in ipairs(container:getItems(true)) do
		if item:getId() == WORM_ITEM_ID then
			item:remove()
			removed = removed + 1
		end
	end
	return removed
end

local function purgeWormsFromHouse(house)
	local removed = 0
	for _, tile in ipairs(house:getTiles()) do
		for _, item in ipairs(tile:getItems()) do
			if item:getId() == WORM_ITEM_ID then
				item:remove()
				removed = removed + 1
			elseif item:isContainer() then
				removed = removed + purgeWormsFromContainer(item)
			end
		end
	end
	return removed
end

local function purgeWormsFromLoadedHouses()
	local removed = 0
	for _, house in ipairs(Game.getHouses()) do
		removed = removed + purgeWormsFromHouse(house)
	end
	return removed
end

local function ensureAccountManager()
	db.query("INSERT IGNORE INTO `accounts` (`name`, `password`, `creation`) VALUES (" .. db.escapeString(ACCOUNT_MANAGER.accountName) .. ", SHA1(" .. db.escapeString(ACCOUNT_MANAGER.password) .. "), " .. os.time() .. ")")
	db.query("UPDATE `accounts` SET `password` = SHA1(" .. db.escapeString(ACCOUNT_MANAGER.password) .. ") WHERE `name` = " .. db.escapeString(ACCOUNT_MANAGER.accountName))

	local accountId = nil
	local resultId = db.storeQuery("SELECT `id` FROM `accounts` WHERE `name` = " .. db.escapeString(ACCOUNT_MANAGER.accountName))
	if resultId ~= false then
		accountId = result.getNumber(resultId, "id")
		result.free(resultId)
	end

	if not accountId then
		print("> Failed to resolve the seeded Account Manager account.")
		return
	end

	db.query("INSERT IGNORE INTO `players` (`name`, `account_id`, `sex`, `looktype`, `town_id`, `posx`, `posy`, `posz`) VALUES (" ..
		db.escapeString(ACCOUNT_MANAGER.characterName) .. ", " .. accountId .. ", " .. ACCOUNT_MANAGER.sex .. ", " .. ACCOUNT_MANAGER.lookType .. ", " .. ACCOUNT_MANAGER.townId .. ", " ..
		ACCOUNT_MANAGER.position.x .. ", " .. ACCOUNT_MANAGER.position.y .. ", " .. ACCOUNT_MANAGER.position.z .. ")")
	db.query("UPDATE `players` SET `account_id` = " .. accountId .. ", `sex` = " .. ACCOUNT_MANAGER.sex .. ", `looktype` = " .. ACCOUNT_MANAGER.lookType .. ", `town_id` = " .. ACCOUNT_MANAGER.townId ..
		", `posx` = " .. ACCOUNT_MANAGER.position.x .. ", `posy` = " .. ACCOUNT_MANAGER.position.y .. ", `posz` = " .. ACCOUNT_MANAGER.position.z ..
		" WHERE `name` = " .. db.escapeString(ACCOUNT_MANAGER.characterName))
end

local function seedAccountNumberCounter()
	local maxNumber = 0
	local resultId = db.storeQuery("SELECT MAX(CAST(`name` AS UNSIGNED)) AS `max_number` FROM `accounts` WHERE `name` REGEXP '^[0-9]+$'")
	if resultId ~= false then
		local value = result.getNumber(resultId, "max_number")
		if value and value > maxNumber then
			maxNumber = value
		end
		result.free(resultId)
	end

	if getGlobalStorageValue(GlobalStorageKeys.accountNumberCounter) < maxNumber then
		setGlobalStorageValue(GlobalStorageKeys.accountNumberCounter, maxNumber)
	end
end

function onStartup()
	setGlobalStorageValue(11003, -1)

	db.query("TRUNCATE TABLE `players_online`")
	db.asyncQuery("DELETE FROM `guild_wars` WHERE `status` = 0")
	db.asyncQuery("DELETE FROM `players` WHERE `deletion` != 0 AND `deletion` < " .. os.time())
	db.asyncQuery("DELETE FROM `ip_bans` WHERE `expires_at` != 0 AND `expires_at` <= " .. os.time())
	db.asyncQuery("DELETE FROM `market_history` WHERE `inserted` <= " .. (os.time() - configManager.getNumber(configKeys.MARKET_OFFER_DURATION)))
	purgeWormsFromDatabase()

	-- Move expired bans to ban history
	local resultId = db.storeQuery("SELECT * FROM `account_bans` WHERE `expires_at` != 0 AND `expires_at` <= " .. os.time())
	if resultId ~= false then
		repeat
			local accountId = result.getNumber(resultId, "account_id")
			db.asyncQuery("INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES (" .. accountId .. ", " .. db.escapeString(result.getString(resultId, "reason")) .. ", " .. result.getNumber(resultId, "banned_at") .. ", " .. result.getNumber(resultId, "expires_at") .. ", " .. result.getNumber(resultId, "banned_by") .. ")")
			db.asyncQuery("DELETE FROM `account_bans` WHERE `account_id` = " .. accountId)
		until not result.next(resultId)
		result.free(resultId)
	end

	-- Check house auctions
	local resultId = db.storeQuery("SELECT `id`, `highest_bidder`, `last_bid`, (SELECT `balance` FROM `players` WHERE `players`.`id` = `highest_bidder`) AS `balance` FROM `houses` WHERE `owner` = 0 AND `bid_end` != 0 AND `bid_end` < " .. os.time())
	if resultId ~= false then
		repeat
			local house = House(result.getNumber(resultId, "id"))
			if house then
				local highestBidder = result.getNumber(resultId, "highest_bidder")
				local balance = result.getNumber(resultId, "balance")
				local lastBid = result.getNumber(resultId, "last_bid")
				if balance >= lastBid then
					db.query("UPDATE `players` SET `balance` = " .. (balance - lastBid) .. " WHERE `id` = " .. highestBidder)
					house:setOwnerGuid(highestBidder)
				end
				db.asyncQuery("UPDATE `houses` SET `last_bid` = 0, `bid_end` = 0, `highest_bidder` = 0, `bid` = 0 WHERE `id` = " .. house:getId())
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	-- store towns in database
	db.query("TRUNCATE TABLE `towns`")
	for i, town in ipairs(Game.getTowns()) do
		local position = town:getTemplePosition()
		db.query("INSERT INTO `towns` (`id`, `name`, `posx`, `posy`, `posz`) VALUES (" .. town:getId() .. ", " .. db.escapeString(town:getName()) .. ", " .. position.x .. ", " .. position.y .. ", " .. position.z .. ")")
	end

	ensureAccountManager()
	seedAccountNumberCounter()

	local removedHouseWorms = purgeWormsFromLoadedHouses()
	if removedHouseWorms > 0 then
		print(string.format("> Purged %d worm item(s) from loaded houses.", removedHouseWorms))
	end

end
