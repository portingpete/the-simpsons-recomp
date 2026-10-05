#!/usr/bin/env python3
"""Pin shipped 16x16 RGBA palette with an independent inverse-address oracle.

No production decoder output or GPU output builds the expected pixels. The
source's 4096-byte tile is walked in physical order, inverse addresses recover
its logical 16x16 region, and the original 8-in-32/ZYXW selection fixes lanes.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
from extract_resource import select_payload
from test_texture_decode import inverse_address

DESCRIPTOR = (0x80400002, 0x86, 0x1e00f, 0xc14, 0, 0x200)
REFERENCES = (
    (Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h'),
     '7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227'),
    (Path('K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/util.cpp'),
     'c16fb6f11fbfe4327d8e648353b8f10f661d5f3b7520ef831cfc67ce1569b81d'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture():
    for path, expected in REFERENCES:
        assert digest(path.read_bytes()) == expected, 'Texture reference changed'
    root = ROOT / 'Simpsons Game, The (USA)'
    data, provenance = select_payload(
        root / 'medal_of_homer/medal_of_homer/story_mode/zone01.str',
        15, 'zone01_split11.itxd', root)
    assert digest(data) == 'cd67e19d9c9b5bbb9d787f3c2daa796f9a56a6bdc9f40cf36791076e37ed1d86'
    metadata = data[0x28:0x128]
    assert digest(metadata) == 'fdc72420da88858789d0b8b99869628cb0f63bc1f0da6e060c9414bd74d8681f'
    assert metadata[16:80].split(b'\0')[0] == b'moh_palette_dark_w_s'
    word = lambda at: struct.unpack_from('>I', metadata, at)[0]
    assert tuple(word(at) for at in (0x84, 0x88, 0xb0, 0xbc, 0xc0, 0xc4)) == (
        16, 16, 0, 4096, 4096, 0x18280186)
    assert tuple(word(at) for at in range(0xe8, 0x100, 4)) == DESCRIPTOR
    tiled = data[4096:8192]
    assert digest(tiled) == '3e63384e888b885ce17aae1c3f20a276a9d38516f6e579019983bd74e0ef01de'
    logical = {}
    physical = []
    padding = []
    for offset in range(0, len(tiled), 4):
        x, y = inverse_address(offset, 32, 4)
        if x < 16 and y < 16:
            assert (x, y) not in logical, 'Inverse address aliases a palette texel'
            logical[x, y] = bytes(tiled[offset + lane] for lane in (1, 2, 3, 0))
            physical.append(offset)
        else:
            padding.append(offset)
    assert len(logical) == 256 and len(set(physical)) == 256
    rgba = b''.join(logical[x, y] for y in range(16) for x in range(16))
    assert digest(rgba) == 'ae36b17cf1a208777ce3bedf316da1e25a949ad4c9df0efaea275b1a0ec699e4'
    assert len(set(logical.values())) == 116 and set(rgba[3::4]) == {255}
    manifest = dict(schema='small_rgba_inverse_fixture_v1', name='moh_palette_dark_w_s',
                    provenance=provenance, record_offset=0x28, width=16, height=16,
                    pitch=32, storage_bytes=len(tiled), descriptor=[f'{v:08X}' for v in DESCRIPTOR],
                    metadata_sha256=digest(metadata), storage_sha256=digest(tiled), rgba_sha256=digest(rgba),
                    inverse_texels=len(logical), distinct_colors=116,
                    padding_texels=len(padding), first_padding_offset=padding[0],
                    references=[dict(path=p.as_posix(), sha256=h) for p, h in REFERENCES],
                    evidence='Source descriptor; pinned Xenos fields/32-texel tile layout; inverse physical addressing; original 8-in-32/ZYXW lanes')
    binary = struct.pack('<6I', *DESCRIPTOR) + tiled + rgba
    return manifest, {'small.bin': binary, 'moh_palette_dark_w_s.metadata': metadata,
                      'manifest.json': (json.dumps(manifest, indent=2) + '\n').encode()}


def main():
    manifest, outputs = fixture()
    folder = ROOT / 'build/itxd-small-rgba'
    folder.mkdir(exist_ok=True)
    for name, data in outputs.items():
        path = folder / name
        if path.exists():
            assert path.read_bytes() == data, 'Fixture changed: ' + name
        else:
            path.write_bytes(data)
    print('PASS original 16x16 RGBA fixture: 256 inverse-address texels / 116 colors; ' + manifest['rgba_sha256'])


if __name__ == '__main__':
    main()
