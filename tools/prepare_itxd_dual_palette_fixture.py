"""Reproduce the rejected dual palette using original bytes and inverse addressing."""
from pathlib import Path
import hashlib
import json
import struct
import sys
from extract_resource import select_payload
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from test_texture_decode import inverse_address

def main():
    root = ROOT/'Simpsons Game, The (USA)'
    data, provenance = select_payload(root/'loc/loc/story_mode/story_mode_design.str',
                                      6,'Story_Mode_Design_split3.itxd',root)
    assert hashlib.sha256(data).hexdigest() == 'a85484c37a176b2e66efd775ffdcc960c66bc929dc58ff3004b71f3c7e61d6a9'
    metadata = data[0x1528:0x1628]
    word = lambda o: struct.unpack_from('>I',metadata,o)[0]
    name = metadata[16:80].split(b'\0')[0].decode()
    descriptor = tuple(word(o) for o in range(0xe8,0x100,4))
    fields = dict(name=name,record_offset='0x1528',width=word(0x84),height=word(0x88),
                  format=hex(word(0xc4)),auxiliary=hex(word(0xb0)),
                  payload_offset=hex(word(0xc0)),payload_bytes=word(0xbc),
                  descriptor=[f'{v:08X}' for v in descriptor])
    print(json.dumps(fields,indent=2))
    assert name == 'dual_simpsons_palette'
    assert (word(0x84),word(0x88),word(0xc4),word(0xb0),word(0xbc)) == (64,64,0x18280186,0,16384)
    assert descriptor == (0x80800002,0x86,0x7e03f,0xc14,0,0x200)
    start,length = word(0xc0),word(0xbc)
    assert start+length <= len(data)
    tiled = data[start:start+length]
    linear = bytearray(length)
    seen = set()
    for at in range(0,length,4):
        x,y = inverse_address(at,64,4)
        assert 0 <= x < 64 and 0 <= y < 64 and (x,y) not in seen
        seen.add((x,y))
        linear[(y*64+x)*4:(y*64+x+1)*4] = bytes(tiled[at+i] for i in (1,2,3,0))
    assert len(seen) == 4096
    outputs = {'dual_simpsons_palette.tiled':tiled,'dual_simpsons_palette.rgba':bytes(linear),
               'dual_simpsons_palette.metadata':metadata}
    manifest = dict(provenance=provenance,fields=fields,
                    sha256={k:hashlib.sha256(v).hexdigest() for k,v in outputs.items()})
    outputs['manifest.json'] = (json.dumps(manifest,indent=2)+'\n').encode()
    folder = ROOT/'build/itxd-dual-palette';folder.mkdir(exist_ok=True)
    for name,value in outputs.items():
        path = folder/name
        if path.exists(): assert path.read_bytes() == value, f'Fixture changed: {name}'
        else: path.write_bytes(value)
    print('PASS: exact existing palette descriptor; 4096 independently addressed pixels')

if __name__ == '__main__': main()
