"""Pinned retail rigid dualtextured UV shaders and exact offline transcription.

The UV family has wave motion, transformed UVs and two material textures. Its
opaque pass samples only the character depth texture; it is not an alias of
the older dualtextured pair. All co-issued results read the old registers.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import analyze_chocolate_shader as chocolate
import analyze_edge_shaders as edge
import analyze_post_effect_catalog as catalog
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
FX_ADDRESS, FX_SIZE = 0x820465E8, 0x3A70
FX_SHA = 'aca225b7e054662efed2e63c5052e2aad8bc4199dd327e2a7c13db8d7a30abd4'
PROFILES = (
    ('VS', 0x82046D9C, 1396, 856, 540, 5, 'e4c391bd252f9b4656eddf902fcf9833e78d062990ba85d973aa97e0197a9fa9'),
    ('PS', 0x820477A8, 1940, 1088, 852, 10, '91f1ed5d3c5d6c266e68acbe12c02d3273d7ee5f6ecc318565428bc4c3b1b1d3'),
    ('AVS', 0x82047318, 1152, 732, 420, 4, 'a0c773625e06cd2a962b93842e68079f1cbf371689cbf0aba49e9de334ba0ec2'),
    ('APS', 0x82047F44, 868, 484, 384, 7, 'e46ae17e3a92fd19a4021987405ee4821f2fccc21f40de9907ecbb239faf25fd'),
)
HEADERS = {
    'VS': (0x102A1101, 792, 604, 36, 132, 632, 672, 0, 0),
    'PS': (0x102A1100, 960, 980, 36, 132, 868, 908, 0, 0),
    'AVS': (0x102A1101, 668, 484, 36, 132, 556, 596, 0, 0),
    'APS': (0x102A1100, 420, 448, 36, 132, 340, 380, 0, 0),
}
CF = {
    'VS': ((0xF1555005,0x1201),(0,0xC200),(0x600A,0x1200),(0x6010,0x1200),(0x2016,0x1200),
           (0,0xC400),(0x6018,0x1200),(0x601E,0x1200),(0x6024,0x1200),(0x202A,0x2200)),
    'AVS': ((0xF0554004,0x1200),(0,0xC200),(0x6008,0x1200),(0x600E,0x1200),(0x2014,0x1200),
            (0,0xC400),(0x6016,0x1200),(0x601C,0x2200)),
    'PS': ((0x25400A,0x1000),(0x4004,0xB000),(0x300E,0x1200),(0x2007,0xB000),
           (0x2011,0x1000),(0x4007,0xB000),(0x2013,0x1200),(0x6015,0x1200),
           (0x601B,0x1200),(0x6021,0x1200),(0x4027,0x1000),(0x4010,0xB000),
           (0x0555602B,0x1200),(0x00956031,0x1200),(0x6037,0x1200),(0x203D,0x1200),
           (0,0xC400),(0x603F,0x1200),(0x1045,0x2200),(0,0)),
    'APS': ((0x255007,0x1000),(0x4004,0xB000),(0x300C,0x1200),(0x200C,0xB000),
            (0x200F,0x1000),(0x4008,0xB000),(0x2011,0x1200),(0x200C,0xB000),
            (0x2013,0x1000),(0x400C,0xB000),(0x2546015,0x1200),(0x201B,0x1200),
            (0,0xC400),(0x201D,0x2200)),
}
VS_LITERALS = (0,)*8 + (0x3F000000,0,0x3E22F983,0x40C90FDB,0xC0490FDB,0,0,0)
LITERALS = {
    'VS': VS_LITERALS, 'AVS': VS_LITERALS,
    'PS': (0,)*12 + (0x3F666666,0x44800000,0x3E99999A,0x3E800000,
                     0x3F333333,0x3FC00000,0x3E000000,0x3F800000,
                     0x42800000,0xBF800000,0x3A002008,0x3A802008,
                     0x3F000000,0xBF000000,0x42000000,0x3E4CCCCD,0x3F75C28F,0,0,0),
    'APS': (0,)*8 + (0x40200000,0x3F800000,0,0,0x3F000000,0,0x3FC00000,0xBF800000),
}
VS_FETCHES = {'VS': ((5,6,[0,1,2,5]),(6,0,[7,0,1,2]),(7,4,[0,1,2,3]),
                         (8,3,[0,1,7,7]),(9,3,[7,7,0,1])),
              'AVS': ((4,0,[7,0,1,2]),(5,3,[0,1,2,7]),(6,2,[0,1,2,3]),(7,1,[0,1,7,7]))}
PS_FETCHES = {'PS': ((10,2,0,5,[0,1,2,3],[2,3,2]),(11,1,0,6,[2,0,1,3],[0,1,0])),
              'APS': ((7,1,0,2,[0,1,2,3],[2,3,2]),(8,0,0,3,[2,1,0,3],[0,1,0]),
                      (22,0,0,0,[7,0,1,2],[2,3,1]),(23,1,4,2,[3,7,7,7],[3,2,3]),
                      (24,1,4,2,[7,3,7,7],[1,0,1]))}


def decode_record(record, profile):
    stage, _, extent, offset, length, pairs, _ = profile
    screen.require(len(record) == extent, 'UV shader record extent differs')
    screen.require(struct.unpack_from('>9I',record) == HEADERS[stage], 'UV shader header differs')
    trailer = {'VS':3,'PS':2,'AVS':1,'APS':0}[stage]
    screen.require(record[-12:].hex() == f'4e4a000{trailer}60e045d15375e8b0', 'UV shader trailer differs')
    literals = LITERALS[stage]
    screen.require(struct.unpack_from('>'+str(len(literals))+'I',record,HEADERS[stage][1]) == literals,
                   'UV literal bank differs')
    screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6]) == (len(literals)*4,length),
                   'UV literal/code span mapping differs')
    code = record[offset:offset+length]
    cf, slots = chocolate.schedule(code,pairs)
    screen.require(tuple(cf) == CF[stage], 'UV shader complete control flow differs')
    rows = {}
    for slot, fetch, _ in slots:
        raw = screen.words(code,slot*12)
        f = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        if not fetch:
            screen.require(not any(f[k] for k in ('absolute_constants','vector_destination_relative',
                           'scalar_destination_relative_or_export_zero','predicated','predicate_condition',
                           'constant_address_register_relative','constant_0_relative','constant_1_relative')),
                           'Unqualified UV ALU modifier')
            screen.require(not any(s['relative_temporary'] for s in f['sources']), 'Relative UV temporary')
        else:
            screen.require(not raw[1]&0x80000000,'Unexpected UV fetch predicate')
        rows[slot] = dict(raw=raw,fetch=fetch,fields=f)
    if stage.endswith('VS'):
        sem_at, associations = ((712,(0x00100005,0x00003006,0x0000A007,0x00005008,0x00215009))
                                if stage=='VS' else (636,(0x00100004,0x00003005,0x0000A006,0x00205007)))
        screen.require(struct.unpack_from('>'+str(len(associations))+'I',record,sem_at)==associations,
                       'UV vertex semantic associations differ')
        for slot,dest,swizzle in VS_FETCHES[stage]:
            screen.require(rows[slot]['fields']==dict(kind='vertex_fetch',source_register=0,
                           destination_register=dest,destination_swizzle=swizzle,fetch_constant_index=95,
                           source_component=0,format_field=0,stride_dwords=0,offset_field=0,
                           runtime_declaration_patch_verified=False),'UV vertex fetch wiring differs')
        # These anchors independently qualify the original material UV leaves.
        uvslot = 36 if stage=='VS' else 33
        f=rows[uvslot]['fields']
        screen.require(f['vector_opcode']==11 and f['export'] and f['vector_destination']==0 and
                       f['vector_mask']==15 and f['sources'][1]['register']==46,'UV scale leaf arithmetic differs')
        for slot,constant in ((32,45),(33,47)) if stage=='VS' else ((30,45),(31,47)):
            screen.require(any(s['bank']=='constant' and s['register']==constant for s in rows[slot]['fields']['sources']),
                           'UV amplitude/velocity constant mapping differs')
    else:
        expected = [entry[0] for entry in PS_FETCHES[stage]] + (list(range(43,52)) if stage=='PS' else [])
        screen.require([i for i,r in rows.items() if r['fetch']]==expected,'UV texture sample inventory differs')
        for slot,bank,src,dest,swizzle,components in PS_FETCHES[stage]:
            f=rows[slot]['fields']
            screen.require((f['fetch_constant_index'],f['source_register'],f['destination_register'],
                            f['destination_swizzle'],f['source_components']) == (bank,src,dest,swizzle,components),
                           'UV material sample operands differ')
        if stage=='PS':
            for i,(x,y,channel) in enumerate(rigid.TAPS):
                f=rows[43+i]['fields']
                screen.require(f['fetch_constant_index']==0 and f['source_components']==[0,1,1] and
                               [v for v in f['destination_swizzle'] if v!=7]==[channel] and
                               f['offset_fields']==[(x*2)&31,(y*2)&31,0], 'UV character shadow tap differs')
        for row in rows.values():
            if row['fetch']:
                f=row['fields']
                screen.require(f['normalized_coordinates'] and f['dimension_field']==1 and f['computed_lod'] and
                               not f['register_lod'] and not f['register_gradients'] and
                               [f[k] for k in ('mag_filter','min_filter','mip_filter','anisotropy','arbitrary_filter',
                                               'volume_mag_filter','volume_min_filter')]==[3,3,3,7,0,3,3] and
                               f['sample_location']==0 and f['lod_bias_field']==0,'UV sample state contract differs')
    # Trial-emit every operation; arithmetic outside the supported grammar fails.
    for slot,row in rows.items():
        if not (stage.endswith('VS') and row['fetch']):
            issue(slot,row,'VS' if stage.endswith('VS') else 'PS')
    return rows


def inspect(image):
    screen.require(len(image)==screen.IMAGE_SIZE and rigid.sha(image)==screen.IMAGE_SHA256,'Retail image differs')
    fx=image[FX_ADDRESS-screen.BASE:FX_ADDRESS-screen.BASE+FX_SIZE]
    screen.require(rigid.sha(fx)==FX_SHA,'UV effect differs')
    metadata=catalog.inspect_blob(fx,FX_ADDRESS)
    screen.require([(t['name'],t['handle'],t['passes'][0]['context_offset']) for t in metadata['techniques']] ==
                   [('rigid','0x0003FFFC',0x30F0),('rigidalpha','0x0007FFFC',0x3440)],'UV technique associations differ')
    result={}
    for profile in PROFILES:
        stage,address,size,offset,length,pairs,digest=profile
        record=image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(record)==digest,'UV '+stage+' record hash differs')
        result[stage]=dict(address=f'{address:08X}',sha256=digest,code_sha256=rigid.sha(record[offset:offset+length]),
                           cf_pairs=pairs,rows=decode_record(record,profile))
    return result


def scalar(row,stage):
    f=row['fields'];op=f['scalar_opcode'];raw=row['raw']
    if op in (48,49):
        return chocolate.scalar(row,stage)
    if 42<=op<=47:
        sw=raw[1]&255
        reg=(op&1)|(((raw[2]>>29)&1)<<1)|(sw&0x3C)
        constant=raw[2]&255
        a=('k'+str(constant) if constant>=252 else ('vc' if stage=='VS' else 'pc')+'['+str(constant)+']')+'.'+'xyzw'[((sw>>6)+3)&3]
        b='r'+str(reg)+'.'+'xyzw'[sw&3]
        screen.require(not f['sources'][2]['negated'],'Unqualified UV split negation')
        return ('rigidLegacyProduct('+a+','+b+')' if op<44 else a+('+' if op<46 else '-')+b)
    return rigid.scalar(row,stage)


def issue(slot,row,stage):
    f=row['fields'];lines=['    { // slot'+str(slot)]
    if row['fetch']:
        screen.require(stage=='PS','Vertex fetch is supplied by the reviewed input interface')
        bank=f['fetch_constant_index'];uv='r'+str(f['source_register'])+'.'+''.join('xyzw'[v] for v in f['source_components'][:2])
        offset=[((v+16)%32-16)//2 for v in f['offset_fields']]
        screen.require(all(v%2==0 for v in f['offset_fields']),'Fractional UV texture offset')
        expression=f'uvTex{bank}.Sample(uvSampler{bank},{uv},int2({offset[0]},{offset[1]}))'
        if stage=='PS':
            # The adapter macro is enabled only for the opaque draw (whose
            # t0 is character depth); the alpha draw's t0 remains material RGBA.
            expression=f'uvSample{bank}({expression})'
        lines.append('        precise float4 v='+expression+';')
        for lane,selector in enumerate(f['destination_swizzle']):
            if selector!=7:
                screen.require(selector<4,'Unqualified UV sample component')
                lines.append(f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[selector]};')
        return '\n'.join(lines+['    }'])
    op,mask,sop,sm=f['vector_opcode'],f['vector_mask'],f['scalar_opcode'],f['scalar_mask']
    if mask:
        a,b,c=(rigid.operand(f,i,stage) for i in range(3))
        if op==0:expr=a+'+'+b
        elif op==1:expr='rigidLegacyMultiply('+a+','+b+')'
        elif op in (2,3):expr=('max' if op==2 else 'min')+'('+a+','+b+')'
        elif op in (5,6):expr='float4('+a+('>' if op==5 else '>=')+b+')'
        elif op==8:expr='frac('+a+')'
        elif op==10:expr='floor('+a+')'
        elif op==11:expr='rigidLegacyMultiply('+a+','+b+')+'+c
        elif op in (15,16):
            lanes='xyzw' if op==15 else 'xyz'
            lines.append('        precise float4 product=rigidLegacyMultiply('+a+','+b+');')
            expr='('+ '+'.join('product.'+lane for lane in lanes)+').xxxx'
        elif op==17:
            lines.append('        precise float4 product=rigidLegacyMultiply('+a+','+b+');')
            expr='((product.x+product.y)+('+c+').x).xxxx'
        else:raise ValueError('Unqualified UV vector opcode '+str(op))
        lines.append('        precise float4 v='+('saturate('+expr+')' if f['vector_clamp'] else expr)+';')
    if sop!=50:
        expr=scalar(row,stage)
        if sop in (28,29):
            lines += ['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
        else:lines.append('        precise float s='+('saturate('+expr+')' if f['scalar_clamp'] else expr)+';')
    if mask:
        sw=''.join('xyzw'[i] for i in range(4) if mask&(1<<i))
        target=('output' if f['export'] else 'r')+str(f['vector_destination'])
        lines.append(f'        {target}.{sw}=v.{sw};')
    if sm:
        screen.require(not f['export'] and sop!=50,'Unqualified UV scalar export/write')
        sw=''.join('xyzw'[i] for i in range(4) if sm&(1<<i))
        lines.append(f'        r{f["scalar_destination"]}.{sw}=s.{"x"*len(sw)};')
    if sop!=50:lines.append('        ps=s;')
    if sop in (28,29):lines.append('        p0=predicate;')
    return '\n'.join(lines+['    }'])


def emit_ps_control(rows,alpha):
    lines=[]
    def emit(first,end):lines.extend(issue(slot,rows[slot],'PS') for slot in range(first,end))
    if not alpha:
        emit(10,14);lines.append('    [branch] if(p0) { // CF1 falls through to the first blend mode.')
        emit(14,17);lines.append('    } else {');emit(17,19)
        lines.append('    [branch] if(p0) {');emit(19,21);lines += ['    }','    }']
        emit(21,43);lines.append('    [branch] if(p0) { // CF11: original receiver enables character depth taps.')
        emit(43,63);lines.append('    }');emit(63,70)
    else:
        emit(7,12);lines.append('    [branch] if(p0) { // CF1 first material blend mode.')
        emit(12,15);lines.append('    } else {');emit(15,17)
        lines.append('    [branch] if(p0) {');emit(17,19);lines.append('    } else {');emit(19,21)
        lines.append('    [branch] if(p0) { // CF9 extra animated material samples.')
        emit(21,29);lines += ['    }','    }','    }'];emit(29,31)
    return lines


def shader_source(image):
    inventory=inspect(image)
    common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common=common[common.index('cbuffer RigidVertexConstants'):]
    common=common.replace('// PS slot15 uses the original SM3 multiplication rule:',
                          '// Original SM3 multiplication rule for normalization and all products:')
    for declaration in ('Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);',
                        'SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);'):
        common=common.replace(declaration+'\n','')
    common=common.replace('float4 vc[30]','float4 vc[48]').replace('float4 pc[50]','float4 pc[51]')
    lines=['// Retail UV: VS82046D9C/PS820477A8 and VS82047318/PS82047F44.',common]
    for bank in range(3):
        lines += [f'Texture2D<float4> uvTex{bank} : register(t{bank});',f'SamplerState uvSampler{bank} : register(s{bank});']
        lines += [f'float4 uvSample{bank}(float4 value) {{',
                  '#if defined(RIGID_UV_CHARACTER_DEPTH_RRRR)' if bank==0 else '#if 0',
                  '    return value.xxxx;','#else','    return value;','#endif','}']
    lines += ['struct RigidUVInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
              '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
              'struct RigidUVOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
              '    float2 t1:TEXCOORD1; float4 t2:TEXCOORD2; float3 t3:TEXCOORD3; float4 t4:TEXCOORD4; };',
              'struct RigidUVAlphaInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
              '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; };',
              'struct RigidUVAlphaOutput { float4 position:SV_Position; float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; };']
    for alpha in (False,True):
        stage='AVS' if alpha else 'VS';name='RigidDualTexturedUVAlpha' if alpha else 'RigidDualTexturedUV'
        output='RigidUVAlphaOutput' if alpha else 'RigidUVOutput';input_type='RigidUVAlphaInput' if alpha else 'RigidUVInput'
        lines += [f'{output} VS{name}({input_type} input) {{']
        for i in range(4):
            lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in VS_LITERALS[4*i:4*i+4])+'));')
        lines += (['    // AVS semantic00100004 position fetch writes r0.yzw; normal fetch writes r3.xyz.',
                   '    precise float4 r0=float4(0,input.position),r1=float4(input.uv,0,0),r2=input.color;',
                   '    precise float4 r3=float4(input.normal,0),r4=0;'] if alpha else
                  ['    precise float4 r6=float4(input.position,1),r0=float4(0,input.normal),r4=input.color;',
                   '    precise float4 r3=float4(input.uv,input.uv1),r1=0,r2=0,r5=0;'])
        lines += ['    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0;',
                  '    precise float ps=0; bool p0=false;']
        lines.extend(issue(slot,row,'VS') for slot,row in inventory[stage]['rows'].items() if not row['fetch'])
        lines += [f'    {output} result; result.position=output62; result.t0=output0; result.t1='+('output1;' if alpha else 'output1.xy;')]
        if not alpha:lines += ['    result.t2=output2; result.t3=output3.xyz; result.t4=output4;']
        lines += ['    return result;','}',f'float4 PS{name}({output} input):SV_Target0 {{']
        stage='APS' if alpha else 'PS';literals=LITERALS[stage];first=256-len(literals)//4
        for i in range(len(literals)//4):
            lines.append(f'    const float4 k{first+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in literals[4*i:4*i+4])+'));')
        lines += (['    precise float4 r0=input.t0,r1=input.t1,r2=0,r3=0,r4=0;'] if alpha else
                  ['    precise float4 r0=input.t0,r1=float4(input.t1,0,0),r2=input.t2,r3=float4(input.t3,0);',
                   '    precise float4 r4=input.t4,r5=0,r6=0,r7=0;'])
        lines += ['    precise float4 output0=0; precise float ps=0; bool p0=false;']
        lines += emit_ps_control(inventory[stage]['rows'],alpha)
        lines += ['    return output0;','}']
    source='\n'.join(lines)+'\n'
    # This PS has eight literal registers, beginning at c248. The shared
    # older-rigid operand formatter only special-cases c252 and above.
    for register in range(248,252):source=source.replace(f'pc[{register}]',f'k{register}')
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl',type=Path)
    parser.add_argument('--verify-hlsl',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes()
    inventory=inspect(image);source=shader_source(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(source,encoding='utf-8')
    if args.verify_hlsl:screen.require(args.verify_hlsl.read_text(encoding='utf-8')==source,'UV native HLSL differs from pinned transcription')
    if args.output:args.output.write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf-8')
    print('PASS retail rigid UV: 39/60 opaque VS/PS slots and 30/24 alpha VS/PS slots; complete CF and material fetch wiring')


if __name__=='__main__':main()
