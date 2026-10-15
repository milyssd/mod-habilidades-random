import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('prepare_core', ROOT / 'tools/prepare_core.py')
preparer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preparer)


def baseline_from_patch(core, patch):
    """Rebuild each old hunk at its original line from the actual distributable patch."""
    files, current, in_hunk = {}, None, False
    for line in patch.read_text(encoding='utf-8').splitlines():
        if line.startswith('--- a/'):
            current = line[len('--- a/'):]
            files[current] = []
            in_hunk = False
        elif line.startswith('@@ '):
            old_start = int(re.match(r'@@ -(\d+)', line).group(1))
            files[current].extend('// fixture padding\n' for _ in range(old_start - 1 - len(files[current])))
            in_hunk = True
        elif line.startswith('diff --git '):
            in_hunk = False
        elif in_hunk and line.startswith((' ', '-')):
            files[current].append(line[1:] + '\n')
        elif in_hunk and not line:
            # Git also accepts an empty context line without its leading space.
            files[current].append('\n')
    for name, lines in files.items():
        destination = core / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(''.join(lines), encoding='utf-8')
    if patch in preparer.PATCHES:
        baseline_from_patch(core, preparer.SAVE_PATCHES[0])


def snapshot(core):
    return {str(path.relative_to(core)): path.read_bytes() for path in core.rglob('*') if path.is_file()}


@unittest.skipUnless(shutil.which('git'), 'Git is required to exercise real patch application')
class PrepareCoreTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='hr preparation ')
        self.addCleanup(self.directory.cleanup)
        # Intentionally no .git directory: archives and paths with spaces must work.
        self.core = Path(self.directory.name) / 'core with spaces'
        self.core.mkdir()

    def test_specific_patch_applies_and_second_run_preserves_every_byte(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        unrelated = self.core / 'notes.txt'
        unrelated.write_text('User work must remain intact.\n', encoding='utf-8')
        message = preparer.prepare(self.core)
        self.assertIn(preparer.PATCHES[0].name, message)
        self.assertEqual(preparer._api_status(self.core), 'ready')
        self.assertEqual(preparer._save_guard_status(self.core), 'ready')
        self.assertEqual(unrelated.read_text(), 'User work must remain intact.\n')
        after = snapshot(self.core)
        self.assertIn('ya tiene', preparer.prepare(self.core))
        self.assertEqual(snapshot(self.core), after)

    def test_check_finds_compatible_patch_without_applying_it(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        before = snapshot(self.core)
        self.assertIn('aún no se aplicó', preparer.prepare(self.core, check=True))
        self.assertEqual(snapshot(self.core), before)

    def test_falls_back_to_generic_patch(self):
        baseline_from_patch(self.core, preparer.PATCHES[1])
        message = preparer.prepare(self.core)
        self.assertIn(preparer.PATCHES[1].name, message)
        self.assertEqual(preparer._api_status(self.core), 'ready')

    def test_partial_api_is_rejected_without_changing_files(self):
        for content in ('#define ACORE_RUNTIME_MODULE_DATA 1\n',
                        'bool RegisterRuntimePlayerInfo(uint32 race);\n'):
            with self.subTest(content=content):
                baseline_from_patch(self.core, preparer.PATCHES[0])
                header = self.core / preparer.GLOBALS / 'ObjectMgr.h'
                header.write_text(header.read_text(encoding='utf-8') + content, encoding='utf-8')
                before = snapshot(self.core)
                with self.assertRaisesRegex(preparer.PreparationError, 'parcialmente'):
                    preparer.prepare(self.core)
                self.assertEqual(snapshot(self.core), before)

    def test_declarations_without_definitions_are_not_ready(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        preparer.prepare(self.core)
        source = self.core / preparer.GLOBALS / 'ObjectMgr.cpp'
        source.write_text('// Definitions removed\n', encoding='utf-8')
        before = snapshot(self.core)
        with self.assertRaisesRegex(preparer.PreparationError, 'parcialmente'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)

    def test_same_names_with_incompatible_signature_are_not_ready(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        preparer.prepare(self.core)
        header = self.core / preparer.GLOBALS / 'ObjectMgr.h'
        header.write_text(header.read_text(encoding='utf-8').replace(
            'RegisterRuntimeCreatureTemplate(CreatureTemplate const& source)',
            'RegisterRuntimeCreatureTemplate(uint32 source)'), encoding='utf-8')
        before = snapshot(self.core)
        with self.assertRaisesRegex(preparer.PreparationError, 'firma distinta'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)

    def test_incompatible_sources_are_rejected_without_changes(self):
        directory = self.core / preparer.GLOBALS
        directory.mkdir(parents=True)
        for name in ('ObjectMgr.cpp', 'ObjectMgr.h'):
            (directory / name).write_text('// Incompatible core.\n', encoding='utf-8')
        baseline_from_patch(self.core, preparer.SAVE_PATCHES[0])
        before = snapshot(self.core)
        with self.assertRaisesRegex(preparer.PreparationError, 'Ningún parche'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)

    def test_wrong_root_is_rejected(self):
        with self.assertRaisesRegex(preparer.PreparationError, 'raíz del core'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), {})

    def test_comment_mentions_do_not_mark_api_as_applied(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        source = self.core / preparer.GLOBALS / 'ObjectMgr.cpp'
        with source.open('a', encoding='utf-8') as file:
            file.write('// RegisterRuntimePlayerInfo RegisterRuntimeCreatureTemplate ACORE_RUNTIME_MODULE_DATA\n')
        self.assertEqual(preparer._api_status(self.core), 'absent')
        preparer.prepare(self.core)
        self.assertEqual(preparer._api_status(self.core), 'ready')

    def test_missing_git_has_clear_error_and_preserves_sources(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        before = snapshot(self.core)
        with mock.patch.object(preparer.subprocess, 'run', side_effect=FileNotFoundError):
            with self.assertRaisesRegex(preparer.PreparationError, 'No se encontró Git'):
                preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)

    def test_cli_defaults_to_current_core_directory(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        result = subprocess.run([sys.executable, str(ROOT / 'tools/prepare_core.py'), '--check'],
                                cwd=self.core, check=False, capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('compatibles', result.stdout)
        self.assertEqual(preparer._api_status(self.core), 'absent')

    def test_existing_runtime_api_gets_missing_save_guard(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        result = preparer._git_apply(self.core, [preparer.PATCHES[0]], check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        before = [(self.core / preparer.GLOBALS / name).read_bytes() for name in ('ObjectMgr.h', 'ObjectMgr.cpp')]
        self.assertEqual(preparer._api_status(self.core), 'ready')
        self.assertEqual(preparer._save_guard_status(self.core), 'absent')
        self.assertIn(preparer.SAVE_PATCHES[0].name, preparer.prepare(self.core))
        self.assertEqual(preparer._save_guard_status(self.core), 'ready')
        self.assertEqual(before, [(self.core / preparer.GLOBALS / name).read_bytes() for name in ('ObjectMgr.h', 'ObjectMgr.cpp')])

    def test_existing_save_guard_gets_missing_runtime_api(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        result = preparer._git_apply(self.core, preparer.SAVE_PATCHES, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        before = [(self.core / preparer.PLAYER / name).read_bytes() for name in ('Player.h', 'PlayerStorage.cpp')]
        self.assertIn(preparer.PATCHES[0].name, preparer.prepare(self.core))
        self.assertEqual(preparer._api_status(self.core), 'ready')
        self.assertEqual(before, [(self.core / preparer.PLAYER / name).read_bytes() for name in ('Player.h', 'PlayerStorage.cpp')])

    def test_incompatible_save_sources_do_not_apply_other_compatible_patch(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        (self.core / preparer.PLAYER / 'PlayerStorage.cpp').write_text('// Incompatible save implementation.\n', encoding='utf-8')
        before = snapshot(self.core)
        with self.assertRaisesRegex(preparer.PreparationError, 'protección de guardado'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)
        self.assertEqual(preparer._api_status(self.core), 'absent')

    def test_partial_save_guard_does_not_apply_runtime_patch(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        header = self.core / preparer.PLAYER / 'Player.h'
        header.write_text(header.read_text() + '\n#define ACORE_MODULE_SAVE_GUARD 1\n', encoding='utf-8')
        before = snapshot(self.core)
        with self.assertRaisesRegex(preparer.PreparationError, 'parcialmente'):
            preparer.prepare(self.core)
        self.assertEqual(snapshot(self.core), before)

    def test_incomplete_full_or_partial_save_guards_are_rejected(self):
        for signature in ('SaveToDB(CharacterDatabaseTransaction trans, bool create, bool logout)',
                          'SaveInventoryAndGoldToDB(CharacterDatabaseTransaction trans)',
                          'SaveGoldToDB(CharacterDatabaseTransaction trans)'):
            with self.subTest(signature=signature):
                baseline_from_patch(self.core, preparer.PATCHES[0])
                preparer.prepare(self.core)
                source = self.core / preparer.PLAYER / 'PlayerStorage.cpp'
                text = source.read_text()
                start = text.index('void Player::' + signature)
                guard = text.index('if (_moduleSaveSuppressed)', start)
                text = text[:guard] + text[guard:].replace('if (_moduleSaveSuppressed)', 'if (false)', 1)
                source.write_text(text, encoding='utf-8')
                before = snapshot(self.core)
                with self.assertRaisesRegex(preparer.PreparationError, 'parcialmente'):
                    preparer.prepare(self.core)
                self.assertEqual(snapshot(self.core), before)

    @unittest.skipUnless(shutil.which('g++'), 'G++ is required to execute the patched C++ save guards')
    def test_patched_guards_prevent_logout_money_item_mismatch_and_reset_on_new_session(self):
        baseline_from_patch(self.core, preparer.PATCHES[0])
        preparer.prepare(self.core)
        header = preparer._code((self.core / preparer.PLAYER / 'Player.h').read_text())
        source = preparer._code((self.core / preparer.PLAYER / 'PlayerStorage.cpp').read_text())
        setter = re.search(r'void SetModuleSaveSuppressed\(bool suppressed\)\s*\{[^}]+\}', header).group()
        field = re.search(r'bool _moduleSaveSuppressed\{false\};', header).group()
        guards = re.findall(r'if\s*\(\s*_moduleSaveSuppressed\s*\)\s*return\s*;', source)
        self.assertEqual(len(guards), 3)
        # Execute the actual setter, field initializer and three guards added by the
        # distributed patch. Remaining core writes use transaction/dirty-flag doubles.
        code = '''#include <cassert>
struct Transaction { int gold = -1; bool item = false; };
struct Database {
    int gold = 100; bool item = false;
    void Commit(Transaction const& tx) { if (tx.gold >= 0) gold = tx.gold; if (tx.item) item = true; }
};
class Player {
public:
    int gold = 100; bool item = false, dirty = false;
    SETTER
    void SaveToDB(Transaction& tx) {
        FULL_GUARD
        SaveInventoryAndGoldToDB(tx);
    }
    void SaveInventoryAndGoldToDB(Transaction& tx) {
        INVENTORY_GUARD
        if (dirty) { tx.item = item; dirty = false; }
        SaveGoldToDB(tx);
    }
    void SaveGoldToDB(Transaction& tx) {
        GOLD_GUARD
        tx.gold = gold;
    }
private:
    FIELD
};
int main() {
    Database db;
    Player session;
    session.gold = 90; session.item = true; session.dirty = true;
    Transaction failed;
    session.SaveToDB(failed); // Dirty inventory flags are consumed when statements are built.
    assert(failed.gold == 90 && failed.item && !session.dirty);
    // Roll back: durable state remains 100 gold without the purchased item.
    session.SetModuleSaveSuppressed(true);
    Transaction logout, partialGold, partialInventory;
    session.SaveToDB(logout);
    session.SaveGoldToDB(partialGold);
    session.SaveInventoryAndGoldToDB(partialInventory);
    db.Commit(logout); db.Commit(partialGold); db.Commit(partialInventory);
    assert(db.gold == 100 && !db.item);
    // A newly loaded Player has suppression=false, so normal saving resumes.
    Player nextSession;
    nextSession.gold = db.gold - 10; nextSession.item = true; nextSession.dirty = true;
    Transaction committed;
    nextSession.SaveToDB(committed);
    db.Commit(committed);
    assert(db.gold == 90 && db.item);
}
'''.replace('SETTER', setter).replace('FIELD', field).replace('FULL_GUARD', ' '.join(guards[0].split())).replace('INVENTORY_GUARD', ' '.join(guards[1].split())).replace('GOLD_GUARD', ' '.join(guards[2].split()))
        program = self.core / 'save-guard-test.cpp'
        program.write_text(code, encoding='utf-8')
        binary = self.core / 'save-guard-test'
        built = subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(program), '-o', str(binary)],
                               capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == '__main__':
    unittest.main()
