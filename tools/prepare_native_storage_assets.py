"""Prepare menu font fixtures from the user's retail frontend package.

Only this reader is distributed. Font metrics and decoded atlas bytes remain
under ignored build/native-assets; the original game resources are read-only.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from extract_resource import select_payload
from decode_itxd import untile_blocks, decode_linear

ROOT = Path(__file__).resolve().parents[1]
FONTS = (
    ('storage-font.dat', 'HighlanderStdMed3636', 36, 20, 'frontend_split19.itxd', 512, 512),
    ('storage-title.dat', 'HighlanderStdBold6060b', 60, 17, 'frontend_split16.itxd', 1024, 512),
)


def prepare(game_root):
    outputs = {}
    package = game_root / 'frontend/frontend.str'
    for output, name, point, entry, dictionary, width, height in FONTS:
        font, _ = select_payload(package, 30, name + '.ffn', game_root)
        pixels, _ = select_payload(package, entry, dictionary, game_root)
        if (font[:4] != b'FONT' or struct.unpack_from('>H', font, 8)[0] != 6
                or struct.unpack_from('>I', font, 12)[0] != 0x434):
            raise ValueError('Unsupported retail font header: ' + name)
        count = struct.unpack_from('>H', font, 10)[0]
        if count != 191 or struct.unpack_from('>I', font, 32)[0] != 0x434 + count * 24:
            raise ValueError('Retail font glyph table changed: ' + name)
        metadata = 0x928
        word = lambda at: struct.unpack_from('>I', pixels, metadata + at)[0]
        if (pixels[metadata + 16:metadata + 80].split(b'\0')[0].decode() != name
                or (word(0x84), word(0x88), word(0xBC), word(0xC4))
                != (width, height, width * height, 0x1A200153)
                or word(0xEC) != 0x53):
            raise ValueError('Retail font texture layout changed: ' + name)
        offset, size = word(0xC0), word(0xBC)
        linear, _ = untile_blocks(pixels[offset:offset + size], width // 4,
            height // 4, width // 4, 16, '8in16')
        rgba = decode_linear(linear, width, height, 19)
        data = bytearray(struct.pack('<8s4I', b'NSSFONT1', width, height, point, count))
        seen = set()
        for i in range(count):
            glyph = struct.unpack_from('>12H', font, 0x434 + i * 24)
            code = glyph[0] >> 8  # Retail FFN stores its byte character in the first lane.
            signed = lambda value: value - 65536 if value >= 32768 else value
            x, y, right, bottom = (value // 16 for value in glyph[6:10])
            if (glyph[0] & 255 or code in seen or any(value % 16 for value in glyph[6:10])
                    or right - x != glyph[1] or bottom - y != glyph[2]
                    or not (0 <= x < right <= width and 0 <= y < bottom <= height)):
                raise ValueError('Unsupported retail glyph metrics: ' + name)
            seen.add(code)
            data.extend(struct.pack('<I7i', code, glyph[1], glyph[2], glyph[3],
                signed(glyph[4]), signed(glyph[5]), x, y))
        if not all(code in seen for code in range(32, 127)):
            raise ValueError('Retail font does not cover printable ASCII: ' + name)
        data.extend(rgba)
        outputs[output] = bytes(data)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, required=True,
        help='Root of generated native-assets; storage-screen is appended')
    parser.add_argument('--game-root', type=Path, default=ROOT / 'Simpsons Game, The (USA)')
    args = parser.parse_args()
    outputs = prepare(args.game_root)
    target = args.output_root / 'storage-screen'
    target.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        path = target / name
        if not path.exists() or path.read_bytes() != value:
            path.write_bytes(value)
        print('Native Storage menu font:', path, hashlib.sha256(value).hexdigest())


if __name__ == '__main__':
    main()
