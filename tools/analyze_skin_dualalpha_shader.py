"""Exact original dual-textured skin alpha transcription; no runtime execution.

The alpha pass has twelve vertex inputs and samples only the material's base
texture at stage0. Neither the second UV nor character shadow is consumed.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import analyze_skin_shader as skin
import analyze_skin_alpha_shader as alpha
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
from skin_shader_emit import vs_issue

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x8201F984, 4616, 3812, 804, 7,
     'ca9ae62d487d4278d793ba52dee3be5fae2053fe49c38b5471ecfb14f200ab4c'),
    ('PS', 0x82021344, 732, 540, 192, 2,
     'bda100966ca44f01208a9b780692c1505005a1034a8d6632f81b294edaf35cb7'),
)
HEADERS = {
    'VS': (0x102A1101, 3748, 868, 36, 128, 3568, 3608, 0, 0),
    'PS': (0x102A1100, 476, 256, 36, 128, 388, 428, 0, 0),
}
CF = {'VS': skin.CF['VS'],
      'PS': ((0x11002, 0x1200), (0, 0xC400), (0x6003, 0x1200), (0x6009, 0x2200))}
LITERALS = {'VS': (0,)*12+(0, 0x3F800000, 0x3F000000, 0x40400000),
            'PS': (0,)*12+(0x3F800000, 0, 0, 0)}
SEMANTICS = (0x00100007, 0x00003008, 0x00005009, 0x0000200A,
             0x0000100B, 0x0000A00C, 0x0001000D, 0x0002000E,
             0x0003000F, 0x00040010, 0x00050011, 0x00260012)
FETCHES = ((5, (0,1,2,7)), (8, (0,1,2,7)), (11, (0,1,7,7)),
           (4, (3,2,1,0)), (7, (0,1,2,3)), (1, (0,1,2,3)),
           (10, (0,1,2,7)), (3, (0,1,2,7)), (2, (0,1,2,7)),
           (0, (7,0,1,2)), (9, (0,1,2,7)), (6, (0,1,2,7)))


def decode_record(record, profile):
    stage, _, size, start, length, pairs, digest = profile
    screen.require(len(record)==size and hashlib.sha256(record).hexdigest()==digest,
                   'Dual skin alpha original record extent/hash changed')
    screen.require(struct.unpack_from('>9I',record)==HEADERS[stage], 'Dual skin alpha header changed')
    screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length), 'Dual skin alpha code bounds changed')
    screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS[stage], 'Dual skin alpha literals changed')
    screen.require(record[-12:].hex()==('4e4a0001' if stage=='VS' else '4e4a0000')+'337e6115b974e3ee',
                   'Dual skin alpha non-executable trailer changed')
    if stage=='VS':
        screen.require(struct.unpack_from('>12I',record,3648)==SEMANTICS, 'Dual skin alpha fetch semantics changed')
    code=record[start:start+length]
    control=[]
    for pair in range(pairs):
        a,b,c=screen.words(code,pair*12)
        control.extend(((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)))
    screen.require(tuple(control)==CF[stage], 'Dual skin alpha control schedule changed')
    rows={}
    for index,(lo,hi) in enumerate(control):
        if hi>>12 not in (1,2): continue
        first,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
        screen.require(0<count<=6 and sequence>>(2*count)==0, 'Dual skin alpha issue sequence changed')
        for n in range(count):
            slot=first+n;screen.require(slot not in rows, 'Repeated dual skin alpha issue')
            raw=screen.words(code,slot*12);fetch=bool(sequence&(1<<(2*n)))
            rows[slot]=dict(raw=raw,fetch=fetch,control_index=index,
                            fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
    screen.require(sorted(rows)==list(range(pairs,length//12-1)), 'Incomplete dual skin alpha instruction coverage')
    if stage=='VS':
        screen.require([s for s,r in rows.items() if r['fetch']]==list(range(7,19)), 'Dual skin alpha vertex fetch set changed')
        for slot,(destination,swizzle) in zip(range(7,19),FETCHES):
            f=rows[slot]['fields']
            screen.require(f['kind']=='vertex_fetch' and f['fetch_constant_index']==95 and
                           f['destination_register']==destination and tuple(f['destination_swizzle'])==swizzle,
                           'Dual skin alpha vertex register assignment changed')
    else:
        screen.require([s for s,r in rows.items() if r['fetch']]==[2], 'Dual skin alpha texture fetch set changed')
        f=rows[2]['fields']
        screen.require(f['fetch_constant_index']==0 and f['source_register']==0 and f['destination_register']==4 and
                       f['source_components']==[0,1,0] and f['destination_swizzle']==[0,1,2,3],
                       'Dual skin alpha stage0 base sample changed')
    return rows


def inspect(image):
    screen.require(hashlib.sha256(image[0x1CD48:0x1CD48+0x7110]).hexdigest()==
                   '9fbe1c024716b82ecdf94cb79121f468663cd2887cc0c945896c075d5ccd9eb7',
                   'Original dual skin effect changed')
    return {p[0]:decode_record(image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]],p) for p in PROFILES}


def shader_source(image):
    rows=inspect(image)
    # Shared register/legacy arithmetic helpers only; all issued operations
    # below come from the independently pinned alpha records.
    common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common=common.replace('float4 vc[30]','float4 vc[256]').replace('float4 pc[50]','float4 pc[64]')
    common=common.replace('shadowSampler0','skinDualAlphaSampler').replace('shadow0','skinDualAlphaBase')
    lines=['// Original dual skin alpha VS8201F984 / PS82021344. Generated offline.', common,
           'struct SkinInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
           '    float4 weights:TEXCOORD2; float4 indices:TEXCOORD3; float4 color:TEXCOORD4; float2 uv:TEXCOORD5;',
           '    float3 morph1:TEXCOORD6; float3 morph2:TEXCOORD7; float3 morph3:TEXCOORD8;',
           '    float3 morph4:TEXCOORD9; float3 morph5:TEXCOORD10; float3 morph6:TEXCOORD11; };',
           'struct SkinOutput { float4 position:SV_Position; float2 t0:TEXCOORD0; float3 t1:TEXCOORD1;',
           '    float4 t2:TEXCOORD2; float4 t3:TEXCOORD3; };',
           'cbuffer SkinDualAlphaProbe : register(b2) { float4 skinDualAlphaProbe[4]; };',
           'SkinOutput VSSkinDualAlpha(SkinInput input) {',
           '    precise float4 r5=float4(input.position,0),r8=float4(input.normal,0);',
           '    precise float4 r7=input.weights,r4=input.indices.wzyx,r1=input.color,r11=float4(input.uv,0,0);',
           '    precise float4 r10=float4(input.morph1,0),r3=float4(input.morph2,0),r2=float4(input.morph3,0);',
           '    precise float4 r0=float4(0,input.morph4),r9=float4(input.morph5,0),r6=float4(input.morph6,0);',
           '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;',
           '    const float4 k255=float4(0,1,0.5,3); precise float ps=0; bool p0=false; int a0=0;']
    lines+=skin.emit_vs_control(rows['VS'])
    lines+=['    SkinOutput o; o.position=output62; o.t0=output0.xy; o.t1=output1.xyz;',
            '    o.t2=output2; o.t3=output3; return o; }',
            'float4 PSSkinDualAlpha(SkinOutput input):SV_Target0 {',
            '    const float4 k255=float4(1,0,0,0); precise float4 r0=float4(input.t0,0,0),r1=float4(input.t1,0);',
            '    precise float4 r2=input.t2,r3=input.t3,r4=0,output0=0; precise float ps=0; bool p0=false;']
    for slot,row in rows['PS'].items():
        text=alpha.ps_issue(slot,row)
        text=text.replace('shadowSampler0','skinDualAlphaSampler').replace('shadow0','skinDualAlphaBase')
        if not row['fetch'] and row['fields']['scalar_opcode']==42:
            expression=rigid.scalar(row,'PS');a,b=expression.split('*')
            text=text.replace(expression,f'rigidLegacyProduct({a},{b})')
        lines.append(text)
    lines+=['    return output0; }',
            '[maxvertexcount(1)] void GSSkinDualAlphaProbe(point SkinOutput input[1],inout PointStream<SkinOutput> stream) { stream.Append(input[0]); }',
            'SkinOutput VSSkinDualAlphaPixelProbe(uint id:SV_VertexID) {',
            '    SkinOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
            '    o.t0=skinDualAlphaProbe[0].xy; o.t1=skinDualAlphaProbe[1].xyz;',
            '    o.t2=skinDualAlphaProbe[2]; o.t3=skinDualAlphaProbe[3]; return o; }','']
    return '\n'.join(lines)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl',type=Path);parser.add_argument('--verify',action='store_true')
    args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes();source=shader_source(image)
    if args.emit_hlsl: args.emit_hlsl.write_text(source,encoding='utf-8')
    print('PASS original dual skin alpha: 59 VS/13 PS issues; twelve VS inputs, single stage0 base sample')

if __name__=='__main__':main()
