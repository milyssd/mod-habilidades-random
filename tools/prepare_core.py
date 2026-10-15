#!/usr/bin/env python3
"""Prepare runtime data and session save protection without changing database files."""
import argparse
from pathlib import Path
import re
import subprocess


MODULE_ROOT = Path(__file__).resolve().parents[1]
PATCHES = (
    MODULE_ROOT / 'patches/hollowsource-3927b32-runtime-data.patch',
    MODULE_ROOT / 'patches/azerothcore-runtime-data.patch',
)
SAVE_PATCHES = (MODULE_ROOT / 'patches/player-save-guard.patch',)
GLOBALS = Path('src/server/game/Globals')
PLAYER = Path('src/server/game/Entities/Player')
METHODS = ('RegisterRuntimePlayerInfo', 'RegisterRuntimeCreatureTemplate')
PARAMETERS = {
    'RegisterRuntimePlayerInfo': (
        r'uint32\s+\w+\s*,\s*uint32\s+\w+\s*,\s*PlayerInfo\s+const\s*&\s*\w+\s*,\s*'
        r'std\s*::\s*vector\s*<\s*PlayerLevelInfo\s*>\s+const\s*&\s*\w+'
    ),
    'RegisterRuntimeCreatureTemplate': r'CreatureTemplate\s+const\s*&\s*\w+',
}


class PreparationError(ValueError):
    """A preparation prerequisite failed; existing core edits must be preserved."""


def _code(text):
    # Ignore comments and string literals when detecting an existing API.
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda match: '\n' * match.group().count('\n') + ' ', text)


def _api_status(core):
    header = _code((core / GLOBALS / 'ObjectMgr.h').read_text(encoding='utf-8-sig'))
    source = _code((core / GLOBALS / 'ObjectMgr.cpp').read_text(encoding='utf-8-sig'))
    checks = [bool(re.search(r'^\s*#\s*define\s+ACORE_RUNTIME_MODULE_DATA\s+1\b', header, re.M))]
    for method in METHODS:
        signature = method + r'\s*\(\s*' + PARAMETERS[method] + r'\s*\)'
        checks.append(bool(re.search(r'\bbool\s+' + signature + r'\s*;', header)))
        checks.append(bool(re.search(r'\bbool\s+ObjectMgr\s*::\s*' + signature + r'\s*\{', source)))
    if all(checks):
        return 'ready'
    if any(re.search(r'\b' + marker + r'\b', header + '\n' + source)
           for marker in ('ACORE_RUNTIME_MODULE_DATA',) + METHODS):
        return 'partial'
    return 'absent'


def _save_guard_status(core):
    header = _code((core / PLAYER / 'Player.h').read_text(encoding='utf-8-sig'))
    source = _code((core / PLAYER / 'PlayerStorage.cpp').read_text(encoding='utf-8-sig'))
    checks = [bool(re.search(r'^\s*#\s*define\s+ACORE_MODULE_SAVE_GUARD\s+1\b', header, re.M)),
              bool(re.search(r'\bvoid\s+SetModuleSaveSuppressed\s*\(\s*bool\s+(\w+)\s*\)\s*'
                             r'\{\s*_moduleSaveSuppressed\s*=\s*\1\s*;\s*\}', header)),
              bool(re.search(r'\bbool\s+_moduleSaveSuppressed\s*\{\s*false\s*\}\s*;', header))]
    signatures = (
        r'SaveToDB\s*\(\s*CharacterDatabaseTransaction\s+\w+\s*,\s*bool\s+\w+\s*,\s*bool\s+\w+\s*\)',
        r'SaveInventoryAndGoldToDB\s*\(\s*CharacterDatabaseTransaction\s+\w+\s*\)',
        r'SaveGoldToDB\s*\(\s*CharacterDatabaseTransaction\s+\w+\s*\)',
    )
    for signature in signatures:
        checks.append(bool(re.search(r'\bvoid\s+Player\s*::\s*' + signature +
                                     r'\s*\{\s*if\s*\(\s*_moduleSaveSuppressed\s*\)\s*return\s*;', source)))
    if all(checks):
        return 'ready'
    if any(re.search(r'\b' + marker + r'\b', header + '\n' + source)
           for marker in ('ACORE_MODULE_SAVE_GUARD', 'SetModuleSaveSuppressed', '_moduleSaveSuppressed')):
        return 'partial'
    return 'absent'


def _git_apply(core, patches, check):
    command = ['git', 'apply']
    if check:
        command.append('--check')
    command.extend(['--'] + [str(patch) for patch in patches])
    try:
        return subprocess.run(command, cwd=core, check=False, capture_output=True,
                              text=True, encoding='utf-8', errors='replace')
    except FileNotFoundError as error:
        raise PreparationError('No se encontró Git. Instala Git y vuelve a ejecutar este comando.') from error


def prepare(core, check=False):
    """Check all missing features before applying their patches together."""
    core = Path(core).resolve()
    required = ((GLOBALS, 'ObjectMgr.h'), (GLOBALS, 'ObjectMgr.cpp'),
                (PLAYER, 'Player.h'), (PLAYER, 'PlayerStorage.cpp'))
    if not all((core / directory / name).is_file() for directory, name in required):
        raise PreparationError('La ruta debe ser la raíz del core: faltan ObjectMgr.h/.cpp o Player.h/PlayerStorage.cpp en src/server/game.')
    features = (('datos runtime', _api_status(core), PATCHES),
                ('protección de guardado', _save_guard_status(core), SAVE_PATCHES))
    if all(status == 'ready' for _, status, _ in features):
        return 'El core ya tiene la API completa; no se modificó ningún archivo.'
    partial = [name for name, status, _ in features if status == 'partial']
    if partial:
        raise PreparationError('La API está aplicada parcialmente o tiene una firma distinta (' + ', '.join(partial) +
                               '). Revisa sus archivos del core; no se modificó ningún archivo.')

    selected = []
    for feature, status, patches in features:
        if status == 'ready':
            continue
        failures = []
        for patch in patches:
            if not patch.is_file():
                failures.append(patch.name + ': falta el archivo de parche')
                continue
            result = _git_apply(core, [patch], check=True)
            if result.returncode:
                failures.append(patch.name + ': ' + (result.stderr.strip() or result.stdout.strip() or 'git apply --check falló'))
                continue
            selected.append(patch)
            break
        else:
            raise PreparationError('Ningún parche coincide con este core para ' + feature +
                                   '. No se modificó ningún archivo.\n' + '\n'.join(failures))
    combined = _git_apply(core, selected, check=True)
    if combined.returncode:
        raise PreparationError('Los parches no se pueden aplicar juntos. No se modificó ningún archivo.\n' +
                               (combined.stderr.strip() or combined.stdout.strip()))
    names = ', '.join(patch.name for patch in selected)
    if check:
        return 'Parches compatibles: ' + names + '. Comprobación solamente; aún no se aplicó.'
    applied = _git_apply(core, selected, check=False)
    if applied.returncode:
        raise PreparationError('No se pudieron aplicar los parches: ' + (applied.stderr.strip() or applied.stdout.strip()))
    if _api_status(core) != 'ready' or _save_guard_status(core) != 'ready':
        raise PreparationError('Git aplicó los parches, pero no se encontró la API completa. Revisa los archivos del core antes de compilar.')
    return 'Parches aplicados: ' + names + '. Vuelve a generar el proyecto con CMake y compila.'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path('.'), help='Raíz del core (por defecto, carpeta actual).')
    parser.add_argument('--check', action='store_true', help='Comprobar sin aplicar cambios.')
    args = parser.parse_args()
    try:
        message = prepare(args.core, args.check)
    except (PreparationError, OSError, UnicodeError) as error:
        parser.exit(1, str(error) + '\n')
    print(message)


if __name__ == '__main__':
    main()
