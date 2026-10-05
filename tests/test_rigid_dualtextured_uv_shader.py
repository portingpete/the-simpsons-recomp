"""Original UV microcode interpreter, semantic regressions and GPU fixture data.

The numerical oracle decodes retail instructions, never native HLSL. Its CF
walk executes original predicate and unconditional jumps, preserving old RHS
register values for co-issued arithmetic. GPU tests consume emitted fixtures.
"""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import analyze_rigid_dualtextured_uv_shader as uv
import analyze_rigid_dualtextured_shader as old_dual
import analyze_screen_shaders as screen

ROOT=Path(__file__).resolve().parents[1]
IMAGE=(ROOT/'analysis/simpsons.pe').read_bytes()


def run_original(stage,constants,inputs,textures):
    profile=next(p for p in uv.PROFILES if p[0]==stage)
    address,size=profile[1:3]
    rows=uv.decode_record(IMAGE[address-screen.BASE:address-screen.BASE+size],profile)
    regs=[[0.0]*4 for _ in range(64)];outputs={i:[0.0]*4 for i in (0,1,2,3,4,62)}
    if stage.endswith('VS'):
        # The pinned semantic associations, in source declaration order, are
        # position, normal, color, UV0, and (opaque only) UV1. Execute each
        # original fetch's destination swizzle instead of copying native seeds.
        attributes=(inputs[0:3]+[1.0],inputs[3:6]+[0.0],inputs[6:10],
                    inputs[10:12]+[0.0,0.0],inputs[12:14]+[0.0,0.0])
        for (slot,_,_),attribute in zip(uv.VS_FETCHES[stage],attributes):
            f=rows[slot]['fields'];dest=regs[f['destination_register']]
            for lane,selector in enumerate(f['destination_swizzle']):
                if selector<4:dest[lane]=attribute[selector]
                elif selector==4:dest[lane]=0.0
                elif selector==5:dest[lane]=1.0
    elif stage=='PS':
        regs[0]=inputs[0:4];regs[1]=inputs[4:6]+[0.0,0.0];regs[2]=inputs[6:10]
        regs[3]=inputs[10:13]+[0.0];regs[4]=inputs[13:17]
    else:regs[0]=inputs[:4];regs[1]=inputs[4:8]
    constants={i:row for i,row in enumerate(constants)}
    literals=uv.LITERALS[stage];first=256-len(literals)//4
    for i in range(len(literals)//4):constants[first+i]=list(struct.unpack('>4f',struct.pack('>4I',*literals[i*4:i*4+4])))
    ps=0.0;p0=False;pc=0;visits=[]
    mul=lambda a,b:0.0 if a==0.0 or b==0.0 else a*b
    sat=lambda a:min(max(a,0.0),1.0)
    while pc<len(uv.CF[stage]):
        low,high=uv.CF[stage][pc];op=high>>12
        if op==11:
            take=bool(low&0x2000) or (bool(low&0x4000) and p0==bool(high&0x400))
            pc=(low&0x1FFF) if take else pc+1;continue
        if op not in (1,2):pc+=1;continue
        for slot in range(low&4095,(low&4095)+((low>>12)&7)):
            row=rows[slot];f=row['fields'];visits.append(slot)
            if row['fetch']:
                if stage.endswith('VS'):continue
                coords=regs[f['source_register']]
                x,y=(coords[i] for i in f['source_components'][:2])
                # Encoded offsets are half texels in the sampled resource;
                # fixture resources have width/height four, including t0.
                x+=((f['offset_fields'][0]+16)%32-16)/8
                y+=((f['offset_fields'][1]+16)%32-16)/8
                value=sample_texture(textures[f['fetch_constant_index']],x,y)
                dest=list(regs[f['destination_register']])
                for lane,selector in enumerate(f['destination_swizzle']):
                    if selector<4:dest[lane]=value[selector]
                    elif selector==4:dest[lane]=0.0
                    elif selector==5:dest[lane]=1.0
                regs[f['destination_register']]=dest;continue
            def operand(index):
                source=f['sources'][index]
                values=(regs if source['bank']=='temporary' else constants)[source['register']]
                values=[values[i] for i in source['components']]
                if source['absolute_temporary'] and source['bank']=='temporary':values=[abs(v)for v in values]
                if source['negated']:values=[-v for v in values]
                return values
            a,b=operand(0),operand(1);vop,sop=f['vector_opcode'],f['scalar_opcode']
            vector=[0.0]*4
            if f['vector_mask']:
                if vop==0:vector=[a[i]+b[i] for i in range(4)]
                elif vop==1:vector=[mul(a[i],b[i]) for i in range(4)]
                elif vop in (2,3):vector=[(max if vop==2 else min)(a[i],b[i])for i in range(4)]
                elif vop in (5,6):vector=[float(a[i]>b[i] if vop==5 else a[i]>=b[i])for i in range(4)]
                elif vop==8:vector=[v-math.floor(v)for v in a]
                elif vop==10:vector=[float(math.floor(v))for v in a]
                elif vop==11:
                    c=operand(2);vector=[mul(a[i],b[i])+c[i]for i in range(4)]
                elif vop in (15,16):vector=[sum(mul(a[i],b[i])for i in range(4 if vop==15 else 3))]*4
                elif vop==17:vector=[mul(a[0],b[0])+mul(a[1],b[1])+operand(2)[0]]*4
                else:raise AssertionError(vop)
                if f['vector_clamp']:vector=[sat(v)for v in vector]
            scalar=ps
            if sop!=50:
                if 42<=sop<=47:
                    sw=row['raw'][1]&255
                    sa=constants[row['raw'][2]&255][((sw>>6)+3)&3]
                    sb=regs[(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)][sw&3]
                    scalar=mul(sa,sb)if sop<44 else sa+sb if sop<46 else sa-sb
                else:
                    c=operand(2);sa,sb=c[3],c[0]
                    if sop==0:scalar=sa+sb
                    elif sop==1:scalar=sa+ps
                    elif sop==5:scalar=max(sa,sb)
                    elif sop==10:scalar=float(sa!=0)
                    elif sop==11:scalar=sa-math.floor(sa)
                    elif sop==13:scalar=float(math.floor(sa))
                    elif sop==19:scalar=1.0/sa if sa else math.inf
                    elif sop==22:scalar=1.0/math.sqrt(sa)if sa else math.inf
                    elif sop in (28,29):p0=(sa!=0 if sop==28 else sa>0);scalar=0.0 if p0 else 1.0
                    elif sop in (48,49):scalar=(math.sin if sop==48 else math.cos)(sa)
                    else:raise AssertionError(sop)
                if f['scalar_clamp']:scalar=sat(scalar)
            dest=outputs[f['vector_destination']] if f['export'] else regs[f['vector_destination']]
            for lane in range(4):
                if f['vector_mask']&(1<<lane):dest[lane]=vector[lane]
            if f['scalar_mask']:
                for lane in range(4):
                    if f['scalar_mask']&(1<<lane):regs[f['scalar_destination']][lane]=scalar
            if sop!=50:ps=scalar
        if op==2:break
        pc+=1
    if stage=='VS':out=outputs[62]+outputs[0]+outputs[1][:2]+outputs[2]+outputs[3][:3]+outputs[4]
    elif stage=='AVS':out=outputs[62]+outputs[0]+outputs[1]
    else:out=outputs[0]
    return out,visits


def sample_texture(texture,x,y):
    # GPU fixture uses original linear/wrap material sampler for each texture.
    size=4;x=x*size-.5;y=y*size-.5;ix=math.floor(x);iy=math.floor(y);fx=x-ix;fy=y-iy
    value=[0.0]*4
    for dx,dy,weight in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)):
        texel=texture[((iy+dy)%size)*size+(ix+dx)%size]
        for lane in range(4):value[lane]+=texel[lane]*weight
    return value


def fixtures():
    textures=[[[.06+.04*((i*3+bank)%13),.13+.03*((i+bank*4)%11),.17+.04*((i*5+bank)%9),.22+.045*((i*7+bank)%12)]for i in range(16)]for bank in range(3)]
    cases=[]
    for stage in ('VS','AVS','PS','APS'):
        for variant in range(8):
            constants=[[0.0]*4 for _ in range(48 if stage.endswith('VS') else 51)]
            if stage.endswith('VS'):
                for base in (0,12,26):
                    for row in range(4):constants[base+row][row]=1.0
                constants[12][3]=.25;constants[13][3]=-.1
                constants[22]=[-1.4+.43*variant,0,0,0]
                constants[43]=[.37,-.41,.13,.29];constants[44]=[.24,.73,-.2,-.67]
                constants[45]=[.09,.13,.17,.21];constants[46]=[1.2,-.8,.63,1.4]
                constants[47]=[.027,-.037,.041,-.053]
                inputs=[-.4+.11*variant,.35-.08*variant,.2+.07*variant,.1,.8,-.3,.71,.43,.29,.89,.13,.73,.61,.24]
            else:
                constants[30]=[.35,.3,1.2,1];constants[31]=[float(variant%2),0,0,0]
                constants[36]=[.13,.84,-.2,0];constants[40]=[2.25,0,0,.38]
                constants[42]=[float(variant%4),.65,.4,.82];constants[48]=[1.0,0,0,0]
                constants[49]=[.31,.43,0.0,0]
                # Binary fractional texture positions avoid implementation
                # differences in the GPUs' fixed-point filtering weights.
                inputs=([.125+.0625*variant,.625-.03125*variant,.4375,.25,.71,.37,.0,.0,.36,1.0,.13,.82,-.22,.4,.6,.7,.8]
                        if stage=='PS' else [.125+.0625*variant,.625-.03125*variant,.4375,.25,.4,.6,.7,.8])
            expected,visits=run_original(stage,constants,inputs,textures)
            cases.append(dict(stage=stage,constants=constants,inputs=inputs,expected=expected,visits=visits))
    return textures,cases


def emit_fixtures(path):
    textures,cases=fixtures();out=['#pragma once','// Independent original-retail microcode outputs; no HLSL evaluated.',
                                 'struct UVCase { unsigned stage; float constants[51][4]; float input[23]; float expected[23]; };',
                                 'inline constexpr float kUVTextures[3][16][4]='+cpp_array(textures)+';',
                                 'inline constexpr UVCase kUVCases[]={']
    for case in cases:
        stage=('VS','AVS','PS','APS').index(case['stage'])
        out.append('{'+str(stage)+','+cpp_array(case['constants'])+','+cpp_array(case['inputs'])+','+cpp_array(case['expected'])+'},')
    out += ['};'];path.write_text('\n'.join(out)+'\n',encoding='utf-8')


def cpp_array(value):
    if isinstance(value,list):return '{'+','.join(cpp_array(v)for v in value)+'}'
    return format(value,'.10e')+'f'


class OriginalUVTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.inventory=uv.inspect(IMAGE)
    def test_pinned_source(self):
        self.assertEqual((ROOT/'renderer/rigid_dualtextured_uv_shader.hlsl').read_text(encoding='utf-8'),uv.shader_source(IMAGE))
    def test_complete_issue_and_fetch_counts(self):
        self.assertEqual([len(self.inventory[s]['rows'])for s in ('VS','PS','AVS','APS')],[39,60,30,24])
        self.assertEqual([sum(r['fetch']for r in self.inventory[s]['rows'].values())for s in ('VS','PS','AVS','APS')],[5,11,4,5])
    def test_old_dual_is_not_the_uv_shader(self):
        for new,old in zip(uv.PROFILES[:2],old_dual.PROFILES):
            self.assertNotEqual(IMAGE[new[1]-screen.BASE+new[3]:new[1]-screen.BASE+new[3]+new[4]],
                                IMAGE[old[1]-screen.BASE+old[3]:old[1]-screen.BASE+old[3]+old[4]])
    def test_semantic_mutations_rejected_without_hash(self):
        for profile in uv.PROFILES:
            stage,address,size,offset,_,pairs,_=profile
            original=IMAGE[address-screen.BASE:address-screen.BASE+size]
            anchors=list(range(0,36,4))+list(range(offset,offset+pairs*12,4))+[size-12]
            anchors+=list(range(uv.HEADERS[stage][1],offset,4))
            anchors+=(list(range(712,732,4))if stage=='VS' else list(range(636,652,4))if stage=='AVS' else [])
            for at in anchors:
                changed=bytearray(original);changed[at]^=1
                with self.subTest(stage=stage,offset=at),self.assertRaises((ValueError,struct.error)):
                    uv.decode_record(changed,profile)
    def test_all_original_blend_paths_and_shadow_branches(self):
        _,cases=fixtures()
        opaque=[c for c in cases if c['stage']=='PS'];alpha=[c for c in cases if c['stage']=='APS']
        self.assertTrue(any(14 in c['visits']for c in opaque));self.assertTrue(any(19 in c['visits']for c in opaque))
        self.assertTrue(any(43 in c['visits']for c in opaque));self.assertTrue(any(43 not in c['visits']for c in opaque))
        for slot in (12,17,21):self.assertTrue(any(slot in c['visits']for c in alpha))
        self.assertTrue(any(21 not in c['visits']for c in alpha))
        self.assertTrue(all(all(math.isfinite(v)for v in c['expected'])for c in cases))
    def test_wave_and_material_uv_changes_reach_exports(self):
        textures,cases=fixtures()
        for stage in ('VS','AVS'):
            case=next(c for c in cases if c['stage']==stage)
            disabled=[row[:]for row in case['constants']];disabled[44]=[0,0,0,0];disabled[45]=[0,0,0,0];disabled[47]=[0,0,0,0]
            plain,_=run_original(stage,disabled,case['inputs'],textures)
            self.assertNotEqual(plain[:3],case['expected'][:3]);self.assertNotEqual(plain[4:8],case['expected'][4:8])
    def test_vertex_fetch_semantics_keep_position_and_normal_distinct(self):
        textures,_=fixtures();constants=[[0.0]*4 for _ in range(48)]
        for base in (0,12,26):
            for row in range(4):constants[base+row][row]=1.0
        constants[46]=[1,1,1,1]
        inputs=[-.4,.35,.2,.1,.8,-.3,.7,.4,.2,.9,.25,.75,.1,.2]
        for stage in ('VS','AVS'):
            result,_=run_original(stage,constants,inputs,textures)
            self.assertEqual(result[:4],inputs[:3]+[1.0])
        source=uv.shader_source(IMAGE)
        self.assertIn('r0=float4(0,input.position)',source)
        self.assertIn('r3=float4(input.normal,0)',source)


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--emit-fixtures',type=Path)
    args,remaining=parser.parse_known_args()
    if args.emit_fixtures:emit_fixtures(args.emit_fixtures)
    else:unittest.main(argv=[sys.argv[0]]+remaining)
