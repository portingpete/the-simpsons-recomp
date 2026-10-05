"""Independent retail-instruction oracle for simpsons_flipbook, including signed atlas frame selection.

The oracle executes decoded original words and CF. It does not evaluate HLSL
or use the generator's arithmetic emitters for expected GPU results.
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
import analyze_flipbook_shader as shader
import analyze_screen_shaders as screen
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()
INVENTORY=shader.inspect(IMAGE)
STAGES=('VS','AVS','PS','APS')


def f32(value):
    return struct.unpack('>f',struct.pack('>f',value))[0]


def product(a,b):
    # Original SM3 arithmetic annihilates infinity with a zero/denormal.
    words=[struct.unpack('>I',struct.pack('>f',v))[0]for v in(a,b)]
    return 0.0 if any((w&0x7F800000)==0 for w in words)else f32(a*b)


def sample_texture(texture,x,y):
    # Four by four linear/wrap resources, including original integer offsets.
    x=x*4-.5;y=y*4-.5;ix=math.floor(x);iy=math.floor(y);fx=x-ix;fy=y-iy
    value=[0.0]*4
    for dx,dy,weight in((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)):
        texel=texture[((iy+dy)%4)*4+(ix+dx)%4]
        for lane in range(4):value[lane]=f32(value[lane]+f32(texel[lane]*weight))
    return value


def run_original(stage,constants,inputs,textures,*,split_negate=True):
    rows=INVENTORY[stage]['rows']
    regs=[[0.0]*4 for _ in range(64)];outputs={i:[0.0]*4 for i in(0,1,2,3,4,62)}
    inputs=[f32(x)for x in inputs]
    if stage.endswith('VS'):
        # Fetch association order comes from the pinned original metadata,
        # then each original fetch selects its actual register and lanes.
        attrs=(inputs[:3]+[1.0],inputs[3:6]+[0.0],inputs[6:10],inputs[10:12]+[0.0,0.0])
        for (slot,_,_),attr in zip(shader.VS_FETCHES[stage],attrs):
            f=rows[slot]['fields']
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:regs[f['destination_register']][lane]=attr[selector]
                elif selector in(4,5):regs[f['destination_register']][lane]=float(selector-4)
    else:
        regs[0]=inputs[:2]+[0.0,0.0]
        if stage=='PS':regs[1]=inputs[2:5]+[0.0];regs[2]=inputs[5:9]
        else:regs[1]=inputs[2:6];regs[2]=inputs[6:9]+[0.0];regs[3]=inputs[9:13]
    constants={i:[f32(v)for v in row]for i,row in enumerate(constants)}
    for i in range(4):constants[252+i]=list(struct.unpack('>4f',struct.pack('>4I',*shader.LITERALS[stage][i*4:i*4+4])))
    previous=0.0;predicate=False;cf=0;visits=[]
    clamp=lambda v:f32(min(max(v,0.0),1.0))
    while cf<len(shader.CF[stage]):
        lo,hi=shader.CF[stage][cf];op=hi>>12
        if op==11:
            take=bool(lo&0x2000)or(bool(lo&0x4000)and predicate==bool(hi&0x400))
            cf=(lo&8191)if take else cf+1;continue
        if op not in(1,2):cf+=1;continue
        for slot in range(lo&4095,(lo&4095)+((lo>>12)&7)):
            row=rows[slot];f=row['fields'];visits.append(slot)
            if row['fetch']:
                if stage.endswith('VS'):continue
                coords=regs[f['source_register']]
                x,y=(coords[i]for i in f['source_components'][:2])
                x+=((f['offset_fields'][0]+16)%32-16)/8
                y+=((f['offset_fields'][1]+16)%32-16)/8
                value=sample_texture(textures[f['fetch_constant_index']],x,y)
                dest=regs[f['destination_register']][:]
                for lane,selector in enumerate(f['destination_swizzle']):
                    if selector<4:dest[lane]=value[selector]
                    elif selector in(4,5):dest[lane]=float(selector-4)
                regs[f['destination_register']]=dest;continue
            def operand(index):
                source=f['sources'][index]
                value=(regs if source['bank']=='temporary'else constants)[source['register']]
                value=[value[i]for i in source['components']]
                if source['absolute_temporary']and source['bank']=='temporary':value=[abs(v)for v in value]
                return [-v for v in value]if source['negated']else value
            a,b=operand(0),operand(1);vop,sop=f['vector_opcode'],f['scalar_opcode']
            if vop==25 and any(x>y for x,y in zip(a,b)):return None,visits
            vector=[0.0]*4
            if f['vector_mask']:
                if vop==0:vector=[f32(x+y)for x,y in zip(a,b)]
                elif vop==1:vector=[product(x,y)for x,y in zip(a,b)]
                elif vop in(2,3):vector=[(max if vop==2 else min)(x,y)for x,y in zip(a,b)]
                elif vop in(5,6):vector=[float(x>y if vop==5 else x>=y)for x,y in zip(a,b)]
                elif vop in(12,13):vector=[y if (x==0 if vop==12 else x>=0)else z for x,y,z in zip(a,b,operand(2))]
                elif vop==10:vector=[float(math.floor(x))for x in a]
                elif vop==11:vector=[f32(product(x,y)+z)for x,y,z in zip(a,b,operand(2))]
                elif vop in(15,16,17):
                    total=0.0
                    for i in range(4 if vop==15 else 3 if vop==16 else 2):total=f32(total+product(a[i],b[i]))
                    if vop==17:total=f32(total+operand(2)[0])
                    vector=[total]*4
                else:raise AssertionError(('vector',stage,slot,vop))
                if f['vector_clamp']:vector=[clamp(v)for v in vector]
            scalar=previous
            if sop!=50:
                if 42<=sop<=47:
                    sw=row['raw'][1]&255
                    sa=constants[row['raw'][2]&255][((sw>>6)+3)&3]
                    sb=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                    if split_negate and f['sources'][2]['negated']:sa,sb=-sa,-sb
                    scalar=product(sa,sb)if sop<44 else f32(sa+sb)if sop<46 else f32(sa-sb)
                else:
                    c=operand(2);sa,sb=c[3],c[0]
                    if sop==0:scalar=f32(sa+sb)
                    elif sop==1:scalar=f32(sa+previous)
                    elif sop==3:scalar=product(sa,previous)
                    elif sop==5:scalar=max(sa,sb)
                    elif sop==10:scalar=float(sa!=0)
                    elif sop==11:scalar=f32(sa-math.floor(sa))
                    elif sop==12:scalar=float(math.trunc(sa))
                    elif sop==13:scalar=float(math.floor(sa))
                    elif sop==19:scalar=f32(1/sa)if sa else math.copysign(math.inf,sa)
                    elif sop==22:scalar=f32(1/math.sqrt(abs(sa)))if sa else math.inf
                    elif sop in(28,29):predicate=sa!=0 if sop==28 else sa>0;scalar=0.0 if predicate else 1.0
                    elif sop in(48,49):scalar=f32((math.sin if sop==48 else math.cos)(sa))
                    else:raise AssertionError(('scalar',stage,slot,sop))
                if f['scalar_clamp']:scalar=clamp(scalar)
            # Both issues observe old operands. Scalar export lanes belong to
            # the same original output register, not its temporary counterpart.
            if f['export']:
                dest=outputs[f['vector_destination']]
                for lane in range(4):
                    vm,sm=f['vector_mask']&(1<<lane),f['scalar_mask']&(1<<lane)
                    if vm:dest[lane]=1.0 if sm else vector[lane]
                    elif sm:dest[lane]=scalar
            else:
                for lane in range(4):
                    if f['vector_mask']&(1<<lane):regs[f['vector_destination']][lane]=vector[lane]
                    if f['scalar_mask']&(1<<lane):regs[f['scalar_destination']][lane]=scalar
            if sop!=50:previous=scalar
        if op==2:break
        cf+=1
    if stage=='VS':result=outputs[62]+outputs[0][:2]+outputs[1][:3]+outputs[2]
    elif stage=='AVS':result=outputs[62]+outputs[0][:2]+outputs[1]+outputs[2][:3]+outputs[3]
    else:result=outputs[0]
    assert all(math.isfinite(v)for v in result),(stage,result)
    return result,visits


def fixtures():
    texture=[[f32((2+(i*3)%13)/16),f32((1+i%11)/16),f32((3+(i*5)%9)/16),
              (0.0,.125,.75,1.0)[i%4]]for i in range(16)]
    cases=[]
    for stage in('VS','AVS'):
        for grid,(framesU,totalFrames)in enumerate(((4,8),(3.75,12.5),(2,6),(1.5,1.75))):
            for wrap in(0,1):
                for phase,clock in enumerate((-3.75,-.5,3.5,18.125)):
                    c=[[0.0]*4 for _ in range(51)]
                    c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
                    c[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1]]
                    c[22][0]=clock;c[47]=[1.5 if grid%2 else 1.0,framesU,totalFrames,float(wrap)]
                    normal=[0,0,0]if phase==0 else[.125,.75,-.25]
                    inputs=[-.4+.03*grid,.35-.02*phase,.2+.01*wrap]+normal+[.71,.43,.29,.89,.125,.0625,17,-23]
                    expected,visits=run_original(stage,c,inputs,[texture])
                    cases.append(dict(stage=stage,constants=c,inputs=inputs,expected=expected,visits=visits))
    for stage in('PS','APS'):
        for i in range(48):
            c=[[0.0]*4 for _ in range(51)]
            c[4]=[2,-1,3,1];c[36]=[.13,.84,-.2,0]
            c[40]=[2.25,0,0,(0,.25,.75)[i%3]]
            c[46][0]=(0,.5,2,-.5)[(i//4)%4]
            c[48][0]=float(i%2);c[49]=[(-1,.25,.75)[i//16],.5,float((i//8)%2),0]
            normal=([0,0,0],[0,0,1],[.125,.75,-.25],[.125,-.5,.75])[i%4]
            color=[.5,.625,.95,(0,.25,.75)[i%3]]
            inputs=[.125+.25*(i%4),.125+.25*((i//4)%4)]+([-.375,.25,.5,1]if stage=='APS'else[])+normal+color
            expected,visits=run_original(stage,c,inputs,[texture])
            cases.append(dict(stage=stage,constants=c,inputs=inputs,expected=expected,visits=visits))
    return texture,cases


def cpp_array(value):
    if isinstance(value,list):return '{'+','.join(cpp_array(v)for v in value)+'}'
    return format(f32(value),'.10e')+'f'


def emit_fixtures(path):
    texture,cases=fixtures()
    out=['#pragma once','// Independent original-retail instruction outputs; native HLSL not evaluated.',
         'struct FlipbookCase { unsigned stage; float constants[51][4]; float input[17]; float expected[17]; };',
         'inline constexpr float kFlipbookTexture[16][4]='+cpp_array(texture)+';',
         'inline constexpr FlipbookCase kFlipbookCases[]={']
    for c in cases:
        out.append('{'+str(STAGES.index(c['stage']))+','+cpp_array(c['constants'])+','+cpp_array(c['inputs'])+','+cpp_array(c['expected'])+'},')
    path.write_text('\n'.join(out+['};','']),encoding='utf-8')


class OriginalFlipbookTests(unittest.TestCase):
    def test_exact_records_and_generated_source(self):
        self.assertEqual((ROOT/'renderer/flipbook_shader.hlsl').read_text(encoding='utf-8'),shader.shader_source(IMAGE))
        self.assertEqual([len(INVENTORY[s]['rows'])for s in STAGES],[32,37,16,13])
        self.assertEqual([sum(r['fetch']for r in INVENTORY[s]['rows'].values())for s in STAGES],[4,4,1,1])

    def test_structural_mutations_rejected_without_record_hash(self):
        for profile in shader.PROFILES:
            stage,address,size,offset,_,pairs,_=profile
            original=IMAGE[address-screen.BASE:address-screen.BASE+size]
            anchors=list(range(0,36,4))+list(range(offset,offset+pairs*12,4))+list(range(shader.HEADERS[stage][1],offset,4))+[size-12]
            if stage.endswith('VS'):anchors+=list(range(shader.SEMANTICS[stage][0],shader.SEMANTICS[stage][0]+16,4))
            for at in anchors:
                bad=bytearray(original);bad[at]^=1
                with self.subTest(stage=stage,offset=at),self.assertRaises((ValueError,struct.error)):shader.decode_record(bad,profile)

    def test_loop_clamp_signed_time_and_grid_truncation(self):
        texture,cases=fixtures();c=[[0.0]*4 for _ in range(51)]
        for base in(0,12):
            for row in range(4):c[base+row][row]=1
        inputs=[-.4,.35,.2,.1,.8,-.3,.7,.4,.2,.9,0,0,101,-103]
        for stage in('VS','AVS'):
            for wrap,clock,wanted in((1,3,(.75,0)),(1,4,(1,.5)),(1,8,(0,0)),(1,9,(.25,0)),
                                       (1,-1,(-.25,0)),(0,9,(2,1)),(0,-1,(-.25,0))):
                c[22][0]=clock;c[47]=[1,4,8,wrap]
                value,visits=run_original(stage,c,inputs,[texture])
                self.assertEqual(value[4:6],list(wanted))
                self.assertEqual(16 in visits,bool(wrap));self.assertEqual(24 in visits,not bool(wrap))
                fractional=[r[:]for r in c];fractional[47][1]=4.9;fractional[47][2]=8.9
                self.assertEqual(run_original(stage,fractional,inputs,[texture])[0],value)
        self.assertEqual([sum(c['stage']==s for c in cases)for s in STAGES],[32,32,48,48])
        self.assertTrue(all(all(math.isfinite(v)for v in c['expected'])for c in cases))

    def test_fetch_semantics_and_atlas_do_not_move_geometry(self):
        texture,cases=fixtures();c=[[0.0]*4 for _ in range(51)]
        for base in(0,12):
            for row in range(4):c[base+row][row]=1
        c[47]=[1,4,8,1];inputs=[-.4,.35,.2,.1,.8,-.3,.7,.4,.2,.9,.25,.125,101,-103]
        for stage in('VS','AVS'):
            value,_=run_original(stage,c,inputs,[texture])
            self.assertEqual(value[:4],[f32(v)for v in inputs[:3]]+[1.0])
            normalAt=6 if stage=='VS'else 10;colorAt=9 if stage=='VS'else 13
            self.assertEqual(value[normalAt:normalAt+3],[f32(v)for v in inputs[3:6]])
            self.assertEqual(value[colorAt:colorAt+4],[f32(v)for v in inputs[6:10]])
            c[22][0]=7;animated,_=run_original(stage,c,inputs,[texture]);c[22][0]=0
            self.assertEqual(animated[:4],value[:4]);self.assertNotEqual(animated[4:6],value[4:6])
            inputs[12:14]=[-100,103];self.assertEqual(run_original(stage,c,inputs,[texture])[0],value)

    def test_coissued_scalar_export_and_rim_are_observable(self):
        texture,cases=fixtures();opaque=[c for c in cases if c['stage']=='PS']
        self.assertTrue(all(c['expected'][3]==f32(.7)for c in opaque))
        # Original output green is scalar slot18, not temporary r0.y. Its
        # operands include the preceding scalar chain and co-issued old state.
        f=INVENTORY['PS']['rows'][18]['fields']
        self.assertTrue(f['export']);self.assertEqual((f['vector_mask'],f['scalar_mask']),(5,2))
        self.assertGreater(len({c['expected'][1]for c in opaque}),1)
        changed=0;unchanged=0
        for source in opaque:
            constants=[r[:]for r in source['constants']];constants[46][0]=0
            result,_=run_original('PS',constants,source['inputs'],[texture])
            if source['constants'][46][0]:
                if result!=source['expected']:changed+=1
                else:unchanged+=1
        self.assertGreater(changed,0);self.assertGreater(unchanged,0)

    def test_alpha_test_and_shadow_maps_are_unconsumed(self):
        texture,cases=fixtures()
        for source in cases:
            if source['stage']not in('PS','APS'):continue
            constants=[r[:]for r in source['constants']]
            for row in(30,31,43,44,48):constants[row]=[17,-19,23,-29]
            if source['stage']=='APS':constants[46]=[17,-19,23,-29]
            self.assertEqual(run_original(source['stage'],constants,source['inputs'],[texture])[0],source['expected'])
        self.assertFalse(any(not r['fetch']and r['fields']['vector_opcode']==25
                             for s in('PS','APS')for r in INVENTORY[s]['rows'].values()))


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--emit-fixtures',type=Path)
    args,rest=parser.parse_known_args()
    if args.emit_fixtures:emit_fixtures(args.emit_fixtures)
    else:unittest.main(argv=[sys.argv[0]]+rest)


