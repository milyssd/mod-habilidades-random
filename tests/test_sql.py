#!/usr/bin/env python3
"""Test the actual schema embedded in C++, using only disposable private databases."""
import argparse
from pathlib import Path
import re
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--client', default='mariadb')
parser.add_argument('--socket', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
database = 'hr_test_' + uuid.uuid4().hex
command = [args.client, '--no-defaults', '--socket=' + args.socket, '-u', 'root', '-N', '-B']
header = (root / 'src/HRDatabaseSchema.h').read_text()
statements = re.findall(r'R"HRSQL\((.*?)\)HRSQL"', header, re.S)
rows = re.findall(r'^    \{(\d+), (\d+)\}', header, re.M)
assert len(rows) == 474 and len({r[0] for r in rows}) == 474
assert len(statements) == 8


def sql(statement, selected=True, success=True):
    result = subprocess.run(command + ([database] if selected else []), input=statement,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if success and result.returncode:
        raise RuntimeError(result.stderr)
    if not success:
        assert result.returncode, 'Expected a transaction error'
    return result.stdout.strip()


def bootstrap():
    for statement in statements:
        sql(statement)
    if sql('SELECT COUNT(*) FROM mod_habilidades_random_meta WHERE id=1') == '0':
        seed = ','.join('(' + i + ',' + r + ')' for i, r in rows)
        sql('START TRANSACTION; INSERT IGNORE INTO mod_habilidades_random_spell_rarity VALUES ' + seed +
            '; INSERT INTO mod_habilidades_random_meta VALUES (1,1); COMMIT;')


sql('CREATE DATABASE `' + database + '`;', selected=False)
try:
    bootstrap()
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_spell_rarity') == '474'
    sql('''INSERT INTO mod_habilidades_random_event
        (player_id,original_class,original_spec,original_specs,spell_count,talent_count,glyph_count)
        VALUES (1,1,1,2,2,1,1);
        INSERT INTO mod_habilidades_random_snapshot VALUES
        (1,100,3,0),(1,200,1,0),(1,300,2,1),(1,400,3,2);
        INSERT INTO mod_habilidades_random_stats (player_id,total_expected_drafts,rerolls,bans)
        VALUES (1,2,5,5);''')
    # Restart/bootstrap must preserve all snapshots and administrator edits to the pool.
    sql('DELETE FROM mod_habilidades_random_spell_rarity WHERE spell_id=66;')
    bootstrap()
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_spell_rarity') == '473'
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_snapshot') == '4'
    assert sql('SELECT original_class,original_spec,original_specs FROM mod_habilidades_random_event') == '1\t1\t2'
    sql('''START TRANSACTION;
        UPDATE mod_habilidades_random_stats SET bans=4 WHERE player_id=1;
        INSERT INTO mod_habilidades_random_bans VALUES (1,100);
        INSERT INTO nonexistent_hr_failure_table VALUES (1);
        COMMIT;''', success=False)
    assert sql('SELECT bans FROM mod_habilidades_random_stats WHERE player_id=1') == '5'
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_bans') == '0'
    print('PASS: embedded schema, initial seed, non-destructive restart, backups, dual spec, glyphs and rollback')

    # Model a crash AFTER the module commit and BEFORE the core character save.
    sql('''START TRANSACTION;
        UPDATE mod_habilidades_random_stats SET successful_drafts=1 WHERE player_id=1;
        INSERT INTO mod_habilidades_random_drafted_spells VALUES (1,500);
        UPDATE mod_habilidades_random_event SET revision=1,character_pending=1 WHERE player_id=1;
        COMMIT;''')
    bootstrap()  # New process still sees the authoritative spell and retry marker.
    assert sql('SELECT character_pending,revision FROM mod_habilidades_random_event WHERE player_id=1') == '1\t1'
    assert sql('SELECT spell_id FROM mod_habilidades_random_drafted_spells WHERE player_guid=1') == '500'
    # Pending completion must also survive loss of the final acknowledgment.
    sql('UPDATE mod_habilidades_random_event SET completed=1,completion_level=80,character_pending=1,revision=2 WHERE player_id=1')
    bootstrap()
    assert sql('SELECT completed,character_pending,completion_level FROM mod_habilidades_random_event WHERE player_id=1') == '1\t1\t80'
    sql('UPDATE mod_habilidades_random_event SET character_pending=0 WHERE player_id=1 AND revision=2')
    assert sql('SELECT completed,character_pending FROM mod_habilidades_random_event WHERE player_id=1') == '1\t0'
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_snapshot WHERE player_id=1') == '4'
    # NPC positions also belong exclusively to the module database.
    sql('INSERT INTO mod_habilidades_random_npcs (map_id,x,y,z,o) VALUES (0,1,2,3,0)')
    bootstrap()
    assert sql('SELECT map_id,x,y,z,o FROM mod_habilidades_random_npcs') == '0\t1\t2\t3\t0'
    print('PASS: durable pending journal, replay markers, completed snapshots and persistent module-owned NPC positions')

    assert not (root / 'data/sql').exists(), 'No SQL import directories may remain'
    for path in (root / 'src').glob('*.cpp'):
        source = path.read_text()
        assert not re.search(r'(?:CharacterDatabase|WorldDatabase)\.(?:Query|Execute|DirectExecute)\s*\(', source), path
    print('PASS: no raw queries to core pools and no world/characters SQL import files')
finally:
    sql('DROP DATABASE `' + database + '`;', selected=False)
