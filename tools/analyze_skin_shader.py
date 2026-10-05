"""Pinned offline inventory and HLSL transcription of the opaque skin pair.

Record identities come from the catalog's opaque pass metadata; this script
additionally pins headers, trailers, complete control flow and full issue-slot
coverage, trial-emits every ALU slot through the shared rigid emitter (with
skin-specific MaxAs/address, indexed-bone and scalar-export forms), and
transcribes the full VS/PS pair to test-only HLSL. Runtime admission, GPU
arithmetic qualification and bone-bearing mesh integration require separate
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
    ('VS', 0x82007C1C, 4604, 3800, 804, 7,
     '13a58b272067edcd276667f284ad4582c9d3f40089798decb67ba3dbeefcc649'),
    ('PS', 0x8200A02C, 1136, 668, 468, 4,
     'b84c92c790a8dffa2a8c051dee9bb2b39238d6e5a270cfcb06f707f240bfa91c'),
)
HEADERS = {
    'VS': (0x102A1101, 3736, 868, 36, 116, 3556, 3596, 0, 0),
    'PS': (0x102A1100, 604, 532, 36, 116, 516, 556, 0, 0),
}
TRAILERS = {
    'VS': '4e4a0003b88abdbf816783b1',
    'PS': '4e4a0002b88abdbf816783b1',
}
CF = {
    'VS': ((0xF5556007, 0x1203), (0xF555600D, 0x1203),
           (0x00022013, 0x1000), (0x00004005, 0xB000),
           (0x00006015, 0x1200), (0, 0xC200),
           (0x0000601B, 0x1200), (0x00006021, 0x1200),
           (0x00006027, 0x1200), (0x0000602D, 0x1200),
           (0, 0xC400), (0x00006033, 0x1200),
           (0x00006039, 0x1200), (0x0000303F, 0x2200)),
    'PS': ((0, 0xC400), (0x00006004, 0x1200),
           (0x0000600A, 0x1200), (0x00006010, 0x1200),
           (0x00006016, 0x1200), (0x0000601C, 0x1200),
           (0x00004022, 0x2200), (0, 0)),
}
# Pinned literal banks.
# VS prefix64: k252..k254 zero, k255=(0,1,0.5,3) for indices*3 and morph enable.
VS_LITERALS = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0x00000000, 0x3F800000, 0x3F000000, 0x40400000)
# PS prefix64: k252..k255 in order.
PS_LITERALS = (0x3A802008, 0x42000000, 0x00000000, 0x00000000,
               0x00000000, 0x3F800000, 0x3D800000, 0x3F000000,
               0x42800000, 0xBF800000, 0x3E800000, 0x3F666666,
               0x3E4CCCCD, 0x3E000000, 0xBD800000, 0x3C23D70A)


def vs_issue(slot, row):
    from skin_shader_emit import vs_issue as emit
    return emit(slot, row)


def ps_issue_body(slot, row):
    """Emit one PS slot with scalar-export and slot15 bypass forms."""
    f = row['fields']
    if slot in (10, 37) and f['export']:
        # Scalar/vector export to output0 (w from 10, xyz from 37).
        if slot == 10:
            screen.require(f['vector_mask'] == 0 and f['scalar_mask'] == 8 and
                           f['scalar_opcode'] == 5, 'Skin alpha export changed')
            a = rigid.operand({**f, 'sources': [{**s} for s in f['sources']]}, 2, 'PS', 1)
            # Op5 max(a,b) with a==b (third-source W/X same?); use shared scalar.
            s = rigid.scalar(row, 'PS')
            return (f'    {{ // slot{slot}\n'
                    f'        precise float s={s};\n'
                    f'        output0.w=s.x;\n'
                    f'        ps=s;\n'
                    f'    }}')
        screen.require(slot == 37 and f['vector_mask'] == 5 and f['scalar_mask'] == 2,
                       'Skin color export changed')
        screen.require(f['vector_destination'] == f['scalar_destination'] == 0,
                       'Skin color output target changed')
        # Keep the co-issued RHS evaluation, then route the grouped vector XZ
        # write and scalar Y write to the color export. The shared emitter uses
        # r0.xz=v.xz, not separate component assignments.
        v = rigid.issue(slot, {**row, 'fields': {**f, 'export': False}}, 'PS')
        screen.require('r0.xz=v.xz;' in v and 'r0.y=s.x;' in v,
                       'Skin color export write form changed')
        v = v.replace('r0.xz=v.xz;', 'output0.xz=v.xz;')
        v = v.replace('r0.y=s.x;', 'output0.y=s.x;')
        return v
    if slot == 15:
        # Bypass rigid slot15 zero-product gate; generic legacy multiply + CONST.
        # Mask11=x,y,w (not z): r4.x,y,w=v.x,y,w; scalar42 r6.y=t.
        screen.require(f['vector_opcode'] == 1 and f['vector_mask'] == 11 and
                       f['vector_destination'] == 4 and f['scalar_opcode'] == 42,
                       'Skin slot15 changed')
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        # Scalar42 split form via shared scalar().
        s = rigid.scalar(row, 'PS')
        return (f'    {{ // slot15\n'
                f'        precise float4 v=rigidLegacyMultiply({a},{b});\n'
                f'        r4.x=v.x; r4.y=v.y; r4.w=v.w;\n'
                f'        precise float t={s};\n'
                f'        r6.y=t.x;\n'
                f'        ps=t;\n'
                f'    }}')
    text = rigid.issue(slot, row, 'PS').replace('pc[251]', 'k251')
    if not row['fetch'] and f['vector_mask'] and f['vector_opcode'] == 1:
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        text = text.replace(a + '*' + b, 'rigidLegacyMultiply(' + a + ',' + b + ')')
    return text


def ps_issue(slot, row):
    text = ps_issue_body(slot, row)
    if row['fields']['scalar_opcode'] == 42:
        # MUL_CONST_0 obeys the same legacy zero-product rule as MULv.
        # The authored zero fakeLightDir normalizes with rsqrt(0); its zero
        # scalar components must remain zero rather than become 0*inf NaNs.
        expression = rigid.scalar(row, 'PS')
        operands = expression.split('*')
        screen.require(len(operands) == 2 and expression in text,
                       'Skin scalar constant multiply form changed')
        text = text.replace(expression, 'rigidLegacyProduct(' + ','.join(operands) + ')')
    return text


def emit_vs_control(rows):
    """Static forward flow for the single morph predicate jump."""
    lines, ends = [], []
    for index, (lo, hi) in enumerate(CF['VS']):
        while ends and ends[-1] == index:
            lines.append('    }')
            ends.pop()
        op = hi >> 12
        if op in (1, 2):
            for slot in range(lo & 4095, (lo & 4095) + ((lo >> 12) & 7)):
                if rows[slot]['fetch']:
                    continue  # Twelve pinned fetches are the input interface.
                lines.append(vs_issue(slot, rows[slot]))
        elif op == 11:
            target = lo & 0xFFF
            screen.require(index == 3 and target == 5, 'Skin VS jump changed')
            lines.append(f'    [branch] if(p0) {{ // CF{index} -> CF{target} morph')
            ends.append(target)
        else:
            screen.require(op in (0, 12), 'Unsupported skin VS CF kind')
    screen.require(not ends, 'Unclosed skin VS block')
    return lines


def emit_ps_control(rows):
    """Straight-line PS with leading ALLOC, trailing pad and EXEC_END."""
    lines = []
    for index, (lo, hi) in enumerate(CF['PS']):
        op = hi >> 12
        if op in (1, 2):
            for slot in range(lo & 4095, (lo & 4095) + ((lo >> 12) & 7)):
                lines.append(ps_issue(slot, rows[slot]))
        elif op == 12:
            screen.require(index == 0, 'Skin PS ALLOC changed')
        else:
            screen.require(op == 0 and index == 7, 'Skin PS pad changed')
    return lines


def shader_source(image):
    """Transcribe skin VS+PS to test-only HLSL; GPU/runtime need separate proof."""
    inventory = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    # Skin VS needs wide bone bank; PS needs k252..k255 (prefix64).
    # Keep vc[30]/pc[50] names but grow via replacement for skin banks.
    # Actual bank sizes are qualified by GPU constant tests, not by name.
    lines = ['// Opaque skin VS82007C1C / PS8200A02C. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common.replace('float4 vc[30]', 'float4 vc[256]').replace('float4 pc[50]', 'float4 pc[64]'),
             'struct SkinInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 weights:TEXCOORD2; float4 indices:TEXCOORD3;',
             '    float4 color:TEXCOORD4; float2 uv:TEXCOORD5;',
             '    float3 morph1:TEXCOORD6; float3 morph2:TEXCOORD7; float3 morph3:TEXCOORD8;',
             '    float3 morph4:TEXCOORD9; float3 morph5:TEXCOORD10; float3 morph6:TEXCOORD11; };',
             'struct SkinOutput { float4 position:SV_Position; float2 t0:TEXCOORD0;',
             '    float3 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3; };',
             'cbuffer SkinProbeInputs : register(b2) { float4 skinProbe[4]; };']
    lines += ['SkinOutput VSSkin(SkinInput input) {',
              '// Fetch mapping by semantic entry: r5 position, r8 normal,',
              '// r9 weights, r4 indices.wzyx, r1 color, r11.xy uv,',
              '// r10/r3/r2/r0.yzw/r6/r7 morph1..6. r6 reused for indices*3.',
              '    precise float4 r5=float4(input.position,0),r8=float4(input.normal,0);',
              '    precise float4 r9=input.weights,r4=input.indices.wzyx,r1=input.color;',
              '    precise float4 r11=float4(input.uv,0,0);',
              '    precise float4 r10=float4(input.morph1,0),r3=float4(input.morph2,0);',
              '    precise float4 r2=float4(input.morph3,0),r0=float4(0,input.morph4);',
              '    precise float4 r6=float4(input.morph5,0),r7=float4(input.morph6,0);',
              '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;',
              '    precise float ps=0; bool p0=false; int a0=0;']
    # VS literals: k255=(0,1,0.5,3).
    lits = struct.unpack_from('>16I', image, PROFILES[0][1] - screen.BASE + 3736)
    screen.require(tuple(lits[12:]) == (0x00000000, 0x3F800000, 0x3F000000, 0x40400000),
                   'Skin VS literal bank changed')
    lines.append('    const float4 k255=asfloat(uint4(0x00000000u,0x3F800000u,0x3F000000u,0x40400000u));')
    lines += emit_vs_control(inventory['VS']['rows'])
    lines += ['    SkinOutput result; result.position=output62;',
              '    result.t0=output0.xy; result.t1=output1.xyz;',
              '    result.t2=output2; result.t3=output3; return result;', '}']
    lines += ['float4 PSSkin(SkinOutput input):SV_Target0 {']
    plits = struct.unpack_from('>16I', image, PROFILES[1][1] - screen.BASE + 604)
    screen.require(tuple(plits) == PS_LITERALS, 'Skin PS literal bank changed')
    for i in range(4):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (252 + i, ','.join('0x%08Xu' % v for v in plits[i * 4:i * 4 + 4])))
    lines += ['    precise float4 r0=float4(input.t0,0,0),r1=float4(input.t1,0),r2=input.t2,r3=input.t3;',
              '    precise float4 r4=0,r5=0,r6=0,r7=0,output0=0;',
              '    precise float ps=0; bool p0=false;']
    lines += emit_ps_control(inventory['PS']['rows'])
    lines += ['    return output0;', '}',
              '[maxvertexcount(1)] void GSSkinProbe(point SkinOutput input[1],inout PointStream<SkinOutput> s) { s.Append(input[0]); }',
              'SkinOutput VSSkinPixelProbe(uint id:SV_VertexID) {',
              '    SkinOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
              '    o.t0=skinProbe[0].xy; o.t1=skinProbe[1].xyz; o.t2=skinProbe[2]; o.t3=skinProbe[3]; return o; }', '']
    return '\n'.join(lines) + '\n'


def inspect(image):
    """Validate pinned records and decode fields; does not qualify execution."""
    result = {}
    for stage, va, size, start, length, pairs, digest in PROFILES:
        record = image[va-screen.BASE:va-screen.BASE+size]
        screen.require(hashlib.sha256(record).hexdigest() == digest,
                       'Skin '+stage+' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage],
                       'Skin '+stage+' header changed')
        screen.require(record[-12:].hex() == TRAILERS[stage],
                       'Skin '+stage+' trailer changed')
        code = record[start:start+length]
        control = []
        for pair in range(pairs):
            a, b, c = screen.words(code, pair*12)
            control.extend(((a, b & 65535), ((b >> 16 | c << 16) & 0xFFFFFFFF, c >> 16)))
        screen.require(tuple(control) == CF[stage], 'Skin '+stage+' control flow changed')
        rows = {}
        for cf_index, (lo, hi) in enumerate(control):
            if hi >> 12 not in (1, 2):
                screen.require(hi >> 12 in (0, 11, 12),
                               'Unsupported skin control opcode')
                continue
            if hi >> 12 == 0 and lo == 0 and hi == 0:
                continue  # Trailing pad entry after EXEC_END.
            address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
            screen.require(0 < count <= 6 and sequence >> (2*count) == 0,
                           'Unqualified skin issue sequence')
            for index in range(count):
                slot = address+index
                screen.require(slot not in rows, 'Repeated skin instruction slot')
                fetch = bool(sequence & (1 << (2*index)))
                raw = screen.words(code, slot*12)
                rows[slot] = dict(fetch=fetch, raw=raw, control_index=cf_index,
                                  fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
        screen.require(sorted(rows) == list(range(pairs, length//12-1)),
                       'Incomplete skin issue coverage')
        result[stage] = dict(address=hex(va), control=control, rows=rows,
                             header=struct.unpack_from('>9I', record),
                             trailer=record[-12:].hex())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
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
                              fetches={slot: dict(constant=f.get('fetch_constant_index'),
                                                  destination=f.get('destination_register'),
                                                  kind=f.get('kind')) for slot, f in fetches.items()},
                              emitted=len(emittable), failures=failures)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
