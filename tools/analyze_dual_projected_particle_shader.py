"""Exact original PS82156DF0 dual-texture/projected-shadow particle program.

The original CPU selects its two vertex shaders and four pixel shaders
independently. Logical texture1 is native t3 because t1 retains the preceding
packed particle destination. Original stage2 depth samples expand to RRRR.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import analyze_particle_shader as particle
import analyze_particle_type5_shader as type5
import analyze_projected_particle_shader as projected
import analyze_dual_particle_shader as dual
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge

ROOT=Path(__file__).resolve().parents[1]
ADDRESS=0x82156DF0
SIZE=752
SHA='69175b7a6c5f6a923f869d1a800b4e2c282a5ec937c7968a3fb21d35ad7ac494'
HEADER=(0x102A1100,0x184,0x16C,0x24,0x80,0x130,0x158,0,0)
CONTROL=((0x05556003,0x1200),(0,0xC400),(0x6009,0x1200),
         (0x600F,0x1200),(0x3015,0x2200),(0,0))
REGISTRY=((0x82CF25C8,0x82156770),(0x82CF25D4,0x82156948),
          (0x82CF25E0,0x82156B60),(0x82CF25EC,ADDRESS),
          (0x82CF25F8,0x821570E0),(0x82CF2604,0x821578E0))
need=screen.require

def inspect(image):
    ordinary=particle.inspect(image)
    type5.inspect(image); projected.inspect(image); dual.inspect(image)
    for address,expected in REGISTRY:
        need(struct.unpack_from('>I',image,address-screen.BASE)[0]==expected,
             'Original particle family registry changed')
    record=image[ADDRESS-screen.BASE:ADDRESS-screen.BASE+SIZE]
    need(hashlib.sha256(record).hexdigest()==SHA,'Original dual/projected particle record changed')
    need(struct.unpack_from('>9I',record)==HEADER,'Dual/projected particle header changed')
    need(struct.unpack_from('>2I',record,HEADER[6])==(64,300),
         'Dual/projected particle code bounds changed')
    literals=struct.unpack_from('>16I',record,HEADER[1])
    need(literals==(0,)*12+(0xBF000000,0,0x3F800000,0x44800000),
         'Dual/projected particle literals changed')
    code=record[HEADER[1]+64:]; control,blocks=particle.schedule(code,3)
    need(control==CONTROL,'Dual/projected particle control schedule changed')
    rows={}
    for block in blocks:
        need(block['predicate_condition'] is None,'Dual/projected particle conditional block')
        for issue in block['issues']:
            raw=screen.words(code,issue['slot']*12)
            f=screen.decode_fetch(raw) if issue['fetch'] else edge.alu(*raw)
            if not issue['fetch']:
                for key in ('absolute_constants','vector_destination_relative',
                            'scalar_destination_relative_or_export_zero','predicated',
                            'constant_address_register_relative','constant_0_relative','constant_1_relative'):
                    need(not f[key],'Unqualified dual/projected modifier '+key)
                need(not any(s['relative_temporary'] for s in f['sources']),
                     'Dual/projected relative register')
                need(f['vector_opcode'] in (0,1,2,6,11,17) and
                     f['scalar_opcode'] in (5,11,25,46,50),
                     'Unqualified dual/projected ALU opcode')
            rows[issue['slot']]={**issue,'raw':raw,'fields':f}
    need(sorted(rows)==list(range(3,24)),'Dual/projected particle instruction coverage differs')
    taps={3:([1,7,7,7],[1,31,0]),4:([7,0,7,7],[31,31,0]),
          5:([7,7,3,7],[1,1,0]),6:([7,7,7,2],[31,1,0])}
    for slot in range(3,9):
        f=rows[slot]['fields']; shadow=slot<7
        swizzle,offset=taps[slot] if shadow else ([0,1,2,3],[0,0,0])
        need(f==dict(kind='texture_fetch',source_register=1 if shadow else 0,
             destination_register=5 if shadow else (3 if slot==7 else 4),
             destination_swizzle=swizzle,fetch_constant_index=2 if shadow else (1 if slot==7 else 0),
             normalized_coordinates=True,source_components=[0,1,1] if shadow else ([2,3,2] if slot==7 else [0,1,0]),
             dimension_field=1,mag_filter=3,min_filter=3,mip_filter=3,anisotropy=7,
             arbitrary_filter=0,volume_mag_filter=3,volume_min_filter=3,computed_lod=True,
             register_lod=False,register_gradients=False,fetch_valid_only=True,sample_location=0,
             lod_bias_field=0,offset_fields=offset) and not rows[slot]['raw'][1]&0x80000000,
             'Dual/projected particle FETCH differs')
    for slot,mask in ((12,8),(21,1),(22,2),(23,4)):
        f=rows[slot]['fields']
        need(f['export'] and f['vector_opcode']==17 and f['vector_mask']==mask,
             'Dual/projected DOT2ADD export differs')
    need(set(r['fields']['vector_destination'] for r in ordinary['VS']['rows'].values()
             if not r['fetch'] and r['fields']['export'])=={62,0,1,2},
         'Ordinary particle output interface changed')
    return dict(address=ADDRESS,record_bytes=SIZE,sha256=SHA,header=HEADER,
                control=control,blocks=blocks,rows=rows,
                literal_registers={252+i:list(literals[4*i:4*i+4]) for i in range(4)},
                vertex_addresses=[0x821570E0,0x821578E0],
                pixel_addresses=[address for _,address in REGISTRY[:4]],
                cpu_setup_sha256=dual.CPU_SHA,depth=projected.inspect(image)['depth'],
                texture_slots={'base':0,'secondary':3,'depth':2,'packed_destination':1})

def issue(slot,row):
    if not row['fetch']: return particle.issue(slot,row,'PS')
    f=row['fields']
    if slot==7:return '    { // slot7\n        r3=particleDualTexture.Sample(particleDualSampler,r0.zw);\n    }'
    if slot==8:return '    { // slot8\n        r4=particleTexture.Sample(particleSampler,r0.xy);\n    }'
    dx,dy=[((v+16)%32-16)*.5 for v in f['offset_fields'][:2]]
    lane='xyzw'[slot-3]; channel='xyzw'[f['destination_swizzle'][slot-3]]
    return ('    { // slot%d: original stage2 D24FS8 depth expands to RRRR\n'
            '        precise float2 uv=r1.xy+float2(%s,%s)/1024.0;\n'
            '        precise float4 fetched=particleShadow.Sample(particleShadowSampler,uv).xxxx;\n'
            '        r5.%s=fetched.%s;\n    }')%(slot,dx,dy,lane,channel)

def shader_source(image):
    report=inspect(image)
    lines=['// Exact original PS82156DF0; independently selected by both particle VS records.',
           '#include "particle_dual_shader.hlsl"',
           'float4 PSParticleDualProjected(ParticleOutput input):SV_Target0 {']
    for reg,words in report['literal_registers'].items():
        lines+=['    const float4 k%d=asfloat(uint4(%s));'%(reg,','.join('0x%08Xu'%w for w in words))]
    lines+=['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=0,r4=0,r5=0,r6=0,output0=0;',
            '    precise float ps=0; bool p0=false;']
    for block in report['blocks']:
        lines.extend(issue(row['slot'],report['rows'][row['slot']]) for row in block['issues'])
    return '\n'.join(lines+['    return output0;','}'])+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path);parser.add_argument('--emit-hlsl',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.emit_hlsl:args.emit_hlsl.write_text(shader_source(image),encoding='utf-8')
    print('PASS dual/projected particle PS82156DF0:21 original issues,6 fetches,4 DOT2ADD exports;2VSx4PS registry pinned')

if __name__=='__main__':main()
