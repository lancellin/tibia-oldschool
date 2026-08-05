function onUpdateDatabase()
	print("> Updating database to version 36 (floor stack actor attribution)")
	db.query([[
		ALTER TABLE `floor_persistence_quarantine`
		  ADD COLUMN IF NOT EXISTS `source_snapshot_updated_at` timestamp(6) NULL DEFAULT NULL
		    AFTER `serialized_bytes`;
	]])
	db.query([[
		ALTER TABLE `floor_persistence_quarantine_items`
		  ADD COLUMN IF NOT EXISTS `last_actor_guid` int unsigned NOT NULL DEFAULT '0'
		    AFTER `duration_ms`,
		  ADD KEY IF NOT EXISTS `last_actor_lookup` (`last_actor_guid`,`is_quarantined`);
	]])
	return true
end
