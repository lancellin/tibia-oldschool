function onUpdateDatabase()
	print("> Updating database to version 37 (coordinated house boundary checkpoints)")
	db.query([[
		ALTER TABLE `floor_persistence_checkpoints`
		  ADD COLUMN IF NOT EXISTS `house_count` int unsigned NOT NULL DEFAULT '0'
		    AFTER `player_count`;
	]])
	return true
end
