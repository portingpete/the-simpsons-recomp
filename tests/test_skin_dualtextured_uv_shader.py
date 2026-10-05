"""Original-instruction oracle for all four skin_dualtextured_uv records (original words, not HLSL); no HLSL input."""
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
import analyze_skin_dualtextured_uv_shader as shader
import analyze_screen_shaders as screen
from test_skin_dualalpha_shader import f32,product
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
ROWS=shader.inspect(IMAGE)


def run_original(stage,constants,inputs,textures):
    """Execute original CF and decoded words, including old MaxAs addressing."""
    regs=[[0.0]*4 for _ in range(64)];exports={i:[0.0]*4 for i in (0,1,2,3,4,5,62)}
    constants={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*shader.LITERALS[stage][4*i:4*i+4])))
    inputs=[f32(x)for x in inputs];previous=0.0;address=0;predicate=False;trace=[]
    if stage.startswith('VS'):
        # Independent original semantic table order: position,normal,UV0,UV1,
        # bone indices,weights,color,morph1..6. Native storage is float40.
        attributes=(inputs[:3]+[0],inputs[3:6]+[0],inputs[6:8]+[0,0],
                    inputs[38:40]+[0,0],inputs[8:12],inputs[12:16],inputs[16:20],
                    *(inputs[20+3*i:23+3*i]+[0]for i in range(6)))
        for slot,attribute in zip(range(10,23),attributes):
            f=ROWS[stage][slot]['fields']
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:regs[f['destination_register']][lane]=attribute[selector]
                elif selector in (4,5):regs[f['destination_register']][lane]=float(selector-4)
    else:
        for i in range(6):regs[i]=inputs[4*i:4*i+4]
    clamp=lambda v:f32(min(max(v,0.0),1.0));cf=0
    while cf<len(shader.CF[stage]):
        low,high=shader.CF[stage][cf];op=high>>12
        if op==11:
            # Original conditional jump skips CF4 when morph predicate is false.
            if low&0x2000 or not predicate:cf=low&4095;continue
        elif op in (1,2):
            for slot in range(low&4095,(low&4095)+((low>>12)&7)):
                row=ROWS[stage][slot];f=row['fields']
                if row['fetch']:
                    if stage.startswith('PS'):
                        coords=regs[f['source_register']]
                        x,y=(coords[i]for i in f['source_components'][:2])
                        ix,iy=math.floor(x*4)%4,math.floor(y*4)%4
                        sample=textures[f['fetch_constant_index']][iy*4+ix]
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
                    elif vop==10:vector=[float(math.floor(x))for x in a]
                    elif vop in (5,6):vector=[float(x>y if vop==5 else x>=y)for x,y in zip(a,b)]
                    elif vop==11:vector=[f32(product(x,y)+z)for x,y,z in zip(a,b,operand(2))]
                    elif vop==12:vector=[y if x==0 else z for x,y,z in zip(a,b,operand(2))]
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
                        elif sop==10:scalar=float(a!=0)
                        elif sop==11:scalar=f32(a-math.floor(a))
                        elif sop==13:scalar=float(math.floor(a))
                        elif sop==19:scalar=f32(1/a)if a else math.copysign(math.inf,a)
                        elif sop==22:scalar=f32(1/math.sqrt(abs(a)))if a else math.inf
                        elif sop==23:scalar=a;next_address=min(max(math.floor(a+0.5),-256),255)
                        elif sop in(48,49):scalar=f32(math.sin(a)if sop==48 else math.cos(a))
                        elif sop in(28,29):predicate=a!=0 if sop==28 else a>0;scalar=0.0 if predicate else 1.0
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
    output=(exports[62]+exports[0][:2]+exports[1][:2]+exports[2][:2]+exports[3][:3]+exports[4][:3]+exports[5])if stage.startswith('VS')else exports[0]
    assert all(math.isfinite(v)for v in output), (stage,output)
    return output,trace


def fixtures():
    textures=[[[f32(.07+.04*((i*3+b)%13)),f32(.09+.035*((i+b*2)%11)),
                 f32(.12+.03*((i*5+b)%9)),f32(.17+.04*((i*7+b)%12))]for i in range(16)]for b in range(2)]
    cases=[]
    for i in range(32):
        c=[[0.0]*4 for _ in range(256)]
        c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
        c[12:15]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4]]
        c[38]=[.11,-.21,.31,.07];c[39]=[-.13,.23,0,(0,.5,.5001,1)[i%4]]
        c[22]=[(-3.125,-1,0,.125,.375,.75,1,3.25)[i%8],0,0,0]
        c[43]=[.3,-.7,1.1,-.2];c[44]=[.05,.03,-.06,.02]if i%5 else[0]*4
        c[45]=[.035,-.05,.075,-.025]if i%6 else[0]*4
        c[46]=[1.2,.85,1.1,.9];c[47]=[.08,-.11,.035,-.075]
        for bone in range(64):
            c[52+3*bone:55+3*bone]=[[1+.003*bone,.02,-.03,.1+.004*bone],[-.01,.8+.002*bone,.04,-.2+.003*bone],[.05,-.02,1.1-.001*bone,.3-.002*bone]]
        inputs=[-.4+.03*i,.35-.02*i,.2+.01*i]+([.2,-.3,.8]if i%7 else[0,0,0])+[.125+.125*(i%4),.625]
        inputs+=[float((i+offset)%64)for offset in(0,17,33,63)]+([.125,.25,.375,.25]if i%3 else[float(j==i%4)for j in range(4)])+[.71,.43,.29,.89]
        inputs+=[.1,-.2,.3,-.4,.5,.6,.7,.8,-.9,-.2,-.3,.4,.3,-.5,.7,.1,.15,-.25]+[.875,.125+.25*(i%3)]
        for stage,tag in enumerate(('VS','VSA')):
            expected,trace=run_original(tag,c,inputs,textures)
            cases.append(dict(stage=stage,constants=c,inputs=inputs,expected=expected,trace=trace))
    for i in range(64):
        c=[[0.0]*4 for _ in range(256)]
        c[4]=[2,-1,3,1]if i%2 else[-2,1,-3,1]
        c[40]=[.03,.08,.17,(0,.25,.75,1)[i%4]]
        c[42]=[(0,.25,.75,1)[i%4],0,0,0]
        c[48]=[(0,.25,.75,1)[(i//4)%4],0,0,0]
        c[49]=[(0,.25,.75,1)[i%4],.1,float((i//8)%2),.2]
        inputs=[.125+.25*(i%4),.125+.25*((i//4)%4),0,0,
                .125+.25*((i+1)%4),.125+.25*((i//4+2)%4),0,0,
                -.7+.031*i,.43-.017*i,0,0]
        inputs+=([.2,-.3,.8,0]if i%7 else[0,0,0,0])+[-.4+.03*i,.35-.02*i,.2+.01*i,0]+[.71,.43,(.1,.89,.9,.9001)[i%4],.89]
        for stage,tag in((2,'PS'),(3,'PSA')):
            expected,trace=run_original(tag,c,inputs,textures)
            cases.append(dict(stage=stage,constants=c,inputs=inputs,expected=expected,trace=trace))
    return textures,cases


def emit_cpp(path):
    textures,cases=fixtures()
    def array(values):
        return '{'+','.join(('%.9g'%f32(v))+(''if '.'in ('%.9g'%f32(v))or'e'in ('%.9g'%f32(v))else'.0')+'f'for v in values)+'}'
    lines=['// Independent original instruction execution; native HLSL never read.','#pragma once',
           'struct SkinDualUVCase { unsigned stage; float constants[256][4]; float input[40]; float expected[20]; };',
           'inline constexpr float kSkinDualUVTextures[2][16][4]={'+','.join('{'+','.join(array(r)for r in t)+'}'for t in textures)+'};',
           'inline constexpr SkinDualUVCase kSkinDualUVCases[]={']
    for c in cases:lines.append('{'+str(c['stage'])+',{'+','.join(array(r)for r in c['constants'])+'},'+array(c['inputs'])+','+array(c['expected'])+'},')
    path.write_text('\n'.join(lines+['};','']),encoding='utf-8')


class SkinDualUVShaderTests(unittest.TestCase):
    def test_complete_records_fetches_and_executable_pass_identity(self):
        self.assertEqual([len(ROWS[k])for k in('VS','VSA','PS','PSA')],[79,79,32,32])
        for stage in('VS','VSA'):
            self.assertEqual([s for s,r in ROWS[stage].items()if r['fetch']],list(range(10,23)))
            self.assertEqual(sum(r['fields']['constant_0_relative']for r in ROWS[stage].values()if not r['fetch']),12)
        self.assertEqual(ROWS['VS'],ROWS['VSA']);self.assertEqual(ROWS['PS'],ROWS['PSA'])

    def test_conditional_and_unconditional_morph_jump_and_old_maxas(self):
        _,cases=fixtures()
        for stage in(0,1):
            group=[c for c in cases if c['stage']==stage]
            self.assertTrue(any(any(t[0]==25 for t in c['trace'])and not any(t[0]==31 for t in c['trace'])for c in group))
            self.assertTrue(any(any(t[0]==31 for t in c['trace'])and not any(t[0]==25 for t in c['trace'])for c in group))
            self.assertTrue(any(any(t[1]!=t[2]for t in c['trace'])for c in group))
        self.assertEqual(shader.VS_CF[3],(0x4006,0xB000));self.assertEqual(shader.VS_CF[5],(0x2007,0xB000))

    def test_animation_and_both_uv_inputs_affect_original_exports(self):
        textures,cases=fixtures()
        for stage,tag in((0,'VS'),(1,'VSA')):
            c=next(c for c in cases if c['stage']==stage and c['constants'][22][0]!=0)
            inputs=c['inputs'][:];inputs[38:40]=[.25,.75]
            out,_=run_original(tag,c['constants'],inputs,textures)
            self.assertNotEqual(out[8:10],c['expected'][8:10])
            self.assertEqual(out[:8],c['expected'][:8])
            const=[r[:]for r in c['constants']];const[22][0]+=.137
            out,_=run_original(tag,const,c['inputs'],textures)
            self.assertNotEqual(out[:8],c['expected'][:8])

    def test_material_rim_texture_flags_both_stages_and_scalar_export(self):
        textures,cases=fixtures()
        for stage,tag in((2,'PS'),(3,'PSA')):
            group=[c for c in cases if c['stage']==stage]
            self.assertEqual(len(group),64)
            self.assertTrue(all(c['expected'][3]==f32(.7)for c in group))
            for row in(42,48):
                changed=False
                for c in group:
                    const=[r[:]for r in c['constants']];const[row][0]=0 if const[row][0] else 1
                    out,_=run_original(tag,const,c['inputs'],textures);changed|=out!=c['expected']
                self.assertTrue(changed,'Original mapped material row has no numerical coverage '+str(row))
            for bank in(0,1):
                changed=[[[.2,.3,.4,.5]for _ in range(16)]if n==bank else t for n,t in enumerate(textures)]
                self.assertTrue(any(run_original(tag,c['constants'],c['inputs'],changed)[0]!=c['expected']for c in group))

    def test_unused_shadow_rows_have_no_effect(self):
        textures,cases=fixtures()
        for c in cases:
            constants=[r[:]for r in c['constants']]
            for row in(26,27,28,29,30,31):constants[row]=[17,-9,23,31]
            tag=('VS','VSA','PS','PSA')[c['stage']]
            self.assertEqual(run_original(tag,constants,c['inputs'],textures)[0],c['expected'])

    def test_every_original_word_and_extent_pinned(self):
        for p in shader.PROFILES:
            raw=IMAGE[p[1]-screen.BASE:p[1]-screen.BASE+p[2]]
            for i in range(0,len(raw),4):
                bad=bytearray(raw);bad[i]^=1
                with self.assertRaises(ValueError):shader.decode_record(bad,p)
            with self.assertRaises(ValueError):shader.decode_record(raw[:-1],p)

    def test_generated_source_exact_export_sampler_control_and_bone_contract(self):
        source=shader.shader_source(IMAGE)
        self.assertEqual(source.count('skinDualUVTexture0.Sample('),2)
        self.assertEqual(source.count('skinDualUVTexture1.Sample('),2)
        self.assertEqual(source.count('} else { // CF5:'),2)
        self.assertEqual(source.count('output0.w=s.x;'),2)
        self.assertIn('input.uv1',source);self.assertIn('vc[a0+52]',source)
        self.assertNotIn('discard;',source);self.assertNotIn('shadow0.Sample(',source)
        saved=ROOT/'renderer/skin_dualtextured_uv_shader.hlsl'
        self.assertEqual(saved.read_text(encoding='utf-8'),source)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--emit-cpp',type=Path);args,rest=parser.parse_known_args()
    if args.emit_cpp:emit_cpp(args.emit_cpp)
    else:unittest.main(argv=[sys.argv[0]]+rest)
