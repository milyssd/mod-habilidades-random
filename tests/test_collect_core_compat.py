import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipFile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('collector', root / 'tools/collect_core_compat.py')
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class CollectorTests(unittest.TestCase):
    def test_whitelisted_file_cannot_redirect_to_private_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            core = Path(directory)
            source = core / collector.FILES[0]
            source.parent.mkdir(parents=True)
            private = core / 'conf/worldserver.conf'
            private.parent.mkdir()
            private.write_text('PRIVATE_CONFIGURATION_SENTINEL')
            try:
                source.symlink_to(private)
            except (OSError, NotImplementedError):
                self.skipTest('Symbolic links are unavailable')
            output = core / 'compat.zip'
            report = collector.collect(core, output)
            self.assertEqual(report['files'], [])
            self.assertIn(collector.FILES[0], report['skipped_symlinks'])
            with ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ['compat-report.json'])
                self.assertNotIn(b'PRIVATE_CONFIGURATION_SENTINEL', archive.read('compat-report.json'))

    def test_whitelisted_directory_cannot_redirect_outside_the_core(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core = root / 'core'
            outside = root / 'private'
            source = core / collector.FILES[0]
            source.parent.parent.mkdir(parents=True)
            outside.mkdir()
            (outside / source.name).write_text('PRIVATE_SOURCE_SENTINEL')
            try:
                source.parent.symlink_to(outside, target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest('Symbolic links are unavailable')
            output = root / 'compat.zip'
            report = collector.collect(core, output)
            self.assertEqual(report['files'], [])
            self.assertIn(collector.FILES[0], report['skipped_symlinks'])
            with ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ['compat-report.json'])

    def test_only_requested_source_is_archived_and_existing_files_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            core = Path(directory)
            source = core / collector.FILES[0]
            source.parent.mkdir(parents=True)
            source.write_text('// PlayerScript API\n')
            private = core / 'conf/worldserver.conf'
            private.parent.mkdir()
            private.write_text('PRIVATE_CONFIGURATION_SENTINEL')
            output = core / 'compat.zip'
            report = collector.collect(core, output)
            self.assertEqual(report['files'], [collector.FILES[0]])
            with ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), {collector.FILES[0], 'compat-report.json'})
                self.assertEqual(json.loads(archive.read('compat-report.json'))['files'], [collector.FILES[0]])
                self.assertNotIn(b'PRIVATE_CONFIGURATION_SENTINEL', archive.read('compat-report.json'))
            original = output.read_bytes()
            with self.assertRaises(FileExistsError):
                collector.collect(core, output)
            self.assertEqual(output.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
