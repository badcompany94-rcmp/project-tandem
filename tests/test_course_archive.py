"""Archive compatibility and malformed-input checks for local course tooling."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from course_archive import pack, unpack


def yaz0(data):
    """Small fixture encoder, including overlapping distance-one references."""
    commands = []
    at = 0
    while at < len(data):
        run = 0
        if at:
            while run < 273 and at + run < len(data) and data[at + run] == data[at - 1]:
                run += 1
        if run >= 3:
            wire = bytes(((run - 2) << 4, 0)) if run < 18 else bytes((0, 0, run - 18))
            commands.append((False, wire)); at += run
        else:
            commands.append((True, data[at:at + 1])); at += 1
    result = bytearray(b'Yaz0' + struct.pack('>I', len(data)) + bytes(8))
    for first in range(0, len(commands), 8):
        group = commands[first:first + 8]
        result.append(sum(int(literal) << (7 - n) for n, (literal, _) in enumerate(group)))
        for _, wire in group:
            result.extend(wire)
    return bytes(result)


class CourseArchiveTests(unittest.TestCase):
    def setUp(self):
        self.files = {'empty': b'', 'timg/title.bti': bytes(range(256)),
                      'more/deep/file': b'\x00' * 300 + b'\xff'}
        self.data = pack('course', self.files)

    def test_declared_string_and_payload_bounds_are_enforced(self):
        for offset, value in ((0x30, 1), (0x10, 0)):
            with self.subTest(offset=offset):
                bad = bytearray(self.data)
                struct.pack_into('>I', bad, offset, value)
                with self.assertRaises(ValueError):
                    unpack(bad)

    def test_raw_and_yaz0_preserve_nested_empty_binary_and_japanese_names(self):
        files = dict(self.files, **{'日本語/画像.bti': b'texture'})
        raw = pack('コース', files)
        self.assertEqual(unpack(raw), ('コース', files))
        self.assertEqual(unpack(yaz0(raw)), ('コース', files))
        self.assertEqual(unpack(pack('empty', {})), ('empty', {}))

    def test_every_truncation_reports_an_archive_error(self):
        for data in (self.data, yaz0(self.data)):
            for length in range(len(data)):
                with self.subTest(magic=data[:4], length=length):
                    with self.assertRaises(ValueError):
                        unpack(data[:length])

    def test_metadata_sections_cannot_overlap_or_leave_the_header(self):
        for offset, value in ((0x24, 0), (0x2c, 32), (0x34, 32),
                              (0x20, 4096), (0x28, 65536), (0x30, 0xffffffff)):
            with self.subTest(offset=offset):
                bad = bytearray(self.data); struct.pack_into('>I', bad, offset, value)
                with self.assertRaises(ValueError):
                    unpack(bad)

    def test_invalid_entry_types_and_outside_string_offsets_are_rejected(self):
        table = struct.unpack_from('>I', self.data, 0x2c)[0] + 32
        strings_size = struct.unpack_from('>I', self.data, 0x30)[0]
        for at, fmt, value in ((table + 4, '>H', 0), (table + 4, '>H', 0x300),
                                (table + 6, '>H', strings_size)):
            bad = bytearray(self.data); struct.pack_into(fmt, bad, at, value)
            with self.assertRaises(ValueError):
                unpack(bad)

    def test_truncated_and_impossible_yaz0_references_are_rejected(self):
        for commands in (b'\0', b'\0\0', b'\0\0\0', b'\0\x10\0'):
            with self.assertRaises(ValueError):
                unpack(b'Yaz0' + struct.pack('>I', 100) + bytes(8) + commands)
        with self.assertRaises(ValueError):
            unpack(b'Yaz0' + struct.pack('>I', 32 * 1024**2 + 1) + bytes(8))

    def test_deep_valid_tree_does_not_depend_on_python_recursion_limit(self):
        files = {'a/' * 1200 + 'leaf': b'leaf'}
        self.assertEqual(unpack(pack('root', files)), ('root', files))

    def test_writer_rejects_ambiguous_paths_and_root_names(self):
        for files in ({'file': b'x', 'file/child': b'y'}, {'../bad': b'x'}, {'.': b'x'}):
            with self.assertRaises(ValueError):
                pack('root', files)
        for root in ('', '.', '..', 'bad/root', 'bad\\root', 'bad\0root', 'x' * 4097):
            with self.assertRaises(ValueError):
                pack(root, {'file': b'x'})


if __name__ == '__main__':
    unittest.main()
