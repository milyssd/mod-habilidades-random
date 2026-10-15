#!/usr/bin/env python3
"""Collect only the source/API files needed to adapt this module to a private core."""
import argparse
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

FILES = (
    'src/server/game/Scripting/ScriptDefines/PlayerScript.h',
    'src/server/game/Scripting/ScriptDefines/WorldScript.h',
    'src/server/game/Scripting/ScriptDefines/DatabaseScript.h',
    'src/server/game/Scripting/ScriptDefines/AccountScript.h',
    'src/server/game/Scripting/ScriptDefines/AllCreatureScript.h',
    'src/server/game/Scripting/ScriptObject.h',
    'src/server/game/Globals/ObjectMgr.h',
    'src/server/game/Globals/ObjectMgr.cpp',
    'src/server/game/Entities/Player/Player.h',
    'src/server/game/Entities/Player/Player.cpp',
    'src/server/game/Entities/Player/PlayerStorage.cpp',
    'src/server/game/Entities/Unit/Unit.h',
    'src/server/game/Entities/Creature/CreatureData.h',
    'src/server/game/Maps/Map.h',
    'src/server/database/Database/DatabaseEnvFwd.h',
    'src/server/database/Database/MySQLConnection.h',
    'src/server/database/Database/Transaction.h',
    'src/server/database/Database/Implementation/CharacterDatabase.h',
)


def collect(core, output):
    if not (core / 'src/server/game').is_dir():
        raise ValueError('La ruta debe ser la raíz del core: falta src/server/game')
    head = None
    try:
        result = subprocess.run(['git', '-C', str(core), 'rev-parse', 'HEAD'],
                                check=False, capture_output=True, text=True)
        if result.returncode == 0:
            head = result.stdout.strip()
    except FileNotFoundError:
        pass
    present, missing = [], []
    # Exclusive creation protects an existing archive. No configs, SQL, git remotes or credentials are collected.
    with ZipFile(output, 'x', ZIP_DEFLATED) as archive:
        for name in FILES:
            source = core / name
            if source.is_file():
                archive.write(source, name)
                present.append(name)
            else:
                missing.append(name)
        report = {'head': head, 'files': present, 'missing': missing}
        archive.writestr('compat-report.json', json.dumps(report, indent=2))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path('.'))
    parser.add_argument('--output', type=Path, default=Path('hollowsource-compat.zip'))
    args = parser.parse_args()
    try:
        report = collect(args.core, args.output)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')
    print(f'ZIP creado: {args.output} ({len(report["files"])} archivos de código)')
    if report['missing']:
        print('Archivos ausentes registrados en compat-report.json: ' + str(len(report['missing'])))


if __name__ == '__main__':
    main()
