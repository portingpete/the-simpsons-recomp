"""Verify the fixed gloss rigid transcription against the retail image."""
from __future__ import annotations
import argparse
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge

ROOT = Path(__file__).resolve().parents[1]
# Gloss opaque VS8201A07C / PS8201A6EC; record bytes and code offsets from the
# pinned catalog spans, complete control flow captured by inspect_gloss_layout.
PROFILES = (
    ("VS", 0x8201A07C, 940, 556, 384, 4, "4d2f1bba808b49ed4e12ca377ba45bb04cedbdadd986bb409bc5a2b46a362ed1"),
    ("PS", 0x8201A6EC, 2504, 1220, 1284, 12, "d4a85f5ceebd611910b25c693ead51879ecdc32531b9115505653b68da7e0530"),
)
CF = {
    "VS": ((0xF1555004, 0x1201), (0, 0xC200), (0x4009, 0x1200), (0, 0xC400),
           (0x600D, 0x1200), (0x6013, 0x1200), (0x6019, 0x2200), (0, 0)),
    "PS": ((0x9600C, 0x1200), (0x6012, 0x1200), (0x6018, 0x1200), (0x601E, 0x1200),
           (0x6024, 0x1200), (0x602A, 0x1200), (0x1030, 0x1000), (0x4016, 0xB000),
           (0x4031, 0x1000), (0x400E, 0xB000), (0x5556035, 0x1200), (0x95603B, 0x1200),
           (0x6041, 0x1200), (0x2047, 0x1200), (0x6049, 0x1200), (0x104F, 0x1000),
           (0x4015, 0xB000), (0x5556050, 0x1200), (0x956056, 0x1200), (0x605C, 0x1200),
           (0x2062, 0x1200), (0x2064, 0x1200), (0, 0xC400), (0x4066, 0x2200)),
}
LITERALS = (0x3F333333, 0x3F666666, 0x3E800000, 0x3F75C28F,
            0x3E99999A, 0x3E000000, 0xBF800000, 0x44800000,
            0x3F000000, 0x3F800000, 0xBF000000, 0x3E4CCCCD,
            0x3A002008, 0x3A802008, 0x42000000, 0x42800000,
            0, 0, 0, 0)

def decode_record(record, profile):
    stage, _, size, offset, code_size, pairs, _ = profile
    screen.require(len(record) == size, "Gloss rigid " + stage + " record extent differs")
    header = ((0x102A1101, 556, 384, 36, 124, 0, 416, 0, 0) if stage == "VS" else
              (0x102A1100, 1092, 1412, 36, 124, 996, 1036, 0, 0))
    screen.require(struct.unpack_from(">9I", record) == header, "Gloss rigid header differs")
    screen.require(record[-12:] == bytes.fromhex("4e4a0003f4504bfd768dc110" if stage == "VS" else
                                                 "4e4a0002f4504bfd768dc110"), "Gloss rigid trailer differs")
    if stage == "VS":
        screen.require(struct.unpack_from(">5I", record, 456) ==
                       (0x00100004, 0x00003005, 0x0000A006, 0x00005007, 0x00215008),
                       "Gloss rigid input semantic associations differ")
    else:
        screen.require(struct.unpack_from(">20I", record, 1140) == LITERALS,
                       "Gloss rigid c251..255 literal bank differs")
    code = record[offset:offset+code_size]
    control = []
    for pair in range(pairs):
        first, middle, last = screen.words(code, pair*12)
        control.extend(((first, middle & 65535), ((middle >> 16 | last << 16) & 0xFFFFFFFF, last >> 16)))
    screen.require(tuple(control) == CF[stage], "Gloss rigid complete control flow differs")
    slots = []
    for first, second in control:
        if second >> 12 in (1, 2):
            start, count, sequence = first & 4095, (first >> 12) & 7, (first >> 16) & 4095
            screen.require(0 < count <= 6 and sequence >> (2*count) == 0, "Gloss rigid issue sequence differs")
            slots.extend((start+i, bool(sequence & (1 << (2*i)))) for i in range(count))
    screen.require([slot for slot, _ in slots] == list(range(pairs, code_size//12-1)),
                   "Gloss rigid executable slot coverage differs")
    rows = {}
    for slot, fetch in slots:
        raw = screen.words(code, slot*12)
        rows[slot] = dict(raw=raw, fetch=fetch,
                          fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
    return rows


def inspect(image):
    screen.require(len(image) == screen.IMAGE_SIZE and rigid.sha(image) == screen.IMAGE_SHA256,
                   'Retail image differs')
    screen.require(rigid.sha(image[0x19988:0x19988+13248]) ==
                   '7115380680315fe44162a744a5121a9c5b5534e0ed3f0e7d04b6ebce5132e983', 'Gloss effect differs')
    result = {}
    for p in PROFILES:
        record = image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]]
        screen.require(rigid.sha(record) == p[6], 'Gloss shader record hash differs')
        result[p[0]] = decode_record(record, p)
    return result


def issue(slot, row, stage):
    f = row['fields']
    if stage == 'PS' and slot == 12:
        screen.require(row['raw'] == (0x10288001, 0x1F1FF688, 0x4000), 'Gloss base fetch differs')
        return '    r8=rigidBase.Sample(rigidBaseSampler,r0.xy);'
    text = rigid.issue(slot, row, stage).replace('pc[251]', 'k251')
    # Every gloss multiply retains SM3 zero-product behavior, including
    # normalization and log(0) times a zero specular exponent.
    if not row['fetch'] and f['vector_mask'] and f['vector_opcode'] == 1:
        a,b,_ = [rigid.operand(f,j,stage) for j in range(3)]
        text = text.replace(a+'*'+b, 'rigidLegacyMultiply('+a+','+b+')')
    if stage == 'PS' and slot == 37:
        text = text.replace('pc[50].x*r0.y', 'rigidLegacyProduct(pc[50].x,r0.y)')
    return text


def emit_control(rows, stage):
    lines, ends = [], []
    for index,(lo,hi) in enumerate(CF[stage]):
        while ends and ends[-1] == index:
            lines.append('    }'); ends.pop()
        op = hi >> 12
        if op in (1,2):
            for slot in range(lo&4095, (lo&4095)+((lo>>12)&7)):
                if stage == 'VS' and rows[slot]['fetch']:
                    continue  # The five pinned fetches are the input interface.
                lines.append(issue(slot,rows[slot],stage))
        elif op == 11:
            target = lo & 8191
            screen.require(lo >> 13 == 2 and hi == 0xB000 and target > index and
                           (not ends or target <= ends[-1]), 'Non-nested gloss predicate jump')
            # condition bit10 is zero: jump when p0 is false, execute body if true.
            lines.append('    [branch] if(p0) { // CF%d -> CF%d on false' % (index,target))
            ends.append(target)
        else:
            screen.require(op in (0,12), 'Unsupported gloss CF kind')
    screen.require(not ends, 'Unclosed gloss predicate block')
    return lines


def shader_source(image):
    rows = inspect(image)
    # Standalone declarations avoid changing the existing rigid shader banks.
    common = rigid.shader_source(image).split('struct RigidInput {',1)[0]
    common = common.replace('pc[50]', 'pc[51]')
    lines = ['// Fixed opaque gloss VS8201A07C / PS8201A6EC. Offline only.',common,
        'Texture2D<float4> rigidBase : register(t2);',
        'SamplerState rigidBaseSampler : register(s2);',
        'struct RigidGlossInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
        '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
        'struct RigidGlossOutput { float4 position:SV_Position; float4 uv:TEXCOORD0; float4 world:TEXCOORD1;',
        '    float4 characterShadow:TEXCOORD2; float4 worldShadow:TEXCOORD3;',
        '    float3 normal:TEXCOORD4; float4 color:TEXCOORD5; };',
        'RigidGlossOutput VSRigidGloss(RigidGlossInput input) {',
        '    precise float4 r4=float4(input.position,1),r1=float4(input.normal,0),r2=input.color;',
        '    precise float4 r3=float4(input.uv,input.uv1),r0=0;',
        '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0;']
    lines += emit_control(rows['VS'],'VS')
    lines += ['    RigidGlossOutput result; result.position=output62; result.uv=output0; result.world=output1;',
        '    result.characterShadow=output2; result.worldShadow=output3; result.normal=output4.xyz;',
        '    result.color=output5; return result;', '}',
        'float4 PSRigidGloss(RigidGlossOutput input):SV_Target0 {']
    for i in range(5):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (251+i, ','.join('0x%08Xu'%v for v in LITERALS[4*i:4*i+4])))
    lines += ['    precise float4 r0=input.uv,r1=input.world,r2=input.characterShadow,r3=input.worldShadow;',
        '    precise float4 r4=float4(input.normal,0),r5=input.color,r6=0,r7=0,r8=0,r9=0,r10=0,r11=0,r12=0,output0=0;',
        '    precise float ps=0; bool p0=false;']
    lines += emit_control(rows['PS'],'PS')
    lines += ['    return output0;','}',
        'cbuffer GlossProbeInputs : register(b2) { float4 glossProbe[6]; };',
        'RigidGlossOutput VSRigidGlossPixelProbe(uint id:SV_VertexID) {',
        '    RigidGlossOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);',
        '    o.uv=glossProbe[0]; o.world=glossProbe[1]; o.characterShadow=glossProbe[2];',
        '    o.worldShadow=glossProbe[3]; o.normal=glossProbe[4].xyz; o.color=glossProbe[5]; return o;','}',
        '[maxvertexcount(1)] void GSRigidGlossProbe(point RigidGlossOutput input[1],inout PointStream<RigidGlossOutput> stream) { stream.Append(input[0]); }','']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    source = shader_source((ROOT/'analysis/simpsons.pe').read_bytes())
    path = ROOT/'renderer/rigid_gloss_shader.hlsl'
    if args.write:
        path.write_text(source, encoding='utf-8')
    if args.verify:
        screen.require(path.read_text(encoding='utf-8') == source, 'Gloss HLSL differs from pinned transcription')
    print(json.dumps(dict(vertex='8201A07C',pixel='8201A6EC',branches=[[7,22],[9,14],[16,21]],
                         source_sha256=rigid.sha(source.encode()),hlsl_verified=args.verify)))


if __name__ == '__main__':
    main()

