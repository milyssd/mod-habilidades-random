#!/usr/bin/env python3
"""Prepare the core's in-memory registration API without changing database files."""
import argparse
from pathlib import Path
import re
import subprocess


MODULE_ROOT = Path(__file__).resolve().parents[1]
PATCHES = (
    MODULE_ROOT / 'patches/hollowsource-3927b32-runtime-data.patch',
    MODULE_ROOT / 'patches/azerothcore-runtime-data.patch',
)
GLOBALS = Path('src/server/game/Globals')
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


def _git_apply(core, patch, check):
    command = ['git', 'apply']
    if check:
        command.append('--check')
    command.extend(['--', str(patch)])
    try:
        return subprocess.run(command, cwd=core, check=False, capture_output=True,
                              text=True, encoding='utf-8', errors='replace')
    except FileNotFoundError as error:
        raise PreparationError('No se encontró Git. Instala Git y vuelve a ejecutar este comando.') from error


def prepare(core, check=False):
    """Return a status message; apply at most one compatible patch, unless check is set."""
    core = Path(core).resolve()
    if not all((core / GLOBALS / name).is_file() for name in ('ObjectMgr.h', 'ObjectMgr.cpp')):
        raise PreparationError('La ruta debe ser la raíz del core: faltan src/server/game/Globals/ObjectMgr.h y/o ObjectMgr.cpp.')
    status = _api_status(core)
    if status == 'ready':
        return 'El core ya tiene la API completa; no se modificó ningún archivo.'
    if status == 'partial':
        raise PreparationError('La API está aplicada parcialmente o tiene una firma distinta. Revisa ObjectMgr.h y ObjectMgr.cpp; no se modificó ningún archivo.')

    failures = []
    for patch in PATCHES:
        if not patch.is_file():
            failures.append(patch.name + ': falta el archivo de parche')
            continue
        result = _git_apply(core, patch, check=True)
        if result.returncode:
            failures.append(patch.name + ': ' + (result.stderr.strip() or result.stdout.strip() or 'git apply --check falló'))
            continue
        if check:
            return 'Parche compatible: ' + patch.name + '. Comprobación solamente; aún no se aplicó.'
        applied = _git_apply(core, patch, check=False)
        if applied.returncode:
            raise PreparationError('No se pudo aplicar ' + patch.name + ': ' + (applied.stderr.strip() or applied.stdout.strip()))
        if _api_status(core) != 'ready':
            raise PreparationError('Git aplicó el parche, pero no se encontró la API completa. Revisa ObjectMgr.h y ObjectMgr.cpp antes de compilar.')
        return 'Parche aplicado: ' + patch.name + '. Vuelve a generar el proyecto con CMake y compila.'
    raise PreparationError('Ningún parche coincide con este core. No se modificó ningún archivo.\n' + '\n'.join(failures))


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
