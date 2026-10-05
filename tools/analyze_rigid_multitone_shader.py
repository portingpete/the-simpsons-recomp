"""Pinned offline inventory of the opaque multitone pair; no runtime admission."""
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
    ('VS', 0x82054F2C, 1292, 704, 588, 'a516ad513feb052d70a1ba27c995aa6499bf0bf55bbb8c6bb4387386950b0b2d'),
    ('PS', 0x82055710, 2464, 1204, 1260, '9c46106ef4378890f5af1ad15230d72f74647bff28cb7a1600f0d3eef76c5c04'),
)
FX_SHA = '20736b7201ff5a656a6409c36df50a4ca102d4b67db6c3a326f3f9000816b8a3'
CF = {
    'VS': ((0xF9556007, 0x1201), (0x600D, 0x1200), (0x6013, 0x1200),
           (0x6019, 0x1200), (0x201F, 0x1000), (0x4009, 0xB000),
           (0x6021, 0x1000), (0x1027, 0x5600), (0x200A, 0xB000),
           (0x1028, 0x1200), (0, 0xC200), (0x1029, 0x1200),
           (0, 0xC400), (0x602A, 0x2200)),
    'PS': ((0x24300D, 0x1000), (0x4004, 0xB000), (0x246010, 0x1200),
           (0x4016, 0x1200), (0x601A, 0x1200), (0x6020, 0x1200),
           (0x6026, 0x1200), (0x302C, 0x1000), (0x4017, 0xB000),
           (0x402F, 0x1000), (0x400F, 0xB000), (0x5556033, 0x1200),
           (0x956039, 0x1200), (0x603F, 0x1200), (0x2045, 0x1200),
           (0x6047, 0x1200), (0x104D, 0x1000), (0x4016, 0xB000),
           (0x555604E, 0x1200), (0x956054, 0x1200), (0x605A, 0x1200),
           (0x2060, 0x1200), (0x2062, 0x1200), (0, 0xC400),
           (0x4064, 0x2200), (0, 0)),
}


def decode_code(code, stage):
    """Inventory all issue blocks, including conditional VS EXEC; do not execute."""
    pairs = len(CF[stage])//2
    screen.require(len(code) == (588 if stage == 'VS' else 1260), 'Multitone code extent changed')
    control = []
    for pair in range(pairs):
        a, b, c = screen.words(code, pair*12)
        control.extend(((a, b & 65535), ((b >> 16 | c << 16) & 0xFFFFFFFF, c >> 16)))
    screen.require(tuple(control) == CF[stage], 'Multitone control flow changed')
    rows = {}
    for cf_index, (lo, hi) in enumerate(control):
        if hi >> 12 not in (1, 2, 5):
            continue
        address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
        screen.require(0 < count <= 6 and sequence >> (2*count) == 0, 'Unqualified multitone issue sequence')
        for index in range(count):
            slot = address+index
            screen.require(slot not in rows, 'Repeated multitone instruction slot')
            fetch = bool(sequence & (1 << (2*index)))
            raw = screen.words(code, slot*12)
            rows[slot] = dict(fetch=fetch, raw=raw, control_index=cf_index,
                              conditional_exec=hi >> 12 == 5,
                              fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
    screen.require(list(rows) == list(range(pairs, len(code)//12-1)), 'Incomplete multitone issue coverage')
    return control, rows


def inspect(image):
    blob = image[0x547E8:0x57E08]
    screen.require(hashlib.sha256(blob).hexdigest() == FX_SHA, 'Multitone effect bytes changed')
    result = {}
    for stage, va, size, start, length, digest in PROFILES:
        record = image[va-screen.BASE:va-screen.BASE+size]
        screen.require(hashlib.sha256(record).hexdigest() == digest, 'Multitone shader record changed')
        code = record[start:start+length]
        control, rows = decode_code(code, stage)
        result[stage] = dict(address=hex(va), control=control, rows=rows,
                             header=struct.unpack_from('>9I', record), trailer=record[-12:].hex())
    body = blob[12:]
    context = 0x2D40
    word = lambda at: struct.unpack_from('>I', body, at)[0]
    result['maps'] = {}
    for namespace, count in ((0, 23), (1, 11)):
        table = word(context+0x40+namespace*4)
        result['maps'][('private', 'shared')[namespace]] = {
            leaf: struct.unpack_from('>4I', body, table+leaf*16) for leaf in range(count)
        }
    return result


def issue(slot, row, stage):
    """Emit one pinned issue, including instruction-level predication."""
    f = row['fields']
    if row['fetch'] and stage == 'PS' and slot in (14, 17):
        expected = ((0x10285001, 0x1F1FF5C1, 0x4000) if slot == 14 else
                    (0x44384081, 0x1F1FFE88, 0x4000))
        screen.require(row['raw'] == expected, 'Multitone material fetch changed')
        texture = 'rigidBase' if slot == 14 else 'rigidNoise'
        source = 'r0.xy' if slot == 14 else 'r4.yx'
        lines = ['    { // slot%d' % slot,
                 '        precise float4 value=%s.Sample(%sSampler,%s);' % (texture, texture, source)]
        for lane, selector in enumerate(f['destination_swizzle']):
            if selector != 7:
                screen.require(selector < 4, 'Unsupported multitone fetch literal')
                lines.append('        r%d.%s=value.%s;' %
                             (f['destination_register'], 'xyzw'[lane], 'xyzw'[selector]))
        return '\n'.join(lines+['    }'])
    text = rigid.issue(slot, row, stage).replace('pc[251]', 'k251')
    if not row['fetch']:
        if f['vector_mask'] and f['vector_opcode'] == 1:
            a, b = [rigid.operand(f, j, stage) for j in range(2)]
            text = text.replace(a+'*'+b, 'rigidLegacyMultiply('+a+','+b+')')
        if f['predicated']:
            # Gate the entire co-issue, including previous scalar and predicate
            # updates. RHS evaluation still precedes all register writes.
            text = '    [branch] if(%s)\n%s' % ('p0' if f['predicate_condition'] else '!p0', text)
    return text


def emit_control(rows, stage):
    """Static forward control flow; no runtime instruction interpreter."""
    lines, ends = [], []
    for index, (lo, hi) in enumerate(CF[stage]):
        while ends and ends[-1] == index:
            lines.append('    }'); ends.pop()
        op = hi >> 12
        if op in (1, 2, 5):
            if op == 5:
                screen.require(stage == 'VS' and index == 7 and hi == 0x5600,
                               'Unqualified multitone predicate EXEC')
                lines.append('    [branch] if(p0) { // CF7 predicate EXEC')
            for slot in range(lo & 4095, (lo & 4095)+((lo >> 12) & 7)):
                if stage == 'VS' and rows[slot]['fetch']:
                    continue
                lines.append(issue(slot, rows[slot], stage))
            if op == 5:
                lines.append('    }')
        elif op == 11:
            target = lo & 8191
            screen.require(hi == 0xB000 and target > index, 'Unqualified multitone forward jump')
            if lo >> 13 == 2:
                screen.require(not ends or target <= ends[-1], 'Non-nested multitone predicate jump')
                lines.append('    [branch] if(p0) { // CF%d -> CF%d on false' % (index, target))
                ends.append(target)
            else:
                # VS CF8 skips the alternate UV assignment at CF9. Close
                # precisely the enclosing CF5 conditional and open its else.
                screen.require(stage == 'VS' and index == 8 and lo == 0x200A and
                               ends and ends[-1] == 9, 'Unqualified multitone else jump')
                ends[-1] = target
                lines.append('    } else { // CF8 -> CF10')
        else:
            screen.require(op in (0, 12), 'Unsupported multitone control opcode')
    screen.require(not ends, 'Unclosed multitone predicate block')
    return lines


def shader_source(image):
    inventory = inspect(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    common = common.replace('vc[30]', 'vc[47]')
    lines = ['// Opaque multitone VS82054F2C / PS82055710. Offline transcription.',
             '// GPU arithmetic and runtime integration require separate qualification.', common,
             'Texture2D<float4> rigidBase : register(t2);',
             'SamplerState rigidBaseSampler : register(s2);',
             'Texture2D<float4> rigidNoise : register(t3);',
             'SamplerState rigidNoiseSampler : register(s3);',
             'struct MultitoneInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
             'struct MultitoneOutput { float4 position:SV_Position; float4 uv:TEXCOORD0;',
             '    float4 characterShadow:TEXCOORD1; float4 worldShadow:TEXCOORD2;',
             '    float3 normal:TEXCOORD3; float2 noiseUV:TEXCOORD4; float4 color:TEXCOORD5; };']
    for stage, va, size, start, length, digest in PROFILES:
        vertex = stage == 'VS'
        lines.append('MultitoneOutput VSRigidMultitone(MultitoneInput input) {' if vertex else
                     'float4 PSRigidMultitone(MultitoneOutput input):SV_Target0 {')
        # The record hash pins the literal banks preceding executable code.
        count = 4 if vertex else 5
        literals = struct.unpack_from('>%dI' % (count*4), image,
                                      va-screen.BASE+start-count*16)
        for index in range(count):
            lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                         (256-count+index, ','.join('0x%08Xu' % v for v in literals[index*4:index*4+4])))
        if vertex:
            # Five pinned semantic fetches: normal occupies r0.yzw; the two
            # UV fetches write disjoint halves of r2. Preserve color in r6.
            lines += ['    precise float4 r0=float4(0,input.normal),r1=0,r2=float4(input.uv,input.uv1);',
                      '    precise float4 r3=float4(input.position,1),r4=0,r5=0,r6=input.color,r7=0;',
                      '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0;']
        else:
            lines += ['    precise float4 r0=input.uv,r1=input.characterShadow,r2=input.worldShadow;',
                      '    precise float4 r3=float4(input.normal,0),r4=float4(input.noiseUV,0,0),r5=input.color;',
                      '    precise float4 r6=0,r7=0,r8=0,r9=0,r10=0,r11=0,r12=0,output0=0;']
        lines.append('    precise float ps=0; bool p0=false;')
        lines += emit_control(inventory[stage]['rows'], stage)
        if vertex:
            lines += ['    MultitoneOutput result; result.position=output62; result.uv=output0;',
                      '    result.characterShadow=output1; result.worldShadow=output2; result.normal=output3.xyz;',
                      '    result.noiseUV=output4.xy; result.color=output5; return result;']
        else:
            lines.append('    return output0;')
        lines.append('}')
    return '\n'.join(lines)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    source = shader_source(image)
    if args.emit_hlsl:
        args.emit_hlsl.write_text(source, encoding='utf-8')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({stage: dict(address=result[stage]['address'],
        slots=len(result[stage]['rows']), control=result[stage]['control']) for stage in ('VS', 'PS')}))


if __name__ == '__main__':
    main()
