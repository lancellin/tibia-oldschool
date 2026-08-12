CREATE TABLE IF NOT EXISTS `accounts` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(32) NOT NULL,
  `password` char(40) NOT NULL,
  `secret` char(16) DEFAULT NULL,
  `type` int NOT NULL DEFAULT '1',
  `premium_ends_at` int unsigned NOT NULL DEFAULT '0',
  `email` varchar(255) NOT NULL DEFAULT '',
  `creation` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `players` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `group_id` int NOT NULL DEFAULT '1',
  `account_id` int NOT NULL DEFAULT '0',
  `level` int NOT NULL DEFAULT '1',
  `vocation` int NOT NULL DEFAULT '0',
  `health` int NOT NULL DEFAULT '150',
  `healthmax` int NOT NULL DEFAULT '150',
  `experience` bigint unsigned NOT NULL DEFAULT '0',
  `lookbody` int NOT NULL DEFAULT '0',
  `lookfeet` int NOT NULL DEFAULT '0',
  `lookhead` int NOT NULL DEFAULT '0',
  `looklegs` int NOT NULL DEFAULT '0',
  `looktype` int NOT NULL DEFAULT '136',
  `lookaddons` int NOT NULL DEFAULT '0',
  `direction` tinyint unsigned NOT NULL DEFAULT '2',
  `maglevel` int NOT NULL DEFAULT '0',
  `mana` int NOT NULL DEFAULT '0',
  `manamax` int NOT NULL DEFAULT '0',
  `manaspent` bigint unsigned NOT NULL DEFAULT '0',
  `soul` int unsigned NOT NULL DEFAULT '0',
  `town_id` int NOT NULL DEFAULT '1',
  `posx` int NOT NULL DEFAULT '0',
  `posy` int NOT NULL DEFAULT '0',
  `posz` int NOT NULL DEFAULT '0',
  `conditions` blob DEFAULT NULL,
  `cap` int NOT NULL DEFAULT '400',
  `sex` int NOT NULL DEFAULT '0',
  `lastlogin` bigint unsigned NOT NULL DEFAULT '0',
  `lastip` int unsigned NOT NULL DEFAULT '0',
  `save` tinyint NOT NULL DEFAULT '1',
  `skull` tinyint NOT NULL DEFAULT '0',
  `skulltime` bigint NOT NULL DEFAULT '0',
  `lastlogout` bigint unsigned NOT NULL DEFAULT '0',
  `blessings` tinyint NOT NULL DEFAULT '0',
  `onlinetime` bigint NOT NULL DEFAULT '0',
  `deletion` bigint NOT NULL DEFAULT '0',
  `balance` bigint unsigned NOT NULL DEFAULT '0',
  `offlinetraining_time` smallint unsigned NOT NULL DEFAULT '43200',
  `offlinetraining_skill` int NOT NULL DEFAULT '-1',
  `stamina` smallint unsigned NOT NULL DEFAULT '2520',
  `skill_fist` int unsigned NOT NULL DEFAULT 10,
  `skill_fist_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_club` int unsigned NOT NULL DEFAULT 10,
  `skill_club_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_sword` int unsigned NOT NULL DEFAULT 10,
  `skill_sword_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_axe` int unsigned NOT NULL DEFAULT 10,
  `skill_axe_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_dist` int unsigned NOT NULL DEFAULT 10,
  `skill_dist_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_shielding` int unsigned NOT NULL DEFAULT 10,
  `skill_shielding_tries` bigint unsigned NOT NULL DEFAULT 0,
  `skill_fishing` int unsigned NOT NULL DEFAULT 10,
  `skill_fishing_tries` bigint unsigned NOT NULL DEFAULT 0,
  `alchemy_level` int unsigned NOT NULL DEFAULT 10,
  `alchemy_tries` bigint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
  KEY `vocation` (`vocation`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_bans` (
  `account_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expires_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`account_id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_ban_history` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expired_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_storage` (
  `account_id` int NOT NULL,
  `key` int unsigned NOT NULL,
  `value` int NOT NULL,
  PRIMARY KEY (`account_id`, `key`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `ip_bans` (
  `ip` int unsigned NOT NULL,
  `reason` varchar(255) NOT NULL,
  `banned_at` bigint NOT NULL,
  `expires_at` bigint NOT NULL,
  `banned_by` int NOT NULL,
  PRIMARY KEY (`ip`),
  FOREIGN KEY (`banned_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_namelocks` (
  `player_id` int NOT NULL,
  `reason` varchar(255) NOT NULL,
  `namelocked_at` bigint NOT NULL,
  `namelocked_by` int NOT NULL,
  PRIMARY KEY (`player_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`namelocked_by`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `account_viplist` (
  `account_id` int NOT NULL COMMENT 'id of account whose viplist entry it is',
  `player_id` int NOT NULL COMMENT 'id of target player of viplist entry',
  `description` varchar(128) NOT NULL DEFAULT '',
  `icon` tinyint unsigned NOT NULL DEFAULT '0',
  `notify` tinyint NOT NULL DEFAULT '0',
  UNIQUE KEY `account_player_index` (`account_id`,`player_id`),
  FOREIGN KEY (`account_id`) REFERENCES `accounts` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guilds` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `ownerid` int NOT NULL,
  `creationdata` int NOT NULL,
  `motd` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY (`name`),
  UNIQUE KEY (`ownerid`),
  FOREIGN KEY (`ownerid`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_invites` (
  `player_id` int NOT NULL DEFAULT '0',
  `guild_id` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`,`guild_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE,
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_ranks` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guild_id` int NOT NULL COMMENT 'guild',
  `name` varchar(255) NOT NULL COMMENT 'rank name',
  `level` int NOT NULL COMMENT 'rank level - leader, vice, member, maybe something else',
  PRIMARY KEY (`id`),
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_membership` (
  `player_id` int NOT NULL,
  `guild_id` int NOT NULL,
  `rank_id` int NOT NULL,
  `nick` varchar(15) NOT NULL DEFAULT '',
  PRIMARY KEY (`player_id`),
  FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`id`) ON DELETE CASCADE ON UPDATE CASCADE,
  FOREIGN KEY (`rank_id`) REFERENCES `guild_ranks` (`id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guild_wars` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guild1` int NOT NULL DEFAULT '0',
  `guild2` int NOT NULL DEFAULT '0',
  `name1` varchar(255) NOT NULL,
  `name2` varchar(255) NOT NULL,
  `status` tinyint NOT NULL DEFAULT '0',
  `started` bigint NOT NULL DEFAULT '0',
  `ended` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `guild1` (`guild1`),
  KEY `guild2` (`guild2`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `guildwar_kills` (
  `id` int NOT NULL AUTO_INCREMENT,
  `killer` varchar(50) NOT NULL,
  `target` varchar(50) NOT NULL,
  `killerguild` int NOT NULL DEFAULT '0',
  `targetguild` int NOT NULL DEFAULT '0',
  `warid` int NOT NULL DEFAULT '0',
  `time` bigint NOT NULL,
  PRIMARY KEY (`id`),
  FOREIGN KEY (`warid`) REFERENCES `guild_wars` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `houses` (
  `id` int NOT NULL AUTO_INCREMENT,
  `owner` int NOT NULL,
  `paid` int unsigned NOT NULL DEFAULT '0',
  `warnings` int NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL,
  `rent` int NOT NULL DEFAULT '0',
  `town_id` int NOT NULL DEFAULT '0',
  `bid` int NOT NULL DEFAULT '0',
  `bid_end` int NOT NULL DEFAULT '0',
  `last_bid` int NOT NULL DEFAULT '0',
  `highest_bidder` int NOT NULL DEFAULT '0',
  `size` int NOT NULL DEFAULT '0',
  `beds` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `town_id` (`town_id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `house_lists` (
  `house_id` int NOT NULL,
  `listid` int NOT NULL,
  `list` text NOT NULL,
  FOREIGN KEY (`house_id`) REFERENCES `houses` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `market_history` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `player_id` int NOT NULL,
  `sale` tinyint NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `amount` smallint unsigned NOT NULL,
  `price` int unsigned NOT NULL DEFAULT '0',
  `expires_at` bigint unsigned NOT NULL,
  `inserted` bigint unsigned NOT NULL,
  `state` tinyint unsigned NOT NULL,
  PRIMARY KEY (`id`),
  KEY `player_id` (`player_id`, `sale`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `market_offers` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `player_id` int NOT NULL,
  `sale` tinyint NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `amount` smallint unsigned NOT NULL,
  `created` bigint unsigned NOT NULL,
  `anonymous` tinyint NOT NULL DEFAULT '0',
  `price` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `sale` (`sale`,`itemtype`),
  KEY `created` (`created`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `players_online` (
  `player_id` int NOT NULL,
  PRIMARY KEY (`player_id`)
) ENGINE=MEMORY DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_deaths` (
  `player_id` int NOT NULL,
  `time` bigint unsigned NOT NULL DEFAULT '0',
  `level` int NOT NULL DEFAULT '1',
  `killed_by` varchar(255) NOT NULL,
  `is_player` tinyint NOT NULL DEFAULT '1',
  `mostdamage_by` varchar(100) NOT NULL,
  `mostdamage_is_player` tinyint NOT NULL DEFAULT '0',
  `unjustified` tinyint NOT NULL DEFAULT '0',
  `mostdamage_unjustified` tinyint NOT NULL DEFAULT '0',
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE,
  KEY `killed_by` (`killed_by`),
  KEY `mostdamage_by` (`mostdamage_by`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_depotlockeritems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL COMMENT 'any given range eg 0-100 will be reserved for depot lockers and all > 100 will be then normal items inside depots',
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_depotitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL COMMENT 'any given range eg 0-100 will be reserved for depot lockers and all > 100 will be then normal items inside depots',
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_inboxitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL,
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_storeinboxitems` (
  `player_id` int NOT NULL,
  `sid` int NOT NULL,
  `pid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL,
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  UNIQUE KEY `player_id_2` (`player_id`, `sid`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_items` (
  `player_id` int NOT NULL DEFAULT '0',
  `pid` int NOT NULL DEFAULT '0',
  `sid` int NOT NULL DEFAULT '0',
  `itemtype` smallint unsigned NOT NULL DEFAULT '0',
  `count` smallint NOT NULL DEFAULT '0',
  `attributes` blob NOT NULL,
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE,
  KEY `sid` (`sid`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_spells` (
  `player_id` int NOT NULL,
  `name` varchar(255) NOT NULL,
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `player_storage` (
  `player_id` int NOT NULL DEFAULT '0',
  `key` int unsigned NOT NULL DEFAULT '0',
  `value` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`player_id`,`key`),
  FOREIGN KEY (`player_id`) REFERENCES `players`(`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `server_config` (
  `config` varchar(50) NOT NULL,
  `value` varchar(256) NOT NULL DEFAULT '',
  PRIMARY KEY `config` (`config`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

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
	`checkpoint_group_id` bigint unsigned NOT NULL DEFAULT '0',
	`checkpoint_group_version` bigint unsigned NOT NULL DEFAULT '0',
	`save_session_id` bigint unsigned NOT NULL DEFAULT '0',
	`city_cleanup_filtered` tinyint(1) unsigned NOT NULL DEFAULT '0',
  `updated_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`world_id`,`generation_id`,`tile_x`,`tile_y`,`tile_z`),
  KEY `generation_updated` (`world_id`,`generation_id`,`updated_at`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;

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

CREATE TABLE IF NOT EXISTS `floor_persistence_checkpoints` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `world_id` int unsigned NOT NULL,
  `generation_id` int unsigned NOT NULL,
  `save_session_id` bigint unsigned NOT NULL DEFAULT '0',
  `checkpoint_group_id` bigint unsigned NOT NULL DEFAULT '0',
  `checkpoint_group_version` bigint unsigned NOT NULL DEFAULT '0',
  `tile_count` int unsigned NOT NULL DEFAULT '0',
  `player_count` int unsigned NOT NULL DEFAULT '0',
  `house_count` int unsigned NOT NULL DEFAULT '0',
  `state` varchar(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `created_at` timestamp(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`id`),
  KEY `world_generation_created` (`world_id`,`generation_id`,`created_at`),
  KEY `save_session` (`save_session_id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;

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
  `source_snapshot_updated_at` timestamp(6) NULL DEFAULT NULL,
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
  `last_actor_guid` int unsigned NOT NULL DEFAULT '0',
  `description` text NOT NULL,
  `special_description` text NOT NULL,
  `written_text` text NOT NULL,
  `writer` varchar(255) NOT NULL DEFAULT '',
  `written_date` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `quarantine_source_item` (`quarantine_id`,`source_item_index`),
  KEY `recovery_quarantined` (`recovery_source_session_id`,`is_quarantined`,`reason_mask`),
  KEY `item_lookup` (`item_id`,`is_quarantined`),
  KEY `last_actor_lookup` (`last_actor_guid`,`is_quarantined`),
  KEY `instance_lookup` (`instance_id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8 ROW_FORMAT=DYNAMIC;

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


CREATE TABLE IF NOT EXISTS `tile_store` (
  `house_id` int NOT NULL,
  `data` longblob NOT NULL,
  FOREIGN KEY (`house_id`) REFERENCES `houses` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

CREATE TABLE IF NOT EXISTS `towns` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `posx` int NOT NULL DEFAULT '0',
  `posy` int NOT NULL DEFAULT '0',
  `posz` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8;

INSERT INTO `server_config` (`config`, `value`) VALUES ('db_version', '34'), ('motd_hash', ''), ('motd_num', '0'), ('players_record', '0');

DROP TRIGGER IF EXISTS `ondelete_players`;
DROP TRIGGER IF EXISTS `oncreate_guilds`;

DELIMITER //
CREATE TRIGGER `ondelete_players` BEFORE DELETE ON `players`
 FOR EACH ROW BEGIN
    UPDATE `houses` SET `owner` = 0 WHERE `owner` = OLD.`id`;
END
//
CREATE TRIGGER `oncreate_guilds` AFTER INSERT ON `guilds`
 FOR EACH ROW BEGIN
    INSERT INTO `guild_ranks` (`name`, `level`, `guild_id`) VALUES ('the Leader', 3, NEW.`id`);
    INSERT INTO `guild_ranks` (`name`, `level`, `guild_id`) VALUES ('a Vice-Leader', 2, NEW.`id`);
    INSERT INTO `guild_ranks` (`name`, `level`, `guild_id`) VALUES ('a Member', 1, NEW.`id`);
END
//
DELIMITER ;
