import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('incremental_recompiler', ROOT/'gcn-recomp/recomp/recomp.py')
recomp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recomp)


class GeneratedOutputTests(unittest.TestCase):
    def test_unchanged_shards_keep_bytes_and_timestamps(self):
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder)/'recomp_000.c', Path(folder)/'sources.cmake'
            self.assertTrue(recomp.write_generated(a, 'void one(void) {}\n'))
            self.assertTrue(recomp.write_generated(b, 'set(RECOMP_SOURCES one.c)\n'))
            for path in (a, b):
                os.utime(path, ns=(1234567890000000000, 1234567890000000000))
            stamps = {p:p.stat().st_mtime_ns for p in (a,b)}
            self.assertFalse(recomp.write_generated(a, 'void one(void) {}\n'))
            self.assertFalse(recomp.write_generated(b, 'set(RECOMP_SOURCES one.c)\n'))
            self.assertEqual(stamps, {p:p.stat().st_mtime_ns for p in (a,b)})
            self.assertTrue(recomp.write_generated(a, 'void changed(void) {}\n'))
            self.assertNotEqual(a.stat().st_mtime_ns, stamps[a])
            self.assertEqual(b.stat().st_mtime_ns, stamps[b])

    def test_existing_windows_line_endings_do_not_force_a_rebuild(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'recomp_funcs.h'
            path.write_bytes(b'void one(void);\r\n')
            os.utime(path, ns=(1234567890000000000, 1234567890000000000))
            before=path.stat().st_mtime_ns
            self.assertFalse(recomp.write_generated(path, 'void one(void);\n'))
            self.assertEqual(path.read_bytes(), b'void one(void);\r\n')
            self.assertEqual(path.stat().st_mtime_ns, before)
