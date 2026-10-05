"""Pinned simpsons_uv opaque/alpha records and instruction-derived HLSL.

Every co-issued result observes old registers. This material transforms UVs,
not geometry, and its opaque texture banks are world/character/base at 0/1/2.
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
FX_ADDRESS, FX_SIZE = 0x82042F58, 0x3690
FX_SHA = 'cbe6bb0f5ddb8f231998a74c18d7eebbe67e929c4836fca2f3cf093b307cdd86'
PROFILES = (
    ('VS',0x8204364C,1388,872,516,5,'9e36e1a5ed3f875e1d48c1dd24d53caf4d75f51a120c28fd9e8871245de6df0f'),
    ('AVS',0x82043BC0,1176,756,420,4,'2693f7016a943af714f3968fa70d629e2a0c8b2fa99a94261ae868e843fbd072'),
    ('PS',0x82044068,2016,1008,1008,10,'9228637d66b44a85795251ab73af326d6441c8a5c13ef3c74df86f10371c65a8'),
    ('APS',0x82044850,836,584,252,3,'81e6a5d104d53b4b7c13d804ae7ba3ca8e59e4cd30349da54b1debdfa7092c3b'),
)
HEADERS = {
    'VS':(0x102A1101,808,580,36,112,636,676,0,0),
    'AVS':(0x102A1101,692,484,36,112,540,580,0,0),
    'PS':(0x102A1100,944,1072,36,112,852,892,0,0),
    'APS':(0x102A1100,520,316,36,112,432,472,0,0),
}
CF = {
    'VS':((0xF0554005,0x1200),(0,0xC200),(0x4009,0x1200),(0,0xC400),(0x600D,0x1200),
          (0x6013,0x1200),(0x6019,0x1200),(0x601F,0x1200),(0x5025,0x2200),(0,0)),
    'AVS':((0xF0554004,0x1200),(0,0xC200),(0x4008,0x1200),(0,0xC400),
           (0x600C,0x1200),(0x6012,0x1200),(0x6018,0x1200),(0x401E,0x2200)),
    'PS':((0x9600A,0x1200),(0x6010,0x1200),(0x4016,0x1000),(0x4012,0xB000),
          (0x401A,0x1000),(0x400A,0xB000),(0x0555601E,0x1200),(0x00956024,0x1200),
          (0x602A,0x1200),(0x2030,0x1200),(0x6032,0x1200),(0x1038,0x1000),
          (0x4011,0xB000),(0x05556039,0x1200),(0x0095603F,0x1200),(0x6045,0x1200),
          (0x204B,0x1200),(0x204D,0x1200),(0,0xC400),(0x404F,0x2200)),
    'APS':((0x243003,0x1000),(0x4003,0xB000),(0x2006,0x1200),(0,0xC400),
           (0x6008,0x1200),(0x600E,0x2200)),
}
VS_LITERALS = (0,)*8+(0x3C8EFA35,0x3F000000,0x3E22F983,0x40C90FDB,0xC0490FDB,0,0,0)
LITERALS = {
    'VS':VS_LITERALS,'AVS':VS_LITERALS,
    'PS':(0x3F666666,0x3E99999A,0,0,0x3A802008,0,0x3E800000,0x3F75C28F,
          0x3F333333,0x3E000000,0x42000000,0x44800000,0x3F000000,0x3F800000,0x3E4CCCCD,0xBF000000),
    'APS':(0,)*12+(0x3F800000,0,0x38D1B717,0),
}
SEMANTICS = {'VS':(716,(0x00100005,0x00003006,0x0000A007,0x00305008)),
             'AVS':(620,(0x00100004,0x00003005,0x0000A006,0x00205007))}
VS_FETCHES = {'VS':((5,5,[0,1,2,5]),(6,3,[0,1,2,7]),(7,4,[0,1,2,3]),(8,0,[0,1,7,7])),
              'AVS':((4,3,[0,1,2,5]),(5,2,[0,1,2,7]),(6,4,[0,1,2,3]),(7,0,[7,7,0,1]))}
EXPORT_MASKS = {'VS':{62:15,0:3,1:15,2:15,3:7,4:15},'AVS':{62:15,0:3,1:15,2:7,3:15},
                'PS':{0:15},'APS':{0:15}}


def decode_record(record,profile):
    stage,_,size,offset,length,pairs,_=profile
    screen.require(len(record)==size,'Rigid UV record extent differs')
    screen.require(struct.unpack_from('>9I',record)==HEADERS[stage],'Rigid UV header differs')
    trailer={'VS':3,'AVS':1,'PS':2,'APS':0}[stage]
    screen.require(record[-12:].hex()==f'4e4a000{trailer}8646e1c7a2739cba','Rigid UV trailer differs')
    screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS[stage],'Rigid UV literal bank differs')
    screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length),'Rigid UV literal/code mapping differs')
    code=record[offset:offset+length];cf,slots=chocolate.schedule(code,pairs)
    screen.require(tuple(cf)==CF[stage],'Rigid UV complete control flow differs')
    rows={};exports={}
    for slot,fetch,_ in slots:
        raw=screen.words(code,slot*12);f=screen.decode_fetch(raw)if fetch else edge.alu(*raw)
        rows[slot]=dict(raw=raw,fetch=fetch,fields=f)
        if not fetch:
            screen.require(not any(f[k]for k in ('absolute_constants','vector_destination_relative',
                'scalar_destination_relative_or_export_zero','predicated','predicate_condition',
                'constant_address_register_relative','constant_0_relative','constant_1_relative')),
                'Unqualified rigid UV ALU modifier')
            screen.require(not any(s['relative_temporary']for s in f['sources']),'Relative rigid UV temporary')
            if f['export']:exports[f['vector_destination']]=exports.get(f['vector_destination'],0)|f['vector_mask']
        else:screen.require(not raw[1]&0x80000000,'Predicated rigid UV fetch')
    screen.require(exports==EXPORT_MASKS[stage],'Rigid UV export linkage differs')
    if stage.endswith('VS'):
        at,sem=SEMANTICS[stage]
        screen.require(struct.unpack_from('>4I',record,at)==sem,'Rigid UV vertex semantic associations differ')
        screen.require([i for i,r in rows.items()if r['fetch']]==[f[0]for f in VS_FETCHES[stage]],'Rigid UV vertex fetch inventory differs')
        for slot,dest,swizzle in VS_FETCHES[stage]:
            screen.require(rows[slot]['fields']==dict(kind='vertex_fetch',source_register=0,destination_register=dest,
                destination_swizzle=swizzle,fetch_constant_index=95,source_component=0,format_field=0,
                stride_dwords=0,offset_field=0,runtime_declaration_patch_verified=False),'Rigid UV vertex fetch wiring differs')
    else:
        expected=[10]+list(range(30,39))+list(range(57,66))if stage=='PS'else[4]
        screen.require([i for i,r in rows.items()if r['fetch']]==expected,'Rigid UV material/depth sample inventory differs')
        for slot in expected:
            f=rows[slot]['fields']
            screen.require(f['normalized_coordinates']and f['computed_lod']and f['fetch_valid_only']and
                not f['register_lod']and not f['register_gradients']and f['dimension_field']==1 and
                not f['sample_location']and not f['lod_bias_field']and
                [f[k]for k in ('mag_filter','min_filter','mip_filter','anisotropy','arbitrary_filter',
                              'volume_mag_filter','volume_min_filter')]==[3,3,3,7,0,3,3],
                'Rigid UV sampled-resource state contract differs')
        f=rows[10 if stage=='PS'else 4]['fields']
        screen.require((f['fetch_constant_index'],f['source_register'],f['source_components'],f['destination_swizzle'])==
            ((2,0,[0,1,0],[0,1,2,7])if stage=='PS'else(0,0,[0,1,0],[3,0,1,2])),
            'Rigid UV base sampler wiring differs')
        if stage=='PS':
            for start,bank,coords in ((30,1,[0,1,1]),(57,0,[1,2,2])):
                for i,(x,y,channel)in enumerate(rigid.TAPS):
                    f=rows[start+i]['fields']
                    screen.require(f['fetch_constant_index']==bank and f['source_components']==coords and
                        [v for v in f['destination_swizzle']if v!=7]==[channel]and
                        f['offset_fields']==[(x*2)&31,(y*2)&31,0],'Rigid UV world/character shadow tap differs')
        else:
            screen.require(rows[6]['fields']['scalar_opcode']==46 and rows[6]['fields']['sources'][2]['negated']and
                rows[7]['fields']['vector_opcode']==25,'Rigid UV strict alpha cutoff differs')
    for slot,row in rows.items():
        if not(stage.endswith('VS')and row['fetch']):issue(slot,row,'VS'if stage.endswith('VS')else'PS')
    return rows


def inspect(image):
    screen.require(len(image)==screen.IMAGE_SIZE and rigid.sha(image)==screen.IMAGE_SHA256,'Retail image differs')
    fx=image[FX_ADDRESS-screen.BASE:FX_ADDRESS-screen.BASE+FX_SIZE]
    screen.require(rigid.sha(fx)==FX_SHA,'Original simpsons_uv effect differs')
    metadata=catalog.inspect_blob(fx,FX_ADDRESS)
    screen.require([(t['name'],t['handle'],t['passes'][0]['context_offset'])for t in metadata['techniques']]==
        [('rigid','0x0003FFFC',0x2D90),('rigidalpha','0x0007FFFC',0x30A0)],'Rigid UV pass associations differ')
    result={}
    for profile in PROFILES:
        stage,address,size,offset,length,pairs,digest=profile
        record=image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(record)==digest,'Rigid UV '+stage+' record hash differs')
        result[stage]=dict(address=f'{address:08X}',sha256=digest,code_sha256=rigid.sha(record[offset:offset+length]),
            cf_pairs=pairs,rows=decode_record(record,profile))
    return result


def scalar(row,stage):
    f=row['fields'];op=f['scalar_opcode']
    if op in(48,49):return chocolate.scalar(row,stage)
    if 42<=op<=47:
        screen.require(op in(42,43,44,46,47),'Unqualified rigid UV split scalar opcode')
        sw=row['raw'][1]&255;reg=(op&1)|(((row['raw'][2]>>29)&1)<<1)|(sw&0x3C);constant=row['raw'][2]&255
        a=('k'+str(constant)if constant>=252 else('vc'if stage=='VS'else'pc')+'['+str(constant)+']')+'.'+'xyzw'[((sw>>6)+3)&3]
        b='r'+str(reg)+'.'+'xyzw'[sw&3]
        if f['sources'][2]['negated']:a='(-'+a+')';b='(-'+b+')'
        return 'rigidLegacyProduct('+a+','+b+')'if op<44 else a+('+'if op<46 else'-')+b
    if op==3:
        return 'rigidLegacyProduct(('+rigid.operand(f,2,stage)+').w,ps)'
    return rigid.scalar(row,stage)


def issue(slot,row,stage):
    f=row['fields'];lines=['    { // slot'+str(slot)]
    if row['fetch']:
        screen.require(stage=='PS','Vertex fetch belongs to reviewed input interface')
        bank=f['fetch_constant_index'];coords='r'+str(f['source_register'])+'.'+''.join('xyzw'[c]for c in f['source_components'][:2])
        offs=[((v+16)%32-16)//2 for v in f['offset_fields']]
        screen.require(all(v%2==0 for v in f['offset_fields']),'Fractional rigid UV texture offset')
        lines.append(f'        precise float4 v=ruvSample{bank}(ruvTex{bank}.Sample(ruvSampler{bank},{coords},int2({offs[0]},{offs[1]})));')
        for lane,selector in enumerate(f['destination_swizzle']):
            if selector!=7:
                screen.require(selector<4,'Unqualified rigid UV sample literal')
                lines.append(f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[selector]};')
        return '\n'.join(lines+['    }'])
    op,mask,sop,sm=f['vector_opcode'],f['vector_mask'],f['scalar_opcode'],f['scalar_mask']
    if op==25:
        screen.require(not mask and sop==50 and not sm,'Rigid UV KILLGT shape differs')
        return f'    {{ // slot{slot}: original KILLGT\n        if(any({rigid.operand(f,0,stage)}>{rigid.operand(f,1,stage)})) discard;\n    }}'
    if mask:
        a,b,c=(rigid.operand(f,i,stage)for i in range(3))
        if op==0:expr=a+'+'+b
        elif op==1:expr='rigidLegacyMultiply('+a+','+b+')'
        elif op in(2,3):expr=('max'if op==2 else'min')+'('+a+','+b+')'
        elif op==6:expr='float4('+a+'>='+b+')'
        elif op==10:expr='floor('+a+')'
        elif op==11:expr='rigidLegacyMultiply('+a+','+b+')+'+c
        elif op in(15,16,17):
            lanes='xyzw'if op==15 else'xyz'if op==16 else'xy'
            lines.append('        precise float4 product=rigidLegacyMultiply('+a+','+b+');')
            expr='('+ '+'.join('product.'+lane for lane in lanes)+(' + ('+c+').x'if op==17 else'')+').xxxx'
        else:raise ValueError('Unqualified rigid UV vector opcode '+str(op))
        lines.append('        precise float4 v='+('saturate('+expr+')'if f['vector_clamp']else expr)+';')
    if sop!=50:
        expr=scalar(row,stage)
        if sop in(28,29):lines+=['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
        else:lines.append('        precise float s='+('saturate('+expr+')'if f['scalar_clamp']else expr)+';')
    if mask:
        sw=''.join('xyzw'[i]for i in range(4)if mask&(1<<i));target=('output'if f['export']else'r')+str(f['vector_destination'])
        lines.append(f'        {target}.{sw}=v.{sw};')
    if sm:
        screen.require(not f['export']and sop!=50,'Unqualified rigid UV scalar export')
        sw=''.join('xyzw'[i]for i in range(4)if sm&(1<<i));lines.append(f'        r{f["scalar_destination"]}.{sw}=s.{"x"*len(sw)};')
    if sop!=50:lines.append('        ps=s;')
    if sop in(28,29):lines.append('        p0=predicate;')
    return '\n'.join(lines+['    }'])


def control_source(stage,rows):
    lines=[];ends=[]
    for ci,(lo,hi)in enumerate(CF[stage]):
        while ends and ends[-1]==ci:lines.append('    }');ends.pop()
        op=hi>>12
        if op in(1,2):
            for slot in range(lo&4095,(lo&4095)+((lo>>12)&7)):
                if not(stage.endswith('VS')and rows[slot]['fetch']):lines.append(issue(slot,rows[slot],'VS'if stage.endswith('VS')else'PS'))
        elif op==11:
            target=lo&8191;screen.require(target>ci and lo&0x4000 and not lo&0x2000 and hi==0xB000,'Rigid UV jump predicate differs')
            lines.append(f'    [branch] if(p0) {{ // CF{ci} jumps to CF{target} when predicate is false.');ends.append(target)
        else:screen.require(op in(0,12),'Unqualified rigid UV CF opcode')
    screen.require(not ends,'Rigid UV unclosed CF branch')
    return lines


def shader_source(image):
    inventory=inspect(image)
    common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common=common[common.index('cbuffer RigidVertexConstants'):]
    for declaration in('Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);',
                       'SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);'):
        common=common.replace(declaration+'\n','')
    common=common.replace('float4 vc[30]','float4 vc[48]').replace('float4 pc[50]','float4 pc[51]')
    lines=['// Original simpsons_uv opaque8204364C/82044068 and alpha82043BC0/82044850.',common]
    for bank in range(3):
        lines+=[f'Texture2D<float4> ruvTex{bank} : register(t{bank});',f'SamplerState ruvSampler{bank} : register(s{bank});',
                f'float4 ruvSample{bank}(float4 v) {{']
        if bank<2:lines+=['#if defined(RIGID_SINGLE_UV_DEPTH_RRRR)','    return v.xxxx;','#else','    return v;','#endif','}']
        else:lines+=['    return v;','}']
    lines+=['struct RigidUVInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1; float4 color:TEXCOORD2; float2 uv:TEXCOORD3; };',
            'struct RigidSingleUVOutput { float4 position:SV_Position; float2 t0:TEXCOORD0; float4 t1:TEXCOORD1;',
            '    float4 t2:TEXCOORD2; float3 t3:TEXCOORD3; float4 t4:TEXCOORD4; };',
            'struct RigidSingleUVAlphaOutput { float4 position:SV_Position; float2 t0:TEXCOORD0; float4 t1:TEXCOORD1;',
            '    float3 t2:TEXCOORD2; float4 t3:TEXCOORD3; };']
    for alpha in(False,True):
        name='RigidUVAlpha'if alpha else'RigidUV';output='RigidSingleUVAlphaOutput'if alpha else'RigidSingleUVOutput'
        lines+=[f'{output} VS{name}(RigidUVInput input) {{']
        for i in range(4):lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in VS_LITERALS[4*i:4*i+4])+'));')
        lines+=(['    precise float4 r0=float4(0,0,input.uv),r1=0,r2=float4(input.normal,0),r3=float4(input.position,1),r4=input.color,r5=0;']if alpha else
                ['    precise float4 r0=float4(input.uv,0,0),r1=0,r2=0,r3=float4(input.normal,0),r4=input.color,r5=float4(input.position,1);'])
        lines+=['    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0; precise float ps=0; bool p0=false;']
        stage='AVS'if alpha else'VS';lines+=control_source(stage,inventory[stage]['rows'])
        lines+=[f'    {output} result;result.position=output62;result.t0=output0.xy;']
        lines+=(['    result.t1=output1;result.t2=output2.xyz;result.t3=output3;']if alpha else
                ['    result.t1=output1;result.t2=output2;result.t3=output3.xyz;result.t4=output4;'])
        lines+=['    return result;','}',f'float4 PS{name}({output} input):SV_Target0 {{']
        stage='APS'if alpha else'PS'
        for i in range(4):lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in LITERALS[stage][4*i:4*i+4])+'));')
        lines+=(['    precise float4 r0=float4(input.t0,0,0),r1=input.t1,r2=float4(input.t2,0),r3=input.t3,r4=0,r5=0,r6=0,r7=0;']if alpha else
                ['    precise float4 r0=float4(input.t0,0,0),r1=input.t1,r2=input.t2,r3=float4(input.t3,0),r4=input.t4,r5=0,r6=0,r7=0;'])
        lines+=['    precise float4 output0=0;precise float ps=0;bool p0=false;']+control_source(stage,inventory[stage]['rows'])+['    return output0;','}']
    return '\n'.join(lines)+'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl',type=Path);parser.add_argument('--verify-hlsl',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();inventory=inspect(image);source=shader_source(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(source,encoding='utf-8')
    if args.verify_hlsl:screen.require(args.verify_hlsl.read_text(encoding='utf-8')==source,'Rigid UV HLSL differs from original transcription')
    if args.output:args.output.write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf-8')
    print('PASS simpsons_uv: four complete original shaders, UV/geometry semantics, both depth banks and strict alpha cutoff')


if __name__=='__main__':main()
