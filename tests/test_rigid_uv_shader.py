"""Independent retail-instruction oracle for simpsons_uv, including discard.

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
import analyze_rigid_uv_shader as shader
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
        regs[0]=inputs[:2]+[0.0,0.0];regs[1]=inputs[2:6]
        if stage=='PS':regs[2]=inputs[6:10];regs[3]=inputs[10:13]+[0.0];regs[4]=inputs[13:17]
        else:regs[2]=inputs[6:9]+[0.0];regs[3]=inputs[9:13]
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
                elif vop==6:vector=[float(x>=y)for x,y in zip(a,b)]
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
                    elif sop==13:scalar=float(math.floor(sa))
                    elif sop==19:scalar=f32(1/sa)if sa else math.copysign(math.inf,sa)
                    elif sop==22:scalar=f32(1/math.sqrt(abs(sa)))if sa else math.inf
                    elif sop in(28,29):predicate=sa!=0 if sop==28 else sa>0;scalar=0.0 if predicate else 1.0
                    elif sop in(48,49):scalar=f32((math.sin if sop==48 else math.cos)(sa))
                    else:raise AssertionError(('scalar',stage,slot,sop))
                if f['scalar_clamp']:scalar=clamp(scalar)
            # All vector/scalar operands have now read the old register file.
            dest=outputs[f['vector_destination']]if f['export']else regs[f['vector_destination']]
            for lane in range(4):
                if f['vector_mask']&(1<<lane):dest[lane]=vector[lane]
            for lane in range(4):
                if f['scalar_mask']&(1<<lane):regs[f['scalar_destination']][lane]=scalar
            if sop!=50:previous=scalar
        if op==2:break
        cf+=1
    if stage=='VS':result=outputs[62]+outputs[0][:2]+outputs[1]+outputs[2]+outputs[3][:3]+outputs[4]
    elif stage=='AVS':result=outputs[62]+outputs[0][:2]+outputs[1]+outputs[2][:3]+outputs[3]
    else:result=outputs[0]
    assert all(math.isfinite(v)for v in result),(stage,result)
    return result,visits


def fixtures():
    cutoff=f32(.0001);below=struct.unpack('>f',struct.pack('>I',0x38D1B716))[0]
    textures=[[[f32((2+(i*3+bank)%13)/16),f32((1+(i+bank*4)%11)/16),
                f32((3+(i*5+bank)%9)/16),f32((2+(i*7+bank)%12)/16)]for i in range(16)]for bank in range(3)]
    for i,t in enumerate(textures[0]):t[3]=(0.0,below,cutoff,.75)[i%4]
    cases=[]
    for stage in('VS','AVS'):
        for i in range(32):
            c=[[0.0]*4 for _ in range(51)]
            c[:4]=[[.8,.04,-.06,.1],[-.03,1.1,.08,-.2],[.1,-.04,.7,.3],[0,0,0,1]]
            c[12:16]=[[1.2,.1,-.15,.2],[-.05,.8,.12,-.3],[.08,-.1,1.1,.4],[.02,-.04,.03,1]]
            c[22:26]=[[.3,.2,.1,.4],[-.1,.3,.5,.2],[0,.1,.7,.3],[0,0,0,1]]
            c[26:30]=[[.2,.3,.4,.1],[.1,-.2,.3,.4],[0,.1,.4,.5],[0,0,0,1]]
            c[22][0]=(-3.5,-1.0,0.0,2.75)[i%4]+i*.0625
            c[45]=[1.2,-.8,.63,1.4];c[46]=[.37,-.41,13.0,-7.0];c[47]=[.17,-.23,.31,-.29]
            if i%8==0:c[46]=[0.0]*4
            if i%8==1:c[45]=[1,1,0,0]
            if i%8==2:c[47]=[0.0]*4
            normal=[.1,.8,-.3]if i%7 else[0,0,0]
            inputs=[-.4+.03*i,.35-.02*i,.2+.01*i]+normal+[.71,.43,.29,.89,.125+.125*(i%4),.625,17,-23]
            expected,visits=run_original(stage,c,inputs,textures)
            cases.append(dict(stage=stage,discarded=False,constants=c,inputs=inputs,expected=expected,depthExpected=[0]*4,visits=visits))
    depthTextures=[[[t[0]]*4 for t in texture]if bank<2 else texture for bank,texture in enumerate(textures)]
    for shadow in(0,1):
        for receiver in(0,1):
            for variant in(0,1):
                for i in range(6):
                    c=[[0.0]*4 for _ in range(51)]
                    c[4]=[2,-1,3,1];c[30]=[.35,.3,1.2,1];c[31][0]=receiver
                    c[36]=([-.13,-.84,.2,0]if i==2 else[.13,.84,-.2,0]);c[40]=[2.25,0,0,.375]
                    c[43][0]=(0,.5,2)[i%3]
                    c[44][0]=shadow;c[49]=[(-1,.25,.75)[i%3],.5,variant,0]
                    normal=([0,0,0],[.125,.75,-.25],[.125,.75,-.25],
                            [0,0,0],[0,0,1],[1,0,0])[i]
                    inputs=[.125+.25*(i%4),.625,-.75,.25,.5,1,.25,-.75,.625,1]+normal+[.5,.625,.75,(0,.25,.75)[i%3]]
                    expected,visits=run_original('PS',c,inputs,textures)
                    depthExpected,_=run_original('PS',c,inputs,depthTextures)
                    cases.append(dict(stage='PS',discarded=False,constants=c,inputs=inputs,expected=expected,depthExpected=depthExpected,visits=visits))
    for alphaTest in(0,1):
        for alphaColumn in range(4):
            for i in range(6):
                c=[[0.0]*4 for _ in range(51)]
                c[4]=[2,-1,3,1];c[40]=[2.25,0,0,(0,.25,.75)[i%3]]
                c[48][0]=alphaTest;c[49]=[(-1,.25,.75)[i%3],.5,7,0]
                normal=[.125,.75,-.25]if i%3 else[0,0,0]
                inputs=[.125+.25*alphaColumn,.125+.25*(i%4),-.375,.25,.5,1]+normal+[.5,.625,.75,(0,.25,.75)[i%3]]
                expected,visits=run_original('APS',c,inputs,textures)
                cases.append(dict(stage='APS',discarded=expected is None,constants=c,inputs=inputs,expected=expected or[0]*4,depthExpected=expected or[0]*4,visits=visits))
    return textures,cases


def cpp_array(value):
    if isinstance(value,list):return '{'+','.join(cpp_array(v)for v in value)+'}'
    return format(f32(value),'.10e')+'f'


def emit_fixtures(path):
    textures,cases=fixtures()
    out=['#pragma once','// Independent original-retail instruction outputs; no native HLSL evaluated.',
         'struct RigidUVCase { unsigned stage; bool discarded; float constants[51][4]; float input[21]; float expected[21]; float depthExpected[4]; };',
         'inline constexpr float kRigidUVTextures[3][16][4]='+cpp_array(textures)+';',
         'inline constexpr RigidUVCase kRigidUVCases[]={']
    for c in cases:
        out.append('{'+str(STAGES.index(c['stage']))+','+str(c['discarded']).lower()+','+cpp_array(c['constants'])+','+
                   cpp_array(c['inputs'])+','+cpp_array(c['expected'])+','+cpp_array(c['depthExpected'])+'},')
    path.write_text('\n'.join(out+['};','']),encoding='utf-8')


class OriginalRigidUVTests(unittest.TestCase):
    def test_exact_records_and_generated_source(self):
        self.assertEqual((ROOT/'renderer/rigid_uv_shader.hlsl').read_text(encoding='utf-8'),shader.shader_source(IMAGE))
        self.assertEqual([len(INVENTORY[s]['rows'])for s in STAGES],[37,30,73,17])
        self.assertEqual([sum(r['fetch']for r in INVENTORY[s]['rows'].values())for s in STAGES],[4,4,19,1])

    def test_structural_mutations_rejected_without_record_hash(self):
        for profile in shader.PROFILES:
            stage,address,size,offset,_,pairs,_=profile
            original=IMAGE[address-screen.BASE:address-screen.BASE+size]
            anchors=list(range(0,36,4))+list(range(offset,offset+pairs*12,4))+list(range(shader.HEADERS[stage][1],offset,4))+[size-12]
            if stage.endswith('VS'):anchors+=list(range(shader.SEMANTICS[stage][0],shader.SEMANTICS[stage][0]+16,4))
            for at in anchors:
                bad=bytearray(original);bad[at]^=1
                with self.subTest(stage=stage,offset=at),self.assertRaises((ValueError,struct.error)):shader.decode_record(bad,profile)

    def test_complete_branch_and_discard_coverage(self):
        textures,cases=fixtures()
        self.assertEqual([sum(c['stage']==s for c in cases)for s in STAGES],[32,32,48,48])
        self.assertEqual(sum(c['discarded']for c in cases),12)
        opaque=[c for c in cases if c['stage']=='PS']
        for c in opaque:
            taps=c['constants'][44][0]!=0 and c['constants'][31][0]>0
            self.assertEqual(30 in c['visits'],taps);self.assertEqual(57 in c['visits'],taps)
        # Original slot11 computes c43.x*.25; slot22 applies it according to
        # the normalized normal's light-direction result from c36. Keep both
        # rim-selected and rim-unselected normals with nonzero rim settings.
        self.assertEqual(sum(c['constants'][43][0]!=0 for c in opaque),32)
        rimChanged=0
        for c in opaque:
            constants=[r[:]for r in c['constants']];constants[43][0]=0
            rimChanged+=run_original('PS',constants,c['inputs'],textures)[0]!=c['expected']
        self.assertEqual(rimChanged,16)
        self.assertTrue(all(all(math.isfinite(v)for v in c['expected']+c['depthExpected'])for c in cases))

    def test_strict_cutoff_and_split_negate_are_observable(self):
        textures,cases=fixtures();source=next(c for c in cases if c['stage']=='APS')
        c=[r[:]for r in source['constants']];c[48][0]=1
        for column,killed in enumerate((True,True,False,False)):
            inputs=source['inputs'][:];inputs[:2]=[.125+.25*column,.125]
            value,_=run_original('APS',c,inputs,textures)
            self.assertEqual(value is None,killed)
            if column==3:self.assertIsNone(run_original('APS',c,inputs,textures,split_negate=False)[0])
        c[48][0]=0;inputs[:2]=[.125,.125]
        self.assertIsNotNone(run_original('APS',c,inputs,textures)[0])

    def test_uv_rows_animate_uvs_without_geometry_or_uv1(self):
        textures,cases=fixtures()
        for stage in('VS','AVS'):
            source=next(c for c in cases if c['stage']==stage and c['constants'][46][0]!=0 and
                        c['constants'][45][2]!=0 and any(c['constants'][47]))
            for row in(22,45,46,47):
                c=[r[:]for r in source['constants']];c[row]=[0.0]*4
                value,_=run_original(stage,c,source['inputs'],textures)
                self.assertEqual(value[:4],source['expected'][:4]);self.assertNotEqual(value[4:6],source['expected'][4:6])
            inputs=source['inputs'][:];inputs[12:14]=[-101,103]
            self.assertEqual(run_original(stage,source['constants'],inputs,textures)[0],source['expected'])

    def test_fetches_keep_position_normal_and_color_distinct(self):
        textures,_=fixtures();c=[[0.0]*4 for _ in range(51)]
        for base in(0,12,26):
            for row in range(4):c[base+row][row]=1
        c[45]=[1,1,0,0]
        inputs=[-.4,.35,.2,.1,.8,-.3,.7,.4,.2,.9,.25,.75,101,-103]
        for stage in('VS','AVS'):
            value,_=run_original(stage,c,inputs,textures)
            self.assertEqual(value[:4],[f32(v)for v in inputs[:3]]+[1.0])
            normalAt=14 if stage=='VS'else 10;colorAt=17 if stage=='VS'else 13
            self.assertEqual(value[normalAt:normalAt+3],[f32(v)for v in inputs[3:6]])
            self.assertEqual(value[colorAt:colorAt+4],[f32(v)for v in inputs[6:10]])

    def test_depth_rrrr_adaptation_does_not_rewrite_material(self):
        textures,cases=fixtures()
        self.assertTrue(any(c['expected']!=c['depthExpected']for c in cases if c['stage']=='PS'))
        changed=[[[t[0]]*4 for t in texture]for texture in textures]
        source=next(c for c in cases if c['stage']=='PS'and c['constants'][44][0]and c['constants'][31][0]and c['expected']!=c['depthExpected'])
        self.assertNotEqual(run_original('PS',source['constants'],source['inputs'],changed)[0],source['depthExpected'])
        source=next(c for c in cases if c['stage']=='APS'and not c['discarded'])
        self.assertNotEqual(run_original('APS',source['constants'],source['inputs'],changed)[0],source['expected'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--emit-fixtures',type=Path)
    args,rest=parser.parse_known_args()
    if args.emit_fixtures:emit_fixtures(args.emit_fixtures)
    else:unittest.main(argv=[sys.argv[0]]+rest)
