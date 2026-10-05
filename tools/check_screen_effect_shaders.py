"""Compare native screen-effect shaders with the original microcode reference.

Builds fixture cases from tools/xenos_pixel_reference.py executing the original
PS records, then runs tests/test_screen_effect_shaders.cpp on WARP or hardware.
Textures are slowly varying float data so bilinear subtexel quantization
stays well inside the tolerance; depth is sampled at exact texel centers.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
import xenos_pixel_reference as reference

ROOT = Path(__file__).resolve().parents[1]
# Native entry index, original record, stage-0 kind, stage-1 kind.
SHADERS = (
    ('PSDof', 0x821517A0, 'color', 'depth'),
    ('PSBlur', 0x82151C50, 'color', None),
    ('PSBloom', 0x82151DB8, 'color', 'second'),
    ('PSFogLinear', 0x82153E40, 'depth', None),
    ('PSFogExp', 0x821540A0, 'depth', None),
    ('PSFogExp2', 0x82154330, 'depth', None),
    ('PSSat', 0x821557C8, 'color', None),
    ('PSFlatEffect', 0x821524C8, None, None),
    ('PSModulatedFlat', 0x82152318, None, 'second'),
)
WIDTH, HEIGHT = 48, 24


class Texture:
    def __init__(self, width, height, channels, linear, fill):
        self.width, self.height, self.channels, self.linear = width, height, channels, linear
        self.data = [fill(x, y) for y in range(height) for x in range(width)]

    def texel(self, x, y):
        x = min(max(x, 0), self.width - 1)
        y = min(max(y, 0), self.height - 1)
        value = self.data[y * self.width + x]
        return tuple(value) if self.channels == 4 else (value, 0.0, 0.0, 1.0)

    def sample(self, u, v):
        if not self.linear:
            return self.texel(math.floor(u * self.width), math.floor(v * self.height))
        x, y = u * self.width - 0.5, v * self.height - 0.5
        ix, iy = math.floor(x), math.floor(y)
        fx, fy = x - ix, y - iy
        a, b = self.texel(ix, iy), self.texel(ix + 1, iy)
        c, d = self.texel(ix, iy + 1), self.texel(ix + 1, iy + 1)
        return tuple((a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (c[i] * (1 - fx) + d[i] * fx) * fy for i in range(4))


def scene(seed):
    phase = [seed * 0.37 + k for k in range(4)]
    return Texture(40, 20, 4, True, lambda x, y: [0.5 + 0.3 * math.sin(0.021 * x * (k + 1) + 0.033 * y + phase[k]) + 0.004 * x for k in range(4)])


def depth(seed, far_band):
    def value(x, y):
        if far_band and y < 4:
            return 0.0  # Far plane with the reversed depth viewport.
        return 0.15 + 0.8 * ((x * 7 + y * 13 + seed * 5) % 97) / 97.0
    return Texture(WIDTH, HEIGHT, 1, False, value)


def query(seed):
    return Texture(64, 8, 4, False, lambda x, y: [((x * 3 + y * 5 + seed) % 23) / 22.0, 0.3, 0.6, 1.0])


def cases(image):
    rng = random.Random(20260927)
    for name, address, stage0, stage1 in SHADERS:
        shader = reference.record(image, address)
        for seed in range(7 if name == 'PSDof' else 3):
            c = {i: tuple(rng.uniform(-1, 1) for _ in range(4)) for i in range(10)}
            if name == 'PSDof':
                c[1] = (rng.uniform(0.04, 0.16), rng.uniform(0.04, 0.16), rng.uniform(0.2, 0.8), rng.uniform(0.5, 6.0))
                c[0] = tuple(rng.uniform(0, 1.2) for _ in range(3)) + (rng.uniform(0, 1),)
                if seed == 3:
                    c[1] = (0.1, 0.1, math.inf, 0.0)  # Original zero focus distance.
                elif seed == 6:
                    c[1] = (0.1, 0.1, math.inf, math.nan)  # Both distance/range zero: Inf + 0/0.
                elif seed >= 4:
                    c[1] = (0.1, 0.1, 0.5, math.inf if seed == 4 else -math.inf)
            elif name == 'PSBloom':
                c[1] = tuple(rng.uniform(0, 0.6) for _ in range(3)) + (rng.uniform(0, 1),)
                # Modulate coordinates at the 64x8 query texel centers.
                c[2] = (rng.uniform(0.04, 0.16), rng.uniform(0.04, 0.16), (rng.randrange(64) + 0.5) / 64, (rng.randrange(8) + 0.5) / 8)
                c[0] = tuple(rng.uniform(0, 1.5) for _ in range(3)) + (rng.uniform(0, 1),)
            elif name == 'PSModulatedFlat':
                c[1] = ((rng.randrange(64) + 0.5) / 64, (rng.randrange(8) + 0.5) / 8, 0.0, 0.0)
            elif name.startswith('PSFog'):
                near, far = rng.uniform(0.5, 2.0), rng.uniform(50, 400)
                c[2] = (0.0, far, near * far, far - near)
                c[1] = (rng.uniform(0, 20), rng.uniform(0.005, 0.05), rng.uniform(0, 0.2), rng.uniform(0.5, 3.0))
                c[0] = tuple(rng.uniform(0, 1) for _ in range(3)) + (rng.uniform(0.2, 1),)
            textures = {}
            if stage0 == 'color':
                textures[0] = scene(seed)
            if stage0 == 'depth' or stage1 == 'depth':
                textures['depth'] = depth(seed, True)
                if name == 'PSDof' and seed >= 4:
                    # Exact focus pixels exercise 0 * infinity, between fully
                    # blurred pixels and the original sharp far-plane band.
                    textures['depth'] = Texture(WIDTH, HEIGHT, 1, False,
                        lambda x, y: 0.0 if y < 4 else (0.5, 0.25, 0.75)[x % 3])
            if stage1 == 'second':
                textures[1] = query(seed)
            stage_map = {0: textures.get('depth') if stage0 == 'depth' else textures.get(0),
                         1: textures.get('depth') if stage1 == 'depth' else textures.get(1)}
            expected = []
            for y in range(HEIGHT):
                for x in range(WIDTH):
                    uv = ((x + 0.5) / WIDTH, (y + 0.5) / HEIGHT)
                    expected.append(reference.run(shader, c, lambda stage, u, v: stage_map[stage].sample(u, v), uv))
            yield SHADERS.index((name, address, stage0, stage1)), c, textures, expected


def write(path: Path, image: bytes):
    out = bytearray(b'SEFX')
    rows = list(cases(image))
    out += struct.pack('<III', 1, len(rows), WIDTH | (HEIGHT << 16))
    for index, c, textures, expected in rows:
        out += struct.pack('<I', index)
        for i in range(10):
            out += struct.pack('<4f', *c[i])
        for key in (0, 1, 'depth'):
            texture = textures.get(key)
            if texture is None:
                out += struct.pack('<4I', 0, 0, 0, 0)
                continue
            out += struct.pack('<4I', texture.width, texture.height, texture.channels, int(texture.linear))
            for value in texture.data:
                out += struct.pack('<%df' % texture.channels, *(value if texture.channels == 4 else (value,)))
        for value in expected:
            out += struct.pack('<4f', *value)
    path.write_bytes(bytes(out))
    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    with tempfile.TemporaryDirectory() as folder:
        fixture = Path(folder) / 'screen-effects.fixture'
        count = write(fixture, image)
        print('fixture cases', count, flush=True)
        command = [str(args.executable), str(fixture)] + (['--hardware'] if args.hardware else [])
        return subprocess.run(command).returncode


if __name__ == '__main__':
    raise SystemExit(main())
