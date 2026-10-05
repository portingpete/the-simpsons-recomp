"""Retail instruction oracle for dual skin alpha, independent of native HLSL."""
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
import analyze_skin_dualalpha_shader as shader
import analyze_screen_shaders as screen

IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
ROWS=shader.inspect(IMAGE)


def f32(value):
    try:return struct.unpack('f',struct.pack('f',value))[0]
    except OverflowError:return math.copysign(math.inf,value)


def product(a,b):
    zero=lambda x:(struct.unpack('I',struct.pack('f',x))[0]&0x7F800000)==0
    return 0.0 if zero(a) or zero(b) else f32(a*b)


def run_original(stage,constants,inputs,texture):
    """Execute original CF and decoded words, including old MaxAs addressing."""
    regs=[[0.0]*4 for _ in range(64)];exports={i:[0.0]*4 for i in (0,1,2,3,62)}
    constants={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*shader.LITERALS[stage][4*i:4*i+4])))
    inputs=[f32(x)for x in inputs];previous=0.0;address=0;predicate=False;trace=[]
    if stage=='VS':
        # Original association order, independently read from shader metadata:
        # pos,normal,UV,bone indices,weights,color,morph1..6. SkinVertex's native
        # storage order is position/normal/uv/indices/weights/color/morphs/UV1.
        attributes=(inputs[:3]+[0],inputs[3:6]+[0],inputs[6:8]+[0,0],
                    inputs[8:12],inputs[12:16],inputs[16:20],
                    *(inputs[20+3*i:23+3*i]+[0]for i in range(6)))
        for slot,attribute in zip(range(7,19),attributes):
            f=ROWS['VS'][slot]['fields']
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:regs[f['destination_register']][lane]=attribute[selector]
                elif selector in (4,5):regs[f['destination_register']][lane]=float(selector-4)
    else:
        for i in range(4):regs[i]=inputs[4*i:4*i+4]
    clamp=lambda v:f32(min(max(v,0.0),1.0));cf=0
    while cf<len(shader.CF[stage]):
        low,high=shader.CF[stage][cf];op=high>>12
        if op==11:
            # Original conditional jump skips CF4 when morph predicate is false.
            if not predicate:cf=low&4095;continue
        elif op in (1,2):
            for slot in range(low&4095,(low&4095)+((low>>12)&7)):
                row=ROWS[stage][slot];f=row['fields']
                if row['fetch']:
                    if stage=='PS':
                        coords=regs[f['source_register']]
                        x,y=(coords[i]for i in f['source_components'][:2])
                        sample=texture[(math.floor(y*4)%4)*4+(math.floor(x*4)%4)]
                        for lane,selector in enumerate(f['destination_swizzle']):regs[f['destination_register']][lane]=sample[selector]
                    continue
                def operand(index):
                    s=f['sources'][index];reg=s['register']
                    if s['bank']=='constant' and f['constant_address_register_relative']:
                        assert index==1 and f['constant_0_relative'] and not f['constant_1_relative'];reg+=address
                    v=(regs if s['bank']=='temporary' else constants)[reg]
                    v=[v[i]for i in s['components']]
                    if s['absolute_temporary'] and s['bank']=='temporary':v=[abs(x)for x in v]
                    return [-x for x in v]if s['negated']else v
                vop,sop=f['vector_opcode'],f['scalar_opcode'];vector=[0.0]*4
                if f['vector_mask']:
                    a,b=operand(0),operand(1)
                    if vop==0:vector=[f32(x+y)for x,y in zip(a,b)]
                    elif vop==1:vector=[product(x,y)for x,y in zip(a,b)]
                    elif vop==2:vector=[max(x,y)for x,y in zip(a,b)]
                    elif vop in (5,6):vector=[float(x>y if vop==5 else x>=y)for x,y in zip(a,b)]
                    elif vop==11:vector=[f32(product(x,y)+z)for x,y,z in zip(a,b,operand(2))]
                    elif vop==12:vector=[y if x==0 else z for x,y,z in zip(a,b,operand(2))]
                    elif vop in (15,16):
                        value=0.0
                        for i in range(4 if vop==15 else 3):value=f32(value+product(a[i],b[i]))
                        vector=[value]*4
                    else:raise AssertionError(('vector',slot,vop))
                    if f['vector_clamp']:vector=[clamp(x)for x in vector]
                scalar=previous;next_address=address
                if sop!=50:
                    if 42<=sop<=47:
                        sw=row['raw'][1]&255
                        a=constants[row['raw'][2]&255][((sw>>6)+3)&3]
                        b=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                        scalar=product(a,b)if sop<44 else f32(a+b)if sop<46 else f32(a-b)
                    else:
                        c=operand(2);a,b=c[3],c[0]
                        if sop==3:scalar=product(a,previous)
                        elif sop==5:scalar=max(a,b)
                        elif sop==22:scalar=f32(1/math.sqrt(abs(a)))if a else math.inf
                        elif sop==23:scalar=a;next_address=min(max(math.floor(a+0.5),-256),255)
                        elif sop==28:predicate=a!=0;scalar=0.0 if predicate else 1.0
                        else:raise AssertionError(('scalar',slot,sop))
                    if f['scalar_clamp']:scalar=clamp(scalar)
                # Vector AND scalar sources are evaluated before either write;
                # only afterwards may MaxAs advance the bone-address register.
                if f['export']:
                    dest=exports[f['vector_destination']]
                    for i in range(4):
                        vm,sm=f['vector_mask']&(1<<i),f['scalar_mask']&(1<<i)
                        if vm:dest[i]=1.0 if sm else vector[i]
                        elif sm:dest[i]=scalar
                else:
                    for i in range(4):
                        if f['vector_mask']&(1<<i):regs[f['vector_destination']][i]=vector[i]
                    for i in range(4):
                        if f['scalar_mask']&(1<<i):regs[f['scalar_destination']][i]=scalar
                if sop!=50:previous=scalar
                trace.append((slot,address,next_address,predicate));address=next_address
            if op==2:break
        else:assert op in (0,12)
        cf+=1
    output=exports[62]+exports[0][:2]+exports[1][:3]+exports[2]+exports[3]if stage=='VS'else exports[0]
    assert all(math.isfinite(v)for v in output), (stage,output)
    return output,trace


def fixtures():
    texture=[[f32(.11+.03*((i*3)%13)),f32(.09+.04*(i%11)),f32(.17+.035*((i*5)%9)),f32(.16+.04*((i*7)%12))]for i in range(16)]
    cases=[]
    for i in range(32):
        c=[[0.0]*4 for _ in range(256)]
        c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
        c[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1]]
        c[38]=[.11,-.21,.31,.07];c[39]=[-.13,.23,.0,(0,.5,.5001,1)[i%4]]
        for bone in range(64):
            c[52+3*bone:55+3*bone]=[[1+.003*bone,.02,-.03,.1+.004*bone],[-.01,.8+.002*bone,.04,-.2+.003*bone],[.05,-.02,1.1-.001*bone,.3-.002*bone]]
        weights=[.125,.25,.375,.25]if i%3 else [float(j==i%4)for j in range(4)]
        inputs=[-.4+.03*i,.35-.02*i,.2+.01*i]+([.2,-.3,.8]if i%7 else [0,0,0])+[.125+.125*(i%4),.625]
        inputs+=[float((i+offset)%64)for offset in (0,17,33,63)]+weights+[.71,.43,.29,.89]
        inputs+=[.1,-.2,.3,-.4,.5,.6,.7,.8,-.9,-.2,-.3,.4,.3,-.5,.7,.1,.15,-.25]+[17,-23]
        expected,trace=run_original('VS',c,inputs,texture)
        cases.append(dict(stage=0,constants=c,inputs=inputs,expected=expected,trace=trace))
    for i in range(32):
        c=[[0.0]*4 for _ in range(256)]
        c[4]=[2,-1,3,1];c[40][3]=(0,.25,.75,1)[i%4]
        c[49]=[(0,.25,.75,1)[i%4],(0,.25,1,2)[(i//4)%4],7,.1]
        inputs=[.125+.25*(i%4),.125+.25*((i//4)%4),0,0]+([.2,-.3,.8,0]if i%7 else [0,0,0,0])
        inputs+=[-.4+.03*i,.35-.02*i,.2+.01*i,1]+[.71,.43,.29,(0,.25,.75,1)[i%4]]
        expected,trace=run_original('PS',c,inputs,texture)
        cases.append(dict(stage=1,constants=c,inputs=inputs,expected=expected,trace=trace))
    return texture,cases


def emit_cpp(path):
    texture,cases=fixtures()
    def array(values):return '{'+','.join(('%.9g'%f32(v))+(''if '.'in ('%.9g'%f32(v))or 'e'in ('%.9g'%f32(v))else '.0')+'f'for v in values)+'}'
    lines=['// Generated from original dual skin alpha instructions; native HLSL is never read.', '#pragma once',
           'struct SkinDualAlphaCase { unsigned stage; float constants[256][4]; float input[40]; float expected[17]; };',
           'inline constexpr float kSkinDualAlphaTexture[16][4]={'+','.join(array(v)for v in texture)+'};',
           'inline constexpr SkinDualAlphaCase kSkinDualAlphaCases[]={']
    for c in cases:lines.append('{'+str(c['stage'])+',{'+','.join(array(r)for r in c['constants'])+'},'+array(c['inputs'])+','+array(c['expected'])+'},')
    lines+=['};',''];path.write_text('\n'.join(lines),encoding='utf-8')


class DualSkinAlphaTests(unittest.TestCase):
    def test_complete_original_slots_and_material_sample(self):
        self.assertEqual(sorted(ROWS['VS']),list(range(7,66)))
        self.assertEqual(sorted(ROWS['PS']),list(range(2,15)))
        self.assertEqual([n for n,r in ROWS['PS'].items()if r['fetch']],[2])
        self.assertEqual(ROWS['PS'][2]['fields']['fetch_constant_index'],0)
        self.assertEqual(sum(r['fields']['constant_0_relative']for r in ROWS['VS'].values()if not r['fetch']),12)

    def test_control_morph_threshold_and_old_address(self):
        _,cases=fixtures()
        for i,c in enumerate(cases[:32]):
            visited={r[0]for r in c['trace']}
            self.assertEqual(21 in visited,i%4>=2)
            row=next(t for t in c['trace']if t[0]==32)
            self.assertNotEqual(row[1],row[2])

    def test_uv1_and_unused_pixel_rows_do_not_affect_alpha(self):
        texture,cases=fixtures()
        for c in cases[:32]:
            inputs=c['inputs'][:];inputs[-2:]=[-100,100]
            self.assertEqual(run_original('VS',c['constants'],inputs,texture)[0],c['expected'])
        for c in cases[32:]:
            constants=[r[:]for r in c['constants']]
            for reg in (30,31,32,33,34,35,36,43,44,45,46,47,48):constants[reg]=[17,23,-9,3]
            self.assertEqual(run_original('PS',constants,c['inputs'],texture)[0],c['expected'])

    def test_each_original_word_and_truncation_is_pinned(self):
        for p in shader.PROFILES:
            original=IMAGE[p[1]-screen.BASE:p[1]-screen.BASE+p[2]]
            for i in range(0,len(original),4):
                bad=bytearray(original);bad[i]^=1
                with self.assertRaises(ValueError):shader.decode_record(bad,p)
            with self.assertRaises(ValueError):shader.decode_record(original[:-1],p)

    def test_generator_has_only_base_sample_and_all_indexed_bones(self):
        source=shader.shader_source(IMAGE)
        self.assertEqual(source.count('skinDualAlphaBase.Sample('),1)
        self.assertNotIn('input.uv1',source)
        self.assertIn('r7=input.weights',source)
        self.assertIn('vc[a0+53]',source)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--emit-cpp',type=Path)
    args,rest=parser.parse_known_args()
    if args.emit_cpp:emit_cpp(args.emit_cpp)
    else:unittest.main(argv=[sys.argv[0]]+rest)
