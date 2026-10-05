"""Pinned transcription of the direct particle type-5 vertex shader 821578E0.

The original CPU path selects this VS from registry 82CF2600 when definition
byte 100 is 5. Its pixel shader remains the ordinary PS82156770 for the
observed flags104=0, mode40=0 case. No arbitrary shader records are admitted.
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
ADDRESS=0x821578E0
SIZE=2028
SHA='c1a368f62bbd1d056d9c0603e99143d8568e53c3b3e57086fac4c5f2e1ead1b0'
HEADER=(0x102A1101,0x374,0x478,0x24,0x7C,0x2F8,0x320,0,0)
CONTROL=((0xC5506009,0x1203),(0x1009600F,0x1200),(0x6015,0x1200),
         (0x601B,0x1200),(0x6021,0x1200),(0x6027,0x1200),
         (0x602D,0x1200),(0x6033,0x1200),(0x6039,0x1200),
         (0x503F,0x1000),(0x6044,0x5600),(0x204A,0x5600),
         (0x304C,0x1000),(0,0xC200),(0x304F,0x1200),(0,0xC400),
         (0x6052,0x1200),(0x1058,0x2200))
FETCHES={
    11:(9,[0,1,2,3],'position','xyzw'),
    12:(8,[0,1,2,3],'velocity','xyzw'),
    13:(2,[1,0,2,3],'uvTime.yxzw','xyzw'),
    14:(5,[0,1,2,7],'size','xyz'),
    15:(4,[0,1,2,3],'color','xyzw'),
}
CPU_SHA='6d1fc540091f1b8f99b8ce155bb1033f2820f1acb5cf32a385f4f935073e57b1'
need=screen.require

def inspect(image):
    particle.inspect(image)
    record=image[ADDRESS-screen.BASE:ADDRESS-screen.BASE+SIZE]
    need(hashlib.sha256(record).hexdigest()==SHA,'Original type-5 particle record changed')
    need(struct.unpack_from('>9I',record)==HEADER,'Type-5 particle header changed')
    need(struct.unpack_from('>2I',record,HEADER[6])==(64,1080),'Type-5 particle code bounds changed')
    literals=struct.unpack_from('>16I',record,HEADER[1])
    code=record[HEADER[1]+64:]
    control,blocks=particle.schedule(code,9)
    need(control==CONTROL,'Type-5 particle control flow changed')
    rows={}
    for block in blocks:
        for issue in block['issues']:
            slot=issue['slot'];raw=screen.words(code,slot*12)
            fields=screen.decode_fetch(raw) if issue['fetch'] else edge.alu(*raw)
            if not issue['fetch']:
                for key in ('absolute_constants','vector_destination_relative',
                            'scalar_destination_relative_or_export_zero',
                            'constant_address_register_relative','constant_0_relative',
                            'constant_1_relative'):
                    need(not fields[key],'Unqualified type-5 modifier '+key)
                need(not any(s['relative_temporary'] for s in fields['sources']),
                     'Unqualified type-5 relative temporary')
                need(fields['vector_opcode'] in (0,1,2,3,5,7,11,12,13) and
                     fields['scalar_opcode'] in (1,2,3,5,6,7,8,12,19,22,29,30,42,43,50),
                     'Unqualified type-5 ALU opcode')
            rows[slot]={**issue,'raw':raw,'fields':fields}
    need(sorted(rows)==list(range(9,89)),'Type-5 particle issue coverage changed')
    need([i for i,row in rows.items() if row['fetch']]==list(FETCHES),
         'Type-5 particle fetch set changed')
    for slot,(destination,swizzle,_,_) in FETCHES.items():
        f=rows[slot]['fields']
        need(f==dict(kind='vertex_fetch',source_register=1,destination_register=destination,
                     destination_swizzle=swizzle,fetch_constant_index=95,source_component=2,
                     format_field=0,stride_dwords=0,offset_field=0,
                     runtime_declaration_patch_verified=False),'Type-5 particle FETCH changed')
    need([(b['first'],b['count'],b['predicate_condition']) for b in blocks
          if b['predicate_condition'] is not None]==[(68,6,True),(74,2,True)],
         'Type-5 particle conditional schedule changed')
    need([(i,rows[i]['fields']['predicate_condition']) for i in rows
          if not rows[i]['fetch'] and rows[i]['fields']['predicated']]==
         [(i,i!=76) for i in (68,69,70,71,72,73,74,75,76,78)],
         'Type-5 particle per-instruction predicates changed')
    need([(i,rows[i]['fields']['vector_destination']) for i in (81,85,86,88)]==
         [(81,62),(85,2),(86,1),(88,0)] and
         all(rows[i]['fields']['export'] for i in (81,85,86,88)),
         'Type-5 particle output interface changed')
    need(screen.words(image,0x82CF2604-screen.BASE)[0]==0x821578E0 and
         screen.words(image,0x82CF25C8-screen.BASE)[0]==0x82156770,
         'Type-5 particle original shader association changed')
    need(hashlib.sha256(image[0x82772CA8-screen.BASE:0x827736B0-screen.BASE]).hexdigest()==CPU_SHA,
         'Type-5 particle CPU setup changed')
    return dict(address=ADDRESS,record_bytes=SIZE,sha256=SHA,header=HEADER,
                control=control,blocks=blocks,rows=rows,
                literal_registers={252+i:list(literals[4*i:4*i+4]) for i in range(4)},
                pixel_address=0x82156770,cpu_setup_sha256=CPU_SHA)

def issue(slot,row):
    if not row['fetch']:
        return particle.issue(slot,row,'VS')
    destination,_,field,swizzle=FETCHES[slot]
    return ('    { // slot%d\n        r%d.%s=input.%s;\n    }'%
            (slot,destination,swizzle,field))

def shader_source(image):
    report=inspect(image)
    lines=['// VS821578E0 selected for direct particle definition type 5.',
           '#include "particle_shader.hlsl"',
           'ParticleOutput VSParticleType5(ParticleInput input) {']
    for reg,words in report['literal_registers'].items():
        lines+=['    const float4 k%d=asfloat(uint4(%s));'%
                (reg,','.join('0x%08Xu'%w for w in words))]
    lines+=['    precise float4 r0=float4(input.originalVertexId,0,0,0),r1=0,r2=0,r3=0,r4=0,r5=0,',
            '        r6=0,r7=0,r8=0,r9=0,r10=0;',
            '    precise float4 output62=0,output0=0,output1=0,output2=0;',
            '    precise float ps=0; bool p0=false;']
    for block in report['blocks']:
        condition=block['predicate_condition']
        if condition is not None:
            lines+=['    [flatten] if('+('p0' if condition else '!p0')+') { // CF'+str(block['control_index'])]
        lines.extend(issue(row['slot'],report['rows'][row['slot']]) for row in block['issues'])
        if condition is not None:lines+=['    }']
    lines+=['    ParticleOutput o;o.position=output62;o.t0=output0;o.t1=output1;o.t2=output2;return o;',
            '}']
    return '\n'.join(lines)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path);parser.add_argument('--emit-hlsl',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes()
    report=inspect(image)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.emit_hlsl:args.emit_hlsl.write_text(shader_source(image),encoding='utf-8')
    print('PASS type-5 particle VS821578E0: complete 80-issue schedule, 5 fetches, 2 conditional blocks')

if __name__=='__main__':main()
