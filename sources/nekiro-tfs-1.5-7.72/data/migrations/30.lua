function onUpdateDatabase()
	print("> Updating database to version 30 (floor persistence stage 3 shadow snapshots)")
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_snapshots` (
		  `world_id` int unsigned NOT NULL,
		  `generation_id` int unsigned NOT NULL,
		  `tile_x` smallint unsigned NOT NULL,
		  `tile_y` smallint unsigned NOT NULL,
		  `tile_z` tinyint unsigned NOT NULL,
		  `tile_version` bigint unsigned NOT NULL,
		  `format_version` smallint unsigned NOT NULL,
		  `policy_version` smallint unsigned NOT NULL,
		  `item_count` int unsigned NOT NULL DEFAULT '0',
		  `top_item_count` int unsigned NOT NULL DEFAULT '0',
		  `serialized_bytes` int unsigned NOT NULL DEFAULT '0',
		  `persist_always_count` int unsigned NOT NULL DEFAULT '0',
		  `persist_clean_only_count` int unsigned NOT NULL DEFAULT '0',
		  `persist_food_count` int unsigned NOT NULL DEFAULT '0',
		  `death_bundle_count` int unsigned NOT NULL DEFAULT '0',
		  `excluded_item_count` int unsigned NOT NULL DEFAULT '0',
		  `identity_missing_count` int unsigned NOT NULL DEFAULT '0',
		  `identity_invalid_count` int unsigned NOT NULL DEFAULT '0',
		  `player_corpse_count` int unsigned NOT NULL DEFAULT '0',
		  `checksum` char(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `serialized_data` longblob NOT NULL,
		  `dirty_reason_mask` int unsigned NOT NULL DEFAULT '0',
		  `dirty_origin_mask` int unsigned NOT NULL DEFAULT '0',
		  `serialization_duration_us` bigint unsigned NOT NULL DEFAULT '0',
		  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
		  PRIMARY KEY (`world_id`,`generation_id`,`tile_x`,`tile_y`,`tile_z`),
		  KEY `generation_updated` (`world_id`,`generation_id`,`updated_at`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	return true
end
