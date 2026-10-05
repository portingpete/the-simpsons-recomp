"""Pinned gloss, multitone and normalmap rigidalpha instruction transcriptions.

Offline only. Gloss uses the byte-identical proven VS168F8; the other two
vertex records share their executable and complete fetch linkage metadata.
Each pixel record samples only the base texture and has no conditional flow.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import analyze_168f8alpha_shader as a168
import analyze_chocolate_shader as chocolate
import analyze_edge_shaders as edge
import analyze_post_effect_catalog as catalog
import analyze_rigid_dualtextured_uv_shader as uv
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('GVS',0x8201A430,0x2AC,0x1B0,0xFC,3,'458c7b1abbaa608b8b67fdee61c9060450e692d98ddf7afcdce4355a2bf23a84'),
    ('GPS',0x8201B0BC,0x460,0x298,0x1C8,4,'46423f9806545e712e6db6dde2ca77346c1bf38acee7e0a5aa8e28b3b288991d'),
    ('MVS',0x82055440,0x2C0,0x1B8,0x108,3,'dbd7d51daade8424ee8460411a49311e305e1049361d5db6157cbf1b50c339bf'),
    ('MPS',0x820560B8,0x2E0,0x220,0xC0,2,'4ddb87041c855ae6d3f0b574192f2868b03f66bb6b599ddc027488542375b474'),
    ('NVS',0x820589EC,0x2C0,0x1B8,0x108,3,'30deb8a3890a3046efede28c1a02c4bdaebd5fec29c186508452bafadc2176fc'),
    ('NPS',0x82059880,0x42C,0x2A0,0x18C,4,'3e5e251e300a40855bb3a33acc62c77aed7facd843ddae2543c4b21819ba4d64'),
)
HEADERS = {
    'GVS':(0x102A1101,0x1B0,0xFC,0x24,0x7C,0,0x144,0,0),
    'GPS':(0x102A1100,0x258,0x208,0x24,0x7C,0x200,0x228,0,0),
    'MVS':(0x102A1101,0x1B8,0x108,0x24,0x80,0,0x148,0,0),
    'MPS':(0x102A1100,0x1E0,0x100,0x24,0x80,0x188,0x1B0,0,0),
    'NVS':(0x102A1101,0x1B8,0x108,0x24,0x80,0,0x148,0,0),
    'NPS':(0x102A1100,0x260,0x1CC,0x24,0x80,0x208,0x230,0,0),
}
CF = {
    'GVS':a168.CF['VS'],
    'MVS':((0xF1555003,0x1201),(0,0xC200),(0x4008,0x1200),(0,0xC400),(0x600C,0x1200),(0x3012,0x2200)),
    'GPS':((0x11004,0x1200),(0,0xC400),(0x6005,0x1200),(0x600B,0x1200),
           (0x6011,0x1200),(0x6017,0x1200),(0x601D,0x1200),(0x2023,0x2200)),
    'MPS':((0x11002,0x1200),(0,0xC400),(0x6003,0x1200),(0x6009,0x2200)),
    'NPS':((0x11004,0x1200),(0,0xC400),(0x6005,0x1200),(0x600B,0x1200),
           (0x6011,0x1200),(0x6017,0x1200),(0x301D,0x2200),(0,0)),
}
CF['NVS']=CF['MVS']
TRAILERS = {stage:'4e4a000'+('1' if stage.endswith('VS') else '0')+tail
            for stage,tail in ((s,t) for s,t in [('GVS','f4504bfd768dc110'),('GPS','f4504bfd768dc110'),
            ('MVS','b9b03c7c177af651'),('MPS','b9b03c7c177af651'),('NVS','6aa0b96f8005367d'),('NPS','6aa0b96f8005367d')])}
LITERALS=(0,)*12+(0x3F800000,0,0,0)
VS_FETCHES=((3,1,[0,1,2,5]),(4,0,[7,0,1,2]),(5,2,[0,1,2,3]),(6,3,[0,1,7,7]),(7,3,[7,7,0,1]))
VS_FETCH_WORDS=((0x05F81000,0xA88,0),(0x05F80000,0x447,0),(0x05F82000,0x688,0),
                (0x05F83000,0xFC8,0),(0x05F83000,0x23F,0))
LINKAGE = {
    'GVS':bytes.fromhex('00000000000000fc003100030000000000000000000034840000000100000004000000090000029000100003000030040000a00500205006000030500001f151000572520008f3a00000100b0000000d0000000e0000000f000010100000001100000012000010130000100c'),
    'MVS':bytes.fromhex('000000000000010800310003000000000000000000003c840000000100000005000000090000029000100003000030040000a00500005006002150070000f0500001f151000572520008f3a00000100c0000000e0000000f00000010000010110000001200000013000010140000100d'),
}
LINKAGE['NVS']=LINKAGE['MVS']
EFFECTS=(('G',0x82019988,0x33C0,'7115380680315fe44162a744a5121a9c5b5534e0ed3f0e7d04b6ebce5132e983',0x2DF0),
         ('M',0x820547E8,0x3620,'20736b7201ff5a656a6409c36df50a4ca102d4b67db6c3a326f3f9000816b8a3',0x3040),
         ('N',0x82057E08,0x3A40,'7ceab412bbcb59ecf575a288618b80d01d854c77ed411fbaa0143c40c99b3f34',0x3450))


def issue(slot,row,stage):
    f=row['fields']
    if f.get('scalar_opcode')==45:
        # ADD_CONST_1: odd TEMP bit belongs to the scalar opcode, not src3_sel.
        # Both vector and scalar read the old r0/r1 values at this co-issue.
        screen.require(stage=='PS' and slot==14 and row['raw']==(0xB5810100,0x046C6CC0,0x8000FF28),
                       'Unqualified family ADD_CONST_1 form')
    source=uv.issue(slot,row,stage)
    if f.get('scalar_opcode')==3:
        old=rigid.scalar(row,stage)
        screen.require(old.endswith('*ps'),'Family MUL_PREV source differs')
        source=source.replace(old,'rigidLegacyProduct('+old[:-3]+',ps)')
    return source


def decode_record(record,profile):
    stage,_,extent,offset,length,pairs,_=profile
    screen.require(len(record)==extent,'Family alpha record extent differs')
    screen.require(struct.unpack_from('>9I',record)==HEADERS[stage],'Family alpha header differs')
    screen.require(record[-12:].hex()==TRAILERS[stage],'Family alpha trailer differs')
    code=record[offset:offset+length];cf,slots=chocolate.schedule(code,pairs)
    screen.require(tuple(cf)==CF[stage],'Family alpha complete CF differs')
    if stage.endswith('VS'):
        screen.require(record[HEADERS[stage][6]:offset]==LINKAGE[stage],'Family alpha vertex linkage differs')
    else:
        screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS,'Family alpha literals differ')
        screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length),'Family alpha literal/code map differs')
    rows={}
    for slot,fetch,_ in slots:
        raw=screen.words(code,slot*12);f=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        if not fetch:
            screen.require(not any(f[k] for k in ('absolute_constants','vector_destination_relative',
                'scalar_destination_relative_or_export_zero','predicated','predicate_condition',
                'constant_address_register_relative','constant_0_relative','constant_1_relative')),
                'Unqualified family alpha ALU modifier')
            screen.require(not any(s['relative_temporary'] for s in f['sources']),'Relative family alpha source')
            screen.require(f['vector_opcode'] in (0,1,2,3,6,11,15,16) and
                           f['scalar_opcode'] in (0,3,5,14,16,22,40,42,44,45,46,50),'Unqualified family alpha arithmetic')
        rows[slot]=dict(raw=raw,fetch=fetch,fields=f)
    if stage.endswith('VS'):
        fetches=VS_FETCHES[:4] if stage=='GVS' else VS_FETCHES
        screen.require([i for i,r in rows.items() if r['fetch']]==[v[0] for v in fetches],'Family vertex fetch inventory differs')
        for (slot,dest,swizzle),raw in zip(fetches,VS_FETCH_WORDS):
            screen.require(rows[slot]['raw']==raw,'Family vertex fetch raw flags differ')
            screen.require(rows[slot]['fields']==dict(kind='vertex_fetch',source_register=0,destination_register=dest,
                destination_swizzle=swizzle,fetch_constant_index=95,source_component=0,format_field=0,stride_dwords=0,
                offset_field=0,runtime_declaration_patch_verified=False),'Family vertex fetch semantics differ')
    else:
        slot=pairs;screen.require([i for i,r in rows.items() if r['fetch']]==[slot],'Family alpha sample inventory differs')
        f=rows[slot]['fields']
        screen.require(rows[slot]['raw']==(0x10084001,0x1F1FF688,0x00004000), 'Family alpha base sample instruction differs')
        if stage=='GPS':
            screen.require(rows[14]['raw']==(0xB5810100,0x046C6CC0,0x8000FF28),
                           'Original gloss ADD_CONST_1 co-issue differs')
    for slot,row in rows.items():
        if not(stage.endswith('VS') and row['fetch']):issue(slot,row,'VS' if stage.endswith('VS') else 'PS')
    return rows


def inspect(image):
    screen.require(len(image)==screen.IMAGE_SIZE and rigid.sha(image)==screen.IMAGE_SHA256,'Retail image differs')
    for path,digest in ((rigid.four.UCODE,rigid.four.UCODE_SHA),(rigid.four.XENOS,rigid.four.XENOS_SHA)):
        screen.require(rigid.sha(path.read_bytes())==digest,'Pinned original instruction format reference differs')
    for _,address,size,digest,context in EFFECTS:
        fx=image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(fx)==digest,'Family effect hash differs')
        metadata=catalog.inspect_blob(fx,address)
        alpha=next(t for t in metadata['techniques'] if t['name']=='rigidalpha')
        screen.require(alpha['handle']=='0x0007FFFC' and alpha['passes'][0]['context_offset']==context,'Family alpha association differs')
    result={}
    for profile in PROFILES:
        stage,address,size,offset,length,_,digest=profile
        record=image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(record)==digest,'Family alpha '+stage+' record hash differs')
        result[stage]=dict(address=f'{address:08X}',sha256=digest,code_sha256=rigid.sha(record[offset:-12]),rows=decode_record(record,profile))
    result['GPS']['scalar45']=dict(slot=14,raw=['B5810100','046C6CC0','8000FF28'],
        operation='ADD_CONST_1',constant=40,constant_component='z',temporary=1,temporary_component='x',
        result='pc[40].z + old r1.x',reference=str(rigid.four.UCODE),reference_sha256=rigid.four.UCODE_SHA)
    old=a168.PROFILES[0];old_record=image[old[1]-screen.BASE:old[1]-screen.BASE+old[2]]
    gloss=PROFILES[0];gloss_record=image[gloss[1]-screen.BASE:gloss[1]-screen.BASE+gloss[2]]
    screen.require(gloss_record[gloss[3]:-12]==old_record[old[3]:-12] and
                   gloss_record[HEADERS['GVS'][6]:gloss[3]]==old_record[a168.HEADERS['VS'][6]:old[3]],'Gloss VS168F8 alias differs')
    screen.require(result['MVS']['code_sha256']==result['NVS']['code_sha256'],'Shared family alpha VS differs')
    return result


def shader_source(image):
    inventory=inspect(image)
    common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common=common[common.index('cbuffer RigidVertexConstants'):common.index('// Original resource XYZW')]
    for declaration in ('Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);',
                        'SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);'):
        common=common.replace(declaration+'\n','')
    common=common.replace('float4 vc[30]','float4 vc[48]').replace('float4 pc[50]','float4 pc[51]')
    common=common.replace('// PS slot15 uses the original SM3 multiplication rule:',
                          '// Original SM3 multiplication rule for all family products:')
    lines=['// Original rigidalpha: gloss 8201A430/8201B0BC, multitone 82055440/820560B8,',
           '// normalmap 820589EC/82059880. Static transcription, old co-issued RHS.',common,
           'Texture2D<float4> uvTex0 : register(t0);','SamplerState uvSampler0 : register(s0);',
           'float4 uvSample0(float4 value) { return value; }',
           'struct RigidFamilyAlphaInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
           '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
           'struct RigidFamilyAlphaOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
           '    float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3; };',
           'RigidFamilyAlphaOutput VSRigidFamilyAlpha(RigidFamilyAlphaInput input) {',
           '    precise float4 r1=float4(input.position,1),r0=float4(0,input.normal);',
           '    precise float4 r2=input.color,r3=float4(input.uv,input.uv1);',
           '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0; precise float ps=0;']
    lines.extend(issue(slot,row,'VS') for slot,row in inventory['MVS']['rows'].items() if not row['fetch'])
    lines+=['    RigidFamilyAlphaOutput result; result.position=output62; result.t0=output0;',
            '    result.t1=output1; result.t2=output2; result.t3=output3; return result;','}']
    for stage,name in (('GPS','Gloss'),('MPS','Multitone'),('NPS','Normalmap')):
        lines.append(f'float4 PSRigid{name}Alpha(RigidFamilyAlphaOutput input):SV_Target0 {{')
        for i in range(4):lines.append(f'    const float4 k{252+i}=asfloat(uint4('+','.join('0x%08Xu'%v for v in LITERALS[i*4:i*4+4])+'));')
        lines+=['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=input.t3,r4=0,r5=0;',
                '    precise float4 output0=0; precise float ps=0; bool p0=false;']
        lines.extend(issue(slot,row,'PS') for slot,row in inventory[stage]['rows'].items())
        lines+=['    return output0;','}']
    return '\n'.join(lines)+'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl',type=Path);parser.add_argument('--verify',action='store_true')
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    source=shader_source(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(source,encoding='utf-8')
    else:screen.require((ROOT/'renderer/rigid_family_alpha_shader.hlsl').read_text(encoding='utf-8')==source,'Family alpha HLSL differs')
    if not args.verify:print(json.dumps(report,indent=2))
    print('PASS pinned family alpha shader records, executable aliases, CF, fetches and transcription')


if __name__=='__main__':main()
