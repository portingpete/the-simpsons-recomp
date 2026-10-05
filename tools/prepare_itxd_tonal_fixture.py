"""Pinned tonal-swirl RGBA8 fixture, independently inverse-addressed per mip."""
from pathlib import Path
import hashlib
import json
import struct
import sys
from extract_resource import select_payload
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from test_texture_decode import inverse_address
DESCRIPTOR = (0x82000002,0x86,0x1fe0ff,0xc14,0x100,0x40a00)
LAYOUT = [(0,0x40000,256,0),(0x40000,0x10000,128,0),
          (0x50000,0x4000,64,0),(0x54000,0x1000,32,0),(0x55000,0x1000,32,16)]

def main():
    root = ROOT/'Simpsons Game, The (USA)'
    copies = []
    sources = [(2,2,0x228,'42dd77e6789af072ed2e3e0753cf105bb15699dae44cd4cde6c62d1d189e9f01'),
               (4,4,0x28,'9a618483a8e1616e6ba4d206a186c4cec571f1ef90dcedd413c6e44b75d8ea07')]
    provenance = []
    for zone,entry,offset,digest in sources:
        data,p = select_payload(root/('loc/loc/zone%02d.str'%zone),entry,
                                'zone%02d_split%d.itxd'%(zone,entry),root)
        assert hashlib.sha256(data).hexdigest() == digest
        meta = data[offset:offset+256]
        word = lambda o: struct.unpack_from('>I',meta,o)[0]
        assert meta[16:80].split(b'\0')[0] == b'loc_tonal_swirl'
        assert tuple(word(o) for o in (0x84,0x88,0xb0,0xbc,0xc4)) == (256,256,0,0x56000,0x18280186)
        assert tuple(word(o) for o in range(0xe8,0x100,4)) == DESCRIPTOR
        start,length = word(0xc0),word(0xbc)
        assert start+length <= len(data)
        copies.append(data[start:start+length]);provenance.append(p)
    assert copies[0] == copies[1]
    tiled = copies[0]
    levels = []
    for level,(start,length,pitch,ox) in enumerate(LAYOUT):
        n = 256 >> level
        pixels = {}
        for at in range(0,length,4):
            x,y = inverse_address(at,pitch,4);x -= ox
            if 0 <= x < n and 0 <= y < n:
                assert (x,y) not in pixels
                pixels[x,y] = bytes(tiled[start+at+i] for i in (1,2,3,0))
        assert len(pixels) == n*n
        levels.append(b''.join(pixels[x,y] for y in range(n) for x in range(n)))
    folder = ROOT/'build/itxd-rgba';folder.mkdir(exist_ok=True)
    manifest = dict(provenance=provenance,layout=LAYOUT,storage_sha256=hashlib.sha256(tiled).hexdigest(),
                    prefix512=tiled[:512].hex(),level_sha256=[hashlib.sha256(v).hexdigest() for v in levels])
    output = struct.pack('<6I',*DESCRIPTOR)+tiled+b''.join(levels)
    for name,value in {'tonal.bin':output,'tonal-manifest.json':(json.dumps(manifest,indent=2)+'\n').encode()}.items():
        path = folder/name
        if path.exists(): assert path.read_bytes() == value, 'Fixture changed: '+name
        else: path.write_bytes(value)
    print('PASS tonal-swirl: two identical original copies, five inverse-addressed RGBA8 levels; '+manifest['storage_sha256'])

if __name__ == '__main__': main()
