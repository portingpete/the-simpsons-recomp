"""Offline inventory of the immediate particle shaders reached by run279.

This is the direct engine path82772CA8, not the catalog FX named 'particles'.
The complete VS schedule includes four predicate-controlled EXEC blocks and
eight literal registers (248..255). Omitting either silently changes geometry.
No runtime draw admission, guest writes or shader aliasing is performed here.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid

ROOT=Path(__file__).resolve().parents[1]
PROFILES={
    'VS':(0x821570E0,2044,9,'04ca6c4cf22f0045e6f566ae457f5f4df11b019940d97f3efbca4376fa29319d'),
    'PS':(0x82156770,468,2,'1b529ab33b380e0f088a27db592508c92f44af25d1ddd07bb6e56fc6a33357e3'),
}
HEADERS={
    'VS':(0x102A1101,0x3A4,0x458,0x24,0x74,0x328,0x350,0,0),
    'PS':(0x102A1100,0x134,0xA0,0x24,0x74,0xE0,0x108,0,0),
}
CF={
    'VS':((0xC5506009,0x1203),(0x1009600F,0x1200),(0x6015,0x1200),
          (0x601B,0x1200),(0x6021,0x1200),(0x6027,0x1200),(0x602D,0x1200),
          (0x4033,0x1000),(0x6037,0x5600),(0x203D,0x5600),(0x603F,0x1000),
          (0x1045,0x1200),(0x6046,0x5200),(0x104C,0x5200),(0,0xC200),
          (0x104D,0x1200),(0,0xC400),(0x304E,0x2200)),
    'PS':((0x11002,0x1200),(0,0xC400),(0x4003,0x2200),(0,0)),
}
VERTEX_FETCHES={
    11:(6,[0,1,2,3]),12:(9,[0,1,2,3]),13:(2,[2,1,0,3]),
    14:(5,[0,1,2,7]),15:(3,[0,1,2,3]),
}
DECLARATION=(0,0x001A23A6,0,16,0x001A23A6,0x00050000,
             32,0x001A23A6,0x00050100,48,0x002A23B9,0x00050200,
             60,0x001A2086,0x000A0000,0x00FF0000,0xFFFFFFFF,0)

def schedule(code,pairs):
    """Decode bounded straight-line and predicate-controlled EXEC blocks."""
    require=screen.require
    require(len(code)%12==0 and 0<pairs<len(code)//12-1,'Invalid particle code bounds')
    control=[];blocks=[];slots=[];ended=False
    for pair in range(pairs):
        a,b,c=screen.words(code,pair*12)
        for lo,hi in ((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)):
            control.append((lo,hi));op=hi>>12
            require(not ended or (lo==0 and hi==0),'Particle control after EXEC_END')
            if op in (1,2,5):
                first,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
                require(0<count<=6 and sequence>>(count*2)==0,'Invalid particle EXEC sequence')
                require(not lo&0x8000 and not hi&0x800,'Unqualified particle yield/address mode')
                require(pairs<=first and first+count<len(code)//12,'Particle EXEC outside issue region')
                condition=bool(hi&0x400) if op==5 else None
                block={'control_index':len(control)-1,'opcode':op,'first':first,'count':count,
                       'predicate_condition':condition,'predicate_clean':bool(hi&0x200),'issues':[]}
                for i in range(count):
                    slot=first+i;flags=(sequence>>(2*i))&3
                    slots.append(slot)
                    block['issues'].append({'slot':slot,'fetch':bool(flags&1),'serialize':bool(flags&2)})
                blocks.append(block);ended=op==2
            else:
                require((op==12 and lo==0 and hi in (0xC200,0xC400)) or (op==0 and lo==hi==0),
                        'Unsupported particle control flow')
    require(ended and sorted(slots)==list(range(pairs,len(code)//12-1)) and len(slots)==len(set(slots)),
            'Incomplete or repeated particle instruction coverage')
    return tuple(control),blocks

def inspect(image):
    screen.require(struct.unpack_from('>18I',image,0x821580D0-screen.BASE)==DECLARATION,
                   'Original particle declaration changed')
    result={}
    for stage,(va,size,pairs,digest) in PROFILES.items():
        record=image[va-screen.BASE:va-screen.BASE+size]
        screen.require(hashlib.sha256(record).hexdigest()==digest,'Original particle '+stage+' record changed')
        header=struct.unpack_from('>9I',record)
        screen.require(header==HEADERS[stage],'Original particle header changed')
        prefix,code_size=struct.unpack_from('>2I',record,header[6])
        screen.require(prefix==(128 if stage=='VS' else 64) and code_size==header[2]-prefix,
                       'Original particle literal/code bounds changed')
        literal_words=struct.unpack_from('>'+str(prefix//4)+'I',record,header[1])
        literals={256-prefix//16+i:list(literal_words[i*4:i*4+4]) for i in range(prefix//16)}
        code=record[header[1]+prefix:]
        control,blocks=schedule(code,pairs)
        screen.require(control==CF[stage],'Original particle control schedule changed')
        rows={}
        for block in blocks:
            for issue in block['issues']:
                slot=issue['slot'];raw=screen.words(code,slot*12)
                f=screen.decode_fetch(raw) if issue['fetch'] else edge.alu(*raw)
                if not issue['fetch']:
                    for key in ('absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero',
                                'constant_address_register_relative','constant_0_relative','constant_1_relative'):
                        screen.require(not f[key],'Unqualified particle ALU modifier '+key)
                    screen.require(not any(s['relative_temporary'] for s in f['sources']),
                                   'Unqualified particle relative register')
                rows[slot]={**issue,'raw':raw,'fields':f,'control_index':block['control_index']}
        if stage=='VS':
            for slot,(destination,swizzle) in VERTEX_FETCHES.items():
                screen.require(rows[slot]['fields']==dict(kind='vertex_fetch',source_register=1,
                    destination_register=destination,destination_swizzle=swizzle,fetch_constant_index=95,
                    source_component=2,format_field=0,stride_dwords=0,offset_field=0,
                    runtime_declaration_patch_verified=False),'Original particle vertex fetch changed')
            screen.require([i for i,row in rows.items() if row['fetch']]==list(VERTEX_FETCHES),
                           'Unexpected particle vertex fetch')
        else:
            screen.require([i for i,row in rows.items() if row['fetch']]==[2],'Particle PS fetch set changed')
            f=rows[2]['fields']
            screen.require(f['fetch_constant_index']==0 and f['source_register']==0 and f['destination_register']==0
                and f['destination_swizzle']==[0,1,2,3] and f['normalized_coordinates'] and
                f['computed_lod'] and not f['register_lod'] and not f['register_gradients'] and
                f['source_components']==[0,1,0] and f['offset_fields']==[0,0,0] and f['dimension_field']==1,
                'Particle PS sample contract changed')
        result[stage]={'address':hex(va),'record_bytes':size,'sha256':digest,'header':header,
            'literal_registers':literals,'control':control,'blocks':blocks,'rows':rows}
    return result

def operand(f,i,stage,n=4):
    # This shader has eight literals; the shared rigid emitter assumes four.
    value=rigid.operand(f,i,stage,n)
    for reg in range(248,252):value=value.replace(('vc' if stage=='VS' else 'pc')+'['+str(reg)+']','k'+str(reg))
    return value

def scalar(row,stage):
    f=row['fields'];op=f['scalar_opcode'];s=f['sources'][2]
    def one(component):
        return operand({**f,'sources':[f['sources'][0],f['sources'][1],{**s,'components':[component]}]},2,stage,1)
    a,b=one(s['components'][3]),one(s['components'][0])
    extra={6:lambda:'min('+a+','+b+')',7:lambda:'('+a+'==0?1.0:0.0)',12:lambda:'trunc('+a+')',
           27:lambda:'('+a+'==0)',30:lambda:'('+a+'>=0)',
           48:lambda:'sin('+a+')',49:lambda:'cos('+a+')'}
    result=extra[op]() if op in extra else rigid.scalar(row,stage)
    for reg in range(248,252):result=result.replace(('vc' if stage=='VS' else 'pc')+'['+str(reg)+']','k'+str(reg))
    return result

def issue(slot,row,stage):
    """Static, byte-pinned transcription. Both lanes read pre-issue values."""
    f=row['fields'];lines=['    { // slot'+str(slot)]
    if row['fetch']:
        if stage=='PS':
            screen.require(not row['raw'][1]&0x80000000,'Unqualified particle sample predication')
            lines+=['        r0=particleTexture.Sample(particleSampler,r0.xy);']
        else:
            # The immutable native input contains the selected record. All
            # five FETCHes use floor((originalVertexId + .5) / 4).
            field={11:'position',12:'velocity',13:'uvTime.zyxw',14:'size',15:'color'}[slot]
            sw='xyz' if slot==14 else 'xyzw'
            lines+=['        r%d.%s=input.%s;'%(f['destination_register'],sw,field)]
    else:
        op=f['vector_opcode'];mask=f['vector_mask'];sm=f['scalar_mask'];sop=f['scalar_opcode']
        a,b,c=[operand(f,i,stage) for i in range(3)]
        if mask:
            if op==0:expr=a+'+'+b
            elif op==1:expr='rigidLegacyMultiply('+a+','+b+')'
            elif op in (2,3):expr=('max' if op==2 else 'min')+'('+a+','+b+')'
            elif op in (4,5,7):expr='float4('+a+({4:'==',5:'>',7:'!='}[op])+b+')'
            elif op==6:expr='float4('+a+'>='+b+')'
            elif op==11:expr='rigidLegacyMultiply('+a+','+b+')+'+c
            elif op==12:expr='float4('+','.join('('+a+').'+lane+'==0?('+b+').'+lane+':('+c+').'+lane for lane in 'xyzw')+')'
            elif op==13:expr='float4('+','.join('('+a+').'+lane+'>=0?('+b+').'+lane+':('+c+').'+lane for lane in 'xyzw')+')'
            elif op==17:
                lines+=['        precise float4 products=rigidLegacyMultiply('+a+','+b+');',
                        '        precise float sum=products.x+products.y;']
                expr='(sum+'+operand(f,2,stage,1)+').xxxx'
            else:raise ValueError('Unqualified particle vector opcode '+str(op))
            lines+=['        precise float4 v='+('saturate('+expr+')' if f['vector_clamp'] else expr)+';']
        if sop!=50:
            expr=scalar(row,stage)
            if sop in (27,28,29,30):
                lines+=['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
            else:lines+=['        precise float s='+('saturate('+expr+')' if f['scalar_clamp'] else expr)+';']
        # Exports share the vector destination and must respect simultaneous
        # scalar/vector evaluation just like temporary writes.
        if mask:
            sw=''.join('xyzw'[i] for i in range(4) if mask&(1<<i))
            target=('output' if f['export'] else 'r')+str(f['vector_destination'])
            lines+=['        '+target+'.'+sw+'=v.'+sw+';']
        if sm:
            screen.require(not f['export'] or not mask&sm,'Unqualified overlapping particle export')
            sw=''.join('xyzw'[i] for i in range(4) if sm&(1<<i))
            target=('output'+str(f['vector_destination']) if f['export'] else 'r'+str(f['scalar_destination']))
            lines+=['        '+target+'.'+sw+'=s.'+'x'*len(sw)+';']
        if sop!=50:lines+=['        ps=s;']
        if sop in (27,28,29,30):lines+=['        p0=predicate;']
        if f['predicated']:
            lines.insert(1,'        [flatten] if('+('p0' if f['predicate_condition'] else '!p0')+') {')
            lines+=['        }']
    return '\n'.join(lines+['    }'])

def shader_source(image):
    report=inspect(image)
    common=rigid.shader_source(image).split('// Original resource XYZW',1)[0]
    common=common[common.index('// PS slot15'):]
    lines=['// Direct particle VS821570E0 / PS82156770; offline static transcription.',
           'cbuffer ParticleConstants:register(b0) { float4 vc[26]; };',common,
           'Texture2D<float4> particleTexture:register(t0);',
           'SamplerState particleSampler:register(s0);',
           'struct ParticleInput { float4 position:TEXCOORD0; float4 velocity:TEXCOORD1;',
           '    float4 uvTime:TEXCOORD2; float3 size:TEXCOORD3; float4 color:TEXCOORD4;',
           '    float originalVertexId:TEXCOORD5; };',
           'struct ParticleOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
           '    float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; };']
    for stage in ('VS','PS'):
        lines+=['ParticleOutput VSParticle(ParticleInput input) {' if stage=='VS' else
                'float4 PSParticle(ParticleOutput input):SV_Target0 {']
        for reg,words in report[stage]['literal_registers'].items():
            lines+=['    const float4 k%d=asfloat(uint4(%s));'%(reg,','.join('0x%08Xu'%w for w in words))]
        if stage=='VS':
            lines+=['    precise float4 r0=float4(input.originalVertexId,0,0,0),r1=0,r2=0,r3=0,r4=0,r5=0,r6=0,r7=0,r8=0,r9=0;',
                    '    precise float4 output62=0,output0=0,output1=0,output2=0;']
        else:lines+=['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,output0=0;']
        lines+=['    precise float ps=0; bool p0=false;']
        for block in report[stage]['blocks']:
            condition=block['predicate_condition']
            if condition is not None:lines+=['    [flatten] if('+('p0' if condition else '!p0')+') { // CF'+str(block['control_index'])]
            lines += [issue(row['slot'],report[stage]['rows'][row['slot']],stage) for row in block['issues']]
            if condition is not None:lines+=['    }']
        if stage=='VS':lines+=['    ParticleOutput o; o.position=output62;o.t0=output0;o.t1=output1;o.t2=output2;return o;','}']
        else:lines+=['    return output0;','}']
    lines+=['[maxvertexcount(1)] void GSParticleProbe(point ParticleOutput input[1],inout PointStream<ParticleOutput> stream) { stream.Append(input[0]); }']
    return '\n'.join(lines)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--emit-hlsl',type=Path)
    args=parser.parse_args()
    image=(ROOT/'analysis/simpsons.pe').read_bytes()
    report=inspect(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(shader_source(image),encoding='utf-8')
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    print(json.dumps({stage:{'address':r['address'],'issues':len(r['rows']),
        'literal_registers':sorted(r['literal_registers']),
        'predicate_blocks':[b['control_index'] for b in r['blocks'] if b['predicate_condition'] is not None]}
        for stage,r in report.items()}))

if __name__=='__main__':main()
