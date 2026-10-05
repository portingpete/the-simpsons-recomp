"""Pinned original BC mip allocations and independent inverse-address fixtures.

All layout tables are recorded explicitly from the original descriptors and the
read-only texture/util.cpp layout contract. No native decoder output is used.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
from extract_resource import select_payload

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_texture_decode import inverse_address

DICTIONARIES = {
    4: ('Story_Mode_Design.itxd', 'c7500761c70ec0d91fa9e314db9e280511bd33c06210365331e44cba82fd6849'),
    6: ('Story_Mode_Design_split3.itxd', 'a85484c37a176b2e66efd775ffdcc960c66bc929dc58ff3004b71f3c7e61d6a9'),
}
# Per level: region start, region bytes, row pitch in blocks, origin X/Y blocks.
CANDY = [(0,32768,64,0,0),(32768,8192,32,0,0),(40960,8192,32,0,0),
         (49152,8192,32,0,0),(57344,8192,32,4,0)]
SHINE = [(0,16384,32,0,0),(16384,16384,32,0,0),(32768,16384,32,4,0)]
BENCH = [(0,8192,32,0,0),(8192,8192,32,0,0),(16384,8192,32,0,4)]
RIVER = [(0,262144,128,0,0),(262144,65536,64,0,0),(327680,16384,32,0,0),
         (344064,16384,32,0,0),(360448,16384,32,0,0),(376832,16384,32,4,0)]
ORIGINALS = [
    (6,0x2128,'loc_coloredcandychunk01','445c66d978ddf309b1507bc1dd41d95ecf900bee32fdeb569947e6f6db79144f',CANDY),
    (4,0x328,'shine','ad658755f675a1084d3e0875c7c7156e5abf8b580abc2af049cdfe4f66f51175',SHINE),
    (6,0x728,'loc_candycanebench','a5d61c2bd541b847aee529439d05ed4762be0d8172f38c6dabb5cecba227f732',BENCH),
    (4,0x728,'loc_riverbase_flow_nm','5571367a5742feb6bd4c6c66f3d89b9b912427c9990aae991740f508f4e4e09f',RIVER),
]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def inverse_levels(storage,w,h,size,layout):
    levels=[]
    for level,(start,length,pitch,ox,oy) in enumerate(layout):
        bw,bh=(max(1,w>>level)+3)//4,(max(1,h>>level)+3)//4
        blocks={}
        for offset in range(0,length,size):
            x,y=inverse_address(offset,pitch,size)
            x,y=x-ox,y-oy
            if 0<=x<bw and 0<=y<bh:
                assert (x,y) not in blocks
                blocks[x,y]=bytes(storage[start+offset+(i^1)] for i in range(size))
        assert len(blocks)==bw*bh
        levels.append(b''.join(blocks[x,y] for y in range(bh) for x in range(bw)))
    return levels

def main():
    reference=Path('K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/util.cpp')
    assert digest(reference.read_bytes())=='c16fb6f11fbfe4327d8e648353b8f10f661d5f3b7520ef831cfc67ce1569b81d'
    root=ROOT/'Simpsons Game, The (USA)'
    cases=[]
    for entry,record,name,meta_hash,layout in ORIGINALS:
        dictionary,source_hash=DICTIONARIES[entry]
        data,provenance=select_payload(root/'loc/loc/story_mode/story_mode_design.str',entry,dictionary,root)
        assert digest(data)==source_hash
        meta=data[record:record+256]
        assert digest(meta)==meta_hash and meta[16:80].split(b'\0')[0].decode()==name
        word=lambda o:struct.unpack_from('>I',meta,o)[0]
        w,h,fmt=word(0x84),word(0x88),word(0xc4)&255
        descriptor=tuple(word(o) for o in range(0xe8,0x100,4))
        assert word(0xb0)==0 and len(layout)==(descriptor[4]>>6)+1
        start,length=word(0xc0),word(0xbc)
        assert start+length<=len(data) and length==max(a+b for a,b,*_ in layout)
        tiled=data[start:start+length]
        levels=inverse_levels(tiled,w,h,8 if fmt==0x52 else 16,layout)
        cases.append((name,w,h,fmt,descriptor,tiled,levels,dict(provenance=provenance,record=record,layout=layout)))

    # Full packed tails, through 1x1, with asymmetric byte patterns. Tables cover
    # both orientations, all five tail placements, and both BC block sizes.
    for name,w,h,fmt,layout in [
        ('synthetic_square',256,256,0x52,CANDY+[(57344,8192,32,x,y) for x,y in [(2,0),(1,0),(0,2),(0,1)]]),
        ('synthetic_wide',128,64,0x54,[(a*2,b*2,p,x,y) for a,b,p,x,y in BENCH]+
         [(32768,16384,32,x,y) for x,y in [(0,2),(0,1),(4,0),(2,0),(1,0)]]),
    ]:
        size=8 if fmt==0x52 else 16
        tiled=bytes((i*73+(i>>9)*17+(i>>4)*11)&255 for i in range(max(a+b for a,b,*_ in layout)))
        descriptor=(0x80000002|((max(w,128)//32)<<22),fmt,((h-1)<<13)|(w-1),0xd10,(len(layout)-1)<<6,layout[1][0]|0xa00)
        levels=inverse_levels(tiled,w,h,size,layout)
        cases.append((name,w,h,fmt,descriptor,tiled,levels,dict(synthetic=True,layout=layout)))

    output=bytearray(struct.pack('<I',len(cases)));manifest=[]
    for name,w,h,fmt,descriptor,tiled,levels,details in cases:
        output.extend(struct.pack('<11I',w,h,fmt,*descriptor,len(tiled),len(levels)))
        output.extend(tiled)
        for level in levels:output.extend(struct.pack('<I',len(level))+level)
        manifest.append(dict(name=name,width=w,height=h,format=fmt,descriptor=descriptor,
                             storage_sha256=digest(tiled),level_sha256=[digest(v) for v in levels],**details))
    folder=ROOT/'build/itxd-mips';folder.mkdir(parents=True,exist_ok=True)
    for name,value in {'cases.bin':bytes(output),'manifest.json':(json.dumps(manifest,indent=2)+'\n').encode()}.items():
        path=folder/name
        if path.exists():assert path.read_bytes()==value, f'Existing fixture changed: {name}'
        else:
            with path.open('xb') as stream:stream.write(value)
    print(f'PASS: {len(cases)} BC mip fixtures; {sum(len(c[6]) for c in cases)} independent levels, pinned original data and full packed tails')

if __name__=='__main__':
    main()
