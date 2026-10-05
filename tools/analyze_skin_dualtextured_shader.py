"""Pinned offline transcription of the opaque dual-textured skin shader pair.

The native runtime does not yet admit this source. GPU probes separately check
bone transforms and material arithmetic; neither is original GPU execution.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
from skin_shader_emit import vs_issue

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x8201E6DC, 4768, 3880, 888, 8,
     '08713acbe69ab3e002dc8f1178c838634aa682333a8b37bac99a8fefa00eeb4c'),
    ('PS', 0x82020B9C, 1952, 1088, 864, 8,
     '3e1ad914c957b00e67590047800efaf960b33826ee9db0f6b9d4e7636a80f227'),
)
HEADERS = {
    'VS': (0x102A1101, 3816, 952, 36, 128, 3616, 3656, 0, 0),
    'PS': (0x102A1100, 960, 992, 36, 128, 864, 904, 0, 0),
}
CF = {
    'VS': ((0xF5556008, 0x1203), (0xF555600E, 0x1203),
           (0x10093014, 0x1000), (0x4005, 0xB000), (0x6017, 0x1200),
           (0, 0xC200), (0x601D, 0x1200), (0x6023, 0x1200),
           (0x6029, 0x1200), (0x602F, 0x1200), (0, 0xC400),
           (0x6035, 0x1200), (0x603B, 0x1200), (0x6041, 0x1200),
           (0x2047, 0x2200), (0, 0)),
    'PS': ((0x96008, 0x1200), (0x600E, 0x1200), (0x6014, 0x1200),
           (0x601A, 0x1200), (0x6020, 0x1000), (0x3026, 0x1000),
           (0x400E, 0xB000), (0x4029, 0x1000), (0x400D, 0xB000),
           (0x555602D, 0x1200), (0x956033, 0x1200), (0x6039, 0x1200),
           (0x203F, 0x1200), (0x2041, 0x1200), (0, 0xC400), (0x4043, 0x2200)),
}
VS_LITERALS = (0,)*12 + (0, 0x3F800000, 0x3F000000, 0x40400000)
PS_LITERALS = (0,)*12 + (
    0x3F666666, 0x3E4CCCCD, 0x3F75C28F, 0x3E99999A,
    0x3F333333, 0x3DCCCCCD, 0x3F800000, 0x44800000,
    0x3E800000, 0x42000000, 0x42800000, 0xBF800000,
    0x3F000000, 0x3A002008, 0x3A802008, 0xBF000000, 0, 0, 0, 0)


def decode_record(record, profile):
    stage, _, size, start, length, pairs, digest = profile
    screen.require(len(record) == size, 'Dual skin record extent differs')
    screen.require(struct.unpack_from('>9I', record) == HEADERS[stage], 'Dual skin header differs')
    screen.require(record[-12:].hex() == ('4e4a0003' if stage == 'VS' else '4e4a0002') +
                   '337e6115b974e3ee', 'Dual skin trailer differs')
    literals = VS_LITERALS if stage == 'VS' else PS_LITERALS
    screen.require(struct.unpack_from('>%dI' % len(literals), record, HEADERS[stage][1]) == literals,
                   'Dual skin literal bank differs')
    if stage == 'VS':
        # Original declaration associations establish input order independently
        # of register destinations: POSITION0, NORMAL0, TEXCOORD0/1,
        # BLENDINDICES0, BLENDWEIGHT0, COLOR0, POSITION1..6.
        screen.require(struct.unpack_from('>13I', record, 3696) ==
                       (0x00100008, 0x00003009, 0x0000500A, 0x0001500B,
                        0x0000200C, 0x0000100D, 0x0000A00E, 0x0001000F,
                        0x00020010, 0x00030011, 0x00040012, 0x00050013, 0x00260014),
                       'Dual skin input semantic associations differ')
    code = record[start:start+length]
    control = []
    for pair in range(pairs):
        a, b, c = screen.words(code, pair*12)
        control.extend(((a, b & 65535), ((b >> 16 | c << 16) & 0xFFFFFFFF, c >> 16)))
    screen.require(tuple(control) == CF[stage], 'Dual skin control flow differs')
    rows = {}
    for index, (lo, hi) in enumerate(control):
        if hi >> 12 not in (1, 2):
            continue
        address, count, seq = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
        screen.require(0 < count <= 6 and seq >> (2*count) == 0, 'Dual skin issue sequence differs')
        for n in range(count):
            slot = address+n
            screen.require(slot not in rows, 'Repeated dual skin instruction')
            raw = screen.words(code, slot*12)
            fetch = bool(seq & (1 << (2*n)))
            rows[slot] = dict(raw=raw, fetch=fetch, control_index=index,
                              fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
    screen.require(list(rows) == list(range(pairs, length//12-1)), 'Incomplete dual skin coverage')
    # Hash also pins reflection, semantic associations, every instruction and
    # unused metadata. Explicit checks above diagnose structural mutations.
    screen.require(hashlib.sha256(record).hexdigest() == digest, 'Dual skin record hash differs')
    return rows


def inspect(image):
    screen.require(hashlib.sha256(image[0x1CD48:0x1CD48+0x7110]).hexdigest() ==
                   '9fbe1c024716b82ecdf94cb79121f468663cd2887cc0c945896c075d5ccd9eb7',
                   'Dual skin effect differs')
    result = {}
    for p in PROFILES:
        result[p[0]] = decode_record(image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]], p)
    return result


def ps_issue(slot, row):
    f = row['fields']
    if row['fetch']:
        screen.require(f['kind'] == 'texture_fetch' and f['normalized_coordinates'] and
                       f['computed_lod'] and not f['register_lod'] and not f['register_gradients'] and
                       f['dimension_field'] == 1 and f['fetch_valid_only'] and
                       not f['sample_location'] and not f['lod_bias_field'] and
                       f['mag_filter'] == f['min_filter'] == f['mip_filter'] == 3,
                       'Unqualified dual skin FETCH')
        if slot == 8:
            screen.require(f['fetch_constant_index'] == 1 and f['source_components'] == [0, 1, 0] and
                           f['source_register'] == 0 and f['destination_register'] == 0 and
                           f['destination_swizzle'] == [0, 2, 1, 7], 'Dual skin base FETCH differs')
            return ('    { // slot8: base color RGB is fetched into xzy.\n'
                    '        precise float4 v=skinBase.Sample(skinBaseSampler,r0.xy);\n'
                    '        r0.xyz=v.xzy;\n    }')
        screen.require(slot in range(45, 54) and f['fetch_constant_index'] == 0 and
                       f['source_register'] == 0 and f['source_components'] == [2, 3, 3],
                       'Dual skin shadow FETCH differs')
        offs = [((v+16) % 32-16)//2 for v in f['offset_fields']]
        screen.require(all(v % 2 == 0 for v in f['offset_fields']), 'Fractional dual skin offset')
        lines = [f'    {{ // slot{slot}',
                 '        precise float4 v=rigidShadowSample(shadow0.Sample(shadowSampler0,r0.zw,' +
                 f'int2({offs[0]},{offs[1]})));']
        for lane, sel in enumerate(f['destination_swizzle']):
            if sel != 7:
                screen.require(sel < 4, 'Dual skin sample literal')
                lines.append(f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[sel]};')
        return '\n'.join(lines+['    }'])
    screen.require(not any(f[k] for k in ('constant_address_register_relative', 'constant_0_relative',
                   'constant_1_relative', 'vector_destination_relative',
                   'scalar_destination_relative_or_export_zero', 'absolute_constants')),
                   'Unqualified dual skin PS modifiers')
    if slot == 23:
        screen.require(row['raw'] == (0xB4210100, 0x006CC6C0, 0xC0000328), 'Dual skin ADD_CONST_1 differs')
        # Split scalar encoding: bit0 of opcode45 selects odd temporary r1,
        # swizzle C0 selects c40.z and r1.x. Both co-issued RHS read old values.
        text = ('    { // slot23\n        precise float v=r0.x+r3.z;\n'
                '        precise float s=pc[40].z+r1.x;\n'
                '        r0.x=v; r1.y=s; ps=s;\n    }')
    else:
        text = rigid.issue(slot, row, 'PS').replace('pc[251]', 'k251')
        if f['vector_mask'] and f['vector_opcode'] == 1:
            a, b = [rigid.operand(f, i, 'PS') for i in range(2)]
            text = text.replace(a+'*'+b, 'rigidLegacyMultiply('+a+','+b+')')
    if f['predicated']:
        screen.require(slot == 34 and f['predicate_condition'], 'Dual skin ALU predicate differs')
        text = '    [branch] if(p0) { // Predicated issue, not an unconditional scalar overwrite.\n'+text+'\n    }'
    return text


def emit_control(rows, stage):
    lines, ends = [], []
    for index, (lo, hi) in enumerate(CF[stage]):
        while ends and ends[-1] == index:
            ends.pop()
            lines.append('    }')
        op = hi >> 12
        if op in (1, 2):
            for slot in range(lo & 4095, (lo & 4095)+((lo >> 12) & 7)):
                if stage == 'VS' and rows[slot]['fetch']:
                    continue
                lines.append((vs_issue if stage == 'VS' else ps_issue)(slot, rows[slot]))
        elif op == 11:
            target = lo & 4095
            screen.require(target > index, 'Non-forward dual skin flow')
            lines.append(f'    [branch] if(p0) {{ // CF{index} -> CF{target}')
            ends.append(target)
        else:
            screen.require(op in (0, 12), 'Unqualified dual skin control opcode')
    screen.require(not ends, 'Unclosed dual skin branch')
    return lines


def shader_source(image):
    rows = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    common = common.replace('float4 vc[30]', 'float4 vc[256]').replace('float4 pc[50]', 'float4 pc[64]')
    lines = ['// Opaque dual skin VS8201E6DC / PS82020B9C. Offline static transcription.', common,
             'Texture2D<float4> skinBase : register(t1);', 'SamplerState skinBaseSampler : register(s1);',
             'struct SkinDualInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 weights:TEXCOORD2; float4 indices:TEXCOORD3; float4 color:TEXCOORD4;',
             '    float2 uv:TEXCOORD5; float3 morph1:TEXCOORD6; float3 morph2:TEXCOORD7;',
             '    float3 morph3:TEXCOORD8; float3 morph4:TEXCOORD9; float3 morph5:TEXCOORD10;',
             '    float3 morph6:TEXCOORD11; float2 uv1:TEXCOORD12; };',
             'struct SkinDualOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float2 uv1:TEXCOORD1;',
             '    float3 normal:TEXCOORD2; float4 world:TEXCOORD3; float4 shadow:TEXCOORD4;',
             '    float4 color:TEXCOORD5; };',
             'SkinDualOutput VSSkinDual(SkinDualInput input) {',
             '    precise float4 r5=float4(input.position,0),r8=float4(input.normal,0);',
             '    precise float4 r6=float4(input.uv,input.uv1),r4=input.indices.wzyx,r7=input.weights,r1=input.color;',
             '    precise float4 r11=float4(input.morph1,0),r3=float4(input.morph2,0),r2=float4(input.morph3,0);',
             '    precise float4 r0=float4(0,input.morph4),r10=float4(input.morph5,0),r9=float4(input.morph6,0);',
             '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0;',
             '    precise float ps=0; bool p0=false; int a0=0;',
             '    const float4 k255=float4(0,1,0.5,3);']
    lines += emit_control(rows['VS'], 'VS')
    lines += ['    SkinDualOutput o; o.position=output62; o.uv=output0.xy; o.uv1=output1.xy;',
              '    o.normal=output2.xyz; o.world=output3; o.shadow=output4; o.color=output5; return o;', '}',
              'float4 PSSkinDual(SkinDualOutput input):SV_Target0 {']
    for i in range(5):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (251+i, ','.join('0x%08Xu' % v for v in PS_LITERALS[12+4*i:16+4*i])))
    lines += ['    precise float4 r0=float4(input.uv,0,0),r1=float4(input.uv1,0,0);',
              '    precise float4 r2=float4(input.normal,0),r3=input.world,r4=input.shadow,r5=input.color;',
              '    precise float4 r6=0,r7=0,r8=0,output0=0; precise float ps=0; bool p0=false;']
    lines += emit_control(rows['PS'], 'PS')
    lines += ['    return output0;', '}',
              '[maxvertexcount(1)] void GSSkinDualProbe(point SkinDualOutput input[1],inout PointStream<SkinDualOutput> s) { s.Append(input[0]); }',
              'cbuffer SkinDualProbeInputs : register(b2) { float4 skinDualProbe[6]; };',
              'SkinDualOutput VSSkinDualPixelProbe(uint id:SV_VertexID) {',
              '    SkinDualOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
              '    o.uv=skinDualProbe[0].xy; o.uv1=skinDualProbe[1].xy; o.normal=skinDualProbe[2].xyz;',
              '    o.world=skinDualProbe[3]; o.shadow=skinDualProbe[4]; o.color=skinDualProbe[5]; return o;', '}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    rows = inspect(image)
    source = shader_source(image)
    if args.emit_hlsl:
        args.emit_hlsl.write_text(source, encoding='utf-8')
    if args.output:
        args.output.write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
    print('PASS opaque dual skin: complete VS65/PS63 slots; native source emitted offline')


if __name__ == '__main__':
    main()
