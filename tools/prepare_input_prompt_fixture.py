"""Pin original menu/gameplay button atlas bytes and the font's UV cells."""
from pathlib import Path
import hashlib
import json
import struct

from extract_resource import select_payload
from decode_itxd import untile_blocks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-input-prompts'


def main():
    root = ROOT / 'Simpsons Game, The (USA)'
    sources = [
        ('frontend/frontend.str', 18, 'frontend_split17.itxd', 0x628, 0x1D000,
         '44caca8ee799903cf12da2cfbb7058c1f1104f644644b64af74d923a6a331a55'),
        ('simpsons_chars/simpsons_chars_global.str', 15,
         'simpsons_chars_global_split11.itxd', 0xA28, 0x96000,
         '6280a466aa06f7a602b44a1beaf7c830d1beff935713acc43e4d39c1ef650c69'),
    ]
    digest = lambda value: hashlib.sha256(value).hexdigest()
    records, provenance = [], []
    for path, entry, name, offset, pixel_offset, expected in sources:
        data, source = select_payload(root / path, entry, name, root)
        if digest(data) != expected:
            raise ValueError('Original button dictionary identity changed')
        metadata = data[offset:offset + 256]
        word = lambda at: struct.unpack_from('>I', metadata, at)[0]
        descriptor = tuple(word(at) for at in range(0xE8, 0x100, 4))
        if (metadata[0x10:0x50].split(b'\0')[0] != b'buttons'
                or descriptor != (0x82000002, 0x53, 0x1FE0FF, 0xD10, 0, 0x200)
                or (word(0x84), word(0x88), word(0xBC), word(0xC4), word(0xB0))
                != (256, 256, 65536, 0x1A200153, 0)
                or word(0xC0) != pixel_offset):
            raise ValueError('Original button atlas metadata changed')
        pixels = data[pixel_offset:pixel_offset + 65536]
        if len(pixels) != 65536:
            raise ValueError('Original button atlas is truncated')
        records.append((metadata, pixels))
        provenance.append(source)
    (metadata, tiled), (_, shared_tiled) = records
    if shared_tiled != tiled:
        raise ValueError('Frontend and gameplay button artwork differ')
    linear, addressing = untile_blocks(tiled, 64, 64, 64, 16, '8in16')
    if addressing['addressed_blocks'] != 4096 or addressing['maximum_addressed_end'] != 65536:
        raise ValueError('Original button atlas block mapping changed')

    # Font glyph records are independent evidence for the cells used by the
    # renderer. The first sixteen records use full 64px cells, including four
    # directional aliases for the D-pad. UV words represent fractions /4096.
    font, font_source = select_payload(root / 'frontend/frontend.str', 30, 'buttons.ffn', root)
    if digest(font) != 'a5a85c9ba9bf646eafcbcde11a5dc58226aca3de3cbdba4d7e1e6e7c2b1e5cc9':
        raise ValueError('Original button font identity changed')
    glyph_cells = [(3, 1), (2, 1), (2, 3), (0, 3), (3, 3), (1, 3),
                   (0, 0), (0, 0), (0, 0), (0, 0), (1, 2), (2, 2),
                   (0, 2), (3, 2), (0, 1), (1, 1)]
    glyphs = []
    for index, (column, row) in enumerate(glyph_cells):
        fields = struct.unpack_from('>12H', font, 0x434 + index * 24)
        uv = (column * 1024, row * 1024, (column + 1) * 1024, (row + 1) * 1024)
        if fields[:4] != (index << 8, 64, 64, 64) or fields[6:10] != uv:
            raise ValueError('Original button font glyph cell changed')
        glyphs.append(dict(index=index, cell=[column, row], uv_words=list(uv)))

    outputs = {'buttons.metadata': metadata, 'buttons.tiled': tiled, 'buttons.bc2': linear}
    evidence = dict(sources=provenance, font_source=font_source, glyphs=glyphs,
                    addressing=addressing, outputs={name: digest(value) for name, value in outputs.items()})
    outputs['original-buttons-manifest.json'] = (json.dumps(evidence, indent=2) + '\n').encode()
    OUT.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        destination = OUT / name
        if destination.exists():
            if destination.read_bytes() != value:
                raise ValueError('Existing original button fixture differs: ' + name)
        else:
            with destination.open('xb') as stream:
                stream.write(value)
    print('PASS: original menu/gameplay buttons share 4096 BC2 blocks; sixteen font glyph cells verified')


if __name__ == '__main__':
    main()
