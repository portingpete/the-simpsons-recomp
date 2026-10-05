"""Exact simpsons_flipbook records, linkage and offline HLSL transcription."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
import analyze_chocolate_shader as chocolate
import analyze_edge_shaders as edge
import analyze_post_effect_catalog as catalog
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen

ROOT=Path(__file__).resolve().parents[1]
FX_ADDRESS,FX_SIZE=0x82039208,0x2E00
FX_SHA='23ae68cc3dc8f0234c1ceb6d6cff0616c457760648792cc3f41837d42a7b1041'
PROFILES=(
    ('VS',0x820398BC,1196,716,480,7,'bb1f3aca80e835052fc68f464fe18e443cd5f095f0763e5cc29c06a7950783ab'),
    ('AVS',0x82039D70,1228,688,540,7,'5b63a4f4ed05cf35050d06f5f24b6fbdb4d41d2cbe2085366239222d3f7cf9e1'),
    ('PS',0x8203A24C,1008,768,240,3,'f4541c0c085a045a0fbcf9d98362f41ff85d461ab90b3798968ff79f6bb96b44'),
    ('APS',0x8203A644,728,536,192,2,'270450a76dce9e3b222a7cf05d062d56e692e59c1b6320cc3a29f12918ccb25a'),
)
HEADERS={'VS':(0x102A1101,652,544,36,120,524,564,0,0),
         'AVS':(0x102A1101,624,604,36,120,476,516,0,0),
         'PS':(0x102A1100,704,304,36,120,620,660,0,0),
         'APS':(0x102A1100,472,256,36,120,384,424,0,0)}
CF={'VS':((0xF2556007,0x1200),(0x300D,0x1000),(0x4006,0xB000),(0x6010,0x1200),
          (0x2016,0x1200),(0x2007,0xB000),(0x1018,0x1200),(0,0xC200),(0x1019,0x1200),
          (0,0xC400),(0x601A,0x1200),(0x6020,0x1200),(0x1026,0x2200),(0,0)),
    'AVS':((0xF2556007,0x1200),(0x300D,0x1000),(0x4006,0xB000),(0x6010,0x1200),
           (0x2016,0x1200),(0x2007,0xB000),(0x1018,0x1200),(0,0xC200),(0x1019,0x1200),
           (0,0xC400),(0x601A,0x1200),(0x6020,0x1200),(0x6026,0x2200),(0,0)),
    'PS':((0x11003,0x1200),(0,0xC400),(0x6004,0x1200),(0x600A,0x1200),(0x3010,0x2200),(0,0)),
    'APS':((0x11002,0x1200),(0,0xC400),(0x6003,0x1200),(0x6009,0x2200))}
LITERALS={'VS':(0,)*13+(0x3F000000,0,0),
          'AVS':(0,)*13+(0x3F800000,0x3F000000,0),
          'PS':(0,)*4+(0x3A802008,0x3F666666,0,0,0x3F333333,0x3E800000,0x3F75C28F,0,
                         0x3E4CCCCD,0x3F000000,0x3E000000,0x42000000),
          'APS':(0,)*12+(0x3F800000,0,0,0)}
SEMANTICS={'VS':(604,(0x00100007,0x00003008,0x0000A009,0x0020500A)),
           'AVS':(556,(0x00100007,0x00003008,0x0000A009,0x0020500A))}
VS_FETCHES={'VS':((7,5,[0,1,2,5]),(8,3,[0,1,2,7]),(9,4,[0,1,2,3]),(10,2,[0,1,7,7])),
            'AVS':((7,3,[0,1,2,5]),(8,4,[0,1,2,7]),(9,5,[0,1,2,3]),(10,2,[0,1,7,7]))}
EXPORT_MASKS={'VS':{62:15,0:3,1:7,2:15},'AVS':{62:15,0:3,1:15,2:7,3:15},'PS':{0:15},'APS':{0:15}}


def decode_record(record,profile):
    stage,_,size,offset,length,pairs,_=profile
    screen.require(len(record)==size,'Flipbook record extent differs')
    screen.require(struct.unpack_from('>9I',record)==HEADERS[stage],'Flipbook header differs')
    screen.require(record[-12:].hex()==f'4e4a000'+str({'VS':3,'AVS':1,'PS':2,'APS':0}[stage])+'033d0f1ce6ae4466','Flipbook trailer differs')
    screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS[stage],'Flipbook literal bank differs')
    screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length),'Flipbook code mapping differs')
    code=record[offset:offset+length];cf,slots=chocolate.schedule(code,pairs)
    screen.require(tuple(cf)==CF[stage],'Flipbook complete control flow differs')
    rows={};exports={}
    for slot,fetch,_ in slots:
        raw=screen.words(code,slot*12);f=screen.decode_fetch(raw)if fetch else edge.alu(*raw)
        rows[slot]=dict(raw=raw,fetch=fetch,fields=f)
        if not fetch:
            screen.require(not any(f[k]for k in('absolute_constants','vector_destination_relative',
                'scalar_destination_relative_or_export_zero','predicated','predicate_condition',
                'constant_address_register_relative','constant_0_relative','constant_1_relative')),'Unqualified flipbook ALU modifier')
            screen.require(not any(s['relative_temporary']for s in f['sources']),'Relative flipbook temporary')
            if f['export']:exports[f['vector_destination']]=exports.get(f['vector_destination'],0)|f['vector_mask']|f['scalar_mask']
        else:screen.require(not raw[1]&0x80000000,'Predicated flipbook fetch')
    screen.require(exports==EXPORT_MASKS[stage],'Flipbook export linkage differs')
    if stage.endswith('VS'):
        at,semantic=SEMANTICS[stage]
        screen.require(struct.unpack_from('>4I',record,at)==semantic,'Flipbook vertex semantic associations differ')
        screen.require([i for i,r in rows.items()if r['fetch']]==[7,8,9,10],'Flipbook vertex fetch inventory differs')
        for slot,dest,swizzle in VS_FETCHES[stage]:
            screen.require(rows[slot]['fields']==dict(kind='vertex_fetch',source_register=0,destination_register=dest,
                destination_swizzle=swizzle,fetch_constant_index=95,source_component=0,format_field=0,stride_dwords=0,
                offset_field=0,runtime_declaration_patch_verified=False),'Flipbook original input fetch differs')
        screen.require(rows[12]['fields']['scalar_opcode']==rows[13]['fields']['scalar_opcode']==12 and
            rows[18]['fields']['sources'][2]['absolute_temporary']and rows[19]['fields']['vector_opcode']==13,
            'Flipbook truncation or signed-loop arithmetic differs')
    else:
        slot=3 if stage=='PS'else 2
        screen.require([i for i,r in rows.items()if r['fetch']]==[slot],'Flipbook sampler inventory differs')
        screen.require(rows[slot]['fields']==dict(kind='texture_fetch',source_register=0,destination_register=3 if stage=='PS'else 4,
            destination_swizzle=[0,1,2,7]if stage=='PS'else[0,1,2,3],fetch_constant_index=0,
            normalized_coordinates=True,source_components=[0,1,0],dimension_field=1,mag_filter=3,min_filter=3,mip_filter=3,
            anisotropy=7,arbitrary_filter=0,volume_mag_filter=3,volume_min_filter=3,computed_lod=True,register_lod=False,
            register_gradients=False,fetch_valid_only=True,sample_location=0,lod_bias_field=0,offset_fields=[0,0,0]),
            'Flipbook original base sample differs')
        if stage=='PS':
            f=rows[18]['fields'];screen.require(f['export']and f['vector_mask']==5 and f['scalar_mask']==2 and
                f['scalar_opcode']==42,'Flipbook co-issued scalar export differs')
    for slot,row in rows.items():
        if not(stage.endswith('VS')and row['fetch']):issue(slot,row,'VS'if stage.endswith('VS')else'PS')
    return rows


def inspect(image):
    screen.require(len(image)==screen.IMAGE_SIZE and rigid.sha(image)==screen.IMAGE_SHA256,'Retail image differs')
    blob=image[FX_ADDRESS-screen.BASE:FX_ADDRESS-screen.BASE+FX_SIZE]
    screen.require(rigid.sha(blob)==FX_SHA,'Original flipbook effect differs')
    metadata=catalog.inspect_blob(blob,FX_ADDRESS)
    screen.require([(t['name'],t['handle'],t['passes'][0]['context_offset'])for t in metadata['techniques']]==
        [('rigid','0x0003FFFC',0x2560),('rigidalpha','0x0007FFFC',0x2840)],'Flipbook pass associations differ')
    out={}
    for p in PROFILES:
        stage,address,size,offset,length,pairs,digest=p;record=image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(record)==digest,'Flipbook '+stage+' record hash differs')
        out[stage]=dict(address=f'{address:08X}',sha256=digest,code_sha256=rigid.sha(record[offset:offset+length]),
                        cf_pairs=pairs,rows=decode_record(record,p))
    return out


def scalar(row,stage):
    f=row['fields'];op=f['scalar_opcode']
    if op==12:
        src=rigid.operand(f,2,stage);return 'trunc(('+src+').w)'
    if op==3:return 'rigidLegacyProduct(('+rigid.operand(f,2,stage)+').w,ps)'
    return rigid.scalar(row,stage)


def issue(slot,row,stage):
    f=row['fields'];lines=['    { // slot'+str(slot)]
    if row['fetch']:
        screen.require(stage=='PS','Original vertex fetch belongs to the input interface')
        lines+=['        precise float4 v=flipbookBase.Sample(flipbookSampler,r0.xy);']
        for lane,selector in enumerate(f['destination_swizzle']):
            if selector<4:lines.append(f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[selector]};')
        return '\n'.join(lines+['    }'])
    op,mask,sop,sm=f['vector_opcode'],f['vector_mask'],f['scalar_opcode'],f['scalar_mask']
    if mask:
        a,b,c=(rigid.operand(f,i,stage)for i in range(3))
        if op==0:expr=a+'+'+b
        elif op==1:expr='rigidLegacyMultiply('+a+','+b+')'
        elif op in(2,3):expr=('max'if op==2 else'min')+'('+a+','+b+')'
        elif op in(5,6):expr='float4('+a+('>'if op==5 else'>=')+b+')'
        elif op==10:expr='floor('+a+')'
        elif op==11:expr='rigidLegacyMultiply('+a+','+b+')+'+c
        elif op in(12,13):
            expr='float4('+','.join('('+a+').'+lane+('==0.0'if op==12 else'>=0.0')+'?('+b+').'+lane+':('+c+').'+lane for lane in'xyzw')+')'
        elif op in(15,16,17):
            lanes='xyzw'if op==15 else'xyz'if op==16 else'xy'
            lines.append('        precise float4 product=rigidLegacyMultiply('+a+','+b+');')
            expr='('+ '+'.join('product.'+lane for lane in lanes)+(' + ('+c+').x'if op==17 else'')+').xxxx'
        else:raise ValueError('Unqualified flipbook vector opcode '+str(op))
        lines.append('        precise float4 v='+('saturate('+expr+')'if f['vector_clamp']else expr)+';')
    if sop!=50:
        expr=scalar(row,stage)
        if sop in(28,29):lines+=['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
        else:lines.append('        precise float s='+('saturate('+expr+')'if f['scalar_clamp']else expr)+';')
    for lane in range(4):
        vm,scalarMask=mask&(1<<lane),sm&(1<<lane)
        if f['export']:
            if vm or scalarMask:lines.append(f'        output{f["vector_destination"]}.{"xyzw"[lane]}='+('1.0'if vm and scalarMask else'v.'+'xyzw'[lane]if vm else's')+';')
        else:
            if vm:lines.append(f'        r{f["vector_destination"]}.{"xyzw"[lane]}=v.{"xyzw"[lane]};')
            if scalarMask:lines.append(f'        r{f["scalar_destination"]}.{"xyzw"[lane]}=s;')
    if sop!=50:lines.append('        ps=s;')
    if sop in(28,29):lines.append('        p0=predicate;')
    return '\n'.join(lines+['    }'])


def control_source(stage,rows):
    def executable(first,last):
        return [issue(i,rows[i],'VS'if stage.endswith('VS')else'PS')for i in range(first,last+1)if not rows[i]['fetch']]
    if stage.endswith('VS'):
        # Exact CF2 conditional jump to CF6, then CF5 unconditional jump to
        # CF7: both original arms rejoin before clip and remaining exports.
        lines=executable(7,15)+['    [branch] if(p0) { // c47.w>0, original CF3..4 signed loop.']
        lines+=executable(16,23)+['    } else { // Original CF6 clamp-to-columns path.']+executable(24,24)+['    }']
        return lines+executable(25,max(rows))
    return [issue(i,row,'PS')for i,row in rows.items()]


def shader_source(image):
    inventory=inspect(image)
    common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common=common[common.index('cbuffer RigidVertexConstants'):]
    for declaration in('Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);',
                       'SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);'):
        common=common.replace(declaration+'\n','')
    common=common.replace('float4 vc[30]','float4 vc[48]').replace('float4 pc[50]','float4 pc[51]')
    lines=['// Exact original simpsons_flipbook opaque and alpha programs.',common,
           'Texture2D<float4> flipbookBase:register(t0);SamplerState flipbookSampler:register(s0);',
           'struct FlipbookInput { float3 position:TEXCOORD0;float3 normal:TEXCOORD1;float4 color:TEXCOORD2;float2 uv:TEXCOORD3; };',
           'struct FlipbookOutput { float4 position:SV_Position;float2 t0:TEXCOORD0;float3 t1:TEXCOORD1;float4 t2:TEXCOORD2; };',
           'struct FlipbookAlphaOutput { float4 position:SV_Position;float2 t0:TEXCOORD0;float4 t1:TEXCOORD1;float3 t2:TEXCOORD2;float4 t3:TEXCOORD3; };']
    for alpha in(False,True):
        output='FlipbookAlphaOutput'if alpha else'FlipbookOutput';name='FlipbookAlpha'if alpha else'Flipbook';stage='AVS'if alpha else'VS'
        lines+=[f'{output} VS{name}(FlipbookInput input) {{']
        for i in range(4):lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in LITERALS[stage][i*4:i*4+4])+'));')
        lines+=['    precise float4 r0=0,r1=0,r2=float4(input.uv,0,0),'+
            ('r3=float4(input.position,1),r4=float4(input.normal,0),r5=input.color;'if alpha else'r3=float4(input.normal,0),r4=input.color,r5=float4(input.position,1);'),
            '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;precise float ps=0;bool p0=false;']
        lines+=control_source(stage,inventory[stage]['rows'])+[f'    {output} result;result.position=output62;result.t0=output0.xy;']
        lines+=(['    result.t1=output1;result.t2=output2.xyz;result.t3=output3;']if alpha else['    result.t1=output1.xyz;result.t2=output2;'])
        lines+=['    return result;','}',f'float4 PS{name}({output} input):SV_Target0 {{'];stage='APS'if alpha else'PS'
        for i in range(4):lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in LITERALS[stage][i*4:i*4+4])+'));')
        lines+=['    precise float4 r0=float4(input.t0,0,0),'+
            ('r1=input.t1,r2=float4(input.t2,0),r3=input.t3,r4=0;'if alpha else'r1=float4(input.t1,0),r2=input.t2,r3=0,r4=0;'),
            '    precise float4 output0=0;precise float ps=0;bool p0=false;']
        lines+=control_source(stage,inventory[stage]['rows'])+['    return output0;','}']
    return '\n'.join(lines)+'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--emit-hlsl',type=Path)
    parser.add_argument('--verify-hlsl',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
    image=(ROOT/'analysis/simpsons.pe').read_bytes();inventory=inspect(image);source=shader_source(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(source,encoding='utf-8')
    if args.verify_hlsl:screen.require(args.verify_hlsl.read_text(encoding='utf-8')==source,'Flipbook generated source differs')
    if args.output:args.output.write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf-8')
    print('PASS simpsons_flipbook: four original programs, signed loop/clamp, truncation, input/export linkage and scalar export')


if __name__=='__main__':main()
