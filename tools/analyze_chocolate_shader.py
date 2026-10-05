"""Pinned offline inventory and HLSL transcription of the chocolate alpha pair.

VS8205E0B8 / PS8205EED4 implement alpha-tested rigid-family geometry
(technique rigidalpha 0007FFFC/0007FFFE, context 0x3AD0): six vertex
fetches (position, normal, tangent, color, uv+uv1 split), a 3-row
transform, sin/cos detail math, three texture samples and
predicated/jump-structured alpha math. Four kCondJmp branches jump on the
predicate being false and one jumps unconditionally; the Boolean-constant
index is ignored for these predicated branches. Fetch-to-input
wiring is declaration-ordered (reach-game-278): the six fetches consume the
six declaration streams in order, so r3 carries the packed normal (no morph
stream exists) and r5 carries the packed tangent; the native input struct
matches buffer order (position/normal/color/uv/uv1/tangent), not fetch order.
This script pins headers, trailers, complete control flow
(including jump targets) and full issue-slot coverage, trial-emits every ALU
slot through the shared rigid emitter (with chocolate-specific sin/cos,
export and sample forms), and transcribes the full VS/PS pair to test-only
HLSL. Runtime admission, GPU arithmetic qualification and mesh integration
require separate proof; transcription alone does not enable rendering.
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

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x8205E0B8, 0x518, 0x2D8, 576, 5,
     '5e22faa7120f19bbc0f636dccf033aed69f9deb3234dcb19b8f7d777f4f37a86'),
    ('PS', 0x8205EED4, 0x688, 0x3B8, 720, 10,
     '4a2c832230f8d3b8f4636a247b003fe94cc7cbc72f6b8151e9946eb9ad3d1809'),
)
HEADERS = {
    'VS': (0x102A1101, 0x298, 0x280, 0x24, 0x78, 0x1F4, 0x21C, 0, 0),
    'PS': (0x102A1100, 0x378, 0x310, 0x24, 0x78, 0x318, 0x340, 0, 0),
}
TRAILERS = {
    'VS': '4e4a00018a47e85583a99812',
    'PS': '4e4a00008a47e85583a99812',
}
CF = {
    'VS': ((0xF5556005, 0x1203), (0, 0xC200), (0x600B, 0x1200), (0x6011, 0x1200),
           (0x2017, 0x1200), (0, 0xC400), (0x6019, 0x1200), (0x601F, 0x1200),
           (0x6025, 0x1200), (0x402B, 0x2200)),
    'PS': ((0x25500A, 0x1000), (0x4004, 0xB000), (0x300F, 0x1200), (0x2007, 0xB000),
           (0x2012, 0x1000), (0x4007, 0xB000), (0x2014, 0x1200), (0x5016, 0x1000),
           (0x400A, 0xB000), (0x101B, 0x1200), (0x401C, 0x1000), (0x400D, 0xB000),
           (0x1020, 0x1200), (0x42021, 0x1200), (0, 0xC400), (0x6023, 0x1200),
           (0x6029, 0x1200), (0x602F, 0x1200), (0x6035, 0x2200), (0, 0)),
}
# Pinned PS jumps: (control entry, target entry, unconditional, bool const,
# cond). Targets are EXEC block starts (control-list indices); all conditional
# jumps have is_predicated=1 and test p0==false (one is unconditional).
JUMPS = (
    (1, 4, False, 0, 0),
    (3, 7, True, 0, 0),
    (5, 7, False, 0, 0),
    (8, 10, False, 0, 0),
    (11, 13, False, 0, 0),
)
VS_FETCHES = {
    5: dict(kind='vertex_fetch', destination_register=1, destination_swizzle=[0, 1, 2, 5]),
    6: dict(kind='vertex_fetch', destination_register=3, destination_swizzle=[0, 1, 2, 7]),
    7: dict(kind='vertex_fetch', destination_register=5, destination_swizzle=[0, 1, 2, 7]),
    8: dict(kind='vertex_fetch', destination_register=4, destination_swizzle=[0, 1, 2, 3]),
    9: dict(kind='vertex_fetch', destination_register=2, destination_swizzle=[0, 1, 7, 7]),
    10: dict(kind='vertex_fetch', destination_register=2, destination_swizzle=[7, 7, 0, 1]),
}
PS_FETCHES = {
    # slot: (const, source_reg, dest_reg, dest_swizzle, source_components).
    10: (1, 0, 6, [0, 1, 2, 3], [2, 3, 2]),
    11: (0, 0, 7, [0, 1, 2, 3], [0, 1, 0]),
    34: (2, 0, 6, [0, 1, 2, 3], [0, 1, 0]),
}
# Pinned PS literal bank (k252..k255).
PS_LITERALS = (0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x3F800000, 0xBF800000, 0x00000000, 0x00000000,
               0x40000000, 0x3F000000, 0x3FC00000, 0x3DCCCCCD)
# Pinned VS literal bank (k252..k255): k254 carries trig scale/pi constants.
VS_LITERALS = (0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x3F000000, 0x00000000, 0x3E22F983, 0x40C90FDB,
               0xC0490FDB, 0x00000000, 0x00000000, 0x00000000)


def schedule(code, pairs):
    """Generic CF walk with predicated EXEC flags, jumps, allocs and NOPs."""
    screen.require(len(code) % 12 == 0 and 0 < pairs < len(code) // 12 - 1,
                   'Malformed chocolate instruction window')
    control, executed = [], []
    ended = False
    for pair in range(pairs):
        a, b, c = screen.words(code, pair * 12)
        for lo, hi in ((a, b & 65535), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)):
            op = hi >> 12
            screen.require(not ended or (lo == 0 and hi == 0), 'Non-NOP after chocolate EXEC_END')
            if op in (1, 2):
                address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
                screen.require(not lo & 0x8000 and hi & 0x0FFC in (0x000, 0x200),
                               'Unproved chocolate EXEC control flags')
                screen.require(0 < count <= 6 and sequence >> (2 * count) == 0, 'Malformed chocolate EXEC count/sequence')
                screen.require(pairs <= address < address + count <= len(code) // 12 - 1, 'Chocolate EXEC overlaps CF/trailer')
                for i in range(count):
                    flags = (sequence >> (i * 2)) & 3
                    executed.append((address + i, bool(flags & 1), bool(flags & 2)))
                ended = op == 2
            elif op == 11:
                address, uncond, pred = lo & 0x1FFF, bool((lo >> 13) & 1), bool((lo >> 14) & 1)
                screen.require(hi & 0x0FFC == 0 and (hi >> 2) & 0xFF == 0 and not (hi >> 10) & 1,
                               'Unproved chocolate JUMP condition (non-b0/false)')
                screen.require(not (hi >> 1) & 1 and not (hi >> 11) & 1, 'Unproved chocolate JUMP direction/mode')
            elif op == 12:
                screen.require(lo == 0 and hi in (0xC200, 0xC400), 'Unproved chocolate ALLOC shape')
            else:
                screen.require(op == 0 and lo == 0 and hi == 0, 'Unsupported chocolate control flow')
            control.append((lo, hi))
    slots = [e[0] for e in executed]
    screen.require(ended and sorted(slots) == list(range(pairs, len(code) // 12 - 1)) and len(set(slots)) == len(slots),
                   'Unaccounted or repeated chocolate scheduled slot')
    return control, executed


def scalar(row, stage='PS'):
    """Scalar emission with chocolate sin/cos forms (opcodes 48/49)."""
    f = row['fields']
    op = f['scalar_opcode']
    if op in (48, 49):
        # Unary transcendental on the third-source W/X selection shared by all
        # scalar unaries (mirrors rigid.scalar operand selection).
        t = dict(f)
        t['sources'] = [dict(x) for x in f['sources']]
        comp = t['sources'][2]['components']
        a = rigid.operand({**t, 'sources': [{**t['sources'][2], 'components': [comp[3]]},
                                            {**t['sources'][2], 'components': [comp[3]]},
                                            {**t['sources'][2], 'components': [comp[3]]}]}, 2, stage, 1)
        return ('sin' if op == 48 else 'cos') + '(' + a + ')'
    expression=rigid.scalar(row, stage)
    if op in (3,42):
        # MUL_PREV and split-constant MUL have the same original SM3
        # zero/denormal annihilation as ordinary scalar/vector MUL.
        screen.require(expression.count('*')==1,'Chocolate scalar product shape changed')
        a,b=expression.split('*')
        return 'rigidLegacyProduct('+a+','+b+')'
    return expression


def chocolate_sample(f):
    """Emit one chocolate texture fetch as an HLSL Sample with pinned contract."""
    slot = f['fetch_constant_index']
    screen.require(slot in (0, 1, 2), 'Unqualified chocolate sampler')
    screen.require(f['normalized_coordinates'] and f['dimension_field'] == 1 and
                   f['computed_lod'] and not f['register_lod'] and not f['register_gradients'],
                   'Unqualified chocolate texture instruction')
    screen.require(f['offset_fields'] == [0, 0, 0], 'Nonzero chocolate texture offset')
    screen.require([f[k] for k in ('mag_filter', 'min_filter', 'mip_filter')] == [3, 3, 3] and f['anisotropy'] == 7 and
                   f['fetch_valid_only'] and f['sample_location'] == 0 and f['lod_bias_field'] == 0,
                   'Unqualified chocolate sampler state contract')
    comps = f['source_components'][:2]
    screen.require(comps == [0, 1] or comps == [2, 3], 'Unqualified chocolate sample coordinates')
    source = 'r' + str(f['source_register']) + '.' + ''.join('xyzw'[c] for c in comps)
    lines = ['        precise float4 v=chocTex%d.Sample(chocSampler%d,%s);' % (slot, slot, source)]
    for lane, selector in enumerate(f['destination_swizzle']):
        if selector != 7:
            screen.require(selector < 4, 'Unqualified chocolate texture component literal')
            lines.append('        r' + str(f['destination_register']) + '.' + 'xyzw'[lane] + '=v.' + 'xyzw'[selector] + ';')
    return '\n'.join(lines)


def issue(slot, row, stage, predicated):
    """Emit one slot, wrapping predicated slots in the current predicate."""
    f = row['fields']
    if row['fetch']:
        screen.require(stage == 'PS', 'VS FETCHes are emitted in the reviewed input interface')
        text = '    { // slot%d\n%s\n    }' % (slot, chocolate_sample(f))
    elif slot == 15 and stage == 'PS':
        # Generic vector multiply plus generic scalar (unlike rigid PS slot15
        # legacy zero-product form, which this slot does not match).
        screen.require(row['raw'] == (0xB8870205, 0x001BC042, 0xC10106FE),
                       'Chocolate slot15 shape changed')
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        s = scalar(row, 'PS')
        mask = f['vector_mask']
        sw = ''.join('xyzw'[lane] for lane in range(4) if mask & (1 << lane))
        sm = f['scalar_mask']
        ssw = ''.join('xyzw'[lane] for lane in range(4) if sm & (1 << lane))
        text = ('    { // slot%d\n        precise float4 v=%s*%s;\n'
                '        r%d.%s=v.%s;\n        precise float s=%s;\n'
                '        r%d.%s=s.%s;\n        ps=s;\n    }') % (
                    slot, a, b, f['vector_destination'], sw, sw, s,
                    f['scalar_destination'], ssw, 'x' * len(ssw))
    elif stage == 'VS' and slot in (17, 18) and f['scalar_opcode'] in (48, 49):
        screen.require(f['vector_mask'] and not f['export'], 'Chocolate sin/cos vector shape changed')
        a, b = [rigid.operand(f, j, 'VS') for j in range(2)]
        op = f['vector_opcode']
        if op == 16:
            expr = 'dot(' + rigid.operand(f, 0, 'VS', 3) + ',' + rigid.operand(f, 1, 'VS', 3) + ').xxxx'
        elif op == 6:
            expr = 'float4(' + a + '>=' + b + ')'
        else:
            raise ValueError('Unqualified chocolate sin/cos vector opcode ' + str(op))
        s = scalar(row, 'VS')
        mask = f['vector_mask']
        sw = ''.join('xyzw'[lane] for lane in range(4) if mask & (1 << lane))
        sm = f['scalar_mask']
        ssw = ''.join('xyzw'[lane] for lane in range(4) if sm & (1 << lane))
        text = ('    { // slot%d\n        precise float4 v=%s;\n'
                '        precise float s=%s;\n        r%d.%s=v.%s;\n'
                '        r%d.%s=s.%s;\n        ps=s;\n    }') % (
                    slot, expr, s, f['vector_destination'], sw, sw,
                    f['scalar_destination'], ssw, 'x' * len(ssw))
    elif stage == 'VS' and slot == 41:
        # Vector multiply-export plus scalar max write (both observable).
        # Opcode22 is IEEE RSQ, not clamp-to-maximum opcode20. A zero
        # tangent-frame length therefore yields infinity at slot37. Preserve
        # the original SM3 zero/denormal annihilation at this normalization
        # multiply instead of exporting native IEEE 0*infinity NaNs.
        screen.require(row['raw'] == (0x14878004, 0x00BA6C1B, 0xE1030302),
                       'Chocolate slot41 shape changed')
        a, b = [rigid.operand(f, j, 'VS') for j in range(2)]
        s = scalar(row, 'VS')
        text = ('    { // slot%d\n        precise float4 v=rigidLegacyMultiply(%s,%s);\n'
                '        precise float s=%s;\n        output4.xyz=v.xyz;\n'
                '        output4.w=s;\n        ps=s;\n    }') % (slot, a, b, s)
    else:
        text = rigid.issue(slot, row, stage)
    if not row['fetch'] and f['vector_opcode']==1 and not (stage=='VS' and slot==41):
        # Keep every issued MUL in this exact pair under the original SM3
        # rule, including both earlier vertex and pixel normalization paths.
        a,b=[rigid.operand(f,j,stage) for j in range(2)]
        native='precise float4 v='+a+'*'+b+';'
        if f['vector_clamp']:native='precise float4 v=saturate('+a+'*'+b+');'
        legacy='rigidLegacyMultiply('+a+','+b+')'
        if f['vector_clamp']:legacy='saturate('+legacy+')'
        screen.require(text.count(native)==1,'Chocolate vector product shape changed')
        text=text.replace(native,'precise float4 v='+legacy+';')
    if not row['fetch'] and f['scalar_opcode'] in (3,42):
        native='precise float s='+rigid.scalar(row,stage)+';'
        legacy='precise float s='+scalar(row,stage)+';'
        screen.require(not f['scalar_clamp'] and text.count(native)==1,'Chocolate scalar product issue changed')
        text=text.replace(native,legacy)
    # Texture predication is encoded by word1.bit31; the three original
    # texture fetches have it clear. Fetch-valid-only is a different field.
    if row['fetch']:
        predicated=bool(row['raw'][1]&0x80000000)
    if predicated:
        inner = '\n'.join('        ' + line if line.strip() else line for line in text.split('\n')[1:-1])
        head, tail = text.split('\n', 1)[0], text.rsplit('\n', 1)[-1]
        return head + '\n    [flatten] if(p0) {\n' + inner + '\n    }\n' + tail
    return text


def emit_vs_control(rows):
    # Vertex fetches are wired in the reviewed input prologue, not emitted.
    return [issue(slot, rows[slot], 'VS', False) for slot in sorted(rows) if not rows[slot]['fetch']]


def emit_ps_control(rows):
    # All conditional jumps have is_predicated=1 and condition=0: jump on
    # !p0. The encoded Boolean index is ignored for these predicate jumps.
    # B0(10-14) -J0-> E4(18-19); B1(15-17) -J1(uncond)-> E7(22-26);
    # E4 -J2-> E7; B3(20-21) falls through; B4(22-26); J3 -skip 27-> E10(28-31);
    # B5(27); B6(28-31); J4 -skip 32-> E13(33-34); B7(32); then straight to END.
    lines = []
    for slot in range(10, 15):
        lines.append(issue(slot, rows[slot], 'PS', slot in (10, 11)))
    lines.append('[flatten] if(!p0) { // CF1 jumps to CF4.')
    for slot in range(18, 20):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('    [flatten] if(p0) { // CF5 falls through.')
    for slot in range(20, 22):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('    }')
    lines.append('} else {')
    for slot in range(15, 18):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('}')
    for slot in range(22, 27):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('[flatten] if(p0) { // CF8 falls through.')
    lines.append(issue(27, rows[27], 'PS', False))
    lines.append('}')
    for slot in range(28, 32):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('[flatten] if(p0) { // CF11 falls through.')
    lines.append(issue(32, rows[32], 'PS', False))
    lines.append('}')
    for slot in range(33, 59):
        lines.append(issue(slot, rows[slot], 'PS', slot == 34))
    return lines


def shader_source(image):
    """Transcribe chocolate VS+PS to test-only HLSL; GPU/runtime need separate proof."""
    inventory = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    # Chocolate samples its own t0-t2/s0-s2; drop the shared shadow declarations
    # (same registers, unused by either chocolate shader).
    for stale in ('Texture2D<float4> shadow0 : register(t0);',
                  'Texture2D<float4> shadow1 : register(t1);',
                  'SamplerState shadowSampler0 : register(s0);',
                  'SamplerState shadowSampler1 : register(s1);'):
        screen.require(stale in common, 'Shared prologue shadow declaration changed')
        common = common.replace(stale + '\n', '')
    lines = ['// Chocolate rigidalpha VS8205E0B8 / PS8205EED4. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common.replace('float4 vc[30]', 'float4 vc[64]').replace('float4 pc[50]', 'float4 pc[51]'),
             'Texture2D<float4> chocTex0 : register(t0);',
             'Texture2D<float4> chocTex1 : register(t1);',
             'Texture2D<float4> chocTex2 : register(t2);',
             'SamplerState chocSampler0 : register(s0);',
             'SamplerState chocSampler1 : register(s1);',
             'SamplerState chocSampler2 : register(s2);',
             'struct ChocInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float3 tangent:TEXCOORD5; float4 color:TEXCOORD2;',
             '    float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
             'struct ChocOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
             '    float4 t1:TEXCOORD1; float3 t2:TEXCOORD2; float4 t3:TEXCOORD3;',
             '    float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; };']
    lines += ['ChocOutput VSChocolate(ChocInput input) {',
              '// Fetch mapping from the captured six-element declaration:',
              '// r1=position, r3=normal, r5=tangent,',
              '// r4=color, r2.xy=uv, r2.zw=uv1.',
              '    precise float4 r1=float4(input.position,1),r3=float4(input.normal,0);',
              '    precise float4 r5=float4(input.tangent,0),r4=input.color;',
              '    precise float4 r2=float4(input.uv,input.uv1);',
              '    precise float4 r0=0,r6=0,r7=0;',
              '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0;',
              '    precise float ps=0;']
    vlits = struct.unpack_from('>16I', image, PROFILES[0][1] - screen.BASE + 0x298)
    screen.require(tuple(vlits) == VS_LITERALS, 'Chocolate VS literal bank changed')
    for i in range(4):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (252 + i, ','.join('0x%08Xu' % v for v in vlits[i * 4:i * 4 + 4])))
    lines += emit_vs_control(inventory['VS']['rows'])
    lines += ['    ChocOutput result; result.position=output62;',
              '    result.t0=output0; result.t1=output1; result.t2=output2.xyz;',
              '    result.t3=output3; result.t4=output4; result.t5=output5; return result;', '}']
    lines += ['float4 PSChocolate(ChocOutput input):SV_Target0 {']
    plits = struct.unpack_from('>16I', image, PROFILES[1][1] - screen.BASE + 0x378)
    screen.require(tuple(plits) == PS_LITERALS, 'Chocolate PS literal bank changed')
    for i in range(4):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (252 + i, ','.join('0x%08Xu' % v for v in plits[i * 4:i * 4 + 4])))
    lines += ['    precise float4 r0=input.t0,r1=input.t1,r3=input.t3,r5=input.t5,r6=0,r7=0;',
              '    precise float4 r2=float4(input.t2,0),r4=input.t4;',
              '    precise float4 output0=0;',
              '    precise float ps=0; bool p0=true;']
    lines += emit_ps_control(inventory['PS']['rows'])
    lines += ['    return output0;', '}']
    return '\n'.join(lines) + '\n'


def inspect(image):
    """Validate pinned records and decode fields; does not qualify execution."""
    result = {}
    for stage, va, size, start, length, pairs, digest in PROFILES:
        record = image[va - screen.BASE:va - screen.BASE + size]
        screen.require(hashlib.sha256(record).hexdigest() == digest,
                       'Chocolate ' + stage + ' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage],
                       'Chocolate ' + stage + ' header changed')
        screen.require(record[-12:].hex() == TRAILERS[stage],
                       'Chocolate ' + stage + ' trailer changed')
        code = record[start:start + length]
        control, executed = schedule(code, pairs)
        screen.require(tuple(control) == tuple((lo, hi) for lo, hi in CF[stage]),
                       'Chocolate ' + stage + ' control flow changed')
        if stage == 'PS':
            seen = []
            for idx, (lo, hi) in enumerate(control):
                if hi >> 12 == 11:
                    seen.append((idx, lo & 0x1FFF, bool((lo >> 13) & 1), (hi >> 2) & 0xFF, bool((hi >> 10) & 1)))
            screen.require(tuple(seen) == JUMPS, 'Chocolate PS jump table changed')
            # Jump targets must land on EXEC block starts.
            exec_entries = {idx for idx, (lo, hi) in enumerate(control) if hi >> 12 in (1, 2)}
            for _, target, _, _, _ in JUMPS:
                screen.require(target in exec_entries, 'Chocolate jump target is not an EXEC block')
        rows = {}
        for slot, fetch, _select in executed:
            raw = screen.words(code, slot * 12)
            fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            if not fetch:
                # Predication is modeled (PS ifs on p0), not rejected; the rest
                # mirrors the shared modifier contract.
                for k in ('absolute_constants', 'vector_destination_relative', 'scalar_destination_relative_or_export_zero',
                          'constant_address_register_relative', 'constant_0_relative', 'constant_1_relative'):
                    screen.require(not fields[k], 'Unqualified chocolate ALU modifier ' + k)
                screen.require(all(not s['relative_temporary'] for s in fields['sources']), 'Unqualified relative temporary')
            rows[slot] = dict(fetch=fetch, raw=raw, fields=fields)
        if stage == 'VS':
            for slot, contract in VS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], 'Chocolate VS fetch slot changed')
                f = rows[slot]['fields']
                for key, value in contract.items():
                    screen.require(f.get(key) == value, 'Chocolate VS fetch contract changed')
                screen.require(f.get('source_register') == 0 and f.get('source_component') == 0,
                               'Chocolate VS fetch source changed')
        else:
            for slot, (const, source, destination, swizzle, components) in PS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], 'Chocolate PS fetch slot changed')
                f = rows[slot]['fields']
                screen.require(f['kind'] == 'texture_fetch' and f['fetch_constant_index'] == const and
                               f['source_register'] == source and f['destination_register'] == destination and
                               f['destination_swizzle'] == swizzle and f['source_components'] == components,
                               'Chocolate PS fetch contract changed')
            exports = [slot for slot, row in rows.items() if row['fields'].get('export')]
            screen.require(exports == [58], 'Chocolate PS export set changed')
        result[stage] = dict(address=hex(va), control=[(hex(lo), hex(hi)) for lo, hi in control], rows=rows,
                             header=struct.unpack_from('>9I', record),
                             trailer=record[-12:].hex())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    if args.emit_hlsl:
        args.emit_hlsl.write_text(shader_source(image), encoding='utf-8')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2, default=str) + '\n', encoding='utf-8')
    summary = {}
    for stage in ('VS', 'PS'):
        rows = result[stage]['rows']
        fetches = {slot: row['fields'] for slot, row in rows.items() if row['fetch']}
        emittable, failures = [], []
        for slot, row in rows.items():
            try:
                if stage == 'PS':
                    issue(slot, row, stage, slot in (10, 11, 34))
                elif row['fetch']:
                    continue
                else:
                    issue(slot, row, stage, False)
                emittable.append(slot)
            except Exception as error:
                failures.append((slot, str(error)[:110]))
        summary[stage] = dict(address=result[stage]['address'], slots=len(rows),
                              fetches={slot: dict(constant=f.get('fetch_constant_index'),
                                                  destination=f.get('destination_register'),
                                                  kind=f.get('kind')) for slot, f in fetches.items()},
                              emitted=len(emittable), failures=failures)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
