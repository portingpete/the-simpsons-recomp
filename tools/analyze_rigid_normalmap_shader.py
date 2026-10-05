"""Pinned offline inventory and HLSL transcription of the opaque normalmap pair.

Record identities come from the catalog's opaque pass metadata; this script
additionally pins headers, trailers, complete control flow and full issue-slot
coverage, trial-emits every ALU slot through the shared rigid emitter, and
transcribes the full VS/PS pair to test-only HLSL. Runtime admission, GPU
arithmetic qualification and tangent-bearing mesh integration require separate
proof; transcription alone does not enable rendering.
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
    ('VS', 0x8205855C, 1160, 572, 588, 5,
     '786c941279b7a70a9cb70475511e604a2610a692a8aa5f39da2a1b1d81202103'),
    ('PS', 0x82058CBC, 3004, 1336, 1668, 16,
     '6b15ab79e672c141cb07cc98cf6d575d069e1d8afd6b01988199ce335efe1d9c'),
)
HEADERS = {
    'VS': (0x102A1101, 0x23C, 0x24C, 0x24, 0x80, 0, 0x1A4, 0, 0),
    'PS': (0x102A1100, 0x4B8, 0x704, 0x24, 0x80, 0x450, 0x478, 0, 0),
}
TRAILERS = {
    'VS': '4e4a00036aa0b96f8005367d',
    'PS': '4e4a00026aa0b96f8005367d',
}


# Full CF tables, independent of the record digest gate. PS has forward jumps
# and instruction predication; ALU acceptance does not qualify their execution.
CF = {
    'VS': ((4116013061,4611),(0,49664),(16395,4608),(0,50176),(24591,4608),
           (24597,4608),(24603,4608),(24609,4608),(24615,4608),(12333,8704)),
    'PS': ((614416,4096),(16387,45056),(4118,4608),(16407,4096),(16390,45056),
           (4123,4608),(614428,4608),(24610,4608),(24616,4608),(24622,4096),
           (24628,4096),(24634,4096),(24640,4608),(24646,4608),(20556,4096),
           (16414,45056),(16465,4096),(16406,45056),(89481301,4608),(9789531,4608),
           (24673,4608),(8295,4608),(24681,4608),(4207,4096),(16413,45056),
           (89481328,4608),(9789558,4608),(24700,4608),(8322,4608),(8324,4608),
           (0,50176),(16518,8704)),
}


# Pinned export maps: output register -> ((slot, vector_mask), ...). VS drives
# eight interpolators (0..7) plus clip position 62; PS exports only output0.
# Masks are per-component write enables; partial masks (7) leave lanes intact.
VS_EXPORTS = {62: ((11, 1), (12, 2), (13, 4), (14, 8)), 0: ((25, 15),),
              7: ((26, 15),), 4: ((27, 7),), 1: ((28, 15),),
              2: ((29, 1), (30, 2), (31, 4), (32, 8)),
              3: ((33, 1), (34, 2), (35, 4), (36, 8)),
              5: ((39, 7),), 6: ((47, 7),)}
PS_EXPORTS = {0: ((137, 15),)}
# (CF index, raw lo word, target CF index). Targets decode as lo & 0xFFF and
# every one is forward; each guarded clause holds shadow-tap fetch pairs, so
# this is the skeleton for the future structured-branch transcription. Target
# address units beyond CF-index order are NOT qualified here.
PS_JUMPS = ((1, 0x4003, 3), (4, 0x4006, 6), (15, 0x401E, 30),
            (17, 0x4016, 22), (24, 0x401D, 29))
# Whole-coissue instruction predication (p0 gating), distinct from CF jumps.
PS_PREDICATED = {52: True, 56: True, 59: True, 62: True}
# Scalar predicate writers (opcodes 28/29 update p0 for jumps and predicated
# issues). VS defines no predicates.
PS_PREDICATE_SOURCES = (21, 26, 51, 55, 58, 61, 80, 83, 111)


def exports(rows, stage):
    """Map exported output registers to exact (slot, mask) writers."""
    found = {}
    for slot, row in rows.items():
        if row['fetch']:
            continue
        fields = row['fields']
        if fields['export']:
            found.setdefault(fields['vector_destination'], []).append(
                (slot, fields['vector_mask']))
    return {key: tuple(value) for key, value in found.items()}


def schedule(control, stage):
    """Decode EXEC coverage, jump targets and terminators from raw CF words.

    Returns (entries, covered_slots). Each entry is a dict with index/op plus
    address/count (EXEC/EXEC_END), target/raw (JUMP) or nothing else (ALLOC).
    JUMP targets are CF indices, asserted forward; EXEC_END additionally
    terminates. ALLOC carries no payload here.
    """
    entries, covered = [], []
    for index, (lo, hi) in enumerate(control):
        op = hi >> 12
        if op in (1, 2):
            address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
            screen.require(0 < count <= 6 and sequence >> (2*count) == 0,
                           'Unqualified normalmap schedule')
            covered.extend(range(address, address+count))
            entries.append({'index': index, 'op': 'EXEC_END' if op == 2 else 'EXEC',
                            'address': address, 'count': count})
        elif op == 11:
            target = lo & 0xFFF
            screen.require(lo & ~0x4FFF == 0 and index < target < len(control),
                           'Unqualified normalmap jump')
            entries.append({'index': index, 'op': 'JUMP', 'target': target, 'raw': lo})
        elif op == 12:
            screen.require(lo == 0, 'Unqualified normalmap ALLOC')
            entries.append({'index': index, 'op': 'ALLOC'})
        else:
            raise ValueError('Unsupported normalmap schedule opcode')
    return entries, covered


# Semantic-association block: six entries at VS record offset 0x1CC plus a
# fetch-count word (6) at 0x1C0. Entry low byte = vertex-fetch instruction
# slot; bits[15:8] = attribute usage << 4 (0x00 position, 0x30 normal,
# 0x60 tangent, 0xA0 color, 0x50 texcoord). Decoded against the dualtextured
# record as Rosetta: identical entry shapes (including the 0x0021 high word
# marking the SECOND texcoord) appear there at the same 0x1CC offset.
SEMANTIC_COUNT_OFFSET = 0x1C0
SEMANTIC_OFFSET = 0x1CC
SEMANTIC_COUNT = 6
USAGE_NAMES = {0: 'POSITION', 3: 'NORMAL', 5: 'TEXCOORD', 6: 'TANGENT', 10: 'COLOR'}
# (fetch slot, usage, dest register, dest swizzle, entry high word).
# Two packed-XYZ direction feeds: normal -> r5, tangent -> r2. The texcoord
# pair shares usage5 across slots 9/10 into r4 with dual-consistent roles
# (first -> xy, second -> zw), i.e. r4 == float4(uv, uv1).
INPUT_MAPPING = ((5, 0, 1, (0, 1, 2, 5), 0x0010),
                 (6, 3, 5, (0, 1, 2, 7), 0x0000),
                 (7, 6, 2, (0, 1, 2, 7), 0x0000),
                 (8, 10, 3, (0, 1, 2, 3), 0x0000),
                 (9, 5, 4, (0, 1, 7, 7), 0x0000),
                 (10, 5, 4, (7, 7, 0, 1), 0x0021))


def input_mapping(image):
    """Join semantic entries with fetch destinations; resolves input roles."""
    info = inspect(image)
    record = image[PROFILES[0][1]-screen.BASE:PROFILES[0][1]-screen.BASE+PROFILES[0][2]]
    count = struct.unpack_from('>I', record, SEMANTIC_COUNT_OFFSET)[0]
    screen.require(count == SEMANTIC_COUNT, 'Normalmap semantic count changed')
    raw_entries = struct.unpack_from('>6I', record, SEMANTIC_OFFSET)
    fetches = {slot: row['fields'] for slot, row in info['VS']['rows'].items()
               if row['fetch']}
    screen.require(sorted(fetches) == [slot for slot, _, _, _, _ in INPUT_MAPPING],
                   'Normalmap fetch slots changed')
    mapping = []
    for slot, usage, dest, swizzle, high in INPUT_MAPPING:
        candidates = [word for word in raw_entries if (word & 0xFF) == slot]
        screen.require(len(candidates) == 1, 'Normalmap semantic entry changed')
        word = candidates[0]
        screen.require(((word >> 8) & 0xF0) == usage << 4 and (word >> 16) == high,
                       'Normalmap semantic entry changed')
        fetch = fetches[slot]
        screen.require(fetch['destination_register'] == dest
                       and tuple(fetch['destination_swizzle']) == swizzle,
                       'Normalmap fetch destination changed')
        mapping.append((slot, USAGE_NAMES[usage], dest))
    return tuple(mapping)


def vs_issue(slot, row):
    """Emit one VS ALU slot with legacy multiply; VS has no predication."""
    fields = row['fields']
    screen.require(not fields['predicated'], 'Unexpected predicated normalmap VS issue')
    text = rigid.issue(slot, row, 'VS')
    if fields['vector_mask'] and fields['vector_opcode'] == 1:
        operands = [rigid.operand(fields, index, 'VS') for index in range(2)]
        text = text.replace(operands[0]+'*'+operands[1],
                            'rigidLegacyMultiply('+operands[0]+','+operands[1]+')')
    return text


def vs_source(image):
    """Transcribe VS8205855C into a test-only HLSL oracle; no admission."""
    info = inspect(image)
    rows = info['VS']['rows']
    for slot, row in rows.items():
        if row['fetch']:
            continue
        for source in row['fields']['sources']:
            screen.require(not (source['bank'] == 'constant' and source['register'] >= 252),
                           'Unexpected normalmap VS literal')
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    lines = ['// Opaque normalmap VS8205855C. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common,
             'struct NormalmapVSInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4;',
             '    float3 tangent:TEXCOORD5; };',
             'struct NormalmapVSOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
             '    float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;',
             '    float3 t4:TEXCOORD4; float3 t5:TEXCOORD5; float3 t6:TEXCOORD6;',
             '    float4 t7:TEXCOORD7; };',
             'NormalmapVSOutput VSRigidNormalmap(NormalmapVSInput input) {',
             '// Fetch-issue mapping by semantic entry: r1 position, r5 normal,',
             '// r2 tangent, r3 color, r4 float4(uv, uv1). r0 is scratch.',
             '    precise float4 r1=float4(input.position,1),r5=float4(input.normal,0);',
             '    precise float4 r2=float4(input.tangent,0),r3=input.color;',
             '    precise float4 r4=float4(input.uv,input.uv1),r0=0;',
             '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;',
             '    precise float4 output4=0,output5=0,output6=0,output7=0;',
             '    precise float ps=0; bool p0=false;']
    entries, covered = schedule(CF['VS'], 'VS')
    screen.require(covered == list(range(5, 48)), 'Normalmap VS schedule changed')
    for entry in entries:
        if entry['op'] == 'ALLOC':
            continue
        for slot in range(entry['address'], entry['address']+entry['count']):
            if rows[slot]['fetch']:
                continue
            lines.append(vs_issue(slot, rows[slot]))
    lines += ['    NormalmapVSOutput result; result.position=output62;',
              '    result.t0=output0; result.t1=output1; result.t2=output2; result.t3=output3;',
              '    result.t4=output4.xyz; result.t5=output5.xyz; result.t6=output6.xyz;',
              '    result.t7=output7; return result;', '}']
    return '\n'.join(lines)+'\n'


# PS texture-fetch clauses. Both 9-tap shadow clauses reuse the standard tap
# offsets and destination channels; only the fetch constant, coordinate
# source and destination registers differ per clause.
PS_TAPS = ((0, 0, 1), (1, 0, 2), (1, -1, 2), (-1, -1, 0), (0, -1, 1),
           (-1, 0, 0), (1, 1, 2), (-1, 1, 0), (0, 1, 1))
PS_MATERIAL_FETCHES = ((16, 2, 8, (0, 2, 1, 3)), (28, 3, 10, (0, 1, 2, 3)))
PS_SHADOW_CLAUSES = ((1, (85, 86, 87, 88, 89, 90, 91, 92, 93), 0, (1, 3, 3)),
                     (0, (112, 113, 114, 115, 116, 117, 118, 119, 120), 2, (0, 1, 1)))


def ps_fetches(rows):
    """Pin material samples and both shadow-tap clauses with filter profile."""
    material, shadow = [], []
    for slot, constant, destination, swizzle in PS_MATERIAL_FETCHES:
        fields = rows[slot]['fields']
        screen.require(rows[slot]['fetch']
                       and fields['fetch_constant_index'] == constant
                       and fields['destination_register'] == destination
                       and tuple(fields['destination_swizzle']) == swizzle
                       and fields['source_register'] == 0
                       and fields['source_components'] == [0, 1, 0],
                       'Normalmap material fetch changed')
        material.append((slot, constant, destination))
    for constant, slots, source, components in PS_SHADOW_CLAUSES:
        taps = []
        for position, slot in enumerate(slots):
            fields = rows[slot]['fields']
            x, y, channel = PS_TAPS[position]
            screen.require(rows[slot]['fetch']
                           and fields['fetch_constant_index'] == constant
                           and fields['source_register'] == source
                           and fields['source_components'] == list(components)
                           and fields['offset_fields'] == [(x*2) & 31, (y*2) & 31, 0]
                           and [v for v in fields['destination_swizzle'] if v != 7] == [channel],
                           'Normalmap shadow tap changed')
            taps.append((slot, fields['destination_register'], channel))
        shadow.append((constant, tuple(taps)))
    for slot, row in rows.items():
        if not row['fetch']:
            continue
        fields = row['fields']
        screen.require(fields['normalized_coordinates'] and fields['dimension_field'] == 1
                       and (fields['mag_filter'], fields['min_filter'], fields['mip_filter']) == (3, 3, 3)
                       and fields['anisotropy'] == 7 and fields['arbitrary_filter'] == 0
                       and (fields['volume_mag_filter'], fields['volume_min_filter']) == (3, 3)
                       and fields['computed_lod'] and not fields['register_lod']
                       and not fields['register_gradients'] and fields['fetch_valid_only']
                       and fields['sample_location'] == 0 and fields['lod_bias_field'] == 0,
                       'Normalmap fetch filter profile changed')
    return {'material': tuple(material), 'shadow': tuple(shadow)}


# PS input registers: temporaries read before any ALU/fetch write, with the
# slot of first use. r0 carries the uv pair (sampled at slots 16/28); the
# remaining roles are resolved by characteristic use during transcription.
PS_INPUT_FIRST_USE = ((0, 24), (1, 18), (2, 81), (3, 106), (4, 17), (6, 33))


def ps_inputs(rows):
    """Map PS input registers to their first-use slots in issue order."""
    written, first = set(), {}
    for slot in sorted(rows):
        row = rows[slot]
        if row['fetch']:
            written.add(row['fields']['destination_register'])
            continue
        for source in row['fields']['sources']:
            if source['bank'] == 'temporary' and source['register'] not in written \
                    and source['register'] not in first:
                first[source['register']] = slot
        if row['fields']['vector_mask']:
            written.add(row['fields']['vector_destination'])
    return tuple(sorted(first.items()))


def predication(rows):
    """Instruction-predicated ALU slots mapped to their p0 condition sense."""
    return {slot: row['fields']['predicate_condition'] for slot, row in rows.items()
            if not row['fetch'] and row['fields']['predicated']}


def predicate_sources(rows):
    """Slots whose scalar opcodes (28/29) write the p0 predicate."""
    return tuple(sorted(slot for slot, row in rows.items()
                        if not row['fetch'] and row['fields']['scalar_opcode'] in (28, 29)))


# Pinned PS literal bank k251..k255 (5 float4, 80 bytes immediately before code).
# Record prefix 1208..1336 is 8 float4; first three are zero, last five are these.
PS_LITERALS = (0x3F333333, 0x3F666666, 0x3F75C28F, 0x3E99999A,
               0x40000000, 0x3DCCCCCD, 0x3E000000, 0x44800000,
               0x3F000000, 0xBF000000, 0x3A002008, 0x3A802008,
               0x42000000, 0x42800000, 0x3E4CCCCD, 0x3E800000,
               0x3F000000, 0x42000000, 0xBF800000, 0x3F800000)


def ps_issue(slot, row):
    """Emit one PS slot with material/shadow fetch specialization."""
    if row['fetch']:
        f = row['fields']
        const, dst, swz = f['fetch_constant_index'], f['destination_register'], f['destination_swizzle']
        src, comps, offs = f['source_register'], f['source_components'], f['offset_fields']
        comp_names = 'xyzw'
        uv = f"r{src}.{comp_names[comps[0]]}{comp_names[comps[1]]}"
        x, y, _ = offs
        ox, oy = ((x + 16) % 32 - 16) // 2, ((y + 16) % 32 - 16) // 2
        if const == 2:
            screen.require((slot, dst, tuple(swz)) in
                           [(s, d, w) for s, _, d, w in PS_MATERIAL_FETCHES],
                           'Normalmap base fetch changed')
            lines = [f'    {{ // slot{slot}',
                     f'        precise float4 value=rigidBase.Sample(rigidBaseSampler,{uv});']
            for lane, sel in enumerate(swz):
                if sel != 7:
                    screen.require(sel < 4, 'Unsupported normalmap fetch literal')
                    lines.append(f'        r{dst}.{"xyzw"[lane]}=value.{"xyzw"[sel]};')
            return '\n'.join(lines + ['    }'])
        if const == 3:
            screen.require((slot, dst, tuple(swz)) in
                           [(s, d, w) for s, _, d, w in PS_MATERIAL_FETCHES],
                           'Normalmap normal fetch changed')
            lines = [f'    {{ // slot{slot}',
                     f'        precise float4 value=rigidNormal.Sample(rigidNormalSampler,{uv});']
            for lane, sel in enumerate(swz):
                if sel != 7:
                    lines.append(f'        r{dst}.{"xyzw"[lane]}=value.{"xyzw"[sel]};')
            return '\n'.join(lines + ['    }'])
        screen.require(const in (0, 1), 'Unqualified normalmap sampler')
        tex, samp = f'shadow{const}', f'shadowSampler{const}'
        lines = [f'    {{ // slot{slot}',
                 f'        precise float4 v=rigidShadowSample({tex}.Sample({samp},{uv},int2({ox},{oy})));']
        for lane, sel in enumerate(swz):
            if sel != 7:
                screen.require(sel < 4, 'Unqualified normalmap tap literal')
                lines.append(f'        r{dst}.{"xyzw"[lane]}=v.{"xyzw"[sel]};')
        return '\n'.join(lines + ['    }'])
    text = rigid.issue(slot, row, 'PS').replace('pc[251]', 'k251')
    f = row['fields']
    if f['vector_mask'] and f['vector_opcode'] == 1:
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        text = text.replace(a + '*' + b, 'rigidLegacyMultiply(' + a + ',' + b + ')')
    if f['predicated']:
        text = '    [branch] if(%s)\n%s' % ('p0' if f['predicate_condition'] else '!p0', text)
    return text


def emit_ps_control(rows):
    """Static forward control flow for the 5 predicate jumps; no interpreter."""
    lines, ends = [], []
    for index, (lo, hi) in enumerate(CF['PS']):
        while ends and ends[-1] == index:
            lines.append('    }')
            ends.pop()
        op = hi >> 12
        if op in (1, 2):
            addr, cnt = lo & 4095, (lo >> 12) & 7
            for slot in range(addr, addr + cnt):
                lines.append(ps_issue(slot, rows[slot]))
        elif op == 11:
            target = lo & 0xFFF
            screen.require(hi in (0x1000, 0xB000, 0x45056, 45056) or (lo >> 13) == 2,
                           'Unqualified normalmap predicate jump')
            screen.require(index < target < len(CF['PS']), 'Unqualified normalmap jump')
            lines.append(f'    [branch] if(p0) {{ // CF{index} -> CF{target}')
            ends.append(target)
        else:
            screen.require(op in (0, 12), 'Unsupported normalmap CF kind')
    screen.require(not ends, 'Unclosed normalmap predicate block')
    return lines


def shader_source(image):
    """Transcribe VS+PS to test-only HLSL; GPU/runtime need separate proof."""
    inventory = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    common = common.replace('pc[50]', 'pc[51]')
    lines = ['// Opaque normalmap VS8205855C / PS82058CBC. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common,
             'Texture2D<float4> rigidBase : register(t2);',
             'SamplerState rigidBaseSampler : register(s2);',
             'Texture2D<float4> rigidNormal : register(t3);',
             'SamplerState rigidNormalSampler : register(s3);',
             'struct NormalmapInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4;',
             '    float3 tangent:TEXCOORD5; };',
             'struct NormalmapOutput { float4 position:SV_Position; float4 t0:TEXCOORD0;',
             '    float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;',
             '    float3 t4:TEXCOORD4; float3 t5:TEXCOORD5; float3 t6:TEXCOORD6;',
             '    float4 t7:TEXCOORD7; };',
             'cbuffer NormalmapProbeInputs : register(b2) { float4 normalmapProbe[6]; };']
    lines += ['NormalmapOutput VSRigidNormalmap(NormalmapInput input) {',
              '// Fetch-issue mapping by semantic entry: r1 position, r5 normal,',
              '// r2 tangent, r3 color, r4 float4(uv, uv1). r0 is scratch.',
              '    precise float4 r1=float4(input.position,1),r5=float4(input.normal,0);',
              '    precise float4 r2=float4(input.tangent,0),r3=input.color;',
              '    precise float4 r4=float4(input.uv,input.uv1),r0=0;',
              '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;',
              '    precise float4 output4=0,output5=0,output6=0,output7=0;',
              '    precise float ps=0; bool p0=false;']
    # Re-emit VS slots via shared emitter to keep this file the single owner.
    vs_rows = inventory['VS']['rows']
    entries, _ = schedule(CF['VS'], 'VS')
    for entry in entries:
        if entry['op'] == 'ALLOC':
            continue
        for slot in range(entry['address'], entry['address'] + entry['count']):
            if vs_rows[slot]['fetch']:
                continue
            lines.append(vs_issue(slot, vs_rows[slot]))
    lines += ['    NormalmapOutput result; result.position=output62;',
              '    result.t0=output0; result.t1=output1; result.t2=output2; result.t3=output3;',
              '    result.t4=output4.xyz; result.t5=output5.xyz; result.t6=output6.xyz;',
              '    result.t7=output7; return result;', '}']
    lines += ['float4 PSRigidNormalmap(NormalmapOutput input):SV_Target0 {']
    lits = struct.unpack_from('>20I', image, PROFILES[1][1] - screen.BASE + 1256)
    screen.require(tuple(lits) == PS_LITERALS, 'Normalmap PS literal bank changed')
    for i in range(5):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (251 + i, ','.join('0x%08Xu' % v for v in lits[i * 4:i * 4 + 4])))
    # Direct interpolator mapping t0->r0, t1->r1, t2->r2, t3->r3, t4->r4, t5->r5,
    # t6->r6. t7 (color) is computed by VS but not read by PS; r7+ are scratch.
    # This mapping is verified by the GPU pixel oracle (uv sampling at 16/28,
    # world/eye at 18, shadow coords at 85/112); any swap fails sampling tests.
    lines += ['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=input.t3;',
              '    precise float4 r4=float4(input.t4,0),r5=float4(input.t5,0),r6=float4(input.t6,0),r7=0;',
              '    precise float4 r8=0,r9=0,r10=0,r11=0,r12=0,output0=0;',
              '    precise float ps=0; bool p0=false;']
    lines += emit_ps_control(inventory['PS']['rows'])
    lines += ['    return output0;', '}',
              '[maxvertexcount(1)] void GSRigidNormalmapProbe(point NormalmapOutput input[1],inout PointStream<NormalmapOutput> s) { s.Append(input[0]); }',
              'NormalmapOutput VSRigidNormalmapPixelProbe(uint id:SV_VertexID) {',
              '    NormalmapOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
              '    o.t0=normalmapProbe[0]; o.t1=normalmapProbe[1]; o.t2=normalmapProbe[2];',
              '    o.t3=normalmapProbe[3]; o.t4=normalmapProbe[4].xyz; o.t5=normalmapProbe[5].xyz;',
              '    o.t6=float3(0,0,1); o.t7=float4(0,0,0,1); return o; }', '']
    return '\n'.join(lines) + '\n'


def inspect(image):
    """Validate pinned records and decode fields; does not qualify execution."""
    result = {}
    for stage, va, size, start, length, pairs, digest in PROFILES:
        record = image[va-screen.BASE:va-screen.BASE+size]
        screen.require(hashlib.sha256(record).hexdigest() == digest,
                       'Normalmap '+stage+' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage],
                       'Normalmap '+stage+' header changed')
        screen.require(record[-12:].hex() == TRAILERS[stage],
                       'Normalmap '+stage+' trailer changed')
        code = record[start:start+length]
        control = []
        for pair in range(pairs):
            a, b, c = screen.words(code, pair*12)
            control.extend(((a, b & 65535), ((b >> 16 | c << 16) & 0xFFFFFFFF, c >> 16)))
        screen.require(tuple(control) == CF[stage], 'Normalmap '+stage+' control flow changed')
        rows = {}
        for cf_index, (lo, hi) in enumerate(control):
            if hi >> 12 not in (1, 2, 5):
                screen.require(hi >> 12 in (0, 3, 4, 6, 7, 8, 9, 11, 12),
                               'Unsupported normalmap control opcode')
                continue
            address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
            screen.require(0 < count <= 6 and sequence >> (2*count) == 0,
                           'Unqualified normalmap issue sequence')
            for index in range(count):
                slot = address+index
                screen.require(slot not in rows, 'Repeated normalmap instruction slot')
                fetch = bool(sequence & (1 << (2*index)))
                raw = screen.words(code, slot*12)
                rows[slot] = dict(fetch=fetch, raw=raw, control_index=cf_index,
                                  conditional_exec=hi >> 12 == 5,
                                  fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
        screen.require(sorted(rows) == list(range(pairs, length//12-1)),
                       'Incomplete normalmap issue coverage')
        result[stage] = dict(address=hex(va), control=control, rows=rows,
                             header=struct.unpack_from('>9I', record),
                             trailer=record[-12:].hex())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--emit-vs-hlsl', type=Path)
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    if args.emit_vs_hlsl:
        args.emit_vs_hlsl.write_text(vs_source(image), encoding='utf-8')
    if args.emit_hlsl:
        args.emit_hlsl.write_text(shader_source(image), encoding='utf-8')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2, default=str)+'\n', encoding='utf-8')
    summary = {}
    for stage in ('VS', 'PS'):
        rows = result[stage]['rows']
        fetches = {slot: row['fields'] for slot, row in rows.items() if row['fetch']}
        emittable, failures = [], []
        for slot, row in rows.items():
            if row['fetch']:
                continue
            try:
                text = rigid.issue(slot, row, stage)
                emittable.append(slot)
            except Exception as error:
                failures.append((slot, str(error)[:110]))
        summary[stage] = dict(address=result[stage]['address'], slots=len(rows),
                              fetches={slot: dict(constant=f['fetch_constant_index'],
                                                  destination=f['destination_register'],
                                                  kind=f['kind']) for slot, f in fetches.items()},
                              emitted=len(emittable), failures=failures)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
