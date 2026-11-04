"""Build a private, read-only virtual FST and file index over a player's disc.

This is loader infrastructure, not executable-mod support. A mod's sys/main.dol
must be handled by an explicitly supported recompilation variant, separately.
No disc image or replacement file is modified or copied by this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gcn-recomp/recomp'))
from dol import DiscImage

MAGIC = b'TDFILES2'
ALIGNMENT = 0x8000
MAX_FST = 4 * 1024 * 1024
MAX_FILES = 65535
MAX_OFFSET = 0x7fffffff  # DVD SDK uses signed seek/read offsets.


def align(value):
    return (value + ALIGNMENT - 1) & -ALIGNMENT


def safe_name(name):
    if (not name or name in ('.', '..') or any(ord(c) < 32 for c in name)
            or any(c in name for c in '/\\:') or name.endswith((' ', '.'))):
        raise ValueError(f'Unsupported disc filename: {name!r}')
    return name


def read_exact(image, offset, size):
    data = image.read(offset, size)
    if len(data) != size:
        raise ValueError(f'Truncated image at {offset:#x}: expected {size} bytes')
    return data


def parse_fst(data):
    """Return path -> (offset, size), checking the directory tree and names."""
    if len(data) < 12 or data[:8] != b'\x01\0\0\0\0\0\0\0':
        raise ValueError('Invalid FST root')
    count = struct.unpack_from('>I', data, 8)[0]
    if not 1 <= count <= MAX_FILES or count * 12 > len(data):
        raise ValueError('Invalid FST entry count')
    names = data[count * 12:]
    files, directories, stack, seen = {}, set(), [('', 0, count)], set()
    for index in range(1, count):
        while stack and index >= stack[-1][2]:
            stack.pop()
        if not stack:
            raise ValueError('FST directory ends before its children')
        tag, value, size = struct.unpack_from('>III', data, index * 12)
        kind, name_offset = tag >> 24, tag & 0xffffff
        end = names.find(b'\0', name_offset)
        if kind not in (0, 1) or end < 0:
            raise ValueError('Invalid FST name or entry type')
        name = safe_name(names[name_offset:end].decode('ascii'))
        parent, parent_index, parent_end = stack[-1]
        path = parent + name
        key = path.casefold()
        if key in seen:
            raise ValueError(f'Duplicate FST path: {path}')
        seen.add(key)
        if kind:
            if value != parent_index or not index < size <= parent_end:
                raise ValueError(f'Invalid FST directory bounds: {path}')
            directories.add(path)
            stack.append((path + '/', index, size))
        else:
            if value + size > 0x100000000:
                raise ValueError(f'FST file range overflows: {path}')
            files[path] = (value, size)
    return files, directories


def encode_fst(files, directories):
    tree = {}
    for path in sorted(directories):
        node = tree
        for part in PurePosixPath(path).parts:
            node = node.setdefault(part, {})
    for path, value in sorted(files.items()):
        parts = PurePosixPath(path).parts
        node = tree
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = value
    entries, strings = [], bytearray(b'\0')

    def walk(name, node, parent):
        name_offset = len(strings) if name else 0
        if name:
            strings.extend(name.encode('ascii') + b'\0')
        index = len(entries)
        if isinstance(node, dict):
            entries.append((0x1000000 | name_offset, parent, 0))
            for child in sorted(node, key=lambda x: (x.casefold(), x)):
                walk(child, node[child], index)
            entries[index] = (0x1000000 | name_offset, parent, len(entries))
        else:
            entries.append((name_offset, *node))

    walk('', tree, 0)
    data = b''.join(struct.pack('>III', *e) for e in entries) + strings
    if len(entries) > MAX_FILES or len(data) > MAX_FST:
        raise ValueError('Merged FST exceeds loader limits')
    return data


def collect_replacements(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Replacement files root must be a real directory')
    root = root.resolve(strict=True)
    paths, directories, seen = {}, set(), set()
    pending = [root]
    while pending:
        folder = pending.pop()
        for item in sorted(folder.iterdir()):
            relative = item.relative_to(root).as_posix()
            for part in PurePosixPath(relative).parts:
                safe_name(part).encode('ascii')
            if item.is_symlink() or getattr(item, 'is_junction', lambda: False)():
                raise ValueError(f'Linked replacement path: {relative}')
            if not item.resolve(strict=True).is_relative_to(root):
                raise ValueError(f'Replacement escapes its root: {relative}')
            key = relative.casefold()
            if key in seen:
                raise ValueError(f'Duplicate replacement path: {relative}')
            seen.add(key)
            if len(seen) > MAX_FILES:
                raise ValueError('Too many replacement entries')
            if item.is_dir():
                pending.append(item)
                directories.add(relative)
            elif item.is_file():
                paths[relative] = item
            else:
                raise ValueError(f'Not a regular replacement file: {relative}')
    return paths, directories


def build_index(image_path, files_root, output, replace=False):
    """Return index bytes plus a JSON-safe inventory; no source assets in output."""
    files_root = Path(files_root)
    replacements, extra_dirs = collect_replacements(files_root)
    output = Path(output)
    if output.resolve().is_relative_to(files_root.resolve()):
        raise ValueError('Write generated indexes outside the replacement files root')
    image = DiscImage(image_path)
    try:
        header = read_exact(image, 0, 0x440)
        fst_offset, fst_size = struct.unpack_from('>II', header, 0x424)
        if not 12 <= fst_size <= MAX_FST:
            raise ValueError('Base FST size exceeds loader limits')
        base_fst = read_exact(image, fst_offset, fst_size)
        files, dirs = parse_fst(base_fst)
        base_files = dict(files)
        known = {p.casefold(): p for p in (*files, *dirs)}
        for path in (*replacements, *extra_dirs):
            existing = known.get(path.casefold())
            if existing is not None and existing != path:
                raise ValueError(f'Case mismatch with base disc: {path} / {existing}')
            if path in dirs and path in replacements or path in files and path in extra_dirs:
                raise ValueError(f'File/directory collision: {path}')
        dol_offset = struct.unpack_from('>I', header, 0x420)[0]
        dol_header = read_exact(image, dol_offset, 0x100)
        dol_end = dol_offset + max([0x100] + [
            struct.unpack_from('>I', dol_header, i * 4)[0]
            + struct.unpack_from('>I', dol_header, 0x90 + i * 4)[0]
            for i in range(18)])
        # Compact the virtual address space. Appending to a full-size retail ISO
        # would exceed the SDK's signed offsets once Plus's files are included.
        # Unchanged files map back to their original positions without copying.
        start = align(max(dol_end, 0x2440))
        # Names, entries and lengths determine FST size; offsets do not.
        files.update({p: (0, item.stat().st_size) for p, item in replacements.items()})
        dirs.update(extra_dirs)
        provisional = encode_fst(files, dirs)
        cursor = align(start + len(provisional))
        records = []
        for path, (_, size) in sorted(files.items()):
            if size > MAX_OFFSET or cursor + align(max(size, 1)) > MAX_OFFSET:
                raise ValueError('Overlay exceeds signed DVD address space')
            files[path] = (cursor, size)
            source = 0xffffffffffffffff if path in replacements else base_files[path][0]
            records.append((cursor, size, source, path))
            cursor += align(max(size, 1))
        fst = encode_fst(files, dirs)
        root_bytes = str(files_root.resolve()).encode('utf-8')
        if len(root_bytes) > 32768:
            raise ValueError('Files root path is too long')
        binary = bytearray(MAGIC + struct.pack('>IIIIQQQ', len(base_fst), len(fst),
                           len(records), len(root_bytes), fst_offset, start, cursor))
        binary.extend(base_fst + fst + root_bytes)
        report_files = []
        for offset, size, source, path in records:
            name = path.encode('ascii')
            binary.extend(struct.pack('>QQQI', offset, size, source, len(name)) + name)
            report_files.append(dict(path=path, offset=offset, size=size,
                                     source='image' if source != 0xffffffffffffffff else 'files',
                                     action=('keep' if path not in replacements else
                                             'replace' if path in base_files else 'add')))
        report = dict(format=2, disc_id=header[:6].decode('ascii'),
                      base_fst_sha256=hashlib.sha256(base_fst).hexdigest(),
                      fst_sha256=hashlib.sha256(fst).hexdigest(), fst_offset=start,
                      fst_size=len(fst), virtual_end=cursor, files=report_files,
                      base_file_count=len(base_files), merged_file_count=len(files),
                      executable_support=False)
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            if output.read_bytes() != binary and not replace:
                raise ValueError(f'Refusing to overwrite a different index: {output}')
        if not output.exists() or output.read_bytes() != binary:
            with tempfile.NamedTemporaryFile(prefix=output.name + '.', suffix='.tmp',
                                             dir=output.parent, delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(binary)
            try:
                temporary.replace(output)
            finally:
                temporary.unlink(missing_ok=True)
        return report
    finally:
        image.f.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True, type=Path)
    parser.add_argument('--files', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = build_index(args.image, args.files, args.output)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Indexed {len(report['files'])} files; merged disc has {report['merged_file_count']} files.")


if __name__ == '__main__':
    main()
