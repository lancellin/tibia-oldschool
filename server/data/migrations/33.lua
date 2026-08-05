function onUpdateDatabase()
	print("> Updating database to version 34 (durable floor recovery confirmation)")
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_recovery_confirmations` (
		  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
		  `world_id` int unsigned NOT NULL,
		  `generation_id` int unsigned NOT NULL,
		  `recovery_source_session_id` bigint unsigned NOT NULL,
		  `apply_session_id` bigint unsigned NOT NULL,
		  `recovery_mode` varchar(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `source_state` varchar(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `snapshot_rows` int unsigned NOT NULL DEFAULT '0',
		  `applied_rows` int unsigned NOT NULL DEFAULT '0',
		  `target_tiles` int unsigned NOT NULL DEFAULT '0',
		  `restored_item_count` int unsigned NOT NULL DEFAULT '0',
		  `restored_top_item_count` int unsigned NOT NULL DEFAULT '0',
		  `quarantine_item_count` int unsigned NOT NULL DEFAULT '0',
		  `suppressed_item_count` int unsigned NOT NULL DEFAULT '0',
		  `suppressed_top_item_count` int unsigned NOT NULL DEFAULT '0',
		  `pending_quarantine_rows` int unsigned NOT NULL DEFAULT '0',
		  `pending_player_match_count` int unsigned NOT NULL DEFAULT '0',
		  `confirmed_by_player_id` int unsigned NOT NULL,
		  `confirmed_by_name` varchar(64) NOT NULL,
		  `confirmed_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
		  PRIMARY KEY (`id`),
		  UNIQUE KEY `recovery_apply` (`world_id`,`generation_id`,`recovery_source_session_id`,`apply_session_id`),
		  KEY `world_generation_confirmed` (`world_id`,`generation_id`,`confirmed_at`),
		  KEY `recovery_source` (`recovery_source_session_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	return true
end
