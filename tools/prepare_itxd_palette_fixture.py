"""Pin the original palette; independently invert tiled byte addresses."""
from pathlib import Path
import hashlib,json,struct,sys
from extract_resource import select_payload
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_texture_decode import inverse_address

def main():
    root=ROOT/'Simpsons Game, The (USA)'
    data,provenance=select_payload(root/'loc/loc.str',6,'loc_split4.itxd',root)
    digest=lambda value:hashlib.sha256(value).hexdigest()
    if digest(data)!='663277a5e672bcb4410e7ef4b6368db1edd6b988e8cbb4ae4c26e8e64c6b71cc':raise ValueError('Original palette dictionary differs')
    metadata=data[0x728:0x828];word=lambda o:struct.unpack_from('>I',metadata,o)[0]
    descriptor=tuple(word(o) for o in range(0xE8,0x100,4))
    if metadata[0x10:0x50].split(b'\0')[0]!=b'simpsons_palette' or descriptor!=(0x80800002,0x86,0x7E03F,0xC14,0,0x200):raise ValueError('Palette name/descriptor differs')
    if tuple(word(o) for o in (0x84,0x88,0xB0,0xBC,0xC0,0xC4))!=(64,64,0,16384,0x27000,0x18280186):raise ValueError('Palette extent/format differs')
    tiled=data[0x27000:0x2B000];linear=bytearray(16384);seen=set()
    for at in range(0,len(tiled),4):
        x,y=inverse_address(at,64,4)
        if not(0<=x<64 and 0<=y<64) or (x,y) in seen:raise ValueError('Inverse palette mapping is not bijective')
        seen.add((x,y));out=(y*64+x)*4
        # Stored ARGB bytes -> 8-in-32 -> components XYZW -> ZYXW selection.
        linear[out:out+4]=bytes(tiled[at+i] for i in (1,2,3,0))
    if len(seen)!=4096:raise ValueError('Palette inverse coverage incomplete')
    outputs={'simpsons_palette.tiled':tiled,'simpsons_palette.rgba':bytes(linear),
        'simpsons_palette.metadata':metadata,'loc_split4.itxd':data}
    evidence={'provenance':provenance,'record_offset':0x728,'descriptor':list(descriptor),
        'inverse_address_pixels':4096,'swizzle':'ZYXW after 8-in-32',
        'outputs':{name:digest(value) for name,value in outputs.items()}}
    outputs['manifest.json']=(json.dumps(evidence,indent=2)+'\n').encode()
    out=ROOT/'build/itxd-palette';out.mkdir(parents=True,exist_ok=True)
    for name,value in outputs.items():
        p=out/name
        if p.exists():
            if p.read_bytes()!=value:raise ValueError('Existing palette fixture differs: '+name)
        else:
            with p.open('xb') as stream:stream.write(value)
    print('PASS original simpsons_palette: 4096 inverse-addressed RGBA pixels, exact dictionary/metadata/format')
if __name__=='__main__':main()
