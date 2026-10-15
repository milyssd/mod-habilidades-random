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
