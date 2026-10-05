"""Pin two authored cache-equivalent hud_target_center texture sources."""
from pathlib import Path
import hashlib,json,struct
from extract_resource import select_payload
from decode_itxd import untile_blocks
ROOT=Path(__file__).resolve().parents[1]
def main():
    root=ROOT/'Simpsons Game, The (USA)';out=ROOT/'build/itxd-cache'
    sources=[('frontend/frontend_global.str',1,'frontend_global.itxd',0x128,'3e4909a83d0dc05698c43567aea90ae0838b5c21b03ecfc326d30431ad4c033f'),
             ('simpsons_chars/simpsons_chars_global.str',10,'simpsons_chars_global_split6.itxd',0x28,'2628693d000c5b1e43c3faed887d4511f03b459b5354e7c712f51f8b69b9f220')]
    records=[];provenance=[]
    for path,entry,name,offset,expected in sources:
        d,p=select_payload(root/path,entry,name,root)
        if hashlib.sha256(d).hexdigest()!=expected:raise ValueError('Cache fixture source changed')
        m=d[offset:offset+256];w=lambda at:struct.unpack_from('>I',m,at)[0]
        pixels=d[w(0xC0):w(0xC0)+w(0xBC)]
        if m[0x10:0x50].split(b'\0')[0]!=b'hud_target_center' or w(0x5C)!=0x959239C5:raise ValueError('Cache fixture identity changed')
        if (w(0x84),w(0x88),w(0xBC),w(0xC4),w(0xB0))!=(32,32,16384,0x1A200153,0):raise ValueError('Cache fixture format changed')
        records.append((m,pixels));provenance.append(p)
    (a,pixels),(b,other)=records
    if pixels!=other or a[0x10:0x50]!=b[0x10:0x50] or a[0xE8:]!=b[0xE8:]:raise ValueError('Authored cached pixels/descriptors differ')
    diff=[i for i in range(0x7C,256) if not(0xAC<=i<0xB0 or 0xC0<=i<0xC4) and a[i]!=b[i]]
    if diff!=[0xA8,0xAA] or a[0xA8:0xAC]!=bytes.fromhex('01001000') or b[0xA8:0xAC]!=bytes(4):raise ValueError('Authored metadata difference changed')
    linear,addressing=untile_blocks(pixels,8,8,32,16,'8in16')
    outputs={'hud_target_center.metadata':a,'hud_target_center.alternate':b,
             'hud_target_center.tiled':pixels,'hud_target_center.bc2':linear}
    outputs['manifest.json']=(json.dumps({'sources':provenance,'difference_offsets':diff,'addressing':addressing,
        'outputs':{k:hashlib.sha256(v).hexdigest() for k,v in outputs.items()}},indent=2)+'\n').encode()
    out.mkdir(parents=True,exist_ok=True)
    for name,value in outputs.items():
        path=out/name
        if path.exists():
            if path.read_bytes()!=value:raise ValueError('Existing cache fixture differs: '+name)
        else:
            with path.open('xb') as f:f.write(value)
    print('PASS: two original cache-equivalent textures; exact pixels/descriptors, one authored metadata word differs')
if __name__=='__main__':main()
