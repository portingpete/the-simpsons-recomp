"""Original-instruction oracle for all four skin_textured shader records; no HLSL input."""
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
import analyze_skin_textured_shader as shader
import analyze_screen_shaders as screen
from test_skin_dualalpha_shader import f32,product
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
ROWS=shader.inspect(IMAGE)


def run_original(stage,constants,inputs,texture,shadow):
    """Execute original CF and decoded words, including old MaxAs addressing."""
    regs=[[0.0]*4 for _ in range(64)];exports={i:[0.0]*4 for i in (0,1,2,3,4,62)}
    constants={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*shader.LITERALS[stage][4*i:4*i+4])))
    inputs=[f32(x)for x in inputs];previous=0.0;address=0;predicate=False;trace=[]
    if stage.startswith('VS'):
        # Original association order, independently read from shader metadata:
        # pos,normal,weights,bone indices,color,UV,morph1..6. SkinVertex's native
        # storage order is position/normal/uv/indices/weights/color/morphs/UV1.
        attributes=(inputs[:3]+[0],inputs[3:6]+[0],inputs[12:16],
                    inputs[8:12],inputs[16:20],inputs[6:8]+[0,0],
                    *(inputs[20+3*i:23+3*i]+[0]for i in range(6)))
        pairs=8 if stage=='VS' else 7
        for slot,attribute in zip(range(pairs,pairs+12),attributes):
            f=ROWS[stage][slot]['fields']
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:regs[f['destination_register']][lane]=attribute[selector]
                elif selector in (4,5):regs[f['destination_register']][lane]=float(selector-4)
    else:
        for i in range(5 if stage=='PS' else 4):regs[i]=inputs[4*i:4*i+4]
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
                    if stage.startswith('PS'):
                        coords=regs[f['source_register']]
                        x,y=(coords[i]for i in f['source_components'][:2])
                        material=stage=='PSA' or f['fetch_constant_index']==1
                        if material:
                            ix,iy=math.floor(x*4)%4,math.floor(y*4)%4;sample=texture[iy*4+ix]
                        else:
                            ox,oy=[((n+16)%32-16)//2 for n in f['offset_fields'][:2]]
                            ix,iy=[min(max(math.floor(v*4)+o,0),3)for v,o in ((x,ox),(y,oy))];sample=shadow[iy*4+ix]
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
                    elif vop==10:vector=[float(math.floor(x))for x in a]
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
                        if f['sources'][2]['negated']:a,b=-a,-b
                        scalar=product(a,b)if sop<44 else f32(a+b)if sop<46 else f32(a-b)
                    else:
                        c=operand(2);a,b=c[3],c[0]
                        if sop==1:scalar=f32(a+previous)
                        elif sop==3:scalar=product(a,previous)
                        elif sop==5:scalar=max(a,b)
                        elif sop==6:scalar=min(a,b)
                        elif sop==10:scalar=float(a!=0)
                        elif sop==11:scalar=f32(a-math.floor(a))
                        elif sop==13:scalar=float(math.floor(a))
                        elif sop==19:scalar=f32(1/a)if a else math.copysign(math.inf,a)
                        elif sop==22:scalar=f32(1/math.sqrt(abs(a)))if a else math.inf
                        elif sop==23:scalar=a;next_address=min(max(math.floor(a+0.5),-256),255)
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
    output=(exports[62]+exports[0][:2]+exports[1][:3]+exports[2]+exports[3]+(exports[4]if stage=='VS'else[]))if stage.startswith('VS')else exports[0]
    assert all(math.isfinite(v)for v in output), (stage,output)
    return output,trace

def fixtures():
    texture=[[f32(.11+.03*((i*3)%13)),f32(.09+.04*(i%11)),f32(.17+.035*((i*5)%9)),f32(.16+.04*((i*7)%12))]for i in range(16)]
    shadow=[[f32(.2+.025*i),f32(.3+.02*((i*5)%13)),f32(.4+.015*((i*7)%11)),1.]for i in range(16)]
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
        c[26:30]=[[.2,.3,.4,384],[.1,-.2,.3,640],[0,.1,.4,.5],[0,0,0,1]]
        for stage,tag in enumerate(('VS','VSA')):
            expected,trace=run_original(tag,c,inputs,texture,shadow)
            cases.append(dict(stage=stage,discarded=False,constants=c,inputs=inputs,expected=expected,trace=trace))
    texture[0][3]=0.0;texture[1][3]=f32(0.0001);texture[2][3]=f32(0.00009999)
    for i in range(64):
        c=[[0.0]*4 for _ in range(256)]
        c[4]=[2,-1,3,1];c[40]=[.1,.2,.3,(0,.25,.75,1)[i%4]]
        c[49]=[(0,.25,.75,1)[i%4],(0,.25,1,2)[(i//4)%4],7,.1]
        c[48]=[float(i>=32),0,0,0]
        c[30]=[0,.7,0,0];c[31]=[float((i//8)%2),0,0,0]
        c[46]=[float((i//16)%2),0,0,0];c[47]=[float((i//4)%2),0,0,0]
        uv=[.125+.25*(i%4),.125+.25*((i//4)%4),0,0]
        normal=[.2,-.3,.8,0]if i%7 else[0,0,0,0]
        world=[-.4+.03*i,.35-.02*i,.2+.01*i,1]
        color=[.71,.43,.29,(0,.25,.75,1)[i%4]]
        for stage,tag in ((2,'PS'),(3,'PSA')):
            inputs=uv+normal+world+([.375,.625,.6,1]if stage==2 else[])+color
            expected,trace=run_original(tag,c,inputs,texture,shadow)
            cases.append(dict(stage=stage,discarded=expected is None,constants=c,inputs=inputs,expected=expected or[0]*4,trace=trace))
    return texture,shadow,cases


def emit_cpp(path):
    texture,shadow,cases=fixtures()
    def array(values):
        return '{'+','.join(('%.9g'%f32(v))+(''if '.'in ('%.9g'%f32(v))or 'e'in ('%.9g'%f32(v))else '.0')+'f'for v in values)+'}'
    lines=['// Original instruction reference; native HLSL never read.', '#pragma once',
           'struct SkinTexturedCase { unsigned stage; bool discarded; float constants[256][4]; float input[40]; float expected[21]; };',
           'inline constexpr float kSkinTexturedTexture[16][4]={'+','.join(array(v)for v in texture)+'};',
           'inline constexpr float kSkinTexturedShadow[16][4]={'+','.join(array(v)for v in shadow)+'};',
           'inline constexpr SkinTexturedCase kSkinTexturedCases[]={']
    for c in cases:
        lines.append('{'+str(c['stage'])+','+str(c['discarded']).lower()+',{'+','.join(array(r)for r in c['constants'])+'},'+array(c['inputs'])+','+array(c['expected'])+'},')
    path.write_text('\n'.join(lines+['};','']),encoding='utf-8')


class SkinTexturedShaderTests(unittest.TestCase):
    def test_complete_original_issue_and_resource_contract(self):
        self.assertEqual([len(ROWS[k])for k in ('VS','VSA','PS','PSA')],[63,59,58,17])
        self.assertEqual([s for s,r in ROWS['PS'].items()if r['fetch']],[10]+list(range(41,50)))
        self.assertEqual([s for s,r in ROWS['PSA'].items()if r['fetch']],[4])
        for stage in('VS','VSA'):
            self.assertEqual(sum(r['fields']['constant_0_relative']for r in ROWS[stage].values()if not r['fetch']),12)

    def test_both_passes_preserve_strict_alpha_cutoff(self):
        texture,shadow,cases=fixtures()
        for stage,tag in((2,'PS'),(3,'PSA')):
            source=next(c for c in cases if c['stage']==stage)
            constants=[r[:]for r in source['constants']];constants[48][0]=1
            for texel,killed in((0,True),(1,False),(2,True),(3,False)):
                inputs=source['inputs'][:];inputs[:2]=[.125+.25*texel,.125]
                expected,_=run_original(tag,constants,inputs,texture,shadow)
                self.assertEqual(expected is None,killed)
            constants[48][0]=0;inputs[:2]=[.125,.125]
            self.assertIsNotNone(run_original(tag,constants,inputs,texture,shadow)[0])

    def test_all_morph_bone_and_shadow_branches_exercised(self):
        _,_,cases=fixtures()
        for stage in(0,1):
            group=[c for c in cases if c['stage']==stage]
            first_morph=22 if stage==0 else 21
            self.assertTrue(any(any(t[0]==first_morph for t in c['trace'])for c in group))
            self.assertTrue(any(not any(t[0]==first_morph for t in c['trace'])for c in group))
            self.assertTrue(any(any(t[1]!=t[2]for t in c['trace'])for c in group))
        opaque=[c for c in cases if c['stage']==2]
        self.assertTrue(any(any(t[0]==41 for t in c['trace'])for c in opaque))
        self.assertTrue(any(not any(t[0]==41 for t in c['trace'])for c in opaque))
        self.assertEqual(sum(c['discarded']for c in cases),8)

    def test_uv1_and_shadow_rows_are_not_alpha_inputs(self):
        texture,shadow,cases=fixtures()
        for c in cases:
            if c['stage']==1:
                inputs=c['inputs'][:];inputs[-2:]=[-100,100]
                self.assertEqual(run_original('VSA',c['constants'],inputs,texture,shadow)[0],c['expected'])
            elif c['stage']==3:
                constants=[r[:]for r in c['constants']]
                for row in(30,31,46,47):constants[row]=[17,-9,23,31]
                actual,_=run_original('PSA',constants,c['inputs'],texture,shadow)
                self.assertEqual(actual,c['expected']if not c['discarded']else None)

    def test_native_mesh_fixture_independent_rrrr_pixel_oracle(self):
        constants=[[0.0]*4 for _ in range(256)]
        constants[4]=[0,0,3,1];constants[49][0]=-1
        constants[40]=[.1,.2,.3,1];constants[31][0]=constants[47][0]=1;constants[30][1]=.25
        texture=[[64/255,128/255,192/255,1.]]*16
        expected_rgb=[.0000977517120,.5083088875,.6505882740]
        for world in((-1,-1,.25,1),(0,0,.25,1),(1,1,.25,1)):
            inputs=[.25,.25,0,0,0,0,1,0]+list(world)+[.5,.5,.5,1]+[1,1,1,1]
            for depth,alpha in((.25,.7),(1.,1.)):
                result,_=run_original('PS',constants,inputs,texture,[[depth]*4]*16)
                for a,b in zip(result,expected_rgb+[alpha]):self.assertAlmostEqual(a,b,delta=1e-7)
            raw,_=run_original('PS',constants,inputs,texture,[[1.,0.,0.,1.]]*16)
            self.assertAlmostEqual(raw[3],.7,delta=1e-7)

    def test_every_original_word_and_extent_is_pinned(self):
        for p in shader.PROFILES:
            raw=IMAGE[p[1]-screen.BASE:p[1]-screen.BASE+p[2]]
            for i in range(0,len(raw),4):
                bad=bytearray(raw);bad[i]^=1
                with self.assertRaises(ValueError):shader.decode_record(bad,p)
            with self.assertRaises(ValueError):shader.decode_record(raw[:-1],p)

    def test_generated_source_retains_sampler_and_kill_side_effects(self):
        source=shader.shader_source(IMAGE)
        self.assertEqual(source.count('skinTexturedBase.Sample('),1)
        self.assertEqual(source.count('skinTexturedAlphaBase.Sample('),1)
        self.assertEqual(source.count('shadow0.Sample('),9)
        self.assertEqual(source.count(' discard;'),2)
        self.assertNotIn('input.uv1',source)
        self.assertIn('pc[48].x!=0',source)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--emit-cpp',type=Path);args,rest=parser.parse_known_args()
    if args.emit_cpp:emit_cpp(args.emit_cpp)
    else:unittest.main(argv=[sys.argv[0]]+rest)
