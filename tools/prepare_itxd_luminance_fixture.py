"""Pin original flare4pt L8 bytes; decode with independent inverse addressing."""
from pathlib import Path
import hashlib,json,struct,sys
from extract_resource import select_payload
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_texture_decode import inverse_address

def prepare_family():
    root=ROOT/'Simpsons Game, The (USA)'
    originals=[
        ('simpsons_chars/simpsons_chars_global.str',16,'simpsons_chars_global_split12.itxd',
         'b460bb976c988f535360539fcdab2abb9d28c17da99e836175f4345e55fde5a1',0x528,'Arcs',
         '3872ff811165ee20d1ae911b49684121c28c81e07377a1187d2b5377adc94a39'),
        ('loc/loc/story_mode/story_mode_design.str',4,'Story_Mode_Design.itxd',
         'c7500761c70ec0d91fa9e314db9e280511bd33c06210365331e44cba82fd6849',0x428,'flash02',
         '2aec68007286f3f06587749dcc01432cd89ef5ea6676098b90d131d339aaa287'),
        ('loc/loc/story_mode/story_mode_design.str',5,'Story_Mode_Design_split2.itxd',
         '02af7edcf82b0b2b6ba7b4d799965c96b840fdf3c0a518fb5e1edb129c0c0b96',0x228,'smash04',
         'b8914b8fffc6c0b25f1b136a2214878625f2a562f8d1160c1bf96e63ee9ba59f'),
        ('loc/loc/story_mode/story_mode_design.str',6,'Story_Mode_Design_split3.itxd',
         'a85484c37a176b2e66efd775ffdcc960c66bc929dc58ff3004b71f3c7e61d6a9',0x1328,'Arc1',
         'a25980aee251f74aa549d13a82cab56d8924cebd0a483cd2711b763a322c3a91'),
    ]
    digest=lambda b:hashlib.sha256(b).hexdigest()
    cases=[];manifest=[]
    def add(name,w,h,descriptor,tiled,details):
        assert len(tiled)==w*h
        rgba=bytearray(w*h*4);seen=set()
        # Independent physical-address -> coordinate oracle, rather than
        # round-tripping through the native forward-address implementation.
        for at,value in enumerate(tiled):
            x,y=inverse_address(at,w,1)
            assert 0<=x<w and 0<=y<h and (x,y) not in seen
            seen.add((x,y));rgba[(y*w+x)*4:(y*w+x+1)*4]=bytes((value,value,value,255))
        assert len(seen)==w*h
        cases.append(struct.pack('<8I',w,h,*descriptor)+tiled+rgba)
        manifest.append(dict(name=name,width=w,height=h,descriptor=descriptor,
                             storage_sha256=digest(tiled),rgba_sha256=digest(rgba),**details))
    for source,entry,dictionary,source_hash,at,name,meta_hash in originals:
        data,provenance=select_payload(root/source,entry,dictionary,root)
        assert digest(data)==source_hash
        meta=data[at:at+256];assert digest(meta)==meta_hash and meta[16:80].split(b'\0')[0].decode()==name
        word=lambda o:struct.unpack_from('>I',meta,o)[0]
        w,h=word(0x84),word(0x88);descriptor=tuple(word(o) for o in range(0xe8,0x100,4))
        assert word(0xc4)==0x28000102 and word(0xb0)==0 and word(0xbc)==w*h
        assert descriptor==(0x80000002|((w//32)<<22),2,((h-1)<<13)|(w-1),0x1400,0,0x200)
        start=word(0xc0);assert start+w*h<=len(data)
        add(name,w,h,descriptor,data[start:start+w*h],dict(provenance=provenance,record=at,metadata_sha256=meta_hash))
    # Asymmetric patterns expose transposed dimensions, hard-coded pitch and
    # macro-tile aliasing; include both orientations and the maximum pitch.
    for w,h in [(64,128),(128,64),(128,512),(512,512),(2048,64),(64,2048)]:
        tiled=bytes((at*37+(at>>7)*19+(at>>11)*83)&255 for at in range(w*h))
        descriptor=(0x80000002|((w//32)<<22),2,((h-1)<<13)|(w-1),0x1400,0,0x200)
        add(f'asymmetric_{w}x{h}',w,h,descriptor,tiled,dict(synthetic=True))
    folder=ROOT/'build/itxd-luminance'
    outputs={'family.bin':struct.pack('<I',len(cases))+b''.join(cases),
             'family-manifest.json':(json.dumps(manifest,indent=2)+'\n').encode()}
    for name,value in outputs.items():
        path=folder/name
        if path.exists():assert path.read_bytes()==value,'Luminance family fixture changed: '+name
        else:path.write_bytes(value)
    print(f'PASS: {len(cases)} independently decoded L8 base fixtures, including four original pickup/effect textures')

def main():
    root=ROOT/'Simpsons Game, The (USA)'
    data,provenance=select_payload(root/'loc/loc/story_mode/story_mode_design.str',6,'Story_Mode_Design_split3.itxd',root)
    digest=lambda b:hashlib.sha256(b).hexdigest()
    assert digest(data)=='a85484c37a176b2e66efd775ffdcc960c66bc929dc58ff3004b71f3c7e61d6a9'
    meta=data[0x528:0x628]
    assert digest(meta)=='4e167ce3e87c331d0ad30ccf9f14f98c42f6b059c2b276b787a9499b76d7b4ff'
    assert meta[16:80].split(b'\0')[0]==b'flare4pt'
    word=lambda o:struct.unpack_from('>I',meta,o)[0]
    assert tuple(word(o) for o in (0x84,0x88,0xb0,0xbc,0xc0,0xc4))==(64,64,0,4096,0x1f000,0x28000102)
    assert tuple(word(o) for o in range(0xe8,0x100,4))==(0x80800002,2,0x7e03f,0x1400,0,0x200)
    # Descriptor swizzle bits1..12 are X,X,X,one; byte format has no endian swap.
    assert [(word(0xf4)>>(1+3*i))&7 for i in range(4)]==[0,0,0,5]
    tiled=data[0x1f000:0x20000];rgba=bytearray(16384);seen=set()
    for at,value in enumerate(tiled):
        x,y=inverse_address(at,64,1)
        assert 0<=x<64 and 0<=y<64 and (x,y) not in seen
        seen.add((x,y));rgba[(y*64+x)*4:(y*64+x+1)*4]=bytes((value,value,value,255))
    assert len(seen)==4096
    outputs={'flare4pt.tiled':tiled,'flare4pt.rgba':rgba,'flare4pt.metadata':meta}
    outputs['manifest.json']=(json.dumps(dict(provenance=provenance,record_offset=0x528,outputs={n:digest(b) for n,b in outputs.items()}),indent=2)+'\n').encode()
    folder=ROOT/'build/itxd-luminance';folder.mkdir(parents=True,exist_ok=True)
    for name,value in outputs.items():
        path=folder/name
        if path.exists():assert path.read_bytes()==value,'Luminance fixture changed: '+name
        else:path.write_bytes(value)
    print('PASS original flare4pt: 4096 independently addressed L8 texels, RRR1, no endian swap')
    prepare_family()
if __name__=='__main__':main()
