"""Pinned offline inventory and HLSL transcription of the 168F8 alpha pair.

VS8201739C / PS82017E4C implement the textured-rigid alpha pass selected by
effect 0x820168F8 (identity 00500022, technique rigidalpha 0007FFFC/0007FFFE,
context 0x2AB0, 6 begin-time samplers): four vertex fetches (position, aux,
color, uv), per-lane position/export math (vop15/vop2/vop16, no scalar work),
one conditional p0==false jump, a single unpredicated stage-0 texture sample and
export-pair color math (vop11). No sin/cos, no loops, no relative addressing.
The r0 fetch wiring (swizzle [7,0,1,2] read as (0,x,y,z) per the shared 5/7
convention) is a functional hypothesis re-verified against the mesh
declaration at mesh integration. This script pins headers, trailers, complete
control flow and full issue-slot coverage, trial-emits every ALU slot through
the shared rigid emitter (with 168F8-specific sample/export forms), and
transcribes the full VS/PS pair to test-only HLSL. Runtime admission, GPU
arithmetic qualification and mesh integration require separate proof;
transcription alone does not enable rendering.
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
    ('VS', 0x8201739C, 0x2AC, 0x1B0, 0xFC, 3,
     'a515ca5181b23d4fbdeb0a1229ec7b8526ab4460a1c1dd6261e30c095da072f2'),
    ('PS', 0x82017E4C, 0x350, 0x254, 0xFC, 3,
     '3db361d5e390f253e7ea69968ebd7a042d9f159a950f268305521fb79af8f2f6'),
)
HEADERS = {
    'VS': (0x102A1101, 0x1B0, 0xFC, 0x24, 0x7C, 0, 0x144, 0, 0),
    'PS': (0x102A1100, 0x214, 0x13C, 0x24, 0x7C, 0x1BC, 0x1E4, 0, 0),
}
TRAILERS = {
    'VS': '4e4a00016d3a27e51935b5ee',
    'PS': '4e4a00006d3a27e51935b5ee',
}
CF = {
    'VS': ((0xF0554003, 0x1200), (0, 0xC200), (0x4007, 0x1200), (0, 0xC400),
           (0x600B, 0x1200), (0x3011, 0x2200)),
    'PS': ((0x243003, 0x1000), (0x4003, 0xB000), (0x2006, 0x1200), (0, 0xC400),
           (0x6008, 0x1200), (0x600E, 0x2200)),
}
# Pinned PS jumps: (control entry, target entry, unconditional, predicated,
# bool address, condition). With predicated=1, the bool address is unused.
JUMPS = (
    (1, 3, False, True, 0, False),
)
VS_FETCHES = {
    3: dict(kind='vertex_fetch', destination_register=1, destination_swizzle=[0, 1, 2, 5]),
    4: dict(kind='vertex_fetch', destination_register=0, destination_swizzle=[7, 0, 1, 2]),
    5: dict(kind='vertex_fetch', destination_register=2, destination_swizzle=[0, 1, 2, 3]),
    6: dict(kind='vertex_fetch', destination_register=3, destination_swizzle=[0, 1, 7, 7]),
}
PS_FETCHES = {
    # slot: (const, source_reg, dest_reg, dest_swizzle, source_components).
    4: (0, 0, 4, [3, 0, 1, 2], [0, 1, 0]),
}
# Pinned PS literal bank (k252..k255) at record+0x214; k255=(1,0,1e-4,0).
PS_LITERALS = (0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x3F800000, 0x00000000, 0x38D1B717, 0x00000000)


def schedule(code, pairs):
    """CF walk with EXEC serialize flags, jumps, allocs and NOPs."""
    screen.require(len(code) % 12 == 0 and 0 < pairs < len(code) // 12 - 1,
                   'Malformed 168F8 instruction window')
    control, executed = [], []
    ended = False
    for pair in range(pairs):
        a, b, c = screen.words(code, pair * 12)
        for lo, hi in ((a, b & 65535), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)):
            op = hi >> 12
            screen.require(not ended or (lo == 0 and hi == 0), 'Non-NOP after 168F8 EXEC_END')
            if op in (1, 2):
                address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
                screen.require(not lo & 0x8000 and hi & 0x0FFC in (0x000, 0x200),
                               'Unproved 168F8 EXEC control flags')
                screen.require(0 < count <= 6 and sequence >> (2 * count) == 0, 'Malformed 168F8 EXEC count/sequence')
                screen.require(pairs <= address < address + count <= len(code) // 12 - 1, '168F8 EXEC overlaps CF/trailer')
                for i in range(count):
                    flags = (sequence >> (i * 2)) & 3
                    executed.append((address + i, bool(flags & 1), bool(flags & 2)))
                ended = op == 2
            elif op == 11:
                address, uncond, pred = lo & 0x1FFF, bool((lo >> 13) & 1), bool((lo >> 14) & 1)
                screen.require(hi & 0x0FFC == 0 and (hi >> 2) & 0xFF == 0 and not (hi >> 10) & 1,
                               'Unproved 168F8 JUMP condition/address bits')
                screen.require(not (hi >> 1) & 1 and not (hi >> 11) & 1, 'Unproved 168F8 JUMP direction/mode')
            elif op == 12:
                screen.require(lo == 0 and hi in (0xC200, 0xC400), 'Unproved 168F8 ALLOC shape')
            else:
                screen.require(op == 0 and lo == 0 and hi == 0, 'Unsupported 168F8 control flow')
            control.append((lo, hi))
    slots = [e[0] for e in executed]
    screen.require(ended and sorted(slots) == list(range(pairs, len(code) // 12 - 1)) and len(set(slots)) == len(slots),
                   'Unaccounted or repeated 168F8 scheduled slot')
    return control, executed


def a168_sample(f):
    """Emit the single stage-0 texture fetch as an HLSL Sample with pinned contract."""
    slot = f['fetch_constant_index']
    screen.require(slot == 0, 'Unqualified 168F8 sampler')
    screen.require(f['normalized_coordinates'] and f['dimension_field'] == 1 and
                   f['computed_lod'] and not f['register_lod'] and not f['register_gradients'],
                   'Unqualified 168F8 texture instruction')
    screen.require(f['offset_fields'] == [0, 0, 0], 'Nonzero 168F8 texture offset')
    screen.require([f[k] for k in ('mag_filter', 'min_filter', 'mip_filter')] == [3, 3, 3] and f['anisotropy'] == 7 and
                   f['fetch_valid_only'] and f['sample_location'] == 0 and f['lod_bias_field'] == 0,
                   'Unqualified 168F8 sampler state contract')
    comps = f['source_components'][:2]
    screen.require(comps == [0, 1], 'Unqualified 168F8 sample coordinates')
    source = 'r' + str(f['source_register']) + '.' + ''.join('xyzw'[c] for c in comps)
    lines = ['        precise float4 v=aTex0.Sample(aSampler0,%s);' % source]
    for lane, selector in enumerate(f['destination_swizzle']):
        if selector != 7:
            screen.require(selector < 4, 'Unqualified 168F8 texture component literal')
            lines.append('        r' + str(f['destination_register']) + '.' + 'xyzw'[lane] + '=v.' + 'xyzw'[selector] + ';')
    return '\n'.join(lines)


def a168_scalar(row, stage='PS'):
    """Split scalar src3 negation applies to BOTH constant and temp operands.

    Slot 6 is (-k255.z)-(-r4.x), i.e. sampled alpha minus the cutoff.
    The separate vector operands do not feed this scalar expression.
    """
    f = row['fields']
    op = f['scalar_opcode']
    s = f['sources'][2]
    sw = (row['raw'][1] & 255)
    screen.require(op in (42, 43, 44, 46, 47) and s['negated'] and s['bank'] == 'constant' and
                   not f['absolute_constants'] and
                   not any(f[k] for k in ('constant_address_register_relative', 'constant_0_relative',
                                          'constant_1_relative')),
                   'Unqualified 168F8 negated-split scalar shape')
    reg = (op & 1) | (((row['raw'][2] >> 29) & 1) << 1) | (sw & 0x3C)
    constant = row['raw'][2] & 255
    a = ('-' + ('k' + str(constant) if constant >= 252 else ('vc' if stage == 'VS' else 'pc') +
                '[' + str(constant) + ']') + '.' + 'xyzw'[((sw >> 6) + 3) & 3])
    b = '(-r' + str(reg) + '.' + 'xyzw'[sw & 3] + ')'
    expressions = {42: lambda: a + '*' + b, 43: lambda: 'rigidLegacyProduct(' + a + ',' + b + ')',
                   44: lambda: a + '+' + b, 46: lambda: a + '-' + b, 47: lambda: a + '-' + b}
    screen.require(op in expressions, 'Unqualified 168F8 negated-split opcode ' + str(op))
    return expressions[op]()


def a168_split_issue(slot, row, stage):
    """Mirror of the shared issue tail for negated-split scalars (mask must be 0)."""
    f = row['fields']
    screen.require(f['vector_mask'] == 0, '168F8 negated-split vector write needs its own form')
    screen.require(f['scalar_opcode'] not in (28, 29), '168F8 negated-split predicate write needs its own form')
    out = ['    { // slot' + str(slot)]
    expr = a168_scalar(row, stage)
    out += ['        precise float s = ' + ('saturate(' + expr + ')' if f['scalar_clamp'] else expr) + ';']
    sm = f['scalar_mask']
    screen.require(sm and not f['export'] and f['scalar_opcode'] != 50, 'Unqualified 168F8 scalar write/export')
    sw = ''.join('xyzw'[lane] for lane in range(4) if sm & (1 << lane))
    out += ['        r' + str(f['scalar_destination']) + '.' + sw + '=s.' + 'x' * len(sw) + ';']
    out += ['        ps=s;']
    return '\n'.join(out + ['    }'])


def issue(slot, row, stage, predicated):
    """Emit one slot, wrapping predicated slots in the current predicate."""
    f = row['fields']
    if row['fetch']:
        screen.require(stage == 'PS', 'VS FETCHes are emitted in the reviewed input interface')
        text = '    { // slot%d\n%s\n    }' % (slot, a168_sample(f))
    elif (stage == 'PS' and f['scalar_opcode'] in (42, 43, 44, 46, 47) and
          f['sources'][2]['negated'] and f['sources'][2]['bank'] == 'constant'):
        text = a168_split_issue(slot, row, stage)
    else:
        text = rigid.issue(slot, row, stage)
    if stage == 'PS' and not row['fetch']:
        if slot == 7:
            screen.require(f['vector_opcode'] == 25 and f['vector_mask'] == 0 and
                           f['scalar_opcode'] == 50 and not f['scalar_mask'],
                           '168F8 alpha kill instruction changed')
            a, b = (rigid.operand(f, i, stage) for i in (0, 1))
            # KILLGT has a side effect even with no destination lanes enabled.
            text = '    { // slot7\n        if(any(' + a + '>' + b + ')) discard;\n    }'
        elif slot in (11, 12):
            a, b = (rigid.operand(f, i, stage) for i in (0, 1))
            before = 'precise float4 v=' + a + '*' + b + ';'
            screen.require(before in text, '168F8 normalization multiplication changed')
            text = text.replace(before, 'precise float4 v=rigidLegacyMultiply(' + a + ',' + b + ');')
    if predicated:
        inner = '\n'.join('        ' + line if line.strip() else line for line in text.split('\n')[1:-1])
        head, tail = text.split('\n', 1)[0], text.rsplit('\n', 1)[-1]
        return head + '\n    [flatten] if(p0) {\n' + inner + '\n    }\n' + tail
    return text


def emit_vs_control(rows):
    # Vertex fetches are wired in the reviewed input prologue, not emitted.
    # Straight line, no jumps or predication (all flags clear).
    return [issue(slot, rows[slot], 'VS', False) for slot in range(7, 20) if not rows[slot]['fetch']]


def emit_ps_control(rows):
    # Fetch 4 is unpredicated. Its EXEC serialize bit is not predication.
    # PRED_SETNE(c48.x) controls the jump: !p0 skips the alpha-test block.
    lines = [issue(3, rows[3], 'PS', False), issue(4, rows[4], 'PS', False),
             issue(5, rows[5], 'PS', False)]
    lines.append('[flatten] if(p0) {')
    for slot in range(6, 8):
        lines.append(issue(slot, rows[slot], 'PS', False))
    lines.append('}')
    for slot in range(8, 20):
        lines.append(issue(slot, rows[slot], 'PS', False))
    return lines


def shader_source(image):
    """Transcribe 168F8 VS+PS to test-only HLSL; GPU/runtime need separate proof."""
    inventory = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    # 168F8 samples its own t0/s0; drop the shared shadow declarations
    # (same registers, unused by either 168F8 shader).
    for stale in ('Texture2D<float4> shadow0 : register(t0);',
                  'Texture2D<float4> shadow1 : register(t1);',
                  'SamplerState shadowSampler0 : register(s0);',
                  'SamplerState shadowSampler1 : register(s1);'):
        screen.require(stale in common, 'Shared prologue shadow declaration changed')
        common = common.replace(stale + '\n', '')
    lines = ['// 168F8 rigidalpha VS8201739C / PS82017E4C. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common.replace('float4 vc[30]', 'float4 vc[64]').replace('float4 pc[50]', 'float4 pc[51]'),
             'Texture2D<float4> aTex0 : register(t0);',
             'SamplerState aSampler0 : register(s0);',
             'struct A168Input { float3 position:TEXCOORD0; float3 aux:TEXCOORD1;',
             '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; };',
             'struct A168Output { float4 position:SV_Position; float4 t0:TEXCOORD0;',
             '    float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3; };']
    lines += ['A168Output VS168F8(A168Input input) {',
              '// Fetch mapping (functional hypothesis; re-verified at mesh',
              '// declaration): r1=position, r0=(0,aux), r2=color, r3=(uv,0,0).',
              '    precise float4 r1=float4(input.position,1),r0=float4(0,input.aux);',
              '    precise float4 r2=input.color,r3=float4(input.uv,0,0);',
              '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;',
              '    precise float ps=0;']
    lines += emit_vs_control(inventory['VS']['rows'])
    lines += ['    A168Output result; result.position=output62;',
              '    result.t0=output0; result.t1=output1; result.t2=output2; result.t3=output3; return result;', '}']
    lines += ['float4 PS168F8(A168Output input):SV_Target0 {']
    plits = struct.unpack_from('>16I', image, PROFILES[1][1] - screen.BASE + 0x214)
    screen.require(tuple(plits) == PS_LITERALS, '168F8 PS literal bank changed')
    for i in range(4):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (252 + i, ','.join('0x%08Xu' % v for v in plits[i * 4:i * 4 + 4])))
    lines += ['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=input.t3,r4=0;',
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
                       '168F8 ' + stage + ' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage],
                       '168F8 ' + stage + ' header changed')
        screen.require(record[-12:].hex() == TRAILERS[stage],
                       '168F8 ' + stage + ' trailer changed')
        code = record[start:start + length]
        control, executed = schedule(code, pairs)
        screen.require(tuple(control) == tuple((lo, hi) for lo, hi in CF[stage]),
                       '168F8 ' + stage + ' control flow changed')
        if stage == 'PS':
            seen = []
            for idx, (lo, hi) in enumerate(control):
                if hi >> 12 == 11:
                    seen.append((idx, lo & 0x1FFF, bool((lo >> 13) & 1), bool((lo >> 14) & 1),
                                 (hi >> 2) & 0xFF, bool((hi >> 10) & 1)))
            screen.require(tuple(seen) == JUMPS, '168F8 PS jump table changed')
            # The jump target is the ALLOC control entry: taken, it skips only
            # the slots 6-7 EXEC and still executes the allocation before the
            # slots 8-13 block (CF addresses count 64-bit control entries).
            exec_entries = {idx for idx, (lo, hi) in enumerate(control) if hi >> 12 in (1, 2)}
            for _, target, _, _, _, _ in JUMPS:
                screen.require(target in exec_entries or control[target][1] >> 12 == 12,
                               '168F8 jump target is not an EXEC/ALLOC block')
        rows = {}
        for slot, fetch, _select in executed:
            raw = screen.words(code, slot * 12)
            fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            if not fetch:
                # Only the CF jump is predicated; these ALU issues are not.
                for k in ('absolute_constants', 'vector_destination_relative', 'scalar_destination_relative_or_export_zero',
                          'constant_address_register_relative', 'constant_0_relative', 'constant_1_relative',
                          'predicated', 'predicate_condition'):
                    screen.require(not fields[k], 'Unqualified 168F8 ALU modifier ' + k)
                screen.require(all(not s['relative_temporary'] for s in fields['sources']), 'Unqualified relative temporary')
            rows[slot] = dict(fetch=fetch, raw=raw, fields=fields)
        if stage == 'VS':
            for slot, contract in VS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], '168F8 VS fetch slot changed')
                f = rows[slot]['fields']
                for key, value in contract.items():
                    screen.require(f.get(key) == value, '168F8 VS fetch contract changed')
                screen.require(f.get('source_register') == 0 and f.get('source_component') == 0,
                               '168F8 VS fetch source changed')
            for slot, row in rows.items():
                if not row['fetch']:
                    for s in row['fields']['sources']:
                        screen.require(s['bank'] != 'constant' or s['register'] < 252, '168F8 VS literal reference')
            exports = [slot for slot, row in rows.items() if row['fields'].get('export')]
            screen.require(exports == list(range(7, 20)), '168F8 VS export set changed')
        else:
            for slot, (const, source, destination, swizzle, components) in PS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], '168F8 PS fetch slot changed')
                f = rows[slot]['fields']
                screen.require(f['kind'] == 'texture_fetch' and f['fetch_constant_index'] == const and
                               f['source_register'] == source and f['destination_register'] == destination and
                               f['destination_swizzle'] == swizzle and f['source_components'] == components,
                               '168F8 PS fetch contract changed')
            exports = [slot for slot, row in rows.items() if row['fields'].get('export')]
            screen.require(exports == [18, 19], '168F8 PS export set changed')
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
                    issue(slot, row, stage, False)
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
