"""Independent original-word oracle for chocolate and projected textures.

Expected results execute original instructions/CF, not native HLSL or emitter
expressions. Includes the existing chocolate alpha program as a regression.
"""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import struct
import sys
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_chocolate_shader as chocolate
import analyze_edge_shaders as edge
import analyze_rigid_remaining_shader as shader
import analyze_screen_shaders as screen
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
INVENTORY=shader.inspect(IMAGE)
PINS=dict(shader.PINS)
for stage,address,size,offset,length,pairs,digest in chocolate.PROFILES:
    name='ChocolateAlpha'+stage
    record=IMAGE[address-screen.BASE:address-screen.BASE+size]
    header=struct.unpack_from('>9I',record)
    code=record[offset:offset+length];cf,slots=chocolate.schedule(code,pairs)
    rows={slot:dict(raw=screen.words(code,slot*12),fetch=fetch,
        fields=screen.decode_fetch(screen.words(code,slot*12))if fetch else edge.alu(*screen.words(code,slot*12)))for slot,fetch,_ in slots}
    INVENTORY[name]=dict(rows=rows)
    PINS[name]=dict(stage=stage,address=address,bytes=size,offset=offset,length=length,pairs=pairs,sha256=digest,
                   prefix_hex=record[header[1]:offset].hex(),cf=cf,fetches=[(slot,rows[slot]['fields'])for slot,fetch,_ in slots if fetch])
FAMILIES=('ChocolateOpaque','ChocolateAlpha','Projtex','ProjtexAlpha')
STAGES=tuple(f+'VS'for f in FAMILIES)+tuple(f+'PS'for f in FAMILIES)
WIDTHS=((4,4,4,3,4,4,4),(4,4,3,4,4,4),(2,4,4,3,2,4),(2,4,3,4))

def f32(x):return struct.unpack('>f',struct.pack('>f',x))[0]
def product(a,b):
    bits=[struct.unpack('>I',struct.pack('>f',v))[0]for v in(a,b)]
    return 0.0 if any(w&0x7F800000==0 for w in bits)else f32(a*b)
def sample(texture,u,v):
    x=u*4-.5;y=v*4-.5;ix=math.floor(x);iy=math.floor(y);fx=x-ix;fy=y-iy;value=[0.0]*4
    for dx,dy,w in((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)):
        t=texture[((iy+dy)%4)*4+(ix+dx)%4]
        for j in range(4):value[j]=f32(value[j]+f32(t[j]*w))
    return value

def run_original(name,constants,inputs,textures,*,export_overlap=True,trace=None):
    pin=PINS[name];rows=INVENTORY[name]['rows'];family=FAMILIES.index(name[:-2]);vertex=name.endswith('VS')
    regs=[[0.0]*4 for _ in range(64)];out={i:[0.0]*4 for i in list(range(7))+[62]};c={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    prefix=bytes.fromhex(pin['prefix_hex']);lits=struct.unpack('>'+str(len(prefix)//4)+'f',prefix)if prefix else()
    for i in range(len(lits)//4):c[256-len(lits)//4+i]=list(lits[i*4:i*4+4])
    values=[f32(v)for v in inputs]
    if vertex:
        attrs=[values[:3]+[1],values[3:6]+[0]]
        attrs+=([values[14:17]+[0],values[6:10],values[10:12]+[0,0],values[12:14]+[0,0]]if family<2 else[values[6:10],values[10:12]+[0,0]])
        for(_,f),a in zip(pin['fetches'],attrs):
            for lane,s in enumerate(f['destination_swizzle']):
                if s<4:regs[f['destination_register']][lane]=a[s]
                elif s in(4,5):regs[f['destination_register']][lane]=float(s-4)
    else:
        at=0
        for i,w in enumerate(WIDTHS[family]):regs[i]=values[at:at+w]+[0]*(4-w);at+=w
    previous=0.0;predicate=False;cf=0;visits=[];branches=[]
    clamp=lambda v:0.0 if not v>0 else 1.0 if v>1 else f32(v)
    while cf<len(pin['cf']):
        lo,hi=pin['cf'][cf];op=hi>>12
        if op==11:
            take=bool(lo&0x2000)or(bool(lo&0x4000)and predicate==bool(hi&0x400))
            branches.append((cf,take));cf=(lo&8191)if take else cf+1;continue
        if op not in(1,2):cf+=1;continue
        for slot in range(lo&4095,(lo&4095)+((lo>>12)&7)):
            row=rows[slot];f=row['fields'];visits.append(slot)
            if f.get('predicated')and predicate!=f['predicate_condition']:continue
            if row['fetch']:
                if vertex:continue
                coords=regs[f['source_register']];u,v=(coords[j]for j in f['source_components'][:2])
                u+=((f['offset_fields'][0]+16)%32-16)/8;v+=((f['offset_fields'][1]+16)%32-16)/8
                value=sample(textures[f['fetch_constant_index']],u,v)
                for lane,s in enumerate(f['destination_swizzle']):
                    if s<4:regs[f['destination_register']][lane]=value[s]
                continue
            def operand(i):
                s=f['sources'][i];a=(regs if s['bank']=='temporary'else c)[s['register']]
                a=[a[j]for j in s['components']]
                if s['absolute_temporary']and s['bank']=='temporary':a=[abs(x)for x in a]
                return[-x for x in a]if s['negated']else a
            vop,sop=f['vector_opcode'],f['scalar_opcode'];vec=[0.0]*4
            if f['vector_mask']:
                a,b=operand(0),operand(1)
                if vop==0:vec=[f32(x+y)for x,y in zip(a,b)]
                elif vop==1:vec=[product(x,y)for x,y in zip(a,b)]
                elif vop in(2,3):vec=[(max if vop==2 else min)(x,y)for x,y in zip(a,b)]
                elif vop in(5,6):vec=[float(x>y if vop==5 else x>=y)for x,y in zip(a,b)]
                elif vop==8:vec=[f32(x-math.floor(x))for x in a]
                elif vop==10:vec=[float(math.floor(x))for x in a]
                elif vop==11:vec=[f32(product(x,y)+z)for x,y,z in zip(a,b,operand(2))]
                elif vop in(12,13):vec=[y if(x==0 if vop==12 else x>=0)else z for x,y,z in zip(a,b,operand(2))]
                elif vop in(15,16,17):
                    total=0.0
                    for i in range(4 if vop==15 else 3 if vop==16 else 2):total=f32(total+product(a[i],b[i]))
                    if vop==17:total=f32(total+operand(2)[0])
                    vec=[total]*4
                else:raise AssertionError((name,slot,'vector',vop))
                if f['vector_clamp']:vec=[clamp(v)for v in vec]
            scalar=previous;newPredicate=None
            if sop!=50:
                if 42<=sop<=47:
                    sw=row['raw'][1]&255;a=c[row['raw'][2]&255][((sw>>6)+3)&3];b=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                    if f['sources'][2]['negated']:a,b=-a,-b
                    scalar=product(a,b)if sop<44 else f32(a+b)if sop<46 else f32(a-b)
                else:
                    third=operand(2);a,b=third[3],third[0]
                    if sop==0:scalar=f32(a+b)
                    elif sop==1:scalar=f32(a+previous)
                    elif sop in(2,3):scalar=product(a,b if sop==2 else previous)
                    elif sop==5:scalar=max(a,b)
                    elif sop==10:scalar=float(a!=0)
                    elif sop==11:scalar=f32(a-math.floor(a))
                    elif sop==13:scalar=float(math.floor(a))
                    elif sop==14:scalar=f32(2**a)
                    elif sop==16:scalar=f32(math.log2(a))if a>0 else -math.inf if a==0 else math.nan
                    elif sop==19:scalar=f32(1/a)if a else math.copysign(math.inf,a)
                    elif sop==22:scalar=f32(1/math.sqrt(a))if a>0 else math.inf if a==0 else math.nan
                    elif sop==25:scalar=f32(a-b)
                    elif sop in(27,28,29):newPredicate=a==0 if sop==27 else a!=0 if sop==28 else a>0;scalar=0.0 if newPredicate else 1.0
                    elif sop==40:scalar=f32(math.sqrt(a))
                    elif sop in(48,49):scalar=f32((math.sin if sop==48 else math.cos)(a))
                    else:raise AssertionError((name,slot,'scalar',sop))
                if f['scalar_clamp']:scalar=clamp(scalar)
            if trace is not None:trace[slot]=(vec[:],scalar)
            # Co-issued operations read all old registers before either write.
            for lane in range(4):
                v,s=bool(f['vector_mask']&(1<<lane)),bool(f['scalar_mask']&(1<<lane))
                if f['export']:
                    if v or s:out[f['vector_destination']][lane]=1.0 if v and s and export_overlap else vec[lane]if v else scalar
                    elif f['scalar_destination_relative_or_export_zero']:out[f['vector_destination']][lane]=0.0
                else:
                    if v:regs[f['vector_destination']][lane]=vec[lane]
                    if s:regs[f['scalar_destination']][lane]=scalar
            if sop!=50:previous=scalar
            if newPredicate is not None:predicate=newPredicate
        if op==2:break
        cf+=1
    result=out[62]+[v for i,w in enumerate(WIDTHS[family])for v in out[i][:w]]if vertex else out[0]
    assert all(math.isfinite(x)for x in result),(name,result)
    return result,visits,branches

def fixtures():
    textures=[[[f32((2+(i*3+b)%13)/16),f32((1+(i+b*4)%11)/16),f32((3+(i*5+b)%9)/16),f32((i+b)%4/4)]for i in range(16)]for b in range(4)]
    # R lies above the .5 comparison while G/B lie below it. Original nine-tap
    # channel selection therefore distinguishes RGBA from native depth RRRR.
    for bank in range(2):
        for i,t in enumerate(textures[bank]):t[:3]=[.875,.125,.25]
    cases=[]
    for si,name in enumerate(STAGES):
        for i in range(32 if si<4 else 64):
            c=[[0.0]*4 for _ in range(51)];c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
            c[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1]]
            c[22:26]=[[.5,0,0,0],[0,.5,0,0],[0,0,.5,0],[0,0,0,1]];c[26:30]=[[.4,.1,0,0],[0,.5,.1,0],[0,0,.4,0],[0,0,0,1]]
            c[22][0]=(-1,.0,.25,1.5)[i%4]
            c[4]=[.75,-.5,2,1];c[30]=[.5,.35,1,0];c[31][0]=float((i>>2)&1);c[36]=[.125,.75,-.25,0];c[40]=[.375,.25,.5,.625]
            c[42]=[.25,.75,-.5,float((i>>3)&1)]if i%5 else[.01,.01,.01,1]
            c[43]=[.75,.25,.5,1];c[44]=[float(i%4),float((i>>2)%3),float((i>>3)&1),float((i>>4)&1)]
            c[45]=[.125,.25,.125,.5];c[46]=[1.25,.75,.5,1];c[47]=[float(i&1),.125,float((i>>1)&1),.25]
            c[49]=[(-1,.25,.75)[i%3],.5,float(i&1),0];c[50]=[2,0,0,0]
            if si<4:
                normal=[.125,.75,-.25]if i%7 else[0,0,0]
                inputs=[-.4+.03*i,.35-.02*i,.2+.01*i]+normal+[.71,.43,.29,.89,.125+.25*(i%4),.625,.375,.875,.5,-.25,.75]
            else:
                family=si-4;widths=WIDTHS[family]
                packets=([[.125,.625,.375,.875],[-.5,.25,.5,1],[.125,-.25,.5,1],[.125,.75,-.25,0],[.5,.625,.75,.875],[.25,-.5,.75,1],[.125,.25,.375,.5]]if family==0 else
                         [[.125,.625,.375,.875],[.125,-.25,.5,1],[.125,.75,-.25,0],[.5,.625,.75,.875],[.25,-.5,.75,1],[.125,.25,.375,.5]]if family==1 else
                         [[.125,.625,0,0],[-.5,.25,.5,1],[.125,-.25,.5,1],[.125,.75,-.25,0],[.375,.875,0,0],[.5,.625,.75,.875]]if family==2 else
                         [[.125,.625,0,0],[.125,-.25,.5,1],[.125,.75,-.25,0],[.5,.625,.75,.875]])
                packets[0][0]=.125+.25*(i%4)
                if i%7==0:packets[3 if family in(0,2)else 2]=[0]*4
                inputs=[v for p,w in zip(packets,widths)for v in p[:w]]
            expected,visits,branches=run_original(name,c,inputs,textures)
            depth=[[[t[0]]*4 for t in texture]if b<2 else texture for b,texture in enumerate(textures)]
            adapted=run_original(name,c,inputs,depth)[0]if si in(4,6)else expected
            cases.append(dict(stage=si,constants=c,input=inputs,expected=expected,depthExpected=adapted[:4]if si>=4 else[0]*4,visits=visits,branches=branches))
    return textures,cases

def emit(path,textures,cases):
    def number(v):return f'{f32(v):.9g}f'if '.'in f'{f32(v):.9g}'or'e'in f'{f32(v):.9g}'else f'{f32(v):.9g}.0f'
    def array(v):return'{'+','.join(array(x)if isinstance(x,list)else number(x)for x in v)+'}'
    lines=['#pragma once','struct RemainingCase {unsigned stage;float constants[51][4];float input[31];float expected[31];float depthExpected[4];};',
           'inline constexpr float kRemainingTextures[4][16][4]='+array(textures)+';', 'inline constexpr RemainingCase kRemainingCases[]={']
    for c in cases:lines.append('{'+str(c['stage'])+','+array(c['constants'])+','+array(c['input'])+','+array(c['expected'])+','+array(c['depthExpected'])+'},')
    path.write_text('\n'.join(lines+['};'])+'\n',encoding='utf-8')

class RemainingShaderTests(unittest.TestCase):
    def test_records_and_source(self):
        source=shader.shader_source(IMAGE);self.assertEqual(source,(ROOT/'renderer/rigid_remaining_shader.hlsl').read_text())
        self.assertEqual(len(INVENTORY),8)
    def test_projected_vertex_inputs_match_original_four_fetches(self):
        source=shader.shader_source(IMAGE)
        self.assertIn('VSChocolateOpaque(RemainingInput input)',source)
        for family in ('Projtex','ProjtexAlpha'):
            self.assertEqual(len(PINS[family+'VS']['fetches']),4)
            self.assertIn('VS'+family+'(RemainingSingleInput input)',source)
        single=source.split('struct RemainingSingleInput {',1)[1].split('};',1)[0]
        self.assertEqual(single.count('TEXCOORD'),4)
        self.assertNotIn('uv1',single)
        self.assertNotIn('tangent',single)
    def test_numeric_cases(self):
        textures,cases=fixtures();self.assertEqual(len(cases),384)
        for stage in range(8):self.assertEqual(sum(c['stage']==stage for c in cases),32 if stage<4 else 64)
    def test_existing_chocolate_alpha_zero_tangent_frame(self):
        from unittest.mock import patch
        textures,cases=fixtures()
        zeros=[c for c in cases if c['stage']==1 and c['input'][3:6]==[0,0,0]]
        self.assertEqual(len(zeros),5)
        # Packed SO lanes19..21 are the final normalized frame export4.xyz.
        # Original IEEE RSQ at zero is infinite; original MUL annihilates it.
        for c in zeros:
            self.assertEqual(c['expected'][19:22],[0.0,0.0,0.0])
            for value in c['expected'][19:22]:self.assertEqual(struct.pack('>f',value),b'\0\0\0\0')
            with patch(__name__+'.product',lambda a,b:f32(a*b)):
                with self.assertRaises(AssertionError):run_original('ChocolateAlphaVS',c['constants'],c['input'],textures)
    def test_existing_chocolate_alpha_scalar_split_products(self):
        from unittest.mock import patch
        textures,cases=fixtures();c=next(c for c in cases if c['stage']==5 and c['input'][0]==.375)
        rows=INVENTORY['ChocolateAlphaPS']['rows']
        expected={39:(0xA8870104,0x00C6C041,0xC100042B),53:(0xA8100000,0x00000041,0xC2000032),55:(0xA8100000,0x00000000,0xC200002B)}
        for slot,raw in expected.items():
            self.assertEqual(rows[slot]['raw'],raw)
            self.assertEqual(rows[slot]['fields']['scalar_opcode'],42)
        changed=0
        for bits in (0x80000000,0x00000001,0x80000001):
            constants=[r[:]for r in c['constants']]
            constants[43][0]=constants[43][3]=constants[50][0]=struct.unpack('>f',struct.pack('>I',bits))[0]
            trace={};run_original('ChocolateAlphaPS',constants,c['input'],textures,trace=trace)
            for slot in expected:self.assertEqual(struct.pack('>f',trace[slot][1]),b'\0\0\0\0')
            native={}
            with patch(__name__+'.product',lambda a,b:f32(a*b)):
                try:run_original('ChocolateAlphaPS',constants,c['input'],textures,trace=native)
                except (AssertionError,ValueError):pass
            changed+=sum(slot in native and struct.pack('>f',native[slot][1])!=b'\0\0\0\0'for slot in expected)
        self.assertGreater(changed,0)
    def test_depth_adapters_are_material_specific(self):
        _,cases=fixtures()
        for stage in(4,6):self.assertTrue(any(c['stage']==stage and c['expected']!=c['depthExpected']for c in cases))
        for stage in(5,7):self.assertTrue(all(c['expected']==c['depthExpected']for c in cases if c['stage']==stage))
    def test_control_arms_executed(self):
        _,cases=fixtures()
        for stage in(4,5,6):
            observed={x for c in cases if c['stage']==stage for x in c['branches']}
            conditional=[i for i,(lo,hi)in enumerate(PINS[STAGES[stage]]['cf'])if hi>>12==11 and not lo&0x2000]
            for ci in conditional:self.assertIn((ci,False),observed);self.assertIn((ci,True),observed)
    def test_scalar_export_is_not_vector_copy(self):
        textures,cases=fixtures();changed=0
        depth=[[[t[0]]*4 for t in bank]if i<2 else bank for i,bank in enumerate(textures)]
        for c in cases:
            if c['stage']==4:
                changed+=run_original(STAGES[4],c['constants'],c['input'],textures,export_overlap=False)[0]!=c['expected']
                changed+=run_original(STAGES[4],c['constants'],c['input'],depth,export_overlap=False)[0]!=c['depthExpected']
        self.assertGreater(changed,0)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--emit-fixtures',type=Path);args,rest=parser.parse_known_args()
    if args.emit_fixtures:
        textures,cases=fixtures();emit(args.emit_fixtures,textures,cases);print(f'PASS {len(cases)} independent original-instruction cases')
    else:unittest.main(argv=[sys.argv[0]]+rest)
