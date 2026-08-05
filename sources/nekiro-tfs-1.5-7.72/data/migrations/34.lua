function onUpdateDatabase()
	print("> Updating database to version 35 (floor quarantine item manifest)")
	db.query([[
		CREATE TABLE IF NOT EXISTS `floor_persistence_quarantine_items` (
		  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
		  `quarantine_id` bigint unsigned NOT NULL,
		  `recovery_source_session_id` bigint unsigned NOT NULL,
		  `source_item_index` int unsigned NOT NULL,
		  `parent_source_item_index` bigint NOT NULL DEFAULT '-1',
		  `depth` int unsigned NOT NULL DEFAULT '0',
		  `item_id` smallint unsigned NOT NULL,
		  `item_name` varchar(255) NOT NULL DEFAULT '',
		  `item_count` int unsigned NOT NULL DEFAULT '0',
		  `item_subtype` int unsigned NOT NULL DEFAULT '0',
		  `is_container` tinyint(1) unsigned NOT NULL DEFAULT '0',
		  `container_capacity` int unsigned NOT NULL DEFAULT '0',
		  `instance_id` char(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
		  `policy_state` varchar(40) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
		  `reason_mask` int unsigned NOT NULL DEFAULT '0',
		  `is_quarantined` tinyint(1) unsigned NOT NULL DEFAULT '0',
		  `death_bundle` tinyint(1) unsigned NOT NULL DEFAULT '0',
		  `player_corpse` tinyint(1) unsigned NOT NULL DEFAULT '0',
		  `action_id` int unsigned NOT NULL DEFAULT '0',
		  `unique_id` int unsigned NOT NULL DEFAULT '0',
		  `duration_ms` int unsigned NOT NULL DEFAULT '0',
		  `description` text NOT NULL,
		  `special_description` text NOT NULL,
		  `written_text` text NOT NULL,
		  `writer` varchar(255) NOT NULL DEFAULT '',
		  `written_date` bigint NOT NULL DEFAULT '0',
		  PRIMARY KEY (`id`),
		  UNIQUE KEY `quarantine_source_item` (`quarantine_id`,`source_item_index`),
		  KEY `recovery_quarantined` (`recovery_source_session_id`,`is_quarantined`,`reason_mask`),
		  KEY `item_lookup` (`item_id`,`is_quarantined`),
		  KEY `instance_lookup` (`instance_id`)
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;
	]])
	return true
end
