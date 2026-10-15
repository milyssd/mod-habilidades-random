-- Additive migration for both fresh installations and existing Blizzard-only pools.
-- Apply to acore_characters BEFORE enabling the new server binary.
-- Immutable snapshots are retained after completion. Never TRUNCATE these tables.
CREATE TABLE IF NOT EXISTS `mod_habilidades_random_event` (
  `player_id` INT UNSIGNED NOT NULL,
  `original_class` TINYINT UNSIGNED NOT NULL,
  `original_spec` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `original_specs` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `spell_count` INT UNSIGNED NOT NULL,
  `talent_count` INT UNSIGNED NOT NULL,
  `glyph_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `completed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `revision` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`player_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_habilidades_random_snapshot` (
  `player_id` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  `spec_mask` TINYINT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL COMMENT '0=spell, 1=talent, 2..7=glyph slot',
  PRIMARY KEY (`player_id`,`kind`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
