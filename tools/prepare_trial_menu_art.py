"""Extract menu textures from the player's image into the PRIVATE build directory.

Only this extractor is distributed. Never package its generated header or assets.
"""
import argparse
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from disc_files import DiscImage, parse_fst
from course_archive import unpack


def prepare(image_path, output):
    image = DiscImage(str(image_path))
    offset, size = struct.unpack('>II', image.read(0x424, 8))
    files, _ = parse_fst(image.read(offset, size))
    groups = {
        'SceneData/English/menu.arc': ['entry_grandprix', 'entry_time_attack', 'bar2',
            'corsor_maru1', 'entry_engine1', 'entry_engine2', 'entry_engine3', 'entry_engine4',
            'power_mozi.1', 'power_mozi.2', 'power_mozi.3', 'power_mozi.4'],
        'MRAM_Locale/English/MRAMLoc.arc': ['mozi_change_minigame', 'mozi_change_course', 'mozi_next_com', 'mozi_saveghost', 'mozi_replay'],
        'SceneData/English/titleline.arc': ['selectmode', 'selectcourse', 'selectbattle', 'selectcharacter'],
    }
    lines = ['// PRIVATE: generated from your disc; do not distribute.', '#pragma once',
             'namespace trial_disc_art {', 'struct Asset {const char* name; const unsigned char* bytes; unsigned size;};']
    entries = []
    for arc, names in groups.items():
        archive = unpack(image.read(*files[arc]))[1]
        for name in names:
            data = archive.get('timg/' + name + '.bti')
            # Some engine-label variations are optional. Text fallback still works.
            if data is None and name.startswith(('entry_engine', 'power_mozi.')):
                continue
            if data is None or not 32 <= len(data) <= 1024*1024:
                raise ValueError('Missing or oversized menu texture: ' + name)
            width, height = struct.unpack_from('>HH', data, 2)
            if not 0 < width <= 1024 or not 0 < height <= 1024:
                raise ValueError('Invalid texture dimensions: ' + name)
            symbol = 'asset_' + str(len(entries))
            lines.append('inline constexpr unsigned char ' + symbol + '[]={' + ','.join(map(str, data)) + '};')
            entries.append('{"' + name + '",' + symbol + ',sizeof(' + symbol + ')}')
    lines.extend(['inline constexpr Asset assets[]={' + ','.join(entries) + '};', '}'])
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    contents = '\n'.join(lines) + '\n'
    if not output.exists() or output.read_text() != contents:
        output.write_text(contents)
    print('Prepared', len(entries), 'private menu textures from the local disc.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prepare(args.image, args.output)
