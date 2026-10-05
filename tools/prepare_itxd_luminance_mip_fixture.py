"""Pin original beam1 L8 mips with an independent physical-address oracle.

No native decoder output or HLSL is read. Layout tables are explicit, derived
from the original descriptor and the pinned local Xenia-derived layout reference.
The oracle walks physical bytes and inverts their tiled addresses; production
decoding walks logical pixels in the other direction.
"""
import sys

sys.dont_write_bytecode = True

import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
from extract_resource import select_payload
from test_texture_decode import inverse_address

SDK_REFERENCES = (
    (Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/pipeline/texture/util.h'),
     'd370dded22778c3154eebe06ae2f4fcb7175e8a8e777012744c16391928db81b'),
    (Path('K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/util.cpp'),
     'c16fb6f11fbfe4327d8e648353b8f10f661d5f3b7520ef831cfc67ce1569b81d'),
)
BEAM_DESCRIPTOR = (0x80800002, 2, 0x0007E03F, 0x1400, 0x80, 0x1A00)
# Per level: allocation start, allocation bytes, pitch in texels, origin X/Y.
# The source descriptor's mip page is +0x1000. Level1's 32x32-byte image and
# level2's packed image each occupy a separately aligned 4096-byte subresource.
BEAM_LAYOUT = [(0, 4096, 64, 0, 0), (4096, 4096, 32, 0, 0),
               (8192, 4096, 32, 16, 0)]
BEAM_LEVEL_HASHES = (
    '987d7e87be135e4ac1b903eaeddb43f645dbc21334b8da2777f35b90ff9c0118',
    '626e064a1c94e80b846390fce882dfcc5e6b589309627384023c9519b0df1504',
    '97636448653818b3acffed2354f35a65947a7639d9040aeaadc91484d5dc81a5',
)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def inverse_levels(storage, width, height, layout):
    """Decode bounded L8 regions using address inversion, retaining RRR1."""
    assert width >= 64 and height >= 64 and width <= 2048 and height <= 2048
    assert width & (width - 1) == 0 and height & (height - 1) == 0
    assert layout and len(storage) == max(start + length for start, length, *_ in layout)
    levels = []
    for level, (start, length, pitch, origin_x, origin_y) in enumerate(layout):
        assert start >= 0 and length > 0 and start + length <= len(storage)
        assert pitch >= 32 and pitch % 32 == 0 and origin_x >= 0 and origin_y >= 0
        logical_width, logical_height = max(1, width >> level), max(1, height >> level)
        pixels = {}
        for offset in range(length):
            x, y = inverse_address(offset, pitch, 1)
            x, y = x - origin_x, y - origin_y
            if 0 <= x < logical_width and 0 <= y < logical_height:
                assert (x, y) not in pixels, 'Aliased physical-address oracle coordinate'
                value = storage[start + offset]
                pixels[x, y] = bytes((value, value, value, 255))
        assert len(pixels) == logical_width * logical_height, 'Incomplete mip logical rectangle'
        levels.append(b''.join(pixels[x, y] for y in range(logical_height) for x in range(logical_width)))
    return levels


def original_case():
    root = ROOT / 'Simpsons Game, The (USA)'
    data, provenance = select_payload(root / 'spr_hub/spr_hub.str', 13, 'spr_hub_split9.itxd', root)
    assert digest(data) == 'bde4a72071a76a75a0c472661cf09e9aa16a1988c71f737287a3ad0657e1312a'
    metadata = data[0xB28:0xC28]
    assert digest(metadata) == '624146a511e5009b3246e4abc5afa16cdcc542f662b18591c57c2dc3d85c0e4c'
    assert metadata[16:80].split(b'\0')[0] == b'beam1'
    word = lambda at: struct.unpack_from('>I', metadata, at)[0]
    assert tuple(word(at) for at in (0x84, 0x88, 0xB0, 0xBC, 0xC0, 0xC4)) == (
        64, 64, 0, 12288, 0x2C000, 0x28000102)
    assert tuple(word(at) for at in range(0xE8, 0x100, 4)) == BEAM_DESCRIPTOR
    storage = data[0x2C000:0x2F000]
    assert digest(storage) == 'dfcd0f0a6d900ca3de0c96f6014b3861d2d4dfd44227c99202d1cb184caa730a'
    levels = inverse_levels(storage, 64, 64, BEAM_LAYOUT)
    assert tuple(digest(level) for level in levels) == BEAM_LEVEL_HASHES
    return ('beam1', 64, 64, BEAM_DESCRIPTOR, storage, levels,
            dict(provenance=provenance, record=0xB28, metadata_sha256=digest(metadata),
                 layout=BEAM_LAYOUT)), metadata


def synthetic_cases():
    # All five tail placements, both rectangle orientations, and maximum base
    # pitch. Repeated ranges are shared tail allocations, not duplicate mips.
    descriptions = [
        ('square_full_tail', 64, 64, BEAM_LAYOUT +
         [(8192, 4096, 32, x, y) for x, y in [(8, 0), (4, 0), (0, 8), (0, 4)]]),
        ('wide_full_tail', 128, 64,
         [(0, 8192, 128, 0, 0), (8192, 4096, 64, 0, 0)] +
         [(12288, 4096, 32, x, y) for x, y in [(0, 16), (0, 8), (0, 4), (16, 0), (8, 0), (4, 0)]]),
        ('tall_full_tail', 64, 128,
         [(0, 8192, 64, 0, 0), (8192, 4096, 32, 0, 0)] +
         [(12288, 4096, 32, x, y) for x, y in [(16, 0), (8, 0), (4, 0), (0, 16), (0, 8), (0, 4)]]),
        ('long_wide_full_tail', 512, 128,
         [(0, 65536, 512, 0, 0), (65536, 16384, 256, 0, 0), (81920, 4096, 128, 0, 0)] +
         [(86016, 4096, 64, x, y) for x, y in [(0, 16), (0, 8), (0, 4), (32, 0), (16, 0), (8, 0), (4, 0)]]),
        ('long_tall_full_tail', 128, 512,
         [(0, 65536, 128, 0, 0), (65536, 16384, 64, 0, 0), (81920, 4096, 32, 0, 0)] +
         [(86016, 4096, 32, x, y) for x, y in [(16, 0), (8, 0), (4, 0), (0, 32), (0, 16), (0, 8), (0, 4)]]),
        ('maximum_wide_full_tail', 2048, 64,
         [(0, 131072, 2048, 0, 0), (131072, 32768, 1024, 0, 0)] +
         [(163840, 16384, 512, x, y) for x, y in [(0, 16), (0, 8), (0, 4), (256, 0), (128, 0), (64, 0),
                                                (32, 0), (16, 0), (8, 0), (4, 0)]]),
        ('maximum_tall_full_tail', 64, 2048,
         [(0, 131072, 64, 0, 0), (131072, 32768, 32, 0, 0)] +
         [(163840, 16384, 32, x, y) for x, y in [(16, 0), (8, 0), (4, 0), (0, 256), (0, 128), (0, 64),
                                               (0, 32), (0, 16), (0, 8), (0, 4)]]),
    ]
    for name, width, height, layout in descriptions:
        storage = bytes((offset * 73 + (offset >> 9) * 17 + (offset >> 4) * 11) & 255
                        for offset in range(max(start + size for start, size, *_ in layout)))
        descriptor = (0x80000002 | ((width // 32) << 22), 2,
                      ((height - 1) << 13) | (width - 1), 0x1400,
                      (len(layout) - 1) << 6, layout[1][0] | 0xA00)
        yield name, width, height, descriptor, storage, inverse_levels(storage, width, height, layout), dict(
            synthetic=True, layout=layout)


def family_bytes(cases):
    """LE L8M1/count; each case W,H,storage bytes,six words,storage,RGBA mips."""
    output = bytearray(b'L8M1' + struct.pack('<I', len(cases)))
    for _, width, height, descriptor, storage, levels, _ in cases:
        assert len(levels) == (descriptor[4] >> 6) + 1
        output.extend(struct.pack('<9I', width, height, len(storage), *descriptor))
        output.extend(storage)
        for level in levels:
            output.extend(level)
    return bytes(output)


def main():
    for path, expected in SDK_REFERENCES:
        assert digest(path.read_bytes()) == expected, 'Local SDK layout reference changed'
    original, metadata = original_case()
    cases = [original, *synthetic_cases()]
    manifest = dict(
        schema='l8_mip_inverse_fixture_v1',
        references=[dict(path=path.as_posix(), sha256=expected) for path, expected in SDK_REFERENCES],
        evidence='SDK GetPackedMipLevel/GetPackedMipOffset/4096-byte subresource stride; explicit layout tables; inverse tiled addresses',
        cases=[dict(name=name, width=width, height=height, descriptor=[f'{word:08X}' for word in descriptor],
                    storage_bytes=len(storage), storage_sha256=digest(storage),
                    level_sha256=[digest(level) for level in levels], **details)
               for name, width, height, descriptor, storage, levels, details in cases])
    outputs = {'family.bin': family_bytes(cases), 'beam1.metadata': metadata, 'beam1.tiled': original[4],
               **{f'beam1.level{level}.rgba': pixels for level, pixels in enumerate(original[5])},
               'manifest.json': (json.dumps(manifest, indent=2) + '\n').encode()}
    folder = ROOT / 'build/itxd-luminance-mip'
    folder.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        path = folder / name
        if path.exists():
            assert path.read_bytes() == value, 'Existing L8 mip fixture changed: ' + name
        else:
            with path.open('xb') as stream:
                stream.write(value)
    print(f'PASS: {len(cases)} L8 mip fixtures, {sum(len(case[5]) for case in cases)} independently decoded levels; original beam1 hashes pinned')


if __name__ == '__main__':
    main()
