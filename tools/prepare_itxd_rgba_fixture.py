"""Original loc_candy_wall RGBA8 levels using independent inverse addressing."""
from pathlib import Path
import hashlib
import json
import struct
import sys
from extract_resource import select_payload
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_texture_decode import inverse_address

# Explicit layout from the original descriptor and texture/util.cpp contract.
LAYOUT = [(0,0x100000,512,0,0),(0x100000,0x40000,256,0,0),
          (0x140000,0x10000,128,0,0),(0x150000,0x4000,64,0,0),
          (0x154000,0x1000,32,0,0),(0x155000,0x1000,32,16,0)]
DESCRIPTOR = (0x84000002,0x86,0x3fe1ff,0xc14,0x140,0x100a00)

def main():
    root = ROOT / 'Simpsons Game, The (USA)'
    data, provenance = select_payload(root/'loc/loc/zone01.str',1,'zone01.itxd',root)
    assert hashlib.sha256(data).hexdigest() == '8f5157ae6b339386732d4b48f56b5d47d1513590445c46bd2b3b8f72542c7155'
    meta = data[0x28:0x128]
    word = lambda o: struct.unpack_from('>I',meta,o)[0]
    assert meta[16:80].split(b'\0')[0] == b'loc_candy_wall'
    assert (word(0x84),word(0x88),word(0xc4),word(0xb0)) == (512,512,0x18280186,0)
    assert tuple(word(o) for o in range(0xe8,0x100,4)) == DESCRIPTOR
    start, length = word(0xc0), word(0xbc)
    assert length == 0x156000 and start+length <= len(data)
    tiled = data[start:start+length]
    levels = []
    for level,(start,length,pitch,ox,oy) in enumerate(LAYOUT):
        n = 512 >> level
        pixels = {}
        for at in range(0,length,4):
            x,y = inverse_address(at,pitch,4)
            x,y = x-ox,y-oy
            if 0 <= x < n and 0 <= y < n:
                assert (x,y) not in pixels
                # 8-in-32 endian followed by descriptor ZYXW selection.
                pixels[x,y] = bytes(tiled[start+at+i] for i in (1,2,3,0))
        assert len(pixels) == n*n
        levels.append(b''.join(pixels[x,y] for y in range(n) for x in range(n)))
    folder = ROOT/'build/itxd-rgba'; folder.mkdir(exist_ok=True)
    output = struct.pack('<6I',*DESCRIPTOR)+tiled+b''.join(levels)
    manifest = dict(provenance=provenance,layout=LAYOUT,
                    storage_sha256=hashlib.sha256(tiled).hexdigest(),
                    level_sha256=[hashlib.sha256(v).hexdigest() for v in levels])
    for name,value in {'candy.bin':output,'manifest.json':(json.dumps(manifest,indent=2)+'\n').encode()}.items():
        path = folder/name
        if path.exists(): assert path.read_bytes() == value, f'Fixture changed: {name}'
        else: path.write_bytes(value)
    print('PASS original RGBA8 fixture: six independently addressed levels; '+manifest['storage_sha256'])

if __name__ == '__main__': main()
