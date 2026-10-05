"""Offline original-byte qualification for the two simpsons_edge shaders.

Static fields and reviewed expressions only. No instruction execution, runtime
translator, console backend, or assertion of console floating-point parity.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import analyze_screen_shaders as screen
import analyze_fourtap_shaders as four

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'analysis/native-edge-shaders.json'
need = screen.require
PROFILES = [
    ('VS', 0x8202E6F0, 320, 224, 96, 3,
     '2d87c7985ec49418be95929538748a6bab56b453b412e610dd696af66fcd72be'),
    ('PS', 0x8202E840, 1116, 708, 408, 5,
     'e68d07ff5b76c143384bff779c0247be3dba0a935291db119746bfece8a6d18c'),
]
CF = {
    'VS': [(0x30052003,0x1200),(0,0xC200),(0x1005,0x1200),
           (0,0xC400),(0x1006,0x2200),(0,0)],
    'PS': [(0x96005,0x1200),(0x200B,0x1200),(0x1F0008,0x7000),
           (0x0400600D,0x1000),(0x26013,0x5600),(0x6019,0x5600),
           (0x101F,0x5600),(0x1F0003,0x8400),(0,0xC400),(0x1020,0x2200)],
}
# Each annotation describes one authored issue slot. Co-issued scalar operands
# and the previous scalar result are read before either destination is written.
EXPRESSIONS = {
    5:'r1 = sample0(r0.xy).wxyz',
    6:'r0.w = r1.x >= c255.y', 7:'r1.x = r1.w >= c255.x',
    8:'r0.z = mad(-r1.x,c254.z,r1.w)',
    9:'r1.w = r0.z >= c254.y; r0.z = 1-r0.w',
    10:'r1.w = r0.z*r1.w',
    11:'r2.x = r1.w != c254.x; r1.w = -abs(r0.x)>0',
    12:'r1.z = r2.x+r1.z; r1.x = 1-r1.x',
    13:'predicate = r1.w == 0',
    14:'r2.x = rcp(c49.x)', 15:'r2.y = rcp(c48.x)',
    16:'r2.xy = r2.xy*c[20+aL].xy',
    17:'r2.xy = mad(r2.yx,c50.xx,r0.yx)',
    18:'r2 = sample0(r2.yx)',
    19:'r4.yz = r2.zw >= c255.xy; previous_scalar = r2.x',
    20:'r1.w = mad(-r4.y,c254.z,r2.z)',
    21:'r4.w = saturate(r0.w+r4.z); r3.x = -r1.y+previous_scalar',
    22:'r4.x = abs(r3.x) >= c255.w',
    23:'r3 = c255.zzzz-r4.xzyw',
    24:'r1.w = r1.w >= c254.y; previous_scalar = r3.z',
    25:'r1.w = mad(r3.y,r1.w,r2.y)',
    26:'r1.w = r1.w-r1.z; r1.x = r1.x*previous_scalar',
    27:'r2.x = saturate(r0.z+r1.x)',
    28:'r1.w = r1.x*abs(r1.w); previous_scalar = r2.x',
    29:'r2.x = r1.w >= c255.w; r1.w = r4.x*previous_scalar',
    30:'r2.x = r2.x-r1.w; r2.y = r3.w*r3.x',
    31:'r1.w = mad(r2.y,r2.x,r1.w)',
    32:'color0 = (r1.w,r1.w,r1.w,1)',
}

def schedule(code, name, pairs):
    fields=[]; slots=[]
    for pair in range(pairs):
        a,b,c=screen.words(code,pair*12)
        fields.extend([(a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)])
    need(fields==CF[name], 'Changed qualified control flow')
    for lo,hi in fields:
        if hi>>12 in (1,2,5):
            at,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(0<count<=6 and seq>>(count*2)==0, 'Invalid exec extent')
            slots.extend((at+i,bool(seq&(1<<(i*2))),bool(seq&(2<<(i*2)))) for i in range(count))
    need(sorted(set(i for i,_,_ in slots))==list(range(pairs,len(code)//12-1)), 'Incomplete scheduled slots')
    need(screen.words(code,len(code)-12)==(0x4E4A0001 if name=='VS' else 0x4E4A0000,0xC93B707B,0x7586E4CA), 'Changed trailer')
    return fields,list(dict.fromkeys(slots))

def alu(a,b,c):
    result={'vector_opcode':(c>>24)&31,'scalar_opcode':a>>26,
        'export':bool(a&0x8000),'vector_destination':a&63,'scalar_destination':(a>>8)&63,
        'vector_mask':(a>>16)&15,'scalar_mask':(a>>20)&15,
        'vector_clamp':bool(a&(1<<24)),'scalar_clamp':bool(a&(1<<25)),
        'absolute_constants':bool(a&128),'vector_destination_relative':bool(a&64),
        'scalar_destination_relative_or_export_zero':bool(a&0x4000),
        'predicated':bool(b&(1<<28)),'predicate_condition':bool(b&(1<<27)),
        'constant_address_register_relative':bool(b&(1<<29)),
        'constant_1_relative':bool(b&(1<<30)),'constant_0_relative':bool(b&(1<<31)),
        'sources':[]}
    for shift,sel,neg in ((16,31,26),(8,30,25),(0,29,24)):
        register=(c>>shift)&255;temporary=bool(c&(1<<sel))
        result['sources'].append({'bank':'temporary' if temporary else 'constant',
            'raw_register':register,'register':register&63 if temporary else register,
            'absolute_temporary':temporary and bool(register&128),
            'relative_temporary':temporary and bool(register&64),
            'components':screen.relative_swizzle((b>>shift)&255),'negated':bool(b&(1<<neg))})
    return result

def inspect(image):
    need(hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,'Original image changed')
    for path,digest in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(hashlib.sha256(path.read_bytes()).hexdigest()==digest,'Declarative field reference changed')
    shaders=[]
    for name,va,total,offset,size,pairs,digest in PROFILES:
        record=image[va-screen.BASE:va-screen.BASE+total]
        need(hashlib.sha256(record).hexdigest()==digest,'Original shader record changed')
        code=record[offset:offset+size];cf,slots=schedule(code,name,pairs);instructions=[]
        for slot,fetch,serial in slots:
            a,b,c=screen.words(code,slot*12)
            if fetch:
                # Shared field reader rejects predicates. Decode the common
                # fields with those bits masked, then explicitly retain them.
                fields=screen.decode_fetch((a,b&0x7FFFFFFF,c&0x7FFFFFFF))
                fields.update(predicated=bool(b>>31),predicate_condition=bool(c>>31))
            else: fields=alu(a,b,c)
            expression=EXPRESSIONS[slot] if name=='PS' else {
                3:'r1.xy = fetch POSITION.xy; r1.z = 1',
                4:'r0.xy = fetch TEXCOORD0.xy',
                5:'position = r1.xyzz = (x,y,1,1)',6:'interpolator0.xy = r0.xy'}[slot]
            instructions.append({'slot':slot,'va':f'{va+offset+slot*12:08X}',
                'words':[f'{x:08X}' for x in (a,b,c)],'serialize':serial,'fields':fields,'reviewed_expression':expression})
        shaders.append({'stage':name,'va':f'{va:08X}','bytes':total,'sha256':digest,
            'executable_offset':offset,'executable_bytes':size,'executable_sha256':hashlib.sha256(code).hexdigest(),
            'control_flow':[[f'{a:08X}',f'{b:04X}'] for a,b in cf], 'instructions':instructions})
    ps=image[0x8202E840-screen.BASE:0x8202E840-screen.BASE+1116]
    loop=struct.unpack_from('>12I',ps,0x230)
    need(loop==(0,1,1,0,0x1C,0x01FC0010,0,0,0x239C0001,0x00010004,0,0),'Changed original constant metadata')
    literal=struct.unpack_from('>16I',ps,644)
    need(literal==(0,0,0,0,0,0,0,0,0,0x3E570A3D,0x3F000000,0,0x3EEB851F,0x3F19999A,0x3F800000,0x38D1B717),'Changed literal constants')
    return {'image_sha256':screen.IMAGE_SHA256,'reference_sha256':{'ucode':four.UCODE_SHA,'xenos':four.XENOS_SHA},
        'scope':'Static exact-record dataflow and native finite-UNORM transcription; not a GPU interpreter or console precision proof.',
        'loop':{'register':31,'constant_word':'00010004','count':4,'start':0,'step':1,
            'body_cf':3,'exit_cf':8,'repeat':False,'predicated_break':False,
            'mask':'Slot13 sets current predicate from r1.w==0; slots14..31 write only under true predicate.'},
        'literal_c252_to_c255':[f'{x:08X}' for x in literal],
        'parameters':{'TexelKernel':'PS c20..27; only c20..23 read by loop','TargetHeight':'PS c48.x',
            'TargetWidth':'PS c49.x','LineWidth':'PS c50.x','g_ColorSampler':'PS texture0'},
        'limits':['VS declaration association and runtime effect ownership require separate qualification.',
            'Native pixel fixtures cover finite RGB10A2, single-level point sampling and wrap/clamp.',
            'Console reciprocal, fused arithmetic, derivatives, rasterization and filtering precision are not established.',
            'No pixel depth export; VS emits literal position Z and W equal to one.'], 'shaders':shaders}

def self_test(image):
    checks=0
    def reject(fn):
        nonlocal checks
        try:fn()
        except ValueError:checks+=1;return
        raise ValueError('Changed edge shader was accepted')
    for name,va,total,offset,size,pairs,_ in PROFILES:
        code=image[va-screen.BASE+offset:va-screen.BASE+offset+size]
        for at in range(pairs*12):
            changed=bytearray(code);changed[at]^=1
            reject(lambda:schedule(changed,name,pairs))
        changed=bytearray(code);changed[-1]^=1;reject(lambda:schedule(changed,name,pairs))
    for va in (0x8202E6F0,0x8202E840,0x8202EBAC,0x8202EA94):
        changed=bytearray(image);changed[va-screen.BASE]^=1;reject(lambda:inspect(changed))
    return checks

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true')
    parser.add_argument('--verify',action='store_true');parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args();need(not(args.write and args.verify),'Choose write or verify')
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.self_test:print(f'PASS {self_test(image)} edge control-flow/identity mutation checks')
    if args.write:REPORT.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    elif args.verify:need(json.loads(REPORT.read_text(encoding="utf-8"))==report,'Saved edge shader evidence differs')
    else:print(json.dumps(report,indent=2))
    print('PASS exact edge VS/PS records, literal and loop constants, 32 static issue slots')
if __name__=='__main__':main()
