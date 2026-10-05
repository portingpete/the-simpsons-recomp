"""Exact offline rigid-pair fields and static HLSL source proof.

Only the selected rigid0003FFFC records are accepted. No original instruction
execution, runtime translation, build, output files or GPU commands.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
sys.dont_write_bytecode=True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

ROOT=Path(__file__).resolve().parents[1]
need=screen.require
PROFILES=(('VS',0x8200D3AC,896,536,360,4,'de5d6675d5d5828b6903a8ac633207d6637a53e66be1798b94a2a5e5ac5f07aa'),
          ('PS',0x8200D9E8,1996,976,1020,10,'8833406e527ab7d947d0e83a40bfc558139dc2885b9f4ab063198e3f2a6e7545'))
CF={'VS':((0xF0554004,0x1200),(0,0xC200),(0x4008,0x1200),(0,0xC400),
          (0x600C,0x1200),(0x6012,0x1200),(0x5018,0x2200),(0,0)),
    'PS':((0x600A,0x1200),(0x6010,0x1200),(0x5016,0x1000),(0x4012,0xB000),
          (0x401B,0x1000),(0x400A,0xB000),(0x0555601F,0x1200),(0x00956025,0x1200),
          (0x602B,0x1200),(0x2031,0x1200),(0x6033,0x1200),(0x1039,0x1000),
          (0x4011,0xB000),(0x0555603A,0x1200),(0x00956040,0x1200),(0x6046,0x1200),
          (0x204C,0x1200),(0x204E,0x1200),(0,0xC400),(0x4050,0x2200))}
FX_VA=0x8200CCB8
FX_SHA='99fb9340ac71f857aaa0b84578d01f51dbfd96981c87cb95d9850aa1a97bf812'
TAPS=((0,0,1),(1,0,2),(1,-1,2),(-1,-1,0),(0,-1,1),(-1,0,0),(1,1,2),(-1,1,0),(0,1,1))
DEPTH_REFERENCE=Path('K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/texture_cache.cpp')
DEPTH_REFERENCE_SHA='ebd1eb6fdcf509e7f7f9b476c24caeb8efaa1d7b80a05f51932bdb6963675d09'

def sha(b):return hashlib.sha256(b).hexdigest()
def word(b,a):return struct.unpack_from('>I',b,a)[0]
def record(image,profile):
    name,va,n,at,size,pairs,digest=profile;b=image[va-screen.BASE:va-screen.BASE+n]
    need(len(b)==n and sha(b)==digest,'Original rigid '+name+' record changed')
    header=(0x102A1101,0x218,0x168,0x24,0x74,0,0x198,0,0) if name=='VS' else (0x102A1100,0x390,0x43C,0x24,0x74,0x334,0x35C,0,0)
    need(struct.unpack_from('>9I',b)==header,'Original shader header changed')
    need(b[-12:]==bytes.fromhex(('4e4a0003' if name=='VS' else '4e4a0002')+'09a7ce5aa0f8c989'),'Original shader trailer changed')
    if name=='VS':
        need(struct.unpack_from('>4I',b,0x1C0)==(0x00100004,0x00003005,0x0000A006,0x00205007),'Original VS semantic association changed')
    else:
        need(struct.unpack_from('>16I',b,912)==(0x3EC7AE14,0x44800000,0,0,0x3A802008,0x3E000000,0x3C23D70A,0x3F666666,
            0x3F000000,0x3F800000,0x42000000,0x42800000,0x3F000000,0xBF800000,0x3E800000,0xBF000000),'Original literal bank changed')
    code=b[at:at+size];cf=[];slots=[]
    for i in range(pairs):
        a,x,y=screen.words(code,i*12);cf.extend(((a,x&65535),((x>>16|y<<16)&0xFFFFFFFF,y>>16)))
    need(tuple(cf)==CF[name],'Original rigid CF changed')
    for lo,hi in cf:
        if hi>>12 in (1,2):
            first,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(0<count<=6 and seq>>(2*count)==0,'Unqualified EXEC sequence')
            slots.extend((first+i,bool(seq&(1<<(2*i)))) for i in range(count))
    need([i for i,_ in slots]==list(range(pairs,size//12-1)),'Original rigid issue coverage differs')
    rows={}
    for i,fetch in slots:
        raw=screen.words(code,12*i);f=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        if not fetch:
            for k in ('absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero',
                      'predicated','predicate_condition','constant_address_register_relative','constant_0_relative','constant_1_relative'):
                need(not f[k],'Unqualified rigid ALU modifier '+k)
            need(all(not s['relative_temporary'] for s in f['sources']),'Unqualified relative temporary')
        rows[i]={'raw':raw,'fetch':fetch,'fields':f}
    if name=='VS':
        for i,destination,swizzle in ((4,4,[0,1,2,5]),(5,1,[0,1,2,7]),(6,2,[0,1,2,3]),(7,3,[0,1,7,7])):
            f=rows[i]['fields']
            need(f==dict(kind='vertex_fetch',source_register=0,destination_register=destination,destination_swizzle=swizzle,
                fetch_constant_index=95,source_component=0,format_field=0,stride_dwords=0,offset_field=0,runtime_declaration_patch_verified=False),
                'Original unpatched VS FETCH differs')
    else:
        for start,bank in ((31,1),(58,0)):
            for i,(x,y,channel) in enumerate(TAPS):
                f=rows[start+i]['fields']
                need(f['fetch_constant_index']==bank and [v for v in f['destination_swizzle'] if v!=7]==[channel] and
                    f['offset_fields']==[(x*2)&31,(y*2)&31,0] and f['source_components']==[0,1,1] and
                    [f[k] for k in ('mag_filter','min_filter','mip_filter','anisotropy','arbitrary_filter','volume_mag_filter','volume_min_filter')]==[3,3,3,7,0,3,3] and
                    f['fetch_valid_only'] and f['sample_location']==0 and f['lod_bias_field']==0,'Original 9-tap sample contract differs')
    return b,rows

def operand(f,i,stage,n=4):
    s=f['sources'][i];name=('r' if s['bank']=='temporary' else ('vc' if stage=='VS' else 'pc'))
    if s['bank']=='temporary':name+=str(s['register'])
    elif s['register']>=252:name='k'+str(s['register'])
    else:name+='['+str(s['register'])+']'
    components=''.join('xyzw'[v] for v in s['components'][:n]);value=name+'.'+components
    if s['absolute_temporary']:value='abs('+value+')'
    if s['negated']:value='-'+value
    return value

def scalar(row,stage='PS'):
    f=row['fields'];op=f['scalar_opcode'];s=f['sources'][2]
    sw=(row['raw'][1]&255);comp=s['components']
    # Scalar operands are W,X of the swizzled third source, not X,Y.
    def one(component):
        t=dict(f);t['sources']=[dict(x) for x in f['sources']];t['sources'][2]['components']=[component]
        return operand(t,2,stage,1)
    a,b=one(comp[3]),one(comp[0])
    if op in (8,25,40):
        need(not f['absolute_constants'] and not s['relative_temporary'] and
             not any(f[k] for k in ('constant_address_register_relative','constant_0_relative','constant_1_relative')),
             'Unqualified scalar operand modifiers')
    if op in (42,43,44,46,47):
        # UCODE split forms: opcode bit0, src3_sel and swizzle bits2..5
        # encode a TEMP index, not generic src3 bank/absolute/relative flags.
        need(not s['negated'] and not f['absolute_constants'],'Unqualified CONST scalar modifiers')
        need(not any(f[k] for k in ('constant_address_register_relative','constant_0_relative','constant_1_relative')),
             'Unqualified CONST scalar addressing')
        reg=(op&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)
        constant=row['raw'][2]&255
        a=('k'+str(constant) if constant>=252 else ('vc' if stage=='VS' else 'pc')+'['+str(constant)+']')+'.'+'xyzw'[((sw>>6)+3)&3]
        b='r'+str(reg)+'.'+'xyzw'[sw&3]
    expressions={0:lambda:a+'+'+b,1:lambda:a+'+ps',2:lambda:'rigidLegacyProduct('+a+','+b+')',
        3:lambda:a+'*ps',5:lambda:'max('+a+','+b+')',
        10:lambda:'('+a+'!=0?1.0:0.0)',11:lambda:'frac('+a+')',13:lambda:'floor('+a+')',
        14:lambda:'exp2('+a+')',16:lambda:'log2('+a+')',
        19:lambda:'rcp('+a+')',22:lambda:'rsqrt('+a+')',42:lambda:a+'*'+b,46:lambda:a+'-'+b,
        8:lambda:'('+a+'>0?1.0:0.0)',9:lambda:'('+a+'>=0?1.0:0.0)',25:lambda:a+'-'+b,40:lambda:'sqrt('+a+')',
        43:lambda:'rigidLegacyProduct('+a+','+b+')',44:lambda:a+'+'+b,47:lambda:a+'-'+b}
    if op in (28,29):return '('+a+('!=0' if op==28 else '>0')+')'
    need(op in expressions,'Unqualified scalar opcode '+str(op));return expressions[op]()

def issue(i,row,stage):
    f=row['fields'];out=['    { // slot'+str(i)]
    if row['fetch']:
        need(stage=='PS' and f['kind']=='texture_fetch','VS FETCHes are emitted in the reviewed input interface')
        slot=f['fetch_constant_index'];need(slot in (0,1),'Unqualified sampler')
        offs=[((v+16)%32-16)//2 for v in f['offset_fields']]
        need(all(v%2==0 for v in f['offset_fields']),'Non-integer texture offset')
        need(f['source_components'][:2]==[0,1] and f['normalized_coordinates'] and f['dimension_field']==1 and
             f['computed_lod'] and not f['register_lod'] and not f['register_gradients'],'Unqualified texture instruction')
        source='r'+str(f['source_register'])+'.xy'
        out+=['        precise float4 v=rigidShadowSample(shadow'+str(slot)+'.Sample(shadowSampler'+str(slot)+','+source+',int2('+str(offs[0])+','+str(offs[1])+')));']
        for lane,selector in enumerate(f['destination_swizzle']):
            if selector!=7:
                need(selector<4,'Unqualified texture component literal');out+=['        r'+str(f['destination_register'])+'.'+'xyzw'[lane]+'=v.'+'xyzw'[selector]+';']
    else:
        op=f['vector_opcode'];mask=f['vector_mask'];sop=f['scalar_opcode'];sm=f['scalar_mask']
        sources=[operand(f,j,stage) for j in range(3)]
        if mask:
            a,b,c=sources
            if op==0:expr=a+'+'+b
            elif op==1:
                if stage=='PS' and i==15:
                    need(a=='r0.xxxx' and b=='r3.xyzz' and mask==7 and
                         f['vector_destination']==4 and not f['export'] and sop==50,
                         'Original normal zero-product instruction changed')
                    expr='rigidLegacyMultiply('+a+','+b+')'
                else:expr=a+'*'+b
            elif op in (2,3):expr=('max' if op==2 else 'min')+'('+a+','+b+')'
            elif op==12: # Xenos CNDEv: src0 == 0 ? src1 : src2, per component.
                # DSTv is opcode28, not opcode12. Select rather than multiply
                # masks so an unselected value cannot contaminate the result.
                screen.require(not f['absolute_constants'],'Unqualified CNDE absolute modifiers')
                expr='float4('+','.join('('+a+').'+lane+'==0.0?('+b+').'+lane+':('+c+').'+lane
                                       for lane in 'xyzw')+')'
            elif op==6:expr='float4('+a+'>='+b+')' # UCODE kSge / SETGTEv.
            elif op==5:expr='float4('+a+'>'+b+')' # UCODE kSgt / SETGTVv.
            elif op==8:
                need(not f['absolute_constants'] and not f['sources'][0]['relative_temporary'] and
                     not any(f[k] for k in ('constant_address_register_relative','constant_0_relative','constant_1_relative')),
                     'Unqualified FRAC operand modifiers')
                expr='frac('+a+')'
            elif op==10:expr='floor('+a+')'
            elif op==11:expr='mad('+a+','+b+','+c+')'
            elif op in (15,16):expr='dot('+operand(f,0,stage,4 if op==15 else 3)+','+operand(f,1,stage,4 if op==15 else 3)+').xxxx'
            elif op==17:
                # DOT2ADD uses swizzled XY products and src2.X, not XZ/Y.
                # Preserve legacy zero products and separate addition rounding.
                out+=['        precise float4 products=rigidLegacyMultiply('+a+','+b+');',
                      '        precise float sum=products.x+products.y;']
                expr='(sum+'+operand(f,2,stage,1)+').xxxx'
            else:raise ValueError('Unqualified vector opcode '+str(op))
            if f['vector_clamp']:expr='saturate('+expr+')'
            out+=['        precise float4 v='+expr+';']
        if sop!=50:
            expr=scalar(row,stage)
            if sop in (28,29):out+=['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
            else:out+=['        precise float s='+('saturate('+expr+')' if f['scalar_clamp'] else expr)+';']
        if mask:
            target=('output'+str(f['vector_destination']) if f['export'] else 'r'+str(f['vector_destination']))
            sw=''.join('xyzw'[lane] for lane in range(4) if mask&(1<<lane))
            out+=['        '+target+'.'+sw+'=v.'+sw+';']
        if sm:
            need(not f['export'] and sop!=50,'Unqualified scalar write/export')
            sw=''.join('xyzw'[lane] for lane in range(4) if sm&(1<<lane))
            out+=['        r'+str(f['scalar_destination'])+'.'+sw+'=s.'+'x'*len(sw)+';']
        if sop!=50:out+=['        ps=s;']
        if sop in (28,29):out+=['        p0=predicate;']
    return '\n'.join(out+['    }'])

def shader_source(image):
    vs,vr=record(image,PROFILES[0]);ps,pr=record(image,PROFILES[1])
    lines=['// Exact selected rigid0003FFFC: VS8200D3AC / PS8200D9E8.',
        '// Offline static transcription; each co-issued result reads old registers.',
        'cbuffer RigidVertexConstants : register(b0) { float4 vc[30]; };',
        'cbuffer RigidPixelConstants : register(b0) { float4 pc[50]; };',
        'Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);',
        'SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);',
        '// PS slot15 uses the original SM3 multiplication rule: a zero/denormal',
        '// operand annihilates even infinity, producing positive zero. Keep rsqrt(0)',
        '// and the original vertex data; this is arithmetic, not a substitute normal.',
        'float rigidLegacyProduct(float a,float b) {',
        '    precise float product=a*b;',
        '    return ((asuint(a)&0x7F800000u)==0u||(asuint(b)&0x7F800000u)==0u)?0.0:product;',
        '}',
        'float4 rigidLegacyMultiply(float4 a,float4 b) {',
        '    return float4(rigidLegacyProduct(a.x,b.x),rigidLegacyProduct(a.y,b.y),',
        '        rigidLegacyProduct(a.z,b.z),rigidLegacyProduct(a.w,b.w));',
        '}',
        '// Original resource XYZW follows depth-format RRRR expansion. Only the',
        '// qualified native depth draw enables this adapter; default PS stays RGBA.',
        'float4 rigidShadowSample(float4 value) {',
        '#ifdef RIGID_NATIVE_D24FS8_DEPTH_RRRR',
        '    return value.xxxx;',
        '#else',
        '    return value;',
        '#endif',
        '}',
        'struct RigidInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1; float4 color:TEXCOORD2; float2 uv:TEXCOORD3; };',
        'struct RigidOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float4 characterShadow:TEXCOORD1;',
        '    float4 worldShadow:TEXCOORD2; float3 normal:TEXCOORD3; float4 color:TEXCOORD4; };',
        'RigidOutput VSRigid(RigidInput input) {',
        '    precise float4 r4=float4(input.position,1),r1=float4(input.normal,0),r2=input.color,r3=float4(input.uv,0,0),r0=0;',
        '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0;']
    lines.extend(issue(i,vr[i],'VS') for i in range(8,29))
    lines+=['    RigidOutput result;result.position=output62;result.uv=output0.xy;result.characterShadow=output1;',
        '    result.worldShadow=output2;result.normal=output3.xyz;result.color=output4;return result;','}',
        'float4 PSRigid(RigidOutput input):SV_Target0 {']
    literals=struct.unpack_from('>16I',ps,912)
    for reg in range(4):lines+=['    const float4 k'+str(252+reg)+'=asfloat(uint4('+','.join('0x%08Xu'%v for v in literals[4*reg:4*reg+4])+'));']
    lines+=['    precise float4 r0=float4(input.uv,0,0),r1=input.characterShadow,r2=input.worldShadow;',
        '    precise float4 r3=float4(input.normal,0),r4=input.color,r5=0,r6=0,r7=0,output0=0;',
        '    precise float ps=0;bool p0=false;']
    lines.extend(issue(i,pr[i],'PS') for i in range(10,27))
    lines+=['    [branch] if(p0) { // CF3: float c47.x != 0 enables shadow work.']
    lines.extend(issue(i,pr[i],'PS') for i in range(27,31))
    lines+=['    [branch] if(p0) { // CF5: c31.x > 0 enables the character-shadow samples.']
    lines.extend(issue(i,pr[i],'PS') for i in range(31,51));lines+=['    }']
    lines.extend(issue(i,pr[i],'PS') for i in range(51,58))
    lines+=['    [branch] if(p0) { // CF12: c31.x > 0 enables the world-shadow samples.']
    lines.extend(issue(i,pr[i],'PS') for i in range(58,78));lines+=['    }']
    lines.extend(issue(i,pr[i],'PS') for i in range(78,80));lines+=['    }']
    lines.extend(issue(i,pr[i],'PS') for i in range(80,84));lines+=['    return output0;','}',
        '// Test-only observers; neither replaces an original game shader.',
        '[maxvertexcount(1)] void GSRigidProbe(point RigidOutput input[1],inout PointStream<RigidOutput> stream) { stream.Append(input[0]); }',
        'cbuffer RigidProbeInputs : register(b1) { float4 probeInputs[5]; };',
        'RigidOutput VSRigidPixelProbe(uint id:SV_VertexID) {',
        '    RigidOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
        '    o.uv=probeInputs[0].xy;o.characterShadow=probeInputs[1];o.worldShadow=probeInputs[2];',
        '    o.normal=probeInputs[3].xyz;o.color=probeInputs[4];return o;','}',
        '// Observe only original PS slots11/14/15 before later packing can hide NaNs.',
        'float4 PSRigidNormalProbe(RigidOutput input):SV_Target0 {',
        '    precise float squared=dot(input.normal.zxy,input.normal.zxy);',
        '    precise float scale=rsqrt(abs(squared));',
        '    return rigidLegacyMultiply(scale.xxxx,input.normal.xyzz);','}']
    return '\n'.join(lines)+'\n'


def constant_maps(body):
    need(len(body)==11988,'Original rigid body size changed')
    need(tuple(word(body,o) for o in (0x108,0x10C,0x114,0x118,0x11C,0x120,0x124,0x128,0x12C))==
         (0x390,0x380,11,26,12,1,1,0x460,0x384),'Original parameter counts/flags/ownership changed')
    spans=((0x390,208,'9a620aebc68756bc44eedd117f3d4c0c84cd19b02e18d968771fdc97c43acbfd'),
           (0x460,544,'34847ac7e67e41ddde21b35cd30bcc59dc6cb321f2a0b86a463c469fd10b8d5d'),
           (0x2C30,96,'17a0ee34e6b79c1ea5aef6a5d2a1d626d08ea93474749010be2eb1bc37a38dc0'),
           (0x2C90,320,'b2d748a9e1be4f2f689d3d036250dccf465e14b99244a52a4a0c0282020aa88c'))
    for at,n,digest in spans:need(sha(body[at:at+n])==digest,'Original rigid descriptor/default bytes changed')
    need(tuple(word(body,o) for o in (0x380,0x384))==(0x2C30,0x2C90),'Original shared pointer cells changed')
    need(tuple(word(body,0x6B0+4*i) for i in range(5))==(0x1B6D,1,0x2620,0x17F0,0x18B0),'Original rigid pass association changed')
    at=0x2620
    need(tuple(word(body,at+o) for o in (0x40,0x44,0x48,0x4C,0x50))==(0x2700,0x2860,0x6E0,0xD1C,0x2910),'Original rigid shader association changed')
    private={2:(0xC0004,0xC0C,0,0),8:(0x300010,0,33,0),9:(0x340012,0,34,0),10:(0x380014,0,35,0),
        11:(0x3C0016,0,36,0),14:(0x48001C,0,49,0),15:(0x4C001E,0,40,0),18:(0x580024,0,47,0),19:(0x5C0026,0,46,0)}
    shared={0:(0x40001,0xC00,0,0),4:(0x140009,0xC16,0,0),5:(0x18000B,0xC1A,0,0),6:(0x1C000D,0,0,0),
        7:(0x20000F,0,0x400000,0),9:(0x280013,0,30,0),10:(0x2C0015,0,31,0)}
    masks=({0:'2000000000000000',1:'00f3300000000000'},{0:'8c00000000000000',1:'0060000000000000',7:'0300000000000000'})
    for space,count,rows in ((0,22,private),(1,11,shared)):
        for cat in range(8):
            offset=word(body,at+32*space+4*cat)
            need(offset==0x2678+64*space+8*cat and body[offset:offset+8]==bytes.fromhex(masks[space].get(cat,'0000000000000000')),'Original category mask changed')
        offset=word(body,at+0x40+space*4)
        for leaf in range(count):need(struct.unpack_from('>4I',body,offset+16*leaf)==rows.get(leaf,(0,0,0,0)),'Original register/texture mapping changed')
    scalar=bytes.fromhex('a0000000'+'00000000'*5+'000000020000002800000001000000300000000100000000')
    need(body[0x17F0:0x1820]==scalar,'Original literal depth states changed')
    need(struct.unpack_from('>35I',body,0x18B0)==(0xEE000000,0xEE000000)+tuple([0]*32)+(12,),'Original sampler bitmap/count changed')
    pairs=tuple((stage<<16|sdk,value) for stage in range(2) for sdk,value in ((0,2),(4,2),(8,2),(0x10,0),(0x14,0),(0x18,2)))
    need(struct.unpack_from('>24I',body,0x193C)==tuple(v for pair in pairs for v in pair) and word(body,0x199C)==0,'Original sampler rows changed')
    return dict(private_leaves=22,shared_leaves=11,private_bytes=544,source_shared_bytes=320,private_map=private,shared_map=shared,
        category_masks=masks,shared_ownership='Resolve source handles against actual attached pool; source-local storage slots are not union-pool slots.',
        ps_reads=['c30.y','c31.x','c36.xyz','c40.x','c46.x','c47.x','c49.z'],mapped_but_unread_ps=[33,34,35])


def depth_contract(image):
    # Static original constructor/binder bytes, not a simulated SDK call.
    for va,n,digest in ((0x8243F928,0x2F8,'51a870ba275247748200d6aaa036bedea8ef940484a4e74d5babd480f4256f8a'),
                        (0x824408E0,0x108,'8b762616a5ccdfa56db9c0d4a69af0b388d641d74fe14e2f88a9c42cbfb1ef31')):
        need(sha(image[va-screen.BASE:va-screen.BASE+n])==digest,'Original texture descriptor builder/binder changed')
    anchors={0x8243FB60:0x7FE3DE70,0x8243FB84:0x7FFEC670,0x8243FBA0:0x7FE8AE70,
        0x8243FBBC:0x7FE79670,0x8243FBD8:0x7FE58E70,0x8243FBE0:0x50E5083C,0x8243FC0C:0x914B0028}
    for va,w in anchors.items():need(word(image,va-screen.BASE)==w,'Original packed swizzle construction changed')
    value=0x1A220197;swizzle=[(value>>i)&7 for i in (18,21,24,27)]
    low=((value>>17)&1)|(sum(v<<(3*i) for i,v in enumerate(swizzle))<<1)
    need(swizzle==[0,1,2,3] and low==0xD11,'Depth resource descriptor is not identity XYZW')
    reference=DEPTH_REFERENCE.read_text(encoding='utf-8')
    need(sha(DEPTH_REFERENCE.read_bytes())==DEPTH_REFERENCE_SHA,'Pinned depth format reference changed')
    section=reference.split('// k_24_8_FLOAT',1)[1].split('// k_16',1)[0]
    need('kLoadShaderIndexDepthFloat' in section and 'XE_GPU_TEXTURE_SWIZZLE_RRRR' in section,'Pinned depth format expansion differs')
    return dict(format='1A220197',surface_format=23,endian=(value>>6)&3,tiled=(value>>8)&1,signs=[(value>>i)&3 for i in (9,11,13,15)],
        number_format=(value>>17)&1,resource_swizzle=swizzle,resource_word28_low19=f'{low:08X}',
        original_builder='8243F928; shifts at8243FB60/84/A0/BC, number atFBD8, packFBE0, storeFC0C',
        format_expansion_reference=str(DEPTH_REFERENCE),format_expansion_reference_sha256=DEPTH_REFERENCE_SHA,
        format_expansion='RRRR depth, then resource XYZW. This is format-reference evidence, not original GPU sampling execution.',
        native_contract='Native D24FS8-equivalent view must explicitly supply replicated depth before original FETCH component selection; raw copy alone does not qualify sampling.',
        hlsl_contract='Unmodified arbitrary float4 samples. No implicit assumption about native R32 missing channels.')


def source_equivalence(image,source):
    compact=lambda s:re.sub(r'\s+','',re.sub(r'//[^\n]*','',s))
    need(compact(source)==compact(shader_source(image)),'Authored HLSL differs from full static original-slot transcription')
    return 'Full VS/PS bodies, interfaces, literals and structured uniform branches match a fixed per-slot transcription; no original instruction execution.'


def inspect(image):
    need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,'Original image changed')
    for path,digest in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(sha(path.read_bytes())==digest,'Pinned ALU/FETCH enum reference changed')
    fx=image[FX_VA-screen.BASE:FX_VA-screen.BASE+12000]
    need(sha(fx)==FX_SHA,'Original rigid effect changed')
    constants=constant_maps(fx[12:]);source=(ROOT/'renderer/rigid_shader.hlsl').read_text(encoding='utf-8')
    proof=source_equivalence(image,source);records=[]
    for profile in PROFILES:
        name,va,n,at,size,cf,digest=profile;b,rows=record(image,profile)
        records.append(dict(stage=name,va=f'{va:08X}',bytes=n,sha256=digest,code_offset=at,code_bytes=size,executable_bytes=size-12,
            code_sha256=sha(b[at:]),instructions=[dict(slot=i,**row) for i,row in rows.items()]))
    return dict(effect=dict(va=f'{FX_VA:08X}',sha256=FX_SHA,technique='rigid0003FFFC',pass_handle='0003FFFE'),records=records,
        constants=constants,depth_view=depth_contract(image),source_sha256_lf=sha(source.encode()),source_proof=proof,
        normal_zero_product=dict(length_squared_slot=11,ieee_rsqrt_slot=14,legacy_multiply_slot=15,
            reference=str(four.UCODE),reference_sha256=four.UCODE_SHA,
            rule='SM3 zero/denormal times anything produces positive zero, including rsqrt(0) times zero.',
            evidence='Pinned format reference citing R5xx Acceleration 8.7.5 and Adreno 200 tests; not original Xbox GPU execution.'),
        native=dict(vs='VSRigid / vs_5_0',ps='PSRigid / ps_5_0',probes=['GSRigidProbe / gs_5_0','VSRigidPixelProbe / vs_5_0','PSRigidNormalProbe / ps_5_0'],
            vertex_float_bank='b0:30 float4 /480 bytes',pixel_float_bank='b0:50 float4 /800 bytes',booleans=None,
            inputs=['POSITION0.xyz -> TEXCOORD0, W=1','NORMAL0.xyz -> TEXCOORD1','COLOR0.rgba -> TEXCOORD2','TEXCOORD0.xy -> TEXCOORD3'],
            textures=['t0/s0 world depth','t1/s1 character depth'],tap_offset_channel=TAPS),
        scope=['Only selected rigid, no rigidalpha alias.','Offline finite-input arithmetic transcription; no claim of identical original GPU rounding.',
               'Zero normal normalization follows the original SM3 positive-zero product at PS slot15. Used projections require nonzero W; broader NaN, denormal and infinity arithmetic is not qualified.',
               'Original geometry decoding, ownership, commits, attachment/state and native texture representation remain separately qualified.'])


def self_test(image):
    checks=0
    def rejects(function,*args):
        nonlocal checks
        try:function(*args)
        except (ValueError,struct.error,IndexError):checks+=1;return
        raise ValueError('Accepted a mutated rigid contract')
    mutable=bytearray(image)
    for profile in PROFILES:
        _,va,n,at,size,_,_=profile
        # Every instruction word and metadata/literal boundary is protected.
        for offset in sorted(set(range(at,n,4))|{0,4,20,24,116,at-1,n-1}):
            index=va-screen.BASE+offset;mutable[index]^=1;rejects(record,mutable,profile);mutable[index]^=1
    body=image[FX_VA-screen.BASE+12:FX_VA-screen.BASE+12000]
    for start,n in ((0x2678,128),(0x2700,22*16),(0x2860,11*16),(0x17F0,48),(0x18B0,240),(0x460,544),(0x2C90,320),(0x6B0,20)):
        for offset in range(start,start+n,4):
            bad=bytearray(body);bad[offset]^=1;rejects(constant_maps,bad)
    source=(ROOT/'renderer/rigid_shader.hlsl').read_text(encoding='utf-8')
    for old,new in (('vc[30]','vc[29]'),('pc[50]','pc[49]'),('float4(input.position,1)','float4(input.position,0)'),
        ('input.normal,0','input.normal,1'),('vc[12].zxy','vc[13].zxy'),('vc[26].wzxy','vc[26].xyzw'),
        ('pc[47].x!=0','pc[47].x>0'),('pc[31].x>0','pc[31].x>=0'),('r4.zzzz>=k253.wwww','r4.xxxx>=k253.wwww'),
        ('r6.xxxx>=k254.yyyy','r6.xxxx>k254.yyyy'),('k253.yyyy>=r0.wwww','k253.yyyy<r0.wwww'),
        ('r0.xxxx>=r4.xyzw','r0.xxxx<r4.xyzw'),('r1.z=v.y','r1.z=v.x'),('r1.w=v.z','r1.w=v.x'),
        ('int2(1,-1)','int2(-1,1)'),('shadow1.Sample','shadow0.Sample'),('frac(r7.y)','frac(r7.x)'),
        ('r4.x+ps','r4.x'),('k254.y-r0.x','k254.y+r0.x'),('0x44800000u','0x44000000u'),('ps=s;','ps=0;'),
        ('return value.xxxx;','return value.xyzw;'),('#else\n    return value;','#else\n    return value.xxxx;'),
        ('rigidLegacyMultiply(r0.xxxx,r3.xyzz)','r0.xxxx*r3.xyzz'),('rsqrt(abs(r0.w))','rcp(abs(r0.w))'),
        ('(asuint(a)&0x7F800000u)==0u','false'),('(asuint(b)&0x7F800000u)==0u','false'),
        (')?0.0:product;',')?-0.0:product;'),('rigidLegacyProduct(a.z,b.z)','rigidLegacyProduct(a.y,b.y)')):
        need(old in source,'Stale shader mutation fixture '+old);rejects(source_equivalence,image,source.replace(old,new))
    for va in (0x8243FB60,0x8243FBE0,0x8243FC0C,0x824408E0):
        index=va-screen.BASE;mutable[index]^=1;rejects(depth_contract,mutable);mutable[index]^=1
    print(f'PASS {checks} rigid record/instruction/mapping/default/state/source/depth-descriptor mutation checks')
    return checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true',help='Suppress JSON; write no files')
    parser.add_argument('--self-test',action='store_true',help='Reject original/source contract mutations')
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.self_test:self_test(image)
    if not args.verify:print(json.dumps(report,indent=2))
    print('PASS exact rigid VS8200D3AC/PS8200D9E8, constants/states, original texture descriptor and full HLSL source')


if __name__=='__main__':main()
