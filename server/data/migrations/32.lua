function onUpdateDatabase()
	print("> Updating database to version 32 (floor recovery quarantine)")
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_quarantine` (
		  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
		  `world_id` int unsigned NOT NULL,
		  `generation_id` int unsigned NOT NULL,
		  `recovery_source_session_id` bigint unsigned NOT NULL,
		  `snapshot_save_session_id` bigint unsigned NOT NULL DEFAULT '0',
		  `tile_x` smallint unsigned NOT NULL,
		  `tile_y` smallint unsigned NOT NULL,
		  `tile_z` tinyint unsigned NOT NULL,
		  `source_tile_version` bigint unsigned NOT NULL DEFAULT '0',
		  `source_checkpoint_group_id` bigint unsigned NOT NULL DEFAULT '0',
		  `reason_mask` int unsigned NOT NULL DEFAULT '0',
		  `quarantine_item_count` int unsigned NOT NULL DEFAULT '0',
		  `player_match_item_count` int unsigned NOT NULL DEFAULT '0',
		  `snapshot_item_count` int unsigned NOT NULL DEFAULT '0',
		  `snapshot_top_item_count` int unsigned NOT NULL DEFAULT '0',
		  `format_version` smallint unsigned NOT NULL,
		  `policy_version` smallint unsigned NOT NULL,
		  `serialized_bytes` int unsigned NOT NULL DEFAULT '0',
		  `checksum` char(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `serialized_data` longblob NOT NULL,
		  `state` varchar(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'PENDING',
		  `active` tinyint(1) unsigned NOT NULL DEFAULT '1',
		  `reviewed_by` varchar(64) NOT NULL DEFAULT '',
		  `resolution_note` varchar(1024) NOT NULL DEFAULT '',
		  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
		  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
		  `resolved_at` timestamp(6) NULL DEFAULT NULL,
		  PRIMARY KEY (`id`),
		  UNIQUE KEY `recovery_tile` (`world_id`,`generation_id`,`recovery_source_session_id`,`tile_x`,`tile_y`,`tile_z`),
		  KEY `recovery_state` (`world_id`,`generation_id`,`recovery_source_session_id`,`state`,`active`),
		  KEY `state_updated` (`state`,`updated_at`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	return true
end

