"""Audit complete retail dual-textured rigid records; does not enable rendering.

This is an offline instruction inventory, not a shader transcription or execution
oracle. Native shader arithmetic and material integration remain unqualified.
"""
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
PROFILES = (
    ("VS", 0x8202B4CC, 944, 560, 384, 4, "0cf15c00e1bf0b3d9e776c8ae4a9e1758947f8bf09862cf939caf8f598c32908"),
    ("PS", 0x8202BB44, 2160, 1092, 1068, 11, "fc21ba7226cd2be8c40887ee95198cf5871ff5cb3b6e3e7fb5552b6addfc8300"),
)
CF = {
    "VS": ((0xF1555004, 0x1201), (0, 0xC200), (0x4009, 0x1200), (0, 0xC400),
           (0x600D, 0x1200), (0x6013, 0x1200), (0x6019, 0x2200), (0, 0)),
    "PS": ((0x9600B, 0x1200), (0x6011, 0x1200), (0x6017, 0x1200), (0x201D, 0x1000),
           (0x4013, 0xB000), (0x401F, 0x1000), (0x400B, 0xB000), (0x05556023, 0x1200),
           (0x00956029, 0x1200), (0x602F, 0x1200), (0x2035, 0x1200), (0x6037, 0x1200),
           (0x103D, 0x1000), (0x4012, 0xB000), (0x0555603E, 0x1200), (0x00956044, 0x1200),
           (0x604A, 0x1200), (0x2050, 0x1200), (0x2052, 0x1200), (0, 0xC400),
           (0x4054, 0x2200), (0, 0)),
}
LITERALS = (0x3F333333, 0, 0x3E800000, 0x3F75C28F,
            0x3E99999A, 0x3E000000, 0xBF800000, 0x44800000,
            0x3F000000, 0x3F800000, 0xBF000000, 0x3E4CCCCD,
            0x3A002008, 0x3A802008, 0x42000000, 0x42800000,
            0x3F666666, 0, 0, 0)


def decode_record(record, profile):
    """Semantic checks independent of the whole-record hash for mutation tests."""
    stage, _, size, offset, code_size, pairs, _ = profile
    screen.require(len(record) == size, "Dual rigid record extent differs")
    header = ((0x102A1101, 560, 384, 36, 128, 0, 420, 0, 0) if stage == "VS" else
              (0x102A1100, 964, 1196, 36, 128, 868, 908, 0, 0))
    screen.require(struct.unpack_from(">9I", record) == header, "Dual rigid header differs")
    trailer = bytes.fromhex(("4e4a0003" if stage == "VS" else "4e4a0002") + "230c67791783d78d")
    screen.require(record[-12:] == trailer, "Dual rigid trailer differs")
    if stage == "VS":
        screen.require(struct.unpack_from(">5I", record, 460) ==
                       (0x00100004, 0x00003005, 0x0000A006, 0x00005007, 0x00215008),
                       "Dual rigid input semantic associations differ")
    else:
        screen.require(struct.unpack_from(">20I", record, 1012) == LITERALS,
                       "Dual rigid c251..255 literals differ")
    code = record[offset:offset+code_size]
    control = []
    for pair in range(pairs):
        first, middle, last = screen.words(code, pair*12)
        control.extend(((first, middle & 65535), ((middle >> 16 | last << 16) & 0xFFFFFFFF, last >> 16)))
    screen.require(tuple(control) == CF[stage], "Dual rigid complete control flow differs")
    slots = []
    for first, second in control:
        if second >> 12 in (1, 2):
            start, count, sequence = first & 4095, (first >> 12) & 7, (first >> 16) & 4095
            screen.require(0 < count <= 6 and sequence >> (2*count) == 0, "Dual rigid issue sequence differs")
            slots.extend((start+i, bool(sequence & (1 << (2*i)))) for i in range(count))
    screen.require([slot for slot, _ in slots] == list(range(pairs, code_size//12-1)),
                   "Dual rigid executable slot coverage differs")
    rows = {}
    for slot, fetch in slots:
        raw = screen.words(code, slot*12)
        fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        rows[slot] = dict(raw=raw, fetch=fetch, fields=fields)
    if stage == "VS":
        for slot, destination, swizzle in ((4, 4, [0, 1, 2, 5]), (5, 1, [0, 1, 2, 7]),
                                            (6, 2, [0, 1, 2, 3]), (7, 3, [7, 7, 0, 1]),
                                            (8, 3, [0, 1, 7, 7])):
            screen.require(rows[slot]["fields"] == dict(kind="vertex_fetch", source_register=0,
                           destination_register=destination, destination_swizzle=swizzle,
                           fetch_constant_index=95, source_component=0, format_field=0,
                           stride_dwords=0, offset_field=0, runtime_declaration_patch_verified=False),
                           "Dual rigid vertex fetch differs")
    else:
        screen.require([slot for slot, row in rows.items() if row["fetch"]] ==
                       [11] + list(range(35, 44)) + list(range(62, 71)), "Dual rigid sample slots differ")
        screen.require(rows[11]["fields"]["fetch_constant_index"] == 2, "Dual rigid base sampler differs")
        for start, bank in ((35, 1), (62, 0)):
            for i, (x, y, channel) in enumerate(rigid.TAPS):
                f = rows[start+i]["fields"]
                screen.require(f["fetch_constant_index"] == bank and
                               [v for v in f["destination_swizzle"] if v != 7] == [channel] and
                               f["offset_fields"] == [(x*2) & 31, (y*2) & 31, 0],
                               "Dual rigid shadow tap differs")
        # The previous ten-pair inspection omitted these four executable slots.
        final = ((0x34100000, 0x0000001B, 0xE2000000),
                 (0xB8100000, 0x00000080, 0xC20000FD),
                 (0xC8010001, 0x006C6C6C, 0x8B00FCFB),
                 (0xC80F8000, 0x00555500, 0xC2010100))
        screen.require(tuple(rows[i]["raw"] for i in range(84, 88)) == final,
                       "Dual rigid final packing/export instructions differ")
    return rows


def inspect(image):
    screen.require(len(image) == screen.IMAGE_SIZE and rigid.sha(image) == screen.IMAGE_SHA256,
                   "Retail image differs")
    screen.require(rigid.sha(image[0x2AD78:0x2AD78+12832]) ==
                   "1c71a2f2f31878a775f79c47d3b161a8a799711b86594bc1a4515c15d2702649",
                   "Dual rigid effect differs")
    report = {}
    for profile in PROFILES:
        stage, address, size, _, _, pairs, digest = profile
        record = image[address-screen.BASE:address-screen.BASE+size]
        screen.require(rigid.sha(record) == digest, "Dual rigid complete record hash differs")
        rows = decode_record(record, profile)
        report[stage] = dict(address=f"{address:08X}", sha256=digest, cf_pairs=pairs,
                            executable_slots=len(rows), first_slot=min(rows), last_slot=max(rows),
                            fetches=sum(row["fetch"] for row in rows.values()),
                            vector_opcodes=sorted({row["fields"]["vector_opcode"] for row in rows.values()
                                                   if not row["fetch"] and row["fields"]["vector_mask"]}),
                            scalar_opcodes=sorted({row["fields"]["scalar_opcode"] for row in rows.values()
                                                   if not row["fetch"] and row["fields"]["scalar_opcode"] != 50}))
    return dict(effect="8202AD78", records=report, texture_stages=[0, 1, 2],
                scope="Complete retail instruction inventory only; no native transcription or gameplay qualification")


def self_test(image):
    checks = 0
    for profile in PROFILES:
        stage, address, size, offset, _, _, _ = profile
        original = image[address-screen.BASE:address-screen.BASE+size]
        # These checks bypass the hash gate and must fail at semantic anchors.
        anchors = list(range(0, 36, 4)) + list(range(offset, offset+profile[5]*12, 4)) + [size-12]
        anchors += list(range(460, 480, 4)) if stage == "VS" else list(range(1012, 1092, 4))
        if stage == "PS":
            anchors += list(range(offset+84*12, offset+88*12, 4))
        for at in anchors:
            changed = bytearray(original)
            changed[at] ^= 1
            try:
                decode_record(changed, profile)
            except (ValueError, struct.error):
                checks += 1
            else:
                raise ValueError(f"Accepted semantic mutation {stage}+{at:X}")
        for truncated in (original[:-1], original[:-12]):
            try:
                decode_record(truncated, profile)
            except ValueError:
                checks += 1
            else:
                raise ValueError("Accepted truncated retail shader")
    # Reproduce the old inspection's ten-pair coverage error without corrupting bytes.
    ps = PROFILES[1]
    changed_profile = (*ps[:5], 10, ps[6])
    try:
        decode_record(image[ps[1]-screen.BASE:ps[1]-screen.BASE+ps[2]], changed_profile)
    except ValueError:
        checks += 1
    else:
        raise ValueError("Accepted incomplete ten-pair pixel inspection")
    return checks


def issue(slot, row, stage):
    f = row["fields"]
    if stage == "PS" and slot == 11:
        screen.require(f["source_register"] == 0 and f["source_components"] == [0, 1, 0] and
                       f["destination_register"] == 6 and f["destination_swizzle"] == [0, 1, 2, 7],
                       "Dual base fetch operands differ")
        return "    { precise float4 value=rigidBase.Sample(rigidBaseSampler,r0.xy); r6.xyz=value.xyz; }"
    if stage == "PS" and slot == 18:
        screen.require([rigid.operand(f, i, stage) for i in range(3)] ==
                       ["r0.xxxx", "r4.xyzz", "r7.zzzz"] and f["scalar_opcode"] == 0,
                       "Dual normalization/ADDs differs")
        return ("    {\n        precise float4 v=rigidLegacyMultiply(r0.xxxx,r4.xyzz);\n"
                "        precise float s=r7.z+r7.z;\n        r5.xyz=v.xyz; r0.x=s; ps=s;\n    }")
    if stage == "PS" and slot == 26:
        screen.require([rigid.operand(f, i, stage) for i in range(3)] ==
                       ["r6.zwww", "k253.wxxx", "r0.xxxx"] and f["vector_opcode"] == 17,
                       "Dual DOT2ADD differs")
        return "    { precise float value=dot(r6.zw,k253.wx)+r0.x; r1.w=value; }"
    if stage == "PS" and slot == 27:
        # ADD_CONST_0 reads c40.x and encoded temporary r0.y. Both
        # scalar and vector RHS are evaluated before either destination write.
        screen.require(row["raw"] == (0xB0160001, 0x00CBC641, 0x8104FE28),
                       "Dual ADD_CONST_0 instruction differs")
        return ("    {\n        precise float4 v=r4.wwzz*k254.zzzz;\n"
                "        precise float s=pc[40].x+r0.y;\n        r1.yz=v.yz; r0.x=s; ps=s;\n    }")
    return rigid.issue(slot, row, stage).replace("pc[251]", "k251")


def shader_source(image):
    inspect(image)
    rows = {}
    for p in PROFILES:
        rows[p[0]] = decode_record(image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]], p)
    lines = ['#include "rigid_shader.hlsl"',
             'Texture2D<float4> rigidBase : register(t2);',
             'SamplerState rigidBaseSampler : register(s2);',
             'struct RigidDualInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1;',
             '    float4 color:TEXCOORD2; float2 uv:TEXCOORD3; float2 uv1:TEXCOORD4; };',
             'struct RigidDualOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float2 uv1:TEXCOORD1;',
             '    float4 characterShadow:TEXCOORD2; float4 worldShadow:TEXCOORD3;',
             '    float3 normal:TEXCOORD4; float4 color:TEXCOORD5; };',
             'RigidDualOutput VSRigidDualTextured(RigidDualInput input) {',
             '    precise float4 r4=float4(input.position,1),r1=float4(input.normal,0),r2=input.color;',
             '    precise float4 r3=float4(input.uv1,input.uv),r0=0;',
             '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0;']
    lines.extend(issue(i, rows['VS'][i], 'VS') for i in range(9, 31))
    lines += ['    RigidDualOutput result; result.position=output62; result.uv=output0.xy; result.uv1=output1.xy;',
              '    result.characterShadow=output2; result.worldShadow=output3; result.normal=output4.xyz;',
              '    result.color=output5; return result;', '}',
              'float4 PSRigidDualTextured(RigidDualOutput input):SV_Target0 {']
    for i in range(5):
        values = ','.join('0x%08Xu' % v for v in LITERALS[i*4:i*4+4])
        lines.append('    const float4 k'+str(251+i)+'=asfloat(uint4('+values+'));')
    lines += ['    precise float4 r0=float4(input.uv,0,0),r1=float4(input.uv1,0,0);',
              '    precise float4 r2=input.characterShadow,r3=input.worldShadow,r4=float4(input.normal,0);',
              '    precise float4 r5=input.color,r6=0,r7=0,output0=0;',
              '    precise float ps=0; bool p0=false;']
    for start, stop, suffix in ((11, 31, '    [branch] if(p0) {'), (31, 35, '    [branch] if(p0) {'),
                               (35, 55, '    }'), (55, 62, '    [branch] if(p0) {'), (62, 82, '    }'),
                               (82, 84, '    }'), (84, 88, '    return output0;')):
        lines.extend(issue(i, rows['PS'][i], 'PS') for i in range(start, stop))
        lines.append(suffix)
    lines += ['}', '[maxvertexcount(1)]',
              'void GSRigidDualProbe(point RigidDualOutput input[1],inout PointStream<RigidDualOutput> stream) { stream.Append(input[0]); }',
              'RigidDualOutput VSRigidDualPixelProbe(uint id:SV_VertexID) {',
              '    RigidDualOutput result; result.position=float4(id==2?3:-1,id==1?3:-1,0,1);',
              '    result.uv=probeInputs[0].xy; result.uv1=probeInputs[0].zw; result.characterShadow=probeInputs[1];',
              '    result.worldShadow=probeInputs[2]; result.normal=probeInputs[3].xyz; result.color=probeInputs[4]; return result;', '}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    image = (ROOT / "analysis/simpsons.pe").read_bytes()
    report = inspect(image)
    if args.verify:
        source = ROOT / "renderer/rigid_dualtextured_shader.hlsl"
        screen.require(source.read_text(encoding="utf-8") == shader_source(image),
                       "Dual rigid HLSL source differs from the audited transcription")
        report["hlsl_source_verified"] = True
    if args.self_test:
        report["rejected_semantic_mutations"] = self_test(image)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
