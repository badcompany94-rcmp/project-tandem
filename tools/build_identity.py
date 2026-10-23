"""Path-independent compatibility identity for locally rebuilt friend packages.

This identifies supported sources, compiler policy and disc revision. It is not
authentication or an anti-cheat signature. No generated game data is distributed.
"""
import argparse
import hashlib
import json
from pathlib import Path

DISC_SHA1 = 'e389e50357d4a5076ab3d38672204658f411123d'


def inputs(root):
    selected = {root / 'CMakeLists.txt', root / 'tools/build_identity.py',
                root / 'gcn-recomp/CMakeLists.txt',root / 'analysis/symbols.txt',
                root / 'analysis/splits.txt',root / 'analysis/config.yml'}
    for directory, extensions in (
        ('src', {'.cpp', '.h', '.inc'}),
        ('services', {'.py'}),
        ('recomp', {'.txt'}), ('gcn-recomp/recomp', {'.py'}),
        ('cmake', {'.cmake'}),
        ('gcn-recomp/runtime', {'.cpp', '.h', '.c', '.inc'}),
        ('gcn-recomp/third_party/libco', {'.c', '.h'}),
        ('gcn-recomp/third_party/imgui', {'.cpp', '.h'}),
        ('gcn-recomp/openxr', {'.cpp', '.h'}),
    ):
        selected.update(p for p in (root / directory).rglob('*')
                        if p.is_file() and p.suffix in extensions and '__pycache__' not in p.parts)
    return sorted(selected, key=lambda p: p.relative_to(root).as_posix())


def identity(root, policy):
    policy = dict(policy)
    for key in ('openxr', 'gl_es', 'checks', 'trace'):
        policy[key] = str(policy.get(key, '')).upper() in ('ON', 'TRUE', '1')
    digest = hashlib.sha256()
    digest.update(b'double-dash-compatible-build-v1\0' + DISC_SHA1.encode() + b'\0')
    digest.update(json.dumps(policy, sort_keys=True, separators=(',', ':')).encode() + b'\0')
    files = {}
    for path in inputs(root):
        name = path.relative_to(root).as_posix()
        data = path.read_bytes().replace(b'\r\n', b'\n')
        file_hash = hashlib.sha256(data).hexdigest()
        files[name] = file_hash
        digest.update(name.encode() + b'\0' + file_hash.encode() + b'\0')
    return dict(format=1, id=digest.hexdigest()[:16], sha256=digest.hexdigest(),
                disc_id='GM4E01', dol_sha1=DISC_SHA1, policy=policy, files=files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--policy', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = identity(args.root.resolve(), json.loads(args.policy.read_text(encoding='utf-8-sig')))
    args.output.mkdir(parents=True, exist_ok=True)
    header = '#pragma once\n#define MKDD_BUILD_ID "' + result['id'] + '"\n'
    for path, data in ((args.output / 'build_identity.h', header),
                       (args.output / 'build-identity.json', json.dumps(result, indent=2) + '\n')):
        if not path.exists() or path.read_text(encoding='utf-8') != data:
            path.write_text(data, encoding='utf-8', newline='\n')
    print(result['id'])


if __name__ == '__main__':
    main()
