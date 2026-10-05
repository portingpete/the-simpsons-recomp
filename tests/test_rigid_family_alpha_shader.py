"""Independent retail instruction evaluator and family alpha GPU fixture data.

The oracle executes decoded original words and CF schedules. It does not read
or evaluate authored HLSL, and computes both co-issued results before writes.
"""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import struct
import sys
import unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import analyze_rigid_family_alpha_shader as family
import analyze_screen_shaders as screen

ROOT=Path(__file__).resolve().parents[1]
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()


def f32(value):
    try:return struct.unpack('f',struct.pack('f',value))[0]
    except OverflowError:return math.copysign(math.inf,value)


def product(a,b):
    # SM3 zero/denormal multiplication is positive zero, even times infinity.
    zero=lambda v:(struct.unpack('I',struct.pack('f',v))[0]&0x7F800000)==0
    return 0.0 if zero(a)or zero(b) else f32(a*b)


def run_original(stage,constants,inputs,texture):
    profile=next(p for p in family.PROFILES if p[0]==stage)
    rows=family.decode_record(IMAGE[profile[1]-screen.BASE:profile[1]-screen.BASE+profile[2]],profile)
    regs=[[0.0]*4 for _ in range(64)];exports={i:[0.0]*4 for i in (0,1,2,3,62)}
    constants={i:[f32(v)for v in row] for i,row in enumerate(constants)}
    inputs=[f32(v)for v in inputs];scalar_previous=0.0;trace=[]
    if stage.endswith('VS'):
        attributes=(inputs[:3]+[1.0],inputs[3:6]+[0.0],inputs[6:10],inputs[10:12]+[0.0,0.0],inputs[12:14]+[0.0,0.0])
        for (_,dest,swizzle),attribute in zip(family.VS_FETCHES[:4] if stage=='GVS' else family.VS_FETCHES,attributes):
            for lane,selector in enumerate(swizzle):
                if selector<4:regs[dest][lane]=attribute[selector]
                elif selector in (4,5):regs[dest][lane]=float(selector-4)
    else:
        for i in range(4):regs[i]=inputs[i*4:i*4+4]
        for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*family.LITERALS[i*4:i*4+4])))
    clamp=lambda v:f32(min(max(v,0.0),1.0))
    for low,high in family.CF[stage]:
        if high>>12 not in (1,2):continue
        for slot in range(low&4095,(low&4095)+((low>>12)&7)):
            row=rows[slot];f=row['fields']
            if row['fetch']:
                if stage.endswith('VS'):continue
                coords=regs[f['source_register']]
                x,y=(coords[lane]for lane in f['source_components'][:2])
                # Every fixture samples an exact texel center. Original
                # linear/wrap state then has a single nonzero filter weight.
                sample=texture[(math.floor(y*4)%4)*4+(math.floor(x*4)%4)]
                for lane,selector in enumerate(f['destination_swizzle']):regs[f['destination_register']][lane]=f32(sample[selector])
                continue
            def source(index):
                s=f['sources'][index];v=(regs if s['bank']=='temporary' else constants)[s['register']]
                v=[v[i]for i in s['components']]
                if s['absolute_temporary'] and s['bank']=='temporary':v=[abs(x)for x in v]
                return [-x for x in v]if s['negated'] else v
            a,b=source(0),source(1);vop,sop=f['vector_opcode'],f['scalar_opcode'];vector=[0.0]*4
            if f['vector_mask']:
                if vop==0:vector=[f32(x+y)for x,y in zip(a,b)]
                elif vop==1:vector=[product(x,y)for x,y in zip(a,b)]
                elif vop in (2,3):vector=[(max if vop==2 else min)(x,y)for x,y in zip(a,b)]
                elif vop==6:vector=[float(x>=y)for x,y in zip(a,b)]
                elif vop==11:vector=[f32(product(x,y)+z)for x,y,z in zip(a,b,source(2))]
                elif vop in (15,16):
                    summed=0.0
                    for i in range(4 if vop==15 else 3):summed=f32(summed+product(a[i],b[i]))
                    vector=[summed]*4
                else:raise AssertionError(vop)
                if f['vector_clamp']:vector=[clamp(x)for x in vector]
            scalar=scalar_previous
            if sop!=50:
                if 42<=sop<=47:
                    # Decode split scalar directly from original instruction
                    # fields, independently of the generator's expression.
                    sw=row['raw'][1]&255
                    ca=constants[row['raw'][2]&255][((sw>>6)+3)&3]
                    rb=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                    scalar=product(ca,rb)if sop<44 else f32(ca+rb)if sop<46 else f32(ca-rb)
                else:
                    c=source(2);sa,sb=c[3],c[0]
                    if sop==0:scalar=f32(sa+sb)
                    elif sop==3:scalar=product(sa,scalar_previous)
                    elif sop==5:scalar=max(sa,sb)
                    elif sop==14:scalar=f32(2.0**sa)
                    elif sop==16:scalar=f32(math.log2(sa))if sa>0 else -math.inf if sa==0 else math.nan
                    elif sop==22:scalar=f32(1.0/math.sqrt(sa))if sa else math.inf
                    elif sop==40:scalar=f32(math.sqrt(sa))
                    else:raise AssertionError(sop)
                if f['scalar_clamp']:scalar=clamp(scalar)
            trace.append((slot,scalar,[r[:]for r in regs]))
            dest=exports[f['vector_destination']]if f['export']else regs[f['vector_destination']]
            for i in range(4):
                if f['vector_mask']&(1<<i):dest[i]=vector[i]
            for i in range(4):
                if f['scalar_mask']&(1<<i):regs[f['scalar_destination']][i]=scalar
            if sop!=50:scalar_previous=scalar
        if high>>12==2:break
    return (exports[62]+exports[0]+exports[1]+exports[2]+exports[3]if stage.endswith('VS')else exports[0]),trace


def fixtures():
    texture=[[f32(.07+.04*((i*3)%13)),f32(.13+.03*(i%11)),f32(.17+.04*((i*5)%9)),f32(.22+.045*((i*7)%12))]for i in range(16)]
    cases=[]
    for stage in ('GVS','MVS','NVS','GPS','MPS','NPS'):
        for variant in range(12):
            constants=[[0.0]*4 for _ in range(51)]
            if stage.endswith('VS'):
                constants[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[.0,.0,.0,1.0]]
                constants[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1.0]]
                inputs=[-.4+.09*variant,.35-.06*variant,.2+.07*variant,.1,.8,-.3,.71,.43,.29,.89,.125,.625,.375+.01*variant,.875]
                if variant==9:inputs[3:6]=[0,0,0]
            else:
                constants[4]=[1.2,-.8,.9,0];constants[40]=[2.25+.2*variant,0,.17+.03*variant,0]
                constants[47]=[.27+.01*variant,.5,.25,0]
                constants[49]=[.15+.05*(variant%4),.31+.02*variant,.42,.2+.04*variant]
                constants[50]=[0.0 if variant==11 else 1.5+variant*.3,0,0,0]
                inputs=[.125+.25*(variant%4),.125+.25*((variant//4)%4),.375,.875,
                        .2,-.1,.3,1.0,.17,.83,-.24,0,.67,.43,.29,.81]
                if variant==9:inputs[8:11]=[0,0,0]
                if variant==10:constants[4][:3]=inputs[4:7]
                if variant==8 and stage in ('MPS','NPS'):
                    # Exact zero equality avoids rsqrt rounding moving a
                    # computed nonzero dot across the comparison threshold.
                    inputs[8:11]=[0,0,0];constants[49][0]=0.0
            expected,trace=run_original(stage,constants,inputs,texture)
            cases.append(dict(stage=stage,variant=variant,constants=constants,inputs=inputs,expected=expected,trace=trace))
    return texture,cases


def cpp_array(value):
    if isinstance(value,list):return '{'+','.join(cpp_array(v)for v in value)+'}'
    return format(value,'.10e')+'f'


def emit_fixtures(path):
    texture,cases=fixtures();out=['#pragma once','// Original-word numerical oracle; native HLSL was not evaluated.',
        'struct FamilyAlphaCase { unsigned stage; float constants[51][4]; float input[20]; float expected[20]; };',
        'inline constexpr float kFamilyAlphaTexture[16][4]='+cpp_array(texture)+';',
        'inline constexpr FamilyAlphaCase kFamilyAlphaCases[]={']
    for c in cases:
        out.append('{'+str(('GVS','MVS','NVS','GPS','MPS','NPS').index(c['stage']))+','+cpp_array(c['constants'])+','+cpp_array(c['inputs'])+','+cpp_array(c['expected'])+'},')
    path.write_text('\n'.join(out+['};'])+'\n',encoding='utf-8')


class FamilyAlphaTests(unittest.TestCase):
    def test_hashes_and_generated_source(self):
        self.assertEqual(len(family.inspect(IMAGE)),6)
        self.assertEqual((ROOT/'renderer/rigid_family_alpha_shader.hlsl').read_text(encoding='utf-8'),family.shader_source(IMAGE))
    def test_semantic_mutations_without_record_hash(self):
        for profile in family.PROFILES:
            stage,address,size,offset,_,pairs,_=profile;record=IMAGE[address-screen.BASE:address-screen.BASE+size]
            anchors=list(range(0,36,4))+list(range(offset,offset+pairs*12,4))+list(range(size-12,size,4))
            anchors+=(list(range(family.HEADERS[stage][6],offset,4))if stage.endswith('VS')else list(range(family.HEADERS[stage][1],offset,4)))
            anchors+=list(range(offset+pairs*12,offset+(pairs+1)*12,4))
            if stage=='GPS':anchors+=list(range(offset+14*12,offset+15*12,4))
            for at in anchors:
                bad=bytearray(record);bad[at]^=1
                with self.subTest(stage=stage,offset=at),self.assertRaises((ValueError,struct.error)):
                    family.decode_record(bad,profile)
    def test_scalar45_pinned_and_old_rhs(self):
        texture,cases=fixtures();source=family.shader_source(IMAGE)
        self.assertIn('precise float s=pc[40].z+r1.x;',source)
        for c in cases:
            if c['stage']!='GPS':continue
            event=next(t for t in c['trace']if t[0]==14)
            self.assertEqual(event[1],f32(f32(c['constants'][40][2])+event[2][1][0]))
        profile=family.PROFILES[1];record=bytearray(IMAGE[profile[1]-screen.BASE:profile[1]-screen.BASE+profile[2]])
        record[profile[3]+14*12+3]^=1
        with self.assertRaises(ValueError):family.decode_record(record,profile)
    def test_all_cases_finite_and_single_sample(self):
        _,cases=fixtures();self.assertEqual(len(cases),72)
        self.assertTrue(all(all(math.isfinite(v)for v in c['expected'])for c in cases))
        for stage in ('GPS','MPS','NPS'):
            rows=family.inspect(IMAGE)[stage]['rows']
            self.assertEqual(sum(r['fetch']for r in rows.values()),1)
    def test_uv_fetch_and_position_normal_semantics(self):
        texture,cases=fixtures()
        c=cases[0];constants=[[0.0]*4 for _ in range(51)]
        for base in (0,12):
            for i in range(4):constants[base+i][i]=1.0
        for stage in ('GVS','MVS','NVS'):
            result,_=run_original(stage,constants,c['inputs'],texture)
            self.assertEqual(result[:4],[f32(v)for v in c['inputs'][:3]]+[1.0])
            self.assertEqual(result[4:8],[f32(v)for v in c['inputs'][10:12]]+([0.0,0.0]if stage=='GVS'else[f32(v)for v in c['inputs'][12:14]]))
            self.assertEqual(result[12:16],[f32(v)for v in c['inputs'][3:6]]+[0.0])
    def test_zero_normal_and_zero_exponent_preserve_legacy_products(self):
        _,cases=fixtures()
        for stage in ('GPS','MPS','NPS'):
            zero=next(c for c in cases if c['stage']==stage and c['variant']==9)
            self.assertTrue(all(math.isfinite(v)for v in zero['expected']))
        self.assertEqual(product(0.0,-math.inf),0.0)
        self.assertEqual(product(math.inf,0.0),0.0)
        self.assertEqual(product(struct.unpack('f',struct.pack('I',1))[0],math.inf),0.0)
    def test_inclusive_line_threshold(self):
        texture,cases=fixtures()
        self.assertIn('>=',family.shader_source(IMAGE))
        for stage in ('MPS','NPS'):
            c=next(c for c in cases if c['stage']==stage and c['variant']==8)
            event=next(t for t in c['trace']if t[0]==(10 if stage=='MPS'else 22))
            self.assertEqual(c['constants'][49][0],event[2][0 if stage=='MPS'else 1][1])
            below=[row[:]for row in c['constants']]
            below[49][0]=-.001
            actual,_=run_original(stage,below,c['inputs'],texture)
            self.assertNotEqual(actual,c['expected'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--emit-fixtures',type=Path)
    args,remaining=parser.parse_known_args()
    if args.emit_fixtures:emit_fixtures(args.emit_fixtures)
    else:unittest.main(argv=[sys.argv[0]]+remaining)
