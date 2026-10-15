#!/usr/bin/env python3
"""Integration checks against a disposable MariaDB/MySQL database.

Requires --client, --socket, --core (an AzerothCore checkout). No production DB is used.
Uses local socket authentication; do not put passwords in arguments.
"""
import argparse
from pathlib import Path
import re
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--client', default='mariadb')
parser.add_argument('--socket', required=True)
parser.add_argument('--core', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
database = 'hr_test_' + uuid.uuid4().hex
command = [args.client, '--no-defaults', '--socket=' + args.socket, '-u', 'root', '-N', '-B']


def sql(statement, selected=True, success=True):
    result = subprocess.run(command + ([database] if selected else []), input=statement,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if success and result.returncode:
        raise RuntimeError(result.stderr)
    if not success:
        assert result.returncode, 'Expected a transaction error'
    return result.stdout.strip()


def load(path):
    sql(path.read_text())


sql('CREATE DATABASE `' + database + '`;', selected=False)
try:
    load(root / 'data/sql/db-characters/base/mod_habilidades_random.sql')
    schema = root / 'data/sql/db-characters/base/event_schema.sql'
    load(schema)
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_spell_rarity') == '474'
    # A snapshot includes independent spec masks, talent and glyph-slot rows.
    sql('''INSERT INTO mod_habilidades_random_event
        (player_id,original_class,original_spec,original_specs,spell_count,talent_count,glyph_count)
        VALUES (1,1,1,2,2,1,1);
        INSERT INTO mod_habilidades_random_snapshot VALUES
        (1,100,3,0),(1,200,1,0),(1,300,2,1),(1,400,3,2);
        INSERT INTO mod_habilidades_random_stats (player_id,total_expected_drafts,rerolls,bans)
        VALUES (1,2,5,5);''')
    load(schema)
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_snapshot') == '4'
    assert sql('SELECT original_class,original_spec,original_specs FROM mod_habilidades_random_event') == '1\t1\t2'
    # Failed ban/selection transactions must not partially consume balances.
    sql('''START TRANSACTION;
        UPDATE mod_habilidades_random_stats SET bans=4 WHERE player_id=1;
        INSERT INTO mod_habilidades_random_bans VALUES (1,100);
        INSERT INTO nonexistent_hr_failure_table VALUES (1);
        COMMIT;''', success=False)
    assert sql('SELECT bans FROM mod_habilidades_random_stats WHERE player_id=1') == '5'
    assert sql('SELECT COUNT(*) FROM mod_habilidades_random_bans') == '0'
    print('PASS: characters schema, 474 IDs, immutable snapshot, both specs, glyphs and transaction rollback')

    # Use actual upstream schemas; world data is loaded only into this disposable database.
    world = args.core / 'data/sql/base/db_world'
    load(world / 'playercreateinfo.sql')
    for name in ['creature_template', 'creature_template_model']:
        source = (world / (name + '.sql')).read_text()
        create = re.search(r'CREATE TABLE `' + name + r'` \(.*?\) ENGINE=.*?;', source, re.S)
        assert create, name
        sql(create.group())
    original = sql('SELECT * FROM playercreateinfo ORDER BY race,class')
    native = sql('SELECT * FROM playercreateinfo WHERE class=11 ORDER BY race')
    migration = root / 'data/sql/db-world/base/event_npc.sql'
    load(migration)
    assert sql('SELECT COUNT(*) FROM playercreateinfo WHERE class=11') == '10'
    assert sql('SELECT * FROM playercreateinfo WHERE class=11 AND race IN (4,6) ORDER BY race') == native
    assert sql('SELECT ScriptName FROM creature_template WHERE entry=900100') == 'npc_habilidades_random_event'
    assert sql('SELECT COUNT(*) FROM creature_template_model WHERE CreatureID=900100') == '1'
    first = sql('SELECT * FROM playercreateinfo ORDER BY race,class')
    load(migration)
    assert sql('SELECT * FROM playercreateinfo ORDER BY race,class') == first
    assert all(line in first.splitlines() for line in original.splitlines())
    # An administrator's custom NPC with the same entry must never be overwritten.
    sql("DELETE FROM creature_template_model WHERE CreatureID=900100; "
        "UPDATE creature_template SET ScriptName='some_other_module' WHERE entry=900100;")
    load(migration)
    assert sql('SELECT ScriptName FROM creature_template WHERE entry=900100') == 'some_other_module'
    assert sql('SELECT COUNT(*) FROM creature_template_model WHERE CreatureID=900100') == '0'
    print('PASS: 10 race/druid pairs, original racial data, repeatable migration, NPC and collision protection')
finally:
    sql('DROP DATABASE `' + database + '`;', selected=False)
