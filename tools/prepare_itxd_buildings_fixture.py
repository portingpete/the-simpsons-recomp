"""Original buildings2 RGBA8: three independently inverse-addressed levels."""
from pathlib import Path
import hashlib
import json
import struct
import sys
from extract_resource import select_payload
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_texture_decode import inverse_address

def main():
    root = ROOT/'Simpsons Game, The (USA)'
    data,provenance = select_payload(root/'loc/loc/zone01.str',3,'zone01_split3.itxd',root)
    assert hashlib.sha256(data).hexdigest() == '910d84b808cb278ef088ac4527cd510ecaed718ec53ccac0db89608cfb3dc9dd'
    meta = data[0x728:0x828]
    word = lambda o: struct.unpack_from('>I',meta,o)[0]
    assert meta[16:80].split(b'\0')[0] == b'loc_buildings2_dualtone'
    assert tuple(word(o) for o in (0x84,0x88,0xb0,0xbc,0xc4)) == (64,64,0,0x6000,0x18280186)
    d = tuple(word(o) for o in range(0xe8,0x100,4))
    assert d == (0x80800002,0x86,0x7e03f,0xc14,0x80,0x4a00)
    start = word(0xc0);tiled = data[start:start+0x6000]
    assert len(tiled) == 0x6000
    layout = [(0,0x4000,64,0),(0x4000,0x1000,32,0),(0x5000,0x1000,32,16)]
    levels = []
    for level,(start,length,pitch,ox) in enumerate(layout):
        n = 64 >> level;pixels = {}
        for at in range(0,length,4):
            x,y = inverse_address(at,pitch,4);x -= ox
            if 0 <= x < n and 0 <= y < n:
                assert (x,y) not in pixels
                pixels[x,y] = bytes(tiled[start+at+i] for i in (1,2,3,0))
        assert len(pixels) == n*n
        levels.append(b''.join(pixels[x,y] for y in range(n) for x in range(n)))
    folder = ROOT/'build/itxd-rgba';folder.mkdir(exist_ok=True)
    manifest = dict(provenance=provenance,layout=layout,storage_sha256=hashlib.sha256(tiled).hexdigest(),
                    level_sha256=[hashlib.sha256(v).hexdigest() for v in levels])
    for name,value in {'buildings.bin':struct.pack('<6I',*d)+tiled+b''.join(levels),
                       'buildings-manifest.json':(json.dumps(manifest,indent=2)+'\n').encode()}.items():
        path = folder/name
        if path.exists(): assert path.read_bytes() == value, 'Fixture changed: '+name
        else: path.write_bytes(value)
    print('PASS buildings2: three original inverse-addressed RGBA8 levels; '+manifest['storage_sha256'])

if __name__ == '__main__': main()
