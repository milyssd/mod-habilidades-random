INSERT INTO `mod_habilidades_random_stats` (`player_id`,`successful_drafts`,`total_expected_drafts`,`rerolls`,`bans`,`offered_spell_1`,`offered_spell_2`,`offered_spell_3`,`last_level`)
SELECT p.`player_id`,p.`successful_drafts`,p.`total_expected_drafts`,p.`rerolls`,p.`bans`,p.`offered_spell_1`,p.`offered_spell_2`,p.`offered_spell_3`,COALESCE(c.`level`,1)
FROM `custom_prestige`.`prestige_stats` p LEFT JOIN `characters` c ON c.`guid`=p.`player_id`
ON DUPLICATE KEY UPDATE `successful_drafts`=VALUES(`successful_drafts`),`total_expected_drafts`=VALUES(`total_expected_drafts`),`rerolls`=VALUES(`rerolls`),`bans`=VALUES(`bans`),`offered_spell_1`=VALUES(`offered_spell_1`),`offered_spell_2`=VALUES(`offered_spell_2`),`offered_spell_3`=VALUES(`offered_spell_3`),`last_level`=VALUES(`last_level`);
INSERT IGNORE INTO `mod_habilidades_random_drafted_spells` SELECT `player_guid`,`spell_id` FROM `custom_prestige`.`drafted_spells`;
INSERT IGNORE INTO `mod_habilidades_random_bans` SELECT `player_id`,`spell_id` FROM `custom_prestige`.`draft_bans`;
