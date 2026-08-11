function onUpdateDatabase()
	print("> Updating database to version 38 (Alchemy profession progress)")
	db.query([[
		ALTER TABLE `players`
		  ADD COLUMN IF NOT EXISTS `alchemy_level` int unsigned NOT NULL DEFAULT '10'
		    AFTER `skill_fishing_tries`,
		  ADD COLUMN IF NOT EXISTS `alchemy_tries` bigint unsigned NOT NULL DEFAULT '0'
		    AFTER `alchemy_level`;
	]])
	return true
end
