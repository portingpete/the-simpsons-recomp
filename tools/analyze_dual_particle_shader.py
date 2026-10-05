"""Offline transcription of the exact dual-texture particle PS82156948.

The original shader samples logical texture1 before texture0. The native
wrapper maps logical texture1 to host t3 because t1 is reserved for the
per-quad packed destination snapshot used by native particle blending.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import analyze_particle_shader as particle
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge

ROOT=Path(__file__).resolve().parents[1]
ADDRESS=0x82156948
SIZE=532
SHA='c778503e0ae3cd600d6fdec7be173d745829d42acf9041a0ea83d9de0df3be84'
HEADER=(0x102A1100,0x15C,0xB8,0x24,0x78,0x108,0x130,0,0)
CONTROL=((335874,4608),(0,50176),(20484,8704),(0,0))
CPU_SHA='6d1fc540091f1b8f99b8ce155bb1033f2820f1acb5cf32a385f4f935073e57b1'
XENOS=Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h')
XENOS_SHA='7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227'
need=screen.require

def inspect(image):
    particle.inspect(image)
    record=image[ADDRESS-screen.BASE:ADDRESS-screen.BASE+SIZE]
    need(hashlib.sha256(record).hexdigest()==SHA,'Original dual particle record changed')
    need(struct.unpack_from('>9I',record)==HEADER,'Dual particle header changed')
    need(struct.unpack_from('>2I',record,HEADER[6])==(64,120),'Dual particle code bounds changed')
    literals=struct.unpack_from('>16I',record,HEADER[1])
    need(literals==(0,)*16,'Dual particle literals changed')
    code=record[HEADER[1]+64:];control,blocks=particle.schedule(code,2)
    need(control==CONTROL,'Dual particle control schedule changed')
    rows={}
    for block in blocks:
        need(block['predicate_condition'] is None,'Dual particle conditional block')
        for issue in block['issues']:
            raw=screen.words(code,issue['slot']*12)
            f=screen.decode_fetch(raw) if issue['fetch'] else edge.alu(*raw)
            if not issue['fetch']:
                for key in ('absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero',
                            'constant_address_register_relative','constant_0_relative','constant_1_relative','predicated'):
                    need(not f[key],'Unqualified dual particle modifier '+key)
                need(not any(s['relative_temporary'] for s in f['sources']),'Dual particle relative register')
            rows[issue['slot']]={**issue,'raw':raw,'fields':f}
    need(sorted(rows)==list(range(2,9)),'Dual particle instruction coverage differs')
    for slot,constant,source,destination,components in (
        (2,1,0,1,[2,3,2]),(3,0,0,0,[0,1,0])):
        f=rows[slot]['fields']
        need(f['kind']=='texture_fetch' and f['fetch_constant_index']==constant and
             f['source_register']==source and f['destination_register']==destination and
             f['destination_swizzle']==[0,1,2,3] and f['source_components']==components and
             f['offset_fields']==[0,0,0] and f['normalized_coordinates'] and f['dimension_field']==1 and
             f['mag_filter']==f['min_filter']==f['mip_filter']==3 and f['computed_lod'] and
             not f['register_lod'] and not f['register_gradients'] and not f['lod_bias_field'] and
             not rows[slot]['raw'][1]&0x80000000,'Dual particle FETCH differs')
    f=rows[4]['fields']
    need(f['vector_opcode']==1 and f['vector_mask']==15 and f['vector_destination']==2 and not f['export'],
         'Dual particle base modulation differs')
    for slot,mask in ((5,1),(6,2),(7,4),(8,8)):
        f=rows[slot]['fields']
        need(f['vector_opcode']==17 and f['vector_mask']==mask and f['export'],
             'Dual particle DOT2ADD export differs')
    need(hashlib.sha256(image[0x82772CA8-screen.BASE:0x827736B0-screen.BASE]).hexdigest()==CPU_SHA,
         'Original dual particle CPU setup changed')
    need(hashlib.sha256(XENOS.read_bytes()).hexdigest()==XENOS_SHA,'Dual particle sampler reference changed')
    return dict(address=ADDRESS,record_bytes=SIZE,sha256=SHA,header=HEADER,control=control,blocks=blocks,rows=rows,
        literal_registers={252+i:list(literals[4*i:4*i+4]) for i in range(4)},cpu_setup_sha256=CPU_SHA,
        host_texture_slot=3,sampler=dict(stage=1,min='linear',mag='linear',mip='point',u='repeat',v='repeat',w='repeat',
            reference=str(XENOS),sha256=XENOS_SHA))

def issue(slot,row):
    if not row['fetch']:return particle.issue(slot,row,'PS')
    if slot==2:return '    { // slot2\n        r1=particleDualTexture.Sample(particleDualSampler,r0.zw);\n    }'
    if slot==3:return '    { // slot3\n        r0=particleTexture.Sample(particleSampler,r0.xy);\n    }'
    raise ValueError('Unexpected dual particle FETCH')

def shader_source(image):
    report=inspect(image)
    lines=['// PS82156948 dual-texture particles; pinned offline transcription.',
           '#include "particle_projected_shader.hlsl"',
           '// Logical Xenon texture1 is host t3; host t1 is the packed destination snapshot.',
           'Texture2D<float4> particleDualTexture:register(t3);',
           'SamplerState particleDualSampler:register(s1);',
           'float4 PSParticleDual(ParticleOutput input):SV_Target0 {']
    for reg,words in report['literal_registers'].items():
        lines+=['    const float4 k%d=asfloat(uint4(%s));'%(reg,','.join('0x%08Xu'%w for w in words))]
    lines+=['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,output0=0;',
            '    precise float ps=0; bool p0=false;']
    for block in report['blocks']:
        lines += [issue(row['slot'],report['rows'][row['slot']]) for row in block['issues']]
    lines+=['    return output0;','}']
    return '\n'.join(lines)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path);parser.add_argument('--emit-hlsl',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    if args.emit_hlsl:args.emit_hlsl.write_text(shader_source(image),encoding='utf-8')
    print('PASS dual particle PS82156948: two exact fetches, base modulation, four DOT2ADD exports, linear/linear/point repeat sampler')
if __name__=='__main__':main()
