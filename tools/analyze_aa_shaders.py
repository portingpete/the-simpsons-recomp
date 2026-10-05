"""Static exact-record qualification of simpsons_aa, not simpsons_edgeAA.

This reports reviewed dataflow; it does not execute console instructions.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import analyze_edge_shaders as edge
import analyze_screen_shaders as screen
import analyze_fourtap_shaders as four

ROOT=Path(__file__).resolve().parents[1]
REPORT=ROOT/'analysis/native-aa-shaders.json'
need=screen.require
PROFILES=[
    ('VS',0x820301A0,316,220,96,3,'cb28bef1f4b0d9ab5603bdbf9c66bef12012731a5589bb3b22a2e004809a4314'),
    ('PS',0x820302EC,800,656,144,3,'aa1610b0e2b4ae380482229f0a7e97c11c724d5c3813e5bae56352605eb259c3')]
CF={'VS':[(0x30052003,0x1200),(0,0xC200),(0x1005,0x1200),(0,0xC400),(0x1006,0x2200),(0,0)],
    'PS':[(0x92003,0x1200),(0x1F0004,0x7000),(0x02405005,0x1200),(0x1F0002,0x8400),(0,0xC400),(0x100A,0x2200)]}
EXPRESSIONS={'VS':{3:'r1.xy = POSITION.xy; r1.z = 1',4:'r0.xy = TEXCOORD0.xy',
                      5:'position = r1.xyzz = (x,y,1,1)',6:'interpolator0.xy = r0.xy'},
    'PS':{3:'r1.xyz = sample0(r0.xy).zyx; alpha is not fetched',4:'no result masks; no state update',
          5:'r0.z = rcp(c50.x)',6:'r0.w = rcp(c49.x)',
          7:'r0.zw = mad(r0.wz,c[20+aL].yx,r0.yx)',
          8:'r2.xyz = sample0(r0.wz).xyz; alpha is not fetched',
          9:'r1.xyz = r2.zyx+r1.xyz',10:'color0.rgb = r1.zyx*c255.xxx; overlapping export W masks write alpha1'}}

def schedule(code,name,pairs):
    fields=[];slots=[]
    for p in range(pairs):
        a,b,c=screen.words(code,12*p)
        fields.extend([(a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)])
    need(fields==CF[name],'Changed original AA control flow')
    for lo,hi in fields:
        if hi>>12 in (1,2):
            at,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(0<count<=6 and sequence>>(2*count)==0,'Malformed AA issue sequence')
            slots.extend((at+i,bool(sequence&(1<<(2*i))),bool(sequence&(2<<(2*i)))) for i in range(count))
    need(sorted(i for i,_,_ in slots)==list(range(pairs,len(code)//12-1)),'Unaccounted AA issue slots')
    need(screen.words(code,len(code)-12)==(0x4E4A0001 if name=='VS' else 0x4E4A0000,0x34BA03FC,0x4CA766DA),'Changed AA trailer')
    return fields,slots

def inspect(image):
    need(hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,'Changed original image')
    for path,digest in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(hashlib.sha256(path.read_bytes()).hexdigest()==digest,'Changed declarative field reference')
    shaders=[]
    for name,va,total,offset,size,pairs,digest in PROFILES:
        record=image[va-screen.BASE:va-screen.BASE+total]
        need(hashlib.sha256(record).hexdigest()==digest,'Changed AA shader record')
        code=record[offset:offset+size];cf,slots=schedule(code,name,pairs)
        instructions=[]
        for i,fetch,serial in slots:
            words=screen.words(code,i*12)
            instructions.append({'slot':i,'words':[f'{w:08X}' for w in words],
                'serialize':serial,'fields':screen.decode_fetch(words) if fetch else edge.alu(*words),
                'reviewed_expression':EXPRESSIONS[name][i]})
        shaders.append({'stage':name,'address':f'{va:08X}','bytes':total,'sha256':digest,
            'executable_offset':offset,'executable_bytes':size,'executable_sha256':hashlib.sha256(code).hexdigest(),
            'control_flow':[[f'{lo:08X}',f'{hi:04X}'] for lo,hi in cf],'instructions':instructions})
    ps=image[0x302EC:0x302EC+800]
    loop=struct.unpack_from('>12I',ps,508)
    need(loop==(0,1,1,0,0x1C,0x01FC0010,0,0,0x239C0001,0x00010004,0,0),'Changed AA loop metadata')
    literals=struct.unpack_from('>16I',ps,592)
    need(literals==(0,)*12+(0x3E4CCCCD,0,0,0),'Changed AA literal0.2')
    body=image[0x2FA84:0x2FA84+6244]
    need(hashlib.sha256(body).hexdigest()=='d43160e340c2ca690a7c21d99d43ccabf63de624430091b3f6ffd4c76385fada','Changed AA effect body')
    bindings=[]
    for i in range(26):
        row=struct.unpack_from('>4I',body,5504+16*i)
        expected={9:(0x2C0012,0,0,0),12:(0x380018,0,50,0),13:(0x3C001A,0,49,0)}.get(i,(0,0,0,0))
        if 14<=i<=21:expected=(0x44001C+(i-14)*0x40002,0,20+i-14,0)
        need(row==expected,'Changed AA parameter binding')
        if row!=(0,0,0,0):bindings.append([f'{w:08X}' for w in row])
    return {'image_sha256':screen.IMAGE_SHA256,'reference_sha256':{'ucode':four.UCODE_SHA,'xenos':four.XENOS_SHA},
        'effect':'simpsons_aa / 8202FA78','shaders':shaders,'bindings':bindings,
        'loop':{'register':31,'word':'00010004','count':4,'start':0,'step':1,'body_cf':2,'exit_cf':4},
        'parameters':{'texture0':'private slot240, second scene copy','c20..27':'private kernel slots416..528; only first four read',
                      'c50.x':'TargetWidth at384','c49.x':'TargetHeight at400','KernelWidth':'original CPU value368, not used by this shader'},
        'native_expression':'RGB=(center+four axis-neighbor samples)*float32(0.2), alpha=1; add order follows original loop.',
        'limits':['Finite UNORM, normalized point/wrap, single-level native sampling only.',
                  'Console reciprocal/fused arithmetic, filtering, rasterization and output rounding parity are unproven.',
                  'No admission of the distinct simpsons_edgeAA effect.']}

def self_test(image):
    checks=0
    def reject(f):
        nonlocal checks
        try:f()
        except ValueError:checks+=1;return
        raise ValueError('Modified AA evidence accepted')
    for name,va,total,offset,size,pairs,_ in PROFILES:
        code=image[va-screen.BASE+offset:va-screen.BASE+offset+size]
        for at in range(pairs*12):
            changed=bytearray(code);changed[at]^=1;reject(lambda:schedule(changed,name,pairs))
        changed=bytearray(code);changed[-1]^=1;reject(lambda:schedule(changed,name,pairs))
    for at in (0x301A0,0x302EC,0x302EC+508,0x302EC+592,0x2FA84+5504):
        changed=bytearray(image);changed[at]^=1;reject(lambda:inspect(changed))
    return checks

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');parser.add_argument('--verify',action='store_true');parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args();need(not(args.write and args.verify),'Choose write or verify')
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.self_test:print(f'PASS {self_test(image)} AA identity/control-flow mutation checks')
    if args.write:REPORT.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    elif args.verify:need(json.loads(REPORT.read_text(encoding="utf-8"))==report,'Saved AA report differs')
    else:print(json.dumps(report,indent=2))
    print('PASS exact AA records, loop/literal constants and bindings; 12 static issue slots')
if __name__=='__main__':main()
