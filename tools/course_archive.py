"""Small RARC reader/writer for retaining retail menu resources in local builds.

No archive bytes are shipped. Yaz0 inputs are decompressed; output is plain RARC.
"""
import struct

MAX_ARCHIVE = 32 * 1024 * 1024
MAX_PATH = 4096


def unpack(data):
    if not isinstance(data, (bytes, bytearray, memoryview)) or len(data) > MAX_ARCHIVE * 9 // 8 + 16:
        raise ValueError('Course archive exceeds size limit')
    if isinstance(data, memoryview):
        data = data.tobytes()
    if data[:4] == b'Yaz0':
        if len(data) < 16:
            raise ValueError('Truncated Yaz0 header')
        size = struct.unpack_from('>I', data, 4)[0]
        if not 64 <= size <= MAX_ARCHIVE:
            raise ValueError('Invalid decompressed course archive size')
        out, p = bytearray(), 16
        while len(out) < size:
            if p >= len(data):
                raise ValueError('Truncated Yaz0 command')
            flags = data[p]; p += 1
            for bit in range(7, -1, -1):
                if len(out) == size: break
                if flags & (1 << bit):
                    if p >= len(data):
                        raise ValueError('Truncated Yaz0 literal')
                    out.append(data[p]); p += 1
                else:
                    if p + 2 > len(data):
                        raise ValueError('Truncated Yaz0 reference')
                    a, b = data[p:p+2]; p += 2
                    length = a >> 4
                    if not length:
                        if p >= len(data):
                            raise ValueError('Truncated Yaz0 reference length')
                        length = data[p] + 18; p += 1
                    else: length += 2
                    distance = ((a & 15) << 8 | b) + 1
                    if distance > len(out) or len(out) + length > size:
                        raise ValueError('Invalid Yaz0 back reference')
                    for _ in range(length): out.append(out[-distance])
        data = bytes(out)
    if data[:4] != b'RARC' or not 64 <= len(data) <= MAX_ARCHIVE:
        raise ValueError('Expected a complete course RARC')
    total, header, payload, payload_size = struct.unpack_from('>4I', data, 4)
    if total != len(data) or header != 32: raise ValueError('Invalid RARC header')
    count, nodes, entries, table, strings_size, strings = struct.unpack_from('>6I', data, 32)
    if not 0 < count < 4096 or entries > 65535: raise ValueError('Invalid RARC counts')
    nodes += 32; table += 32; strings += 32; payload += 32
    if payload < 64 or payload > len(data) or payload_size > len(data) - payload:
        raise ValueError('Invalid RARC data region')
    regions = ((nodes, count * 16), (table, entries * 20), (strings, strings_size))
    for start, size in regions:
        if start < 64 or start > payload or size > payload - start:
            raise ValueError('Invalid RARC metadata region')
    nonempty = sorted((start, start + size) for start, size in regions if size)
    if any(a[1] > b[0] for a, b in zip(nonempty, nonempty[1:])):
        raise ValueError('Overlapping RARC metadata regions')
    names = {}
    def name(offset):
        if offset in names:
            return names[offset]
        if offset >= strings_size:
            raise ValueError('RARC name is outside its string table')
        start = strings + offset; end = data.find(b'\0', start, strings + strings_size)
        if end < 0:
            raise ValueError('Unterminated RARC name')
        try:
            value = data[start:end].decode('shift-jis')
        except UnicodeError as error:
            raise ValueError('Invalid RARC name encoding') from error
        if '/' in value or '\\' in value or not value: raise ValueError('Unsafe archive name')
        names[offset] = value
        return value
    root = name(struct.unpack_from('>I', data, nodes + 4)[0])
    if root in ('.', '..') or len(root.encode('shift-jis')) > MAX_PATH:
        raise ValueError('Invalid archive root name')
    files, visited, used_entries = {}, set(), set()
    pending = [(0, '')]
    path_bytes = 0
    while pending:
        index, prefix = pending.pop()
        if index >= count or index in visited: raise ValueError('Invalid RARC node tree')
        visited.add(index)
        _, _, _, n, first = struct.unpack_from('>4sIHHI', data, nodes + 16 * index)
        if first + n > entries: raise ValueError('Invalid RARC entry range')
        siblings = set()
        for k in range(first, first+n):
            if k in used_entries:
                raise ValueError('Overlapping RARC directory entries')
            used_entries.add(k)
            _, _, flags, offset, at, size, _ = struct.unpack_from('>4H3I', data, table + 20*k)
            directory = bool(flags & 0x200)
            if bool(flags & 0x100) == directory:
                raise ValueError('Invalid RARC entry type')
            part = name(offset)
            if part in siblings:
                raise ValueError('Duplicate RARC sibling name')
            siblings.add(part)
            if part in ('.', '..'):
                if not directory:
                    raise ValueError('Invalid RARC directory link')
                continue
            path = prefix + part
            length = len(path.encode('shift-jis'))
            path_bytes += length
            if length > MAX_PATH or path_bytes > MAX_ARCHIVE:
                raise ValueError('RARC paths exceed size limit')
            if directory: pending.append((at, path + '/'))
            else:
                if at > payload_size or size > payload_size - at or path in files: raise ValueError('Invalid RARC payload')
                files[path] = data[payload+at:payload+at+size]
    return root, files


def pack(root, files):
    if (not isinstance(root, str) or not root or root in ('.', '..')
            or any(c in root for c in ('/', '\\', '\0'))):
        raise ValueError('Invalid archive root name')
    if sum(len(data) for data in files.values()) > MAX_ARCHIVE:
        raise ValueError('Course archive exceeds size limit')
    align = lambda n: (n+31) & ~31
    tree = {}
    for path, data in sorted(files.items()):
        parts = path.split('/'); node = tree
        if any(p in ('', '.', '..') or '\\' in p or '\0' in p for p in parts): raise ValueError('Unsafe archive path')
        for part in parts[:-1]:
            node = node.setdefault(part, {})
            if not isinstance(node, dict):
                raise ValueError('Archive path is both a file and directory')
        if parts[-1] in node:
            raise ValueError('Archive path is both a file and directory')
        node[parts[-1]] = data
    directories = [(root, tree, 0)]
    for index, (_, node, _) in enumerate(directories):
        for name, item in node.items():
            if isinstance(item, dict): directories.append((name, item, index))
    if len(directories) >= 4096 or len(files) + 2 * len(directories) > 65535:
        raise ValueError('Course archive has too many entries')
    indexes = {id(item): i for i, (_, item, _) in enumerate(directories)}
    strings, offsets, entries, nodes, payload = bytearray(), {}, bytearray(), bytearray(), bytearray()
    def text(value):
        if value not in offsets:
            try:
                encoded = value.encode('shift-jis')
            except UnicodeError as error:
                raise ValueError('Archive name cannot be encoded') from error
            offsets[value] = len(strings); strings.extend(encoded + b'\0')
        return offsets[value]
    def hash_name(value):
        result = 0
        try:
            encoded = value.encode('shift-jis')
        except UnicodeError as error:
            raise ValueError('Archive name cannot be encoded') from error
        for c in encoded: result = (result * 3 + c) & 65535
        return result
    file_id = 0
    for index, (name, node, parent) in enumerate(directories):
        first = len(entries)//20
        for part, item in list(node.items()) + [('.', node), ('..', directories[parent][1])]:
            directory = isinstance(item, dict)
            if directory: at, size, ident = indexes[id(item)], 16, 65535
            else:
                at, size, ident = len(payload), len(item), file_id; file_id += 1
                payload.extend(item); payload.extend(bytes(align(len(payload))-len(payload)))
            offset = text(part)
            if offset > 65535:
                raise ValueError('Archive entry name table exceeds size limit')
            entries.extend(struct.pack('>4H3I', ident, hash_name(part), 0x200 if directory else 0x1100, offset, at, size, 0))
        tag = b'ROOT' if not index else name.upper().encode('shift-jis')[:4].ljust(4, b'\0')
        nodes.extend(struct.pack('>4sIHHI', tag, text(name), hash_name(name), len(node)+2, first))
    node_at = 64; entry_at = align(node_at+len(nodes)); string_at = align(entry_at+len(entries)); data_at = align(string_at+len(strings))
    if data_at + len(payload) > MAX_ARCHIVE:
        raise ValueError('Course archive exceeds size limit')
    out = bytearray(data_at+len(payload))
    struct.pack_into('>4s7I', out, 0, b'RARC', len(out), 32, data_at-32, len(payload), len(payload), 0, 0)
    struct.pack_into('>6IHBBI', out, 32, len(directories), node_at-32, len(entries)//20, entry_at-32, len(strings), string_at-32, file_id, 1, 0, 0)
    out[node_at:node_at+len(nodes)] = nodes; out[entry_at:entry_at+len(entries)] = entries
    out[string_at:string_at+len(strings)] = strings; out[data_at:] = payload
    if unpack(out) != (root, files): raise ValueError('RARC round-trip mismatch')
    return bytes(out)
