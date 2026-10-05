"""Independent original-instruction oracle for the complete direct particle family.

Instruction fields come from the pinned retail records; the executor below
does not use generated HLSL or its expression emitter. GPU fixtures exercise
both VS and all four PS with distinct base/secondary texels and retained depth.
"""
from pathlib import Path
import argparse
import math
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_particle_shader as particle
import analyze_particle_type5_shader as type5
import analyze_projected_particle_shader as projected
import analyze_dual_particle_shader as dual
import analyze_dual_projected_particle_shader as combined

def f32(value):
    try:return struct.unpack('<f',struct.pack('<f',value))[0]
    except OverflowError:return math.copysign(math.inf,value)

def product(a,b):return 0.0 if a==0 or b==0 else f32(a*b)
def clamp(x):return min(1.0,max(0.0,x))
def recip(x):return math.copysign(math.inf,x) if x==0 else f32(1/x)
def inverse_sqrt(x):return math.inf if x==0 else f32(1/math.sqrt(x))

BASE_TEXTURE=[[f32((x+1)/8),f32((y+1)/8),f32((x+y+1)/16),f32((x+2*y+4)/16)]
              for y in range(4) for x in range(4)]
SECONDARY_TEXTURE=[[f32((y+2)/8),f32((x+2)/8),f32((x+2*y+2)/16),f32((2*x+y+3)/16)]
                   for y in range(4) for x in range(4)]

def linear_wrap(texture,uv):
    # The numerical cases sample exactly representable texel centers, including
    # signed repeat coordinates. This oracle still evaluates the full filter.
    x,y=[f32(f32(v*4)-.5) for v in uv]
    ix,iy=math.floor(x),math.floor(y);fx,fy=f32(x-ix),f32(y-iy)
    result=[]
    for lane in range(4):
        a=texture[(iy%4)*4+ix%4][lane];b=texture[(iy%4)*4+(ix+1)%4][lane]
        c=texture[((iy+1)%4)*4+ix%4][lane];d=texture[((iy+1)%4)*4+(ix+1)%4][lane]
        top=f32(product(a,f32(1-fx))+product(b,fx))
        bottom=f32(product(c,f32(1-fx))+product(d,fx))
        result.append(f32(product(top,f32(1-fy))+product(bottom,fy)))
    return result

def mirror_coord(value):
    q=value%2
    if q>1:q=2-q
    return min(1023,math.floor(q*1024))

def depth_sample(uv,mask):
    x,y=[mirror_coord(v) for v in uv]
    return .75 if 511<=x<=512 and 511<=y<=512 and mask&(1<<((y-511)*2+x-511)) else .25

def execute(report,inputs,constants=None,shadow_mask=0):
    vertex=constants is not None
    registers=[[0.0]*4 for _ in range(16)];exports={i:[0.0]*4 for i in (0,1,2,62)}
    if vertex:registers[0][0]=inputs[19]
    else:
        for i in range(3):registers[i]=[f32(x) for x in inputs[i]]
    literals={i:[struct.unpack('>f',struct.pack('>I',x))[0] for x in words]
              for i,words in report['literal_registers'].items()}
    prev=0.0;predicate=False
    def source(s):
        raw=registers[s['register']] if s['bank']=='temporary' else (
            literals[s['register']] if s['register'] in literals else constants[s['register']])
        values=[raw[i] for i in s['components']]
        if s['absolute_temporary']:values=[abs(x) for x in values]
        if s['negated']:values=[-x for x in values]
        return values
    for block in report['blocks']:
        if block['predicate_condition'] is not None and predicate!=block['predicate_condition']:continue
        for issue in block['issues']:
            row=report['rows'][issue['slot']];f=row['fields']
            if issue['fetch']:
                if vertex:
                    association={11:inputs[0:4],12:inputs[4:8],13:inputs[8:12],
                                 14:inputs[12:15]+[0.0],15:inputs[15:19]}[issue['slot']]
                    for lane,selector in enumerate(f['destination_swizzle']):
                        if selector<4:registers[f['destination_register']][lane]=association[selector]
                        elif selector==4:registers[f['destination_register']][lane]=0.0
                        elif selector==5:registers[f['destination_register']][lane]=1.0
                        else:assert selector==7
                else:
                    old=registers[f['source_register']][:]
                    uv=[old[i] for i in f['source_components'][:2]]
                    offsets=[((x+16)%32-16)*.5 for x in f['offset_fields'][:2]]
                    if f['fetch_constant_index']==2:
                        uv=[f32(uv[i]+f32(offsets[i]/1024)) for i in range(2)]
                        value=[depth_sample(uv,shadow_mask)]*4
                    else:
                        assert offsets==[0.0,0.0]
                        value=linear_wrap(BASE_TEXTURE if f['fetch_constant_index']==0 else SECONDARY_TEXTURE,uv)
                    for lane,selector in enumerate(f['destination_swizzle']):
                        if selector!=7:registers[f['destination_register']][lane]=value[selector]
                continue
            if f['predicated'] and predicate!=f['predicate_condition']:continue
            op=f['vector_opcode'];vector=None
            if f['vector_mask']:
                a,b=[source(s) for s in f['sources'][:2]]
                c=source(f['sources'][2]) if op in (11,12,13,17) else None
                if op==0:vector=[f32(x+y) for x,y in zip(a,b)]
                elif op==1:vector=[product(x,y) for x,y in zip(a,b)]
                elif op==2:vector=[max(x,y) for x,y in zip(a,b)]
                elif op==3:vector=[min(x,y) for x,y in zip(a,b)]
                elif op in (4,5,6,7):
                    compare={4:lambda x,y:x==y,5:lambda x,y:x>y,6:lambda x,y:x>=y,7:lambda x,y:x!=y}[op]
                    vector=[float(compare(x,y)) for x,y in zip(a,b)]
                elif op==11:vector=[f32(product(x,y)+z) for x,y,z in zip(a,b,c)]
                elif op in (12,13):vector=[y if (x==0 if op==12 else x>=0) else z for x,y,z in zip(a,b,c)]
                elif op==17:vector=[f32(f32(product(a[0],b[0])+product(a[1],b[1]))+c[0])]*4
                else:raise ValueError('Unqualified oracle vector '+str(op))
                if f['vector_clamp']:vector=[clamp(x) for x in vector]
            sop=f['scalar_opcode'];scalar=None;next_predicate=predicate
            if sop!=50:
                if sop in (42,43,44,46,47):
                    sw=row['raw'][1]&255
                    reg=(sop&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C)
                    constant=row['raw'][2]&255
                    x=(literals[constant] if constant in literals else constants[constant])[((sw>>6)+3)&3]
                    y=registers[reg][sw&3]
                else:
                    c=source(f['sources'][2]);x,y=c[3],c[0]
                if sop==1:scalar=f32(x+prev)
                elif sop==2:scalar=product(x,y)
                elif sop==3:scalar=product(x,prev)
                elif sop==5:scalar=max(x,y)
                elif sop==6:scalar=min(x,y)
                elif sop==7:scalar=float(x==0)
                elif sop==8:scalar=float(x>0)
                elif sop==11:scalar=f32(x-math.floor(x))
                elif sop==12:scalar=f32(math.trunc(x))
                elif sop==19:scalar=recip(x)
                elif sop==22:scalar=inverse_sqrt(x)
                elif sop in (25,46,47):scalar=f32(x-y)
                elif sop in (27,28,29,30):
                    next_predicate={27:x==0,28:x!=0,29:x>0,30:x>=0}[sop]
                    scalar=0.0 if next_predicate else 1.0
                elif sop in (42,43):scalar=product(x,y)
                elif sop==44:scalar=f32(x+y)
                elif sop==48:scalar=f32(math.sin(x))
                elif sop==49:scalar=f32(math.cos(x))
                else:raise ValueError('Unqualified oracle scalar '+str(sop))
                if f['scalar_clamp']:scalar=clamp(scalar)
            # Coissued reads, including previous scalar, finish before writes.
            destination=exports[f['vector_destination']] if f['export'] else registers[f['vector_destination']]
            if vector is not None:
                for lane in range(4):
                    if f['vector_mask']&(1<<lane):destination[lane]=vector[lane]
            if scalar is not None:
                destination=exports[f['vector_destination']] if f['export'] else registers[f['scalar_destination']]
                for lane in range(4):
                    if f['scalar_mask']&(1<<lane):destination[lane]=scalar
                prev=scalar;predicate=next_predicate
    return exports[62]+exports[0]+exports[1]+exports[2] if vertex else exports[0]

def reports(image):
    ordinary=particle.inspect(image)
    return [ordinary['VS'],type5.inspect(image)], [ordinary['PS'],dual.inspect(image),projected.inspect(image),combined.inspect(image)]

def vertex_cases(vs):
    cases=[]
    for stage,report in enumerate(vs):
        for n in range(32):
            c=[[0.0]*4 for _ in range(26)]
            for row in range(4):c[row][row]=1.0;c[4+row][row]=1.0
            c[8]=[1,1,1,1];c[9][0]=1;c[10][2]=float(n&1)
            c[15]=[255]*4;c[16]=[1,1,1,63.75];c[17]=[1,1,1000,0];c[19][3]=255
            c[18]=[.5,0,0,1]
            c[20][2]=.125 if n&4 else 0
            c[21]=[.5,0,0,0];c[22]=[0,.5,0,0];c[23]=[0,0,.5,0];c[24]=[.5,.5,.25,1]
            c[25][0]=.4 if n&2 else -1
            inputs=[.0625*(n%3-1),.0625*((n//3)%3-1),.5+.0625*(n%4),.125 if n&16 else 0,
                    .125 if n&8 else 0,.0625 if n&8 else 0,0,0,
                    .125,.375,.625,.875,
                    .25+.0625*(n%4),0,1,
                    .125,.25,.375,.5,float(n%4)]
            inputs=[f32(x) for x in inputs];c=[[f32(x) for x in row] for row in c]
            expected=execute(report,inputs,c)
            assert all(math.isfinite(x) for x in expected),(stage,n,expected)
            cases.append((stage,inputs,c,expected))
    return cases

def pixel_cases(ps):
    cases=[]
    coords=[(511.75/1024,511.75/1024),(-511.75/1024,1+511.75/1024),(512.25/1024,512.25/1024)]
    for stage,report in enumerate(ps):
        for mask in range(16):
            for uv in coords:
                for z in (-.125,.25,.75,1.125):
                    for ambient in (0,.375):
                        n=len(cases)
                        inputs=[[(n%4+.5)/4-1,((n//4)%4+.5)/4+1,
                                 ((n//3)%4+.5)/4+2,((n//7)%4+.5)/4-2],
                                [uv[0],uv[1],z,ambient],[.25,.375,.5,.625]]
                        inputs=[[f32(x) for x in row] for row in inputs]
                        cases.append((stage,mask,inputs,execute(report,inputs,shadow_mask=mask)))
    return cases

def emit_cpp(path,image):
    vs,ps=reports(image);vcases=vertex_cases(vs);pcases=pixel_cases(ps)
    def number(x):return '%.9gf'%x if x!=int(x) else str(int(x))+'.0f'
    def array(xs):return '{'+','.join(number(x) for x in xs)+'}'
    lines=['#pragma once','// Independent execution of hash-pinned original particle instructions.',
           'struct ParticleVertexCase { unsigned stage; float input[20]; float constants[26][4]; float expected[16]; };',
           'struct ParticlePixelCase { unsigned stage,shadowMask; float input[3][4]; float expected[4]; };',
           'inline constexpr float kParticleBaseTexture[16][4]={'+','.join(array(x) for x in BASE_TEXTURE)+'};',
           'inline constexpr float kParticleSecondaryTexture[16][4]={'+','.join(array(x) for x in SECONDARY_TEXTURE)+'};',
           'inline constexpr ParticleVertexCase kParticleVertexCases[]={']
    lines.extend('{%du,%s,{%s},%s},'%(stage,array(inp),','.join(array(x) for x in c),array(out))
                 for stage,inp,c,out in vcases)
    lines+=['};','inline constexpr ParticlePixelCase kParticlePixelCases[]={']
    lines.extend('{%du,%du,{%s},%s},'%(stage,mask,','.join(array(x) for x in inp),array(out))
                 for stage,mask,inp,out in pcases)
    lines+=['};'];path.write_text('\n'.join(lines)+'\n',encoding='utf-8')

class ParticleFamilyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=(ROOT/'analysis/simpsons.pe').read_bytes();cls.vs,cls.ps=reports(cls.image)

    def test_complete_original_dual_shadow_schedule(self):
        self.assertEqual(sorted(self.ps[3]['rows']),list(range(3,24)))
        self.assertEqual([r['fields']['fetch_constant_index'] for r in self.ps[3]['rows'].values() if r['fetch']],[2,2,2,2,1,0])
        self.assertEqual(self.ps[3]['literal_registers'][255],[0xBF000000,0,0x3F800000,0x44800000])

    def test_mutated_record_registry_and_cpu_rejected(self):
        for address in (combined.ADDRESS,combined.ADDRESS+combined.HEADER[1],combined.ADDRESS+combined.SIZE-1,
                        0x82CF25EC,0x82CF2604,0x827730CC):
            image=bytearray(self.image);image[address-0x82000000]^=1
            with self.subTest(address=hex(address)),self.assertRaisesRegex(ValueError,'changed'):combined.inspect(image)

    def test_native_mapping_and_exact_channel_offsets(self):
        source=combined.shader_source(self.image)
        self.assertIn('r3=particleDualTexture.Sample(particleDualSampler,r0.zw)',source)
        self.assertIn('r4=particleTexture.Sample(particleSampler,r0.xy)',source)
        for slot,offset,lane,channel in ((3,'0.5,-0.5','x','y'),(4,'-0.5,-0.5','y','x'),
                                       (5,'0.5,0.5','z','w'),(6,'-0.5,0.5','w','z')):
            text=combined.issue(slot,self.ps[3]['rows'][slot])
            self.assertIn('float2('+offset+')/1024.0',text)
            self.assertIn('r5.'+lane+'=fetched.'+channel,text)
            self.assertIn('.xxxx',text)

    def test_scalar_vector_coissue_and_literal_forms(self):
        self.assertIn('k255.z-r0.x',combined.issue(11,self.ps[3]['rows'][11]))
        source=combined.issue(13,self.ps[3]['rows'][13])
        self.assertLess(source.index('float s='),source.index('r5.xyzw=v.xyzw'))
        self.assertEqual(self.ps[3]['rows'][16]['fields']['scalar_opcode'],25)

    def test_full_fixture_matrix_and_second_uv_predicate(self):
        vs=vertex_cases(self.vs);ps=pixel_cases(self.ps)
        self.assertEqual([sum(x[0]==stage for x in vs) for stage in range(2)],[32,32])
        self.assertEqual([sum(x[0]==stage for x in ps) for stage in range(4)],[384]*4)
        # VS c10.z changes secondary UVs while preserving the position, so the
        # original BE16 mode flag must reach the selected vertex program.
        changed=0
        for stage,inp,c,out in vs:
            c2=[row[:] for row in c];c2[10][2]=1-c[10][2]
            other=execute(self.vs[stage],inp,c2)
            if out[6:8]!=other[6:8]:changed+=1
            self.assertEqual(out[:4],other[:4])
        self.assertGreater(changed,0)
        for stage,mask,inp,out in ps:
            self.assertTrue(all(math.isfinite(x) for x in out))
            # Depth visibility changes RGB only; both textures still determine alpha.
            other=execute(self.ps[stage],inp,shadow_mask=mask^15)
            self.assertEqual(out[3],other[3])

    def test_instruction_oracle_matches_independent_shadow_formula(self):
        # A separate closed-form result checks the combined instruction oracle.
        for stage,mask,inp,out in pixel_cases(self.ps):
            base=linear_wrap(BASE_TEXTURE,inp[0][:2]);second=linear_wrap(SECONDARY_TEXTURE,inp[0][2:])
            if stage>=2:
                uv=inp[1][:2];z=1-clamp(inp[1][2]);ambient=inp[1][3]
                visible=lambda dx,dy:float(z>=depth_sample([f32(uv[0]+dx/1024),f32(uv[1]+dy/1024)],mask))
                fx=(uv[0]*1024-.5)%1;fy=(uv[1]*1024-.5)%1
                left=visible(-.5,-.5)*(1-fy)+visible(-.5,.5)*fy
                right=visible(.5,-.5)*(1-fy)+visible(.5,.5)*fy
                light=ambient+(1-ambient)*(left*(1-fx)+right*fx)
            else:light=1
            expected=[2*base[i]*inp[2][i]*(second[i] if stage&1 else 1)*(light if i<3 else 1) for i in range(4)]
            for a,b in zip(out,expected):self.assertAlmostEqual(a,b,places=6)

if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--emit-cpp',type=Path)
    args,rest=parser.parse_known_args()
    if args.emit_cpp:emit_cpp(args.emit_cpp,(ROOT/'analysis/simpsons.pe').read_bytes())
    else:unittest.main(argv=[sys.argv[0]]+rest)
