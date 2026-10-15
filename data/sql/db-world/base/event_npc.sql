-- Required for temporary druids of ALL existing WotLK races.
-- Does not change characters.race, models, gender, factions or existing spawn points.
-- AzerothCore computes level stats from class data + racial modifiers for each playercreateinfo pair.
-- RESTART worldserver after applying this file; reloading the NPC alone is insufficient.
INSERT IGNORE INTO `playercreateinfo`
 (`race`,`class`,`map`,`zone`,`position_x`,`position_y`,`position_z`,`orientation`)
SELECT p.`race`,11,p.`map`,p.`zone`,p.`position_x`,p.`position_y`,p.`position_z`,p.`orientation`
FROM `playercreateinfo` p
JOIN (SELECT `race`,MIN(`class`) AS source_class FROM `playercreateinfo` WHERE `class`<>6 GROUP BY `race`) r
 ON r.`race`=p.`race` AND r.source_class=p.`class`;

-- Reserved example NPC entry: check that 900100 is unused on your server before importing.
-- No existing creature entry or spawn is overwritten. If you choose another entry, change both INSERTs.
INSERT INTO `creature_template`
 (`entry`,`name`,`subname`,`minlevel`,`maxlevel`,`faction`,`npcflag`,`unit_class`,`type`,`flags_extra`,`ScriptName`)
SELECT 900100,'Guardián del desafío','Habilidades aleatorias: nivel 80',80,80,35,1,1,7,2,'npc_habilidades_random_event'
WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry`=900100);

INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`)
SELECT 900100,0,3167,1,1
WHERE EXISTS (SELECT 1 FROM `creature_template` WHERE `entry`=900100 AND `ScriptName`='npc_habilidades_random_event')
 AND NOT EXISTS (SELECT 1 FROM `creature_template_model` WHERE `CreatureID`=900100);
-- Spawn deliberately left to the administrator: .npc add 900100
