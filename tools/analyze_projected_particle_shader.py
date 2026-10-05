"""Offline transcription of the original projected-shadow particle PS82156B60.

The VS remains821570E0. Native depth sampling explicitly expands the retained
D24FS8-equivalent depth to RRRR before original FETCH channel selection.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import analyze_particle_shader as particle
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_fourtap_shaders as four

ROOT=Path(__file__).resolve().parents[1]
ADDRESS=0x82156B60
SIZE=652
SHA='6b4a47e0cc4cc014e9c5226ee70dc1ec413ba9e7d020d4b3d2b20b197a287ccb'
HEADER=(0x102A1100,0x15C,0x130,0x24,0x7C,0x108,0x130,0,0)
CONTROL=((22368258,4608),(0,50176),(24583,4608),(24589,8704))
XENOS=Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h')
XENOS_SHA='7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227'
CPU_SHA='6d1fc540091f1b8f99b8ce155bb1033f2820f1acb5cf32a385f4f935073e57b1'
need=screen.require

def inspect(image):
    particle.inspect(image)
    record=image[ADDRESS-screen.BASE:ADDRESS-screen.BASE+SIZE]
    need(hashlib.sha256(record).hexdigest()==SHA,'Original projected particle record changed')
    need(struct.unpack_from('>9I',record)==HEADER,'Projected particle header changed')
    need(struct.unpack_from('>2I',record,HEADER[6])==(64,240),'Projected particle code bounds changed')
    literals=struct.unpack_from('>16I',record,HEADER[1])
    need(literals==(0,)*12+(0x44800000,0xBF000000,0x3F800000,0),'Projected particle literals changed')
    code=record[HEADER[1]+64:];control,blocks=particle.schedule(code,2)
    need(control==CONTROL,'Projected particle schedule changed')
    rows={}
    for block in blocks:
        for issue in block['issues']:
            raw=screen.words(code,issue['slot']*12)
            f=screen.decode_fetch(raw) if issue['fetch'] else edge.alu(*raw)
            if not issue['fetch']:
                for key in ('absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero',
                            'constant_address_register_relative','constant_0_relative','constant_1_relative','predicated'):
                    need(not f[key],'Unqualified projected particle modifier '+key)
                need(not any(s['relative_temporary'] for s in f['sources']),'Projected particle relative register')
            rows[issue['slot']]={**issue,'raw':raw,'fields':f}
    need(sorted(rows)==list(range(2,19)),'Projected particle instruction coverage differs')
    for slot,(swizzle,offset) in {2:([1,7,7,7],[1,31,0]),3:([7,0,7,7],[31,31,0]),
                                4:([7,7,3,7],[1,1,0]),5:([7,7,7,2],[31,1,0]),
                                6:([0,1,2,3],[0,0,0])}.items():
        f=rows[slot]['fields']
        need(f['kind']=='texture_fetch' and f['fetch_constant_index']==(0 if slot==6 else 2) and
             f['source_register']==(0 if slot==6 else 1) and f['destination_register']==(3 if slot==6 else 4) and
             f['destination_swizzle']==swizzle and f['offset_fields']==offset and f['normalized_coordinates'] and
             f['source_components']==([0,1,0] if slot==6 else [0,1,1]) and f['dimension_field']==1 and
             f['mag_filter']==f['min_filter']==f['mip_filter']==3 and f['computed_lod'] and
             not f['register_lod'] and not f['register_gradients'] and not f['lod_bias_field'] and
             not rows[slot]['raw'][1]&0x80000000,'Projected particle FETCH differs')
    need(hashlib.sha256(image[0x82772CA8-screen.BASE:0x827736B0-screen.BASE]).hexdigest()==CPU_SHA,
         'Original projected particle CPU setup changed')
    need(hashlib.sha256(XENOS.read_bytes()).hexdigest()==XENOS_SHA,'Projected sampler reference changed')
    need(hashlib.sha256(four.UCODE.read_bytes()).hexdigest()==four.UCODE_SHA,'Projected offset reference changed')
    need('float offset_x() const { return data_.offset_x * 0.5f; }' in four.UCODE.read_text(encoding="utf-8"),
         'Projected fractional offset interpretation changed')
    return dict(address=ADDRESS,record_bytes=SIZE,sha256=SHA,header=HEADER,control=control,blocks=blocks,rows=rows,
        literal_registers={252+i:list(literals[4*i:4*i+4]) for i in range(4)},
        depth=rigid.depth_contract(image),cpu_setup_sha256=CPU_SHA,
        sampler=dict(stage=2,min='point',mag='point',mip='point',u='mirror',v='mirror',
                     reference=str(XENOS),sha256=XENOS_SHA),
        offsets=dict(reference=str(four.UCODE),sha256=four.UCODE_SHA,scale=0.5))

def issue(slot,row):
    if not row['fetch']:return particle.issue(slot,row,'PS')
    f=row['fields']
    if slot==6:return '    { // slot6\n        r3=particleTexture.Sample(particleSampler,r0.xy);\n    }'
    dx,dy=[((v+16)%32-16)*.5 for v in f['offset_fields'][:2]]
    destination='xyzw'[slot-2];source='xyzw'[f['destination_swizzle'][slot-2]]
    return ('    { // slot%d: D24FS8 depth RRRR before original channel selection\n'
            '        precise float2 uv=r1.xy+float2(%s,%s)/1024.0;\n'
            '        precise float4 fetched=particleShadow.Sample(particleShadowSampler,uv).xxxx;\n'
            '        r4.%s=fetched.%s;\n    }')%(slot,dx,dy,destination,source)

def shader_source(image):
    report=inspect(image)
    lines=['// PS82156B60 projected-shadow particles; pinned offline transcription.',
        particle.shader_source(image),'Texture2D<float> particleShadow:register(t2);',
        'SamplerState particleShadowSampler:register(s2);',
        'float4 PSParticleProjected(ParticleOutput input):SV_Target0 {']
    for reg,words in report['literal_registers'].items():
        lines+=['    const float4 k%d=asfloat(uint4(%s));'%(reg,','.join('0x%08Xu'%w for w in words))]
    lines+=['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=0,r4=0,r5=0,output0=0;',
            '    precise float ps=0; bool p0=false;']
    for block in report['blocks']:
        need(block['predicate_condition'] is None,'Projected particle conditional block')
        lines += [issue(row['slot'],report['rows'][row['slot']]) for row in block['issues']]
    lines+=['    return output0;','}',
        'cbuffer ParticleProjectedProbe:register(b3) { float4 probeT0;float4 probeT1;float4 probeT2; };',
        'ParticleOutput VSParticleProjectedProbe(uint id:SV_VertexID) {',
        '    ParticleOutput o;o.position=float4(id==2?3.0:-1.0,id==1?3.0:-1.0,0,1);',
        '    o.t0=probeT0;o.t1=probeT1;o.t2=probeT2;return o;','}']
    return '\n'.join(lines)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path);parser.add_argument('--emit-hlsl',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    if args.emit_hlsl:args.emit_hlsl.write_text(shader_source(image),encoding='utf-8')
    print('PASS projected particle PS82156B60: complete17-issue schedule, five fetches, point/mirror sampler, D24FS8 RRRR')
if __name__=='__main__':main()
