function onUpdateDatabase()
	print("> Updating database to version 31 (coordinated floor/player checkpoints)")
	db.query([[
		ALTER TABLE `floor_persistence_snapshots`
		  ADD COLUMN IF NOT EXISTS `checkpoint_group_id` bigint unsigned NOT NULL DEFAULT '0' AFTER `serialization_duration_us`,
		  ADD COLUMN IF NOT EXISTS `checkpoint_group_version` bigint unsigned NOT NULL DEFAULT '0' AFTER `checkpoint_group_id`,
		  ADD COLUMN IF NOT EXISTS `save_session_id` bigint unsigned NOT NULL DEFAULT '0' AFTER `checkpoint_group_version`,
		  ADD COLUMN IF NOT EXISTS `city_cleanup_filtered` tinyint(1) unsigned NOT NULL DEFAULT '0' AFTER `save_session_id`;
	]])
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_save_sessions` (
		  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
		  `world_id` int unsigned NOT NULL,
		  `generation_id` int unsigned NOT NULL,
		  `state` varchar(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `player_count` int unsigned NOT NULL DEFAULT '0',
		  `tile_count` int unsigned NOT NULL DEFAULT '0',
		  `error` varchar(512) NOT NULL DEFAULT '',
		  `started_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
		  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
		  `committed_at` timestamp(6) NULL DEFAULT NULL,
		  PRIMARY KEY (`id`),
		  KEY `world_generation_started` (`world_id`,`generation_id`,`started_at`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_checkpoints` (
		  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
		  `world_id` int unsigned NOT NULL,
		  `generation_id` int unsigned NOT NULL,
		  `save_session_id` bigint unsigned NOT NULL DEFAULT '0',
		  `checkpoint_group_id` bigint unsigned NOT NULL DEFAULT '0',
		  `checkpoint_group_version` bigint unsigned NOT NULL DEFAULT '0',
		  `tile_count` int unsigned NOT NULL DEFAULT '0',
		  `player_count` int unsigned NOT NULL DEFAULT '0',
		  `state` varchar(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
		  PRIMARY KEY (`id`),
		  KEY `world_generation_created` (`world_id`,`generation_id`,`created_at`),
		  KEY `save_session` (`save_session_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	return true
end
