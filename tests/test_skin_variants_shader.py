"""Independent arithmetic execution of eight original skin shader programs.

Only immutable decoded original words are input. Native HLSL/profile tables
are not read by the oracle or fixture exporter.
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
import analyze_skin_variants_shader as shader
import analyze_screen_shaders as screen
from test_skin_dualalpha_shader import f32,product
from test_skin_variant_reciprocal import AtlasReciprocalTests
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
ROWS={f:shader.inspect(IMAGE,f)for f in shader.PINS}


def run_original(family,stage,constants,inputs,texture):
    """Execute original CF and decoded words, including old MaxAs addressing."""
    pin=shader.PINS[family]['records'][stage];rows=ROWS[family][stage]
    regs=[[0.0]*4 for _ in range(64)];exports={i:[0.0]*4 for i in (0,1,2,3,4,62)}
    constants={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*pin['literals'][4*i:4*i+4])))
    inputs=[f32(x)for x in inputs];previous=0.0;address=0;predicate=False;trace=[]
    if stage.startswith('VS'):
        # Native160-byte order: position,normal,UV0,indices,weights,color,
        # six morph vectors,UV1. Original opaque fetch order adds UV1 afterUV0.
        attributes=[inputs[:3]+[0],inputs[3:6]+[0],inputs[6:8]+[0,0]]
        if stage=='VS':attributes.append(inputs[38:40]+[0,0])
        attributes += [inputs[8:12],inputs[12:16],inputs[16:20]]
        attributes += [inputs[20+3*i:23+3*i]+[0]for i in range(6)]
        pairs=pin['profile'][5]
        for slot,attribute in zip(range(pairs,pairs+len(attributes)),attributes):
            f=rows[slot]['fields']
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:regs[f['destination_register']][lane]=attribute[selector]
                elif selector in (4,5):regs[f['destination_register']][lane]=float(selector-4)
    else:
        for i in range(5 if stage=='PS' else 4):regs[i]=inputs[4*i:4*i+4]
    clamp=lambda v:f32(min(max(v,0.0),1.0));cf=0
    while cf<len(pin['cf']):
        low,high=pin['cf'][cf];op=high>>12
        if op==11:
            # Exact stock forward jump: bit13 unconditional, bit14 predicate-false.
            if low&0x2000 or not predicate:cf=low&4095;continue
        elif op in (1,2):
            for slot in range(low&4095,(low&4095)+((low>>12)&7)):
                row=rows[slot];f=row['fields']
                if row['fetch']:
                    if stage.startswith('PS'):
                        coords=regs[f['source_register']]
                        x,y=(coords[i]for i in f['source_components'][:2])
                        assert f['fetch_constant_index']==0
                        ix,iy=math.floor(x*4)%4,math.floor(y*4)%4;sample=texture[iy*4+ix]
                        for lane,selector in enumerate(f['destination_swizzle']):
                            if selector<4:regs[f['destination_register']][lane]=sample[selector]
                    trace.append((slot,address,address,predicate));continue
                def operand(index):
                    s=f['sources'][index];reg=s['register']
                    if s['bank']=='constant' and f['constant_address_register_relative']:
                        assert index==1 and f['constant_0_relative'] and not f['constant_1_relative'];reg+=address
                    v=(regs if s['bank']=='temporary' else constants)[reg]
                    v=[v[i]for i in s['components']]
                    if s['absolute_temporary'] and s['bank']=='temporary':v=[abs(x)for x in v]
                    return [-x for x in v]if s['negated']else v
                if f['predicated']and predicate!=f['predicate_condition']:continue
                vop,sop=f['vector_opcode'],f['scalar_opcode'];vector=[0.0]*4
                if vop==25 and any(x>y for x,y in zip(operand(0),operand(1))):return None,trace
                if f['vector_mask']:
                    a,b=operand(0),operand(1)
                    if vop==0:vector=[f32(x+y)for x,y in zip(a,b)]
                    elif vop==1:vector=[product(x,y)for x,y in zip(a,b)]
                    elif vop==2:vector=[max(x,y)for x,y in zip(a,b)]
                    elif vop==3:vector=[min(x,y)for x,y in zip(a,b)]
                    elif vop==8:vector=[f32(x-math.floor(x))for x in a]
                    elif vop==9:vector=[float(math.trunc(x))for x in a]
                    elif vop==10:vector=[float(math.floor(x))for x in a]
                    elif vop in (5,6):vector=[float(x>y if vop==5 else x>=y)for x,y in zip(a,b)]
                    elif vop==11:vector=[f32(product(x,y)+z)for x,y,z in zip(a,b,operand(2))]
                    elif vop==12:vector=[y if x==0 else z for x,y,z in zip(a,b,operand(2))]
                    elif vop==13:vector=[y if x>=0 else z for x,y,z in zip(a,b,operand(2))]
                    elif vop in (15,16,17):
                        value=0.0
                        for i in range(4 if vop==15 else 3 if vop==16 else 2):value=f32(value+product(a[i],b[i]))
                        if vop==17:value=f32(value+operand(2)[0])
                        vector=[value]*4
                    else:raise AssertionError(('vector',slot,vop))
                    if f['vector_clamp']:vector=[clamp(x)for x in vector]
                scalar=previous;next_address=address
                if sop!=50:
                    if 42<=sop<=47:
                        sw=row['raw'][1]&255
                        a=constants[row['raw'][2]&255][((sw>>6)+3)&3]
                        b=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                        if f['sources'][2]['negated']:a,b=-a,-b
                        scalar=product(a,b)if sop<44 else f32(a+b)if sop<46 else f32(a-b)
                    else:
                        c=operand(2);a,b=c[3],c[0]
                        if sop==0:scalar=f32(a+b)
                        elif sop==1:scalar=f32(a+previous)
                        elif sop==2:scalar=product(a,b)
                        elif sop==3:scalar=product(a,previous)
                        elif sop==5:scalar=max(a,b)
                        elif sop==6:scalar=min(a,b)
                        elif sop==8:scalar=float(a>0)
                        elif sop==9:scalar=float(a>=0)
                        elif sop==10:scalar=float(a!=0)
                        elif sop==11:scalar=f32(a-math.floor(a))
                        elif sop==12:scalar=float(math.trunc(a))
                        elif sop==13:scalar=float(math.floor(a))
                        elif sop==14:scalar=f32(2**a)
                        elif sop==16:scalar=f32(math.log2(abs(a)))if a else -math.inf
                        elif sop==19:scalar=f32(1/a)if a else math.copysign(math.inf,a)
                        elif sop==22:scalar=f32(1/math.sqrt(abs(a)))if a else math.inf
                        elif sop==23:scalar=a;next_address=min(max(math.floor(a+0.5),-256),255)
                        elif sop in(27,28,29):predicate=a==0 if sop==27 else a!=0 if sop==28 else a>0;scalar=0.0 if predicate else 1.0
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
    output=(exports[62]+sum([exports[i]for i in range(5)],[]))if stage.startswith('VS')else exports[0]
    assert all(math.isfinite(v)for v in output), (stage,output)
    return output,trace


def fixtures():
    texture=[[f32(.11+.03*((i*3)%13)),f32(.09+.04*(i%11)),
              f32(.17+.035*((i*5)%9)),f32(.16+.04*((i*7)%12))]for i in range(16)]
    texture[0][3]=0.0
    texture[1][3]=f32(0.0001)
    cases=[]
    for family_id,family in enumerate(shader.PINS):
        for i in range(48):
            c=[[0.0]*4 for _ in range(256)]
            c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
            c[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1]]
            c[38]=[.11,-.21,.31,.07];c[39]=[-.13,.23,.0,(0,.5,.5001,1)[i%4]]
            c[22]=[(-1.125,-.25,.375,.25,1.125,2.5)[i%6],0,0,0]
            c[47]=[(1,4,8,-4)[i%4],(1,2,4)[(i//4)%3],(1,4,8)[(i//12)%3],float((i//24)%2)]
            if c[47][3]:c[22][0]=(2.5,3.25,4.125)[i%3]
            for bone in range(64):
                c[52+3*bone:55+3*bone]=[[1+.003*bone,.02,-.03,.1+.004*bone],
                    [-.01,.8+.002*bone,.04,-.2+.003*bone],[.05,-.02,1.1-.001*bone,.3-.002*bone]]
            weights=[.125,.25,.375,.25]if i%3 else [float(j==i%4)for j in range(4)]
            inputs=[-.4+.03*i,.35-.02*i,.2+.01*i]+([.2,-.3,.8]if i%7 else [0,0,0])+[.125+.125*(i%4),.625]
            inputs+=[float((i+offset)%64)for offset in (0,17,33,63)]+weights+[.71,.43,.29,.89]
            inputs+=[.1,-.2,.3,-.4,.5,.6,.7,.8,-.9,-.2,-.3,.4,.3,-.5,.7,.1,.15,-.25]+[.375,.875]
            for stage,tag in enumerate(('VS','VSA')):
                expected,trace=run_original(family,tag,c,inputs,texture)
                cases.append(dict(family=family_id,stage=stage,constants=c,inputs=inputs,expected=expected,trace=trace))
        for i in range(96):
            c=[[0.0]*4 for _ in range(256)]
            c[4]=[2,-1,3,1];c[40]=[.1,.2,.3,(0,.25,.75,1)[i%4]]
            c[49]=[(0,.25,.75,1)[i%4],(0,.25,1,2)[(i//4)%4],7,.1]
            c[50]=[(.5,1,4,16)[(i//4)%4],0,0,0]
            c[46]=[float((i//16)%2),0,0,0]
            c[47]=[(0,.25,.75,1)[(i//8)%4],0,0,0]
            c[45]=[float((i//32)%2),0,0,0]
            c[44]=[.4,.7,.9,.35]
            uv=[.125+.25*(i%4),.125+.25*((i//4)%4),0,0]
            uv1=[.875,.375,0,0]
            normal=[.2,-.3,.8,0]if i%7 else[0,0,0,0]
            world=[-.4+.03*i,.35-.02*i,.2+.01*i,0]
            color=[.71,.43,.29,(0,.25,.75,1)[i%4]]
            for stage,tag in ((2,'PS'),(3,'PSA')):
                inputs=uv+(uv1+normal+world if stage==2 else normal+world)+color
                expected,trace=run_original(family,tag,c,inputs,texture)
                cases.append(dict(family=family_id,stage=stage,constants=c,inputs=inputs,expected=expected,trace=trace))
    return texture,cases


def emit_cpp(path):
    texture,cases=fixtures()
    def array(values):
        return '{'+','.join(('%.9g'%f32(v))+(''if '.'in ('%.9g'%f32(v))or 'e'in ('%.9g'%f32(v))else '.0')+'f'for v in values)+'}'
    lines=['// Independent original instruction oracle; native HLSL never read.', '#pragma once',
        'struct SkinVariantCase { unsigned family,stage; float constants[256][4]; float input[40]; float expected[24]; };',
        'inline constexpr float kSkinVariantTexture[16][4]={'+','.join(array(v)for v in texture)+'};',
        'inline constexpr SkinVariantCase kSkinVariantCases[]={']
    for c in cases:
        lines.append('{'+str(c['family'])+','+str(c['stage'])+',{'+','.join(array(r)for r in c['constants'])+'},'+array(c['inputs'])+','+array(c['expected'])+'},')
    path.write_text('\n'.join(lines+['};','']),encoding='utf-8')


class SkinVariantShaderTests(unittest.TestCase):
    def test_original_record_extent_and_every_word_are_pinned(self):
        for family,pins in shader.PINS.items():
            for stage,p in pins['records'].items():
                profile=p['profile'];raw=IMAGE[profile[1]-screen.BASE:profile[1]-screen.BASE+profile[2]]
                for at in range(0,len(raw),4):
                    bad=bytearray(raw);bad[at]^=1
                    with self.assertRaises(ValueError):shader.decode_record(bad,p)
                with self.assertRaises(ValueError):shader.decode_record(raw[:-1],p)

    def test_original_issue_coverage_and_input_export_linkage(self):
        self.assertEqual([len(ROWS['gloss'][s])for s in ('VS','VSA','PS','PSA')],[60,58,57,13])
        self.assertEqual([len(ROWS['flipbook'][s])for s in ('VS','VSA','PS','PSA')],[80,78,28,13])
        for family in shader.PINS:
            self.assertEqual([sum(r['fetch']for r in ROWS[family][s].values())for s in ('VS','VSA','PS','PSA')],[13,12,1,1])
            for s in ('VS','VSA'):
                self.assertEqual(sum(r['fields']['constant_0_relative']for r in ROWS[family][s].values()if not r['fetch']),12)
            self.assertFalse(any(r['fields']['vector_opcode']==25 for s in ('PS','PSA')for r in ROWS[family][s].values()if not r['fetch']))

    def test_opaque_uv1_and_alpha_input_exclusion(self):
        texture,cases=fixtures()
        for family_id,family in enumerate(shader.PINS):
            opaque=next(c for c in cases if c['family']==family_id and c['stage']==0)
            changed=opaque['inputs'][:];changed[38:40]=[-3,7]
            out,_=run_original(family,'VS',opaque['constants'],changed,texture)
            self.assertNotEqual(out,opaque['expected'])
            alpha=next(c for c in cases if c['family']==family_id and c['stage']==1)
            self.assertEqual(run_original(family,'VSA',alpha['constants'],changed,texture)[0],alpha['expected'])

    def test_no_alpha_test_or_unused_rim_gummi_sampler_consumption(self):
        texture,cases=fixtures()
        for c in cases:
            if c['stage']<2:continue
            family=list(shader.PINS)[c['family']];tag='PS'if c['stage']==2 else'PSA'
            constants=[r[:]for r in c['constants']];constants[48]=[99,-7,23,1]
            if c['stage']==3:
                for row in (44,45,46,47,50):constants[row]=[17,-9,23,31]
            self.assertEqual(run_original(family,tag,constants,c['inputs'],texture)[0],c['expected'])
        self.assertEqual(len(cases),576)

    def test_nonzero_rim_gloss_gummi_and_flipbook_animation_are_observed(self):
        texture,cases=fixtures()
        for family_id,family in enumerate(shader.PINS):
            changed=False
            for c in cases:
                if c['family']!=family_id or c['stage']!=2:continue
                constants=[r[:]for r in c['constants']];constants[46][0]=1-constants[46][0]
                if run_original(family,'PS',constants,c['inputs'],texture)[0]!=c['expected']:changed=True;break
            self.assertTrue(changed,'Nonzero rim must affect actual original output')
        animated=next(c for c in cases if c['family']==1 and c['stage']==1)
        constants=[r[:]for r in animated['constants']];constants[22][0]=.25;constants[47]=[4,2,4,0]
        shifted,_=run_original('flipbook','VSA',constants,animated['inputs'],texture)
        constants[22][0]=0;base,_=run_original('flipbook','VSA',constants,animated['inputs'],texture)
        self.assertAlmostEqual(shifted[4]-base[4],.5)
        self.assertAlmostEqual(shifted[5]-base[5],0)

    def test_gloss_exponent_scale_and_gummi_are_independently_observed(self):
        texture,cases=fixtures()
        for row in (50,47,45,44):
            changed=False
            for c in cases:
                if c['family']!=0 or c['stage']!=2:continue
                constants=[r[:]for r in c['constants']]
                if row==45:constants[row][0]=1-constants[row][0]
                elif row==44:constants[row]=[.9,.2,.4,.75]
                elif row==50:constants[row][0]=2 if constants[row][0]!=2 else 4
                else:constants[row][0]=.5 if constants[row][0]!=.5 else .75
                if run_original('gloss','PS',constants,c['inputs'],texture)[0]!=c['expected']:changed=True;break
            self.assertTrue(changed,'Original gloss row'+str(row)+' must affect an exercised output')

    def test_exact_wrap_arm_differs_from_clamp_and_has_zero_divisor_limit(self):
        texture,cases=fixtures();source=next(c for c in cases if c['family']==1 and c['stage']==1)
        constants=[r[:]for r in source['constants']];constants[47]=[4,2,4,0];constants[22][0]=.25
        clamped,_=run_original('flipbook','VSA',constants,source['inputs'],texture)
        constants[47][3]=1;wrapped,_=run_original('flipbook','VSA',constants,source['inputs'],texture)
        self.assertNotEqual(clamped[4:6],wrapped[4:6])
        # No finite numerical receipt is claimed for the stock issued RCP at a
        # rounded zero frame. Retain the arithmetic; never substitute a frame.
        constants[22][0]=0
        with self.assertRaises(OverflowError):run_original('flipbook','VSA',constants,source['inputs'],texture)

    def test_selected_material_and_shared_register_maps(self):
        for family,pins in shader.PINS.items():
            for context,spaces in pins['maps'].items():
                used_shared=[(i,row[0],row[2],row[3])for i,row in enumerate(spaces[1])if row[0]]
                self.assertEqual(used_shared,[(0,1,0xC00,0),(1,2,0,4)])
                sampler=[(i,row)for i,row in enumerate(spaces[0])if row[0]&0x80]
                leaf=84 if family=='gloss'else 85
                self.assertEqual(sampler,[(leaf,(128,0x016400A8 if family=='gloss'else 0x016800AA,0,0,0))])
                self.assertEqual(spaces[0][16][0],0)
            if family=='flipbook':
                for spaces in pins['maps'].values():
                    self.assertEqual(spaces[0][19],(1,0x005C0026,47,0,0))
                    self.assertEqual(spaces[0][20],(1,0x00600028,22,0,0))

    def test_generated_source_preserves_original_stage_and_static_cfg(self):
        for family in shader.PINS:
            source=shader.shader_source(IMAGE,family)
            self.assertEqual(source.count('skinVariantBase.Sample('),2)
            self.assertNotIn('discard',source)
            self.assertNotIn('Texture2D<float> shadow',source)
            self.assertEqual(source.count('Texture2D<'),1)
            self.assertIn('o.depth=PSShadowMeshDepth(input.position)',source)
            self.assertNotIn('switch(cf)',source)
            self.assertIn('input.uv1',source)
            alpha_signature=source.split('struct SkinVariantAlphaInput {',1)[1].split('};',1)[0]
            self.assertEqual(alpha_signature.count(':TEXCOORD'),12)
            self.assertNotIn('uv1',alpha_signature)
            prefix='Skin'+family.title()
            self.assertIn('VS'+prefix+'Alpha(SkinVariantAlphaInput input)',source)
            self.assertNotIn('rcp(',source)
            self.assertIn('precise float estimate=asfloat((k+127u)<<23)/float(m)',source)
            self.assertNotIn('umulExtended',source)
            self.assertNotIn('double ',source)
            self.assertEqual(source.count('precise float s=atlasReciprocalIEEE('),6 if family=='flipbook'else 0)
            self.assertEqual(source.count('precise float s=(1.0/('),2 if family=='gloss'else 1)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--emit-cpp',type=Path);args,rest=parser.parse_known_args()
    if args.emit_cpp:emit_cpp(args.emit_cpp)
    else:unittest.main(argv=[sys.argv[0]]+rest)

