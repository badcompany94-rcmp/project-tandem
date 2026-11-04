import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('disc_files', ROOT / 'tools/disc_files.py')
disc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(disc)


class DiscFilesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.image = self.root / 'base.iso'
        self.files = self.root / 'pack/files'
        self.files.mkdir(parents=True)
        self.output = self.root / 'index.bin'
        original = {'Course/luigi.arc': (0x10000, 17), 'AudioRes/a.ast': (0x18000, 65793)}
        fst = disc.encode_fst(original, {'Course', 'AudioRes'})
        image = bytearray(0x30000)
        image[:6] = b'GM4E01'
        struct.pack_into('>IIII', image, 0x420, 0x3000, 0x4000, len(fst), len(fst))
        struct.pack_into('>I', image, 0x3000, 0x100)
        struct.pack_into('>I', image, 0x3090, 0x20)
        image[0x4000:0x4000 + len(fst)] = fst
        image[0x10000:0x10011] = b'original archive!'
        image[0x18000:0x18000 + 65793] = bytes(i % 251 for i in range(65793))
        self.image.write_bytes(image)

    def put(self, name, data):
        path = self.files / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def build(self):
        return disc.build_index(self.image, self.files, self.output)

    def native(self, index=None, reject=False):
        executable = os.environ.get('TANDEM_DISC_FILES_TEST')
        if not executable:
            self.skipTest('native reader fixture not configured')
        result = subprocess.run([executable, str(self.image), str(index or self.output),
                                 '--reject' if reject else str(self.files)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_virtual_tree_preserves_base_and_maps_new_larger_and_empty_files(self):
        before = self.image.read_bytes()
        self.put('Course/luigi.arc', bytes(range(256)) * 300 + b'end')
        self.put('NewFolder/new.bin', b'a new file')
        self.put('NewFolder/empty.bin', b'')
        report = self.build()
        self.assertEqual(report['merged_file_count'], 4)
        self.assertEqual({f['action'] for f in report['files']}, {'add', 'keep', 'replace'})
        self.assertLess(report['virtual_end'], 0x7fffffff)
        self.assertEqual(before, self.image.read_bytes())
        self.native()

    def test_empty_pack_native_reads_every_original_file(self):
        report = self.build()
        self.assertEqual(report['base_file_count'], report['merged_file_count'])
        self.assertTrue(all(f['source'] == 'image' for f in report['files']))
        self.native()

    def test_native_rejects_truncation_bad_names_forged_fst_and_wrong_image(self):
        self.put('new.bin', b'content')
        self.build()
        original = self.output.read_bytes()
        base_size, fst_size, count, root_size = struct.unpack_from('>IIII', original, 8)
        new_fst_offset = 48 + base_size
        first_record = new_fst_offset + fst_size + root_size
        variants = [original[:20], original[:-1], b'BADINDEX' + original[8:]]
        invalid_root = bytearray(original)
        invalid_root[new_fst_offset] = 2
        variants.append(invalid_root)
        invalid_range = bytearray(original)
        struct.pack_into('>Q', invalid_range, first_record, 0x8000)
        variants.append(invalid_range)
        overflow = bytearray(original)
        struct.pack_into('>Q', overflow, 32, 0xffffffffffff8000)
        variants.append(overflow)
        invalid_name = bytearray(original)
        name_size = struct.unpack_from('>I', invalid_name, first_record + 24)[0]
        invalid_name[first_record + 28:first_record + 28 + name_size] = b'/' * name_size
        variants.append(invalid_name)
        for index, data in enumerate(variants):
            with self.subTest(corruption=index):
                path = self.root / f'invalid-{index}.bin'
                path.write_bytes(data)
                self.native(path, reject=True)
        image = bytearray(self.image.read_bytes())
        image[0x4000 + base_size - 2] ^= 1
        self.image.write_bytes(image)
        self.native(reject=True)

    def test_native_rejects_missing_or_resized_replacement(self):
        self.put('new.bin', b'content')
        self.build()
        self.put('new.bin', b'changed size')
        self.native(reject=True)
        (self.files / 'new.bin').unlink()
        self.native(reject=True)

    def test_case_mismatch_and_file_directory_conflict_are_rejected(self):
        self.put('course/new.arc', b'case mismatch')
        with self.assertRaisesRegex(ValueError, 'Case mismatch'):
            self.build()
        (self.files / 'course/new.arc').unlink()
        (self.files / 'course').rmdir()
        self.put('Course/luigi.arc/child', b'file becomes directory')
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.build()

    def test_invalid_fst_names_and_directory_ranges_are_rejected(self):
        fst = disc.encode_fst({'Folder/a': (10, 20)}, {'Folder'})
        cases = [fst[:10], b'\0' + fst[1:]]
        invalid = bytearray(fst)
        struct.pack_into('>I', invalid, 20, 10000)
        cases.append(invalid)
        invalid = bytearray(fst)
        invalid[-2] = ord('/')
        cases.append(invalid)
        for data in cases:
            with self.assertRaises((ValueError, UnicodeDecodeError)):
                disc.parse_fst(data)

    def test_generation_is_repeatable_but_never_overwrites_different_index(self):
        self.put('new.bin', b'content')
        self.build()
        original = self.output.read_bytes()
        self.build()
        self.assertEqual(original, self.output.read_bytes())
        self.put('new.bin', b'different length')
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.build()

    def test_output_inside_pack_is_rejected(self):
        self.output = self.files / 'generated.bin'
        with self.assertRaisesRegex(ValueError, 'outside'):
            self.build()


if __name__ == '__main__':
    unittest.main()
