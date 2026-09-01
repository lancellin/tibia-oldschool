local WORM_ITEM_ID = 3976
local wormItemTables = {
	"player_depotlockeritems",
	"player_depotitems",
	"player_inboxitems",
	"player_storeinboxitems",
	"player_items"
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

	seedAccountNumberCounter()

	local removedHouseWorms = purgeWormsFromLoadedHouses()
	if removedHouseWorms > 0 then
		print(string.format("> Purged %d worm item(s) from loaded houses.", removedHouseWorms))
	end

end
