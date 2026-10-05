"""Bounded offline inspector for two pinned FourTapBlend shader records.

No GPU command stream, runtime translator, reflection/caller association, or
original writes. JSON is deterministic; --verify reads the saved report.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'analysis/native-fourtap-shaders.json'
UCODE = Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/format/ucode.h')
UCODE_SHA = 'e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb'
XENOS = UCODE.parents[1] / 'xenos.h'
XENOS_SHA = '7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227'
PROFILES = {
    'VSFourTap': (0x820B8F08,0x114,0xA8,
        '8dcb727e02d552e6ca8d18b09ffc57d11856c8d418d962d00b24c914e11a7347',3,0x4E4A0001),
    'PSFourTap': (0x820B90D4,0x188,0x84,
        '2c71ebc59531050284a4473619cb2b810d2854c65ad0dcf483577b107e7c7ea3',2,0x4E4A0000),
}
require = screen.require

def schedule(code, pair_count, trailer):
    require(len(code)%12==0 and 0<pair_count<len(code)//12-1,'Malformed instruction window')
    result, executed = [], []
    ended = False
    for pair in range(pair_count):
        a,b,c = screen.words(code,pair*12)
        for lo,hi in ((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)):
            op=hi>>12
            require(not ended or (lo==0 and hi==0),'Non-NOP after EXEC_END')
            if op in (1,2):
                address,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
                require(not lo&0x8000 and hi&0x0FFC==0x200,'Unproved EXEC control flags')
                require(0<count<=6 and sequence>>(2*count)==0,'Malformed EXEC count/sequence')
                require(pair_count<=address and address+count<=len(code)//12-1,'EXEC overlaps CF/trailer')
                result.append({'opcode':op,'name':'EXEC_END' if op==2 else 'EXEC',
                    'address':address,'count':count,'sequence':sequence,
                    'vertex_cache_hint_hi':lo>>28,'vertex_cache_hint_lo':hi&3,
                    'predicate_clean':True,'yield':False,'address_mode':0,
                    'raw':[f'{lo:08X}',f'{hi:04X}']})
                for i in range(count):
                    flags=(sequence>>(i*2))&3
                    executed.append((address+i,bool(flags&1),bool(flags&2)))
                ended=op==2
            elif op==12:
                require(lo==0 and hi in (0xC200,0xC400),'Unproved ALLOC shape')
                result.append({'opcode':12,'name':'ALLOC','type':(hi>>9)&3,'size_field':0,
                               'raw':[f'{lo:08X}',f'{hi:04X}']})
            else:
                require(op==0 and lo==0 and hi==0,'Unsupported control flow')
                result.append({'opcode':0,'name':'NOP','raw':['00000000','0000']})
    slots=[e[0] for e in executed]
    require(ended and sorted(slots)==list(range(pair_count,len(code)//12-1)) and len(set(slots))==len(slots),
            'Unaccounted or repeated scheduled slot')
    require(screen.words(code,len(code)-12)==(trailer,0x2F199EE6,0xED5862DD),'Changed unexecuted trailer')
    return result,executed

def alu(raw):
    a,b,c=screen.triplet(raw)
    op=(c>>24)&31
    require(op in (1,2,11),'Unsupported ALU operation')
    require(a&0x03007FC0==0 and a>>26==50 and (a>>20)&15==0 and b>>24==0,
            'Unproved ALU modifiers/scalar work/relative addressing')
    require((a>>16)&15,'Empty vector result')
    sources=[]
    for operand,(shift,selector) in enumerate(((16,31),(8,30),(0,29))[:3 if op==11 else 2],1):
        register=(c>>shift)&255
        require(register<64,'Unsupported register or temporary source modifier')
        swizzle=(b>>shift)&255
        sources.append({'operand':operand,'bank':'temporary' if c&(1<<selector) else 'constant',
                        'register':register,'relative_swizzle':f'{swizzle:02X}',
                        'components':screen.relative_swizzle(swizzle)})
    if op!=11:
        require(c&0x200000FF==0 and b&255==0,'Unexpected unused third source')
    return {'kind':'alu','operation':{1:'MUL',2:'MAX',11:'MULADD'}[op],
            'destination':a&63,'export':bool(a&0x8000),'mask':(a>>16)&15,
            'sources':sources,'scalar_opcode':50,'scalar_mask':0,'clamp':False,
            'negate':False,'absolute':False,'relative_addressing':False,'predicated':False}

def fetch(raw):
    decoded=screen.decode_fetch(raw)
    a,b,c=raw
    if decoded['kind']=='vertex_fetch':
        require(a&0xF8000000==0 and a&(1<<19) and b&0xFFFFF000==0 and c==0,
                'Unproved vertex fetch index/conversion/layout flags')
        decoded.update({'prefetch_count_field':0,'must_be_one':1,'signed':False,
            'normalized_field':True,'signed_rf_mode':0,'index_rounded':False,'exp_adjust':0,
            'mini_fetch':False,'format_stride_offset_are_placeholders':True})
    else:
        require(a&~((63<<5)|(63<<12))==0x10080001 and b==0x1F1FF688 and c==0x00004000,
                'Unproved texture fetch filters/LOD/offsets/modifiers')
        decoded.update({'sample_location_name':'centroid','lod_bias':0.0,
                        'offset_texels':[0.0,0.0,0.0],'unknown_bit30':False,
                        'arbitrary_filter_name':'k2x4Sym; field deprecated per declarative reference',
                        'inherited_sampler':True})
    return decoded

def record(data,name):
    require(name in PROFILES,'Unknown record')
    va,offset,size,digest,pairs,trailer=PROFILES[name]
    require(len(data)==offset+size and screen.words(data,4,2)==(offset,size),'Changed record extent/header')
    require(hashlib.sha256(data).hexdigest()==digest,'Changed original record bytes')
    code=data[offset:]
    cf,slots=schedule(code,pairs,trailer)
    instructions=[]
    for slot,is_fetch,serialize in slots:
        raw=screen.words(code,slot*12)
        decoded=fetch(raw) if is_fetch else alu(raw)
        instructions.append({'slot':slot,'address':f'{va+offset+slot*12:08X}',
            'words':[f'{word:08X}' for word in raw],'serialize':serialize,**decoded})
    return {'name':name,'address':f'{va:08X}','record_size':len(data),'sha256':digest,
            'instruction_offset':offset,'instruction_size':size,'control_flow':cf,
            'instructions':instructions,'unexecuted_trailer':[f'{x:08X}' for x in screen.words(code,len(code)-12)]}

def inspect(image):
    require(len(image)==screen.IMAGE_SIZE and hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,
            'Wrong original image identity')
    require(hashlib.sha256(UCODE.read_bytes()).hexdigest()==UCODE_SHA,'Changed declarative ucode reference')
    require(hashlib.sha256(XENOS.read_bytes()).hexdigest()==XENOS_SHA,'Changed declarative Xenos enum reference')
    records=[]
    for name,(va,offset,size,*_) in PROFILES.items():
        begin=va-screen.BASE;records.append(record(image[begin:begin+offset+size],name))
    return {'schema_version':1,'image_sha256':screen.IMAGE_SHA256,
        'authority':{'path':str(UCODE),'sha256':UCODE_SHA,
                     'enum_path':str(XENOS),'enum_sha256':XENOS_SHA},'records':records,
        'vertex_contract':{'fetch_order':['position.xyzw','tap0.xy','tap1.xy','tap2.xy','tap3.xy'],
            'temporary_writes':['r2.xyzw','r1.zw','r1.xy','r0.zw','r0.xy'],
            'exports':['position62=r2.xyzw','interpolator0.xy=r1.zw','interpolator1.xy=r1.xy',
                       'interpolator2.xy=r0.zw','interpolator3.xy=r0.xy'],
            'constant_registers':[],'position_transform':'none; all four components fetched',
            'native_semantics':'POSITION float4, TEXCOORD0..3 float2 label fetch order; original declaration association deferred'},
        'pixel_contract':{'texture_fetch_constant':0,'texture_count':1,'sample_count':4,
            'uv_sources':['r0.xy','r1.xy','r2.xy','r3.xy'],'sample_destinations':['r4','r1','r2','r0'],
            'constants':['c0.x','c1.x','c2.x','c3.x'],
            'scheduled_arithmetic':['r0=tap3*c3.x','r0=MULADD(tap2.wzyx,c2.x,r0.wzyx)',
                                    'r0=MULADD(tap1,c1.x,r0.wzyx)','color0=MULADD(tap0,c0.x,r0)'],
            'clamp_alpha_test_depth_export':False,'weights_normalized':False,
            'native_constant_layout':'b0: float4 weights[4], byte offsets0/16/32/48, X only',
            'native_resources':'t0 Texture2D<float4>, s0 externally supplied sampler; computed LOD Sample'},
        'qualification':{'executed_fetches':9,'executed_alus':9,'conditional_control_flow':False,
            'native_mad_policy':'precise mad, ordered; D3D11 permits fused or unfused hardware implementation',
            'limits':['No original caller/reflection/vertex declaration patch association.',
                      'No engine effect readiness, fixed-function state, original scene or console FP bit-identity claim.',
                      'Texture views/samplers/interpolation/raster policy remain caller inputs; initial fixture is single-sample.',
                      'Exceptional/subnormal FP and exact Xenos versus D3D texture-filter/LOD precision are not qualified.']}}

def self_test(image):
    checks=0
    def check(condition):
        nonlocal checks
        require(condition,'Four-tap self-test failed');checks+=1
    def rejects(fn):
        try:fn()
        except ValueError:check(True);return
        check(False)
    full=inspect(image)
    vs,ps=full['records']
    check(len(vs['instructions'])==10 and len(ps['instructions'])==8)
    check([i['slot'] for i in vs['instructions']]==list(range(3,13)))
    check([i['slot'] for i in ps['instructions']]==list(range(2,10)))
    check([i['destination_swizzle'] for i in vs['instructions'][:5]]==[[0,1,2,3],[7,7,0,1],[0,1,7,7],[7,7,0,1],[0,1,7,7]])
    check([i['sources'][1]['register'] for i in ps['instructions'][4:]]==[3,2,1,0])
    check([i['sources'][1]['components'] for i in ps['instructions'][4:]]==[[0,0,0,0]]*4)
    check(ps['instructions'][5]['sources'][0]['components']==[3,2,1,0])
    check(ps['instructions'][0]['arbitrary_filter']==0 and
          ps['instructions'][0]['arbitrary_filter_name'].startswith('k2x4Sym'))
    for name,(va,offset,size,*_) in PROFILES.items():
        original=image[va-screen.BASE:va-screen.BASE+offset+size]
        for at in (0,4,offset,offset+12,len(original)-1):
            mutated=bytearray(original);mutated[at]^=1
            rejects(lambda:record(mutated,name))
        rejects(lambda:record(original[:-1],name))
        rejects(lambda:record(original+b'\0',name))
    rejects(lambda:alu((0xC80F0000,0,0xC0000000))) # ADD outside bounded profile.
    rejects(lambda:alu((0xC90F0000,0x00006C00,0x81000300))) # clamp
    rejects(lambda:alu((0xC80F0000,0x10006C00,0x81000300))) # predicate
    rejects(lambda:fetch((0x10084001,0x1F1FF688,0x00014000))) # offset
    rejects(lambda:fetch((0x10084001,0x3F1FF688,0x00004000))) # register LOD
    va,off,size,_,pairs,trailer=PROFILES['PSFourTap']
    code=bytearray(image[va-screen.BASE+off:va-screen.BASE+off+size])
    code[0:4]=struct.pack('>I',0x0055400A)
    rejects(lambda:schedule(code,pairs,trailer))
    # Independent symbolic components through the decoded aliasing/swizzles.
    reg={0:['t3'+x for x in 'rgba'],1:['t1'+x for x in 'rgba'],
         2:['t2'+x for x in 'rgba'],4:['t0'+x for x in 'rgba']}
    for ins in ps['instructions'][4:]:
        src=[]
        for s in ins['sources']:
            value=reg[s['register']] if s['bank']=='temporary' else [f'w{s["register"]}']*4
            src.append([value[c] for c in s['components']])
        out=[f'({src[0][c]}*{src[1][c]})' for c in range(4)]
        if len(src)==3:out=[f'({out[c]}+{src[2][c]})' for c in range(4)]
        reg[ins['destination']]=out
    for c,label in enumerate('rgba'):
        check(reg[0][c]==f'((t0{label}*w0)+((t1{label}*w1)+((t2{label}*w2)+(t3{label}*w3))))')
    return checks

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    p.add_argument('--write',action='store_true')
    p.add_argument('--verify',action='store_true')
    p.add_argument('--self-test',action='store_true')
    args=p.parse_args();require(not(args.write and args.verify),'Choose write or verify')
    image=args.image.read_bytes();report=inspect(image)
    if args.self_test:print(f'PASS {self_test(image)} bounded shader semantic/mutation checks',file=sys.stderr)
    if args.verify:
        require(json.loads(REPORT.read_text(encoding="utf-8"))==report,'Saved report differs')
        print('PASS two pinned records, 18 scheduled instructions and complete CF',file=sys.stderr)
    elif args.write:REPORT.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    else:print(json.dumps(report,indent=2))

if __name__=='__main__':
    try:main()
    except (ValueError,OSError) as exc:
        print(f'four-tap: {exc}',file=sys.stderr);sys.exit(2)
