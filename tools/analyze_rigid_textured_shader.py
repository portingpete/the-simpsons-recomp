"""Verify the fixed textured rigid shader transcription against the retail image."""
from __future__ import annotations
import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ("VS", 0x8201700C, 904, 544, 360, 4, "5291fe8949bda463baa23ca9c54fbc5a24b61d79e969203e28f36927f535b177"),
    ("PS", 0x82017658, 2028, 1020, 1008, 10, "859c01d893f0b772b397235e02f4b190c93055b895ca65507aabcd38e46e78b1"),
)
PIXEL_CF = ((0x9600A, 0x1200), (0x6010, 0x1200), (0x4016, 0x1000), (0x4012, 0xB000),
            (0x401A, 0x1000), (0x400A, 0xB000), (0x0555601E, 0x1200), (0x00956024, 0x1200),
            (0x602A, 0x1200), (0x2030, 0x1200), (0x6032, 0x1200), (0x1038, 0x1000),
            (0x4011, 0xB000), (0x05556039, 0x1200), (0x0095603F, 0x1200), (0x6045, 0x1200),
            (0x204B, 0x1200), (0x204D, 0x1200), (0, 0xC400), (0x404F, 0x2200))
LITERALS = (0x3F666666, 0x3E99999A, 0, 0, 0x3A802008, 0, 0x3E800000, 0x3F75C28F,
            0x3F333333, 0x3E000000, 0x42000000, 0x44800000, 0x3F000000, 0x3F800000, 0x3E4CCCCD, 0xBF000000)


def records(image):
    decoded = {}
    for stage, address, size, offset, code_size, pairs, digest in PROFILES:
        record = image[address-screen.BASE:address-screen.BASE+size]
        screen.require(len(record) == size and rigid.sha(record) == digest, "Textured rigid " + stage + " record changed")
        header = ((0x102A1101, 0x220, 0x168, 0x24, 0x7C, 0, 0x1A0, 0, 0) if stage == "VS" else
                  (0x102A1100, 0x3BC, 0x430, 0x24, 0x7C, 0x360, 0x388, 0, 0))
        screen.require(struct.unpack_from(">9I", record) == header, "Textured shader header changed")
        code = record[offset:offset+code_size]
        control = []
        for pair in range(pairs):
            first, middle, last = screen.words(code, pair*12)
            control.extend(((first, middle & 65535), ((middle >> 16 | last << 16) & 0xFFFFFFFF, last >> 16)))
        screen.require(tuple(control) == (rigid.CF["VS"] if stage == "VS" else PIXEL_CF), "Textured control flow changed")
        slots = []
        for first, second in control:
            if second >> 12 in (1, 2):
                start, count, sequence = first & 4095, (first >> 12) & 7, (first >> 16) & 4095
                screen.require(0 < count <= 6 and sequence >> (2*count) == 0, "Textured issue sequence changed")
                slots.extend((start+index, bool(sequence & (1 << (2*index)))) for index in range(count))
        screen.require([slot for slot, _ in slots] == list(range(pairs, code_size//12-1)), "Textured instruction coverage differs")
        rows = {}
        for slot, fetch in slots:
            raw = screen.words(code, slot*12)
            fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            rows[slot] = dict(raw=raw, fields=fields, fetch=fetch)
        decoded[stage] = rows
        if stage == "VS":
            original, _ = rigid.record(image, rigid.PROFILES[0])
            screen.require(record[offset:-12] == original[536:-12], "Textured VS differs from the qualified rigid instructions")
            screen.require(record[0x1C8:0x1D8] == original[0x1C0:0x1D0], "Textured VS semantic association differs")
        else:
            screen.require(struct.unpack_from(">16I", record, 956) == LITERALS, "Textured pixel literal bank changed")
    return decoded


def issue(slot, row):
    fields = row["fields"]
    if row["fetch"]:
        sampler = fields["fetch_constant_index"]
        screen.require(sampler in (0, 1, 2) and fields["kind"] == "texture_fetch" and
                       fields["normalized_coordinates"] and fields["dimension_field"] == 1 and
                       fields["computed_lod"] and not fields["register_lod"] and not fields["register_gradients"],
                       "Unsupported textured rigid FETCH")
        coordinates = "".join("xyzw"[component] for component in fields["source_components"][:2])
        offsets = [((value+16) % 32-16)//2 for value in fields["offset_fields"]]
        screen.require(all(value % 2 == 0 for value in fields["offset_fields"]), "Fractional textured offset")
        source = "r" + str(fields["source_register"]) + "." + coordinates
        if sampler == 2:
            screen.require(slot == 10 and offsets == [0, 0, 0], "Unexpected base texture sample")
            expression = "rigidBase.Sample(rigidBaseSampler," + source + ")"
        else:
            expression = ("rigidShadowSample(shadow" + str(sampler) + ".Sample(shadowSampler" + str(sampler) +
                          "," + source + ",int2(" + str(offsets[0]) + "," + str(offsets[1]) + ")))" )
        lines = ["    {", "        precise float4 value=" + expression + ";"]
        for lane, component in enumerate(fields["destination_swizzle"]):
            if component != 7:
                screen.require(component < 4, "Unsupported texture component")
                lines.append("        r" + str(fields["destination_register"]) + "." + "xyzw"[lane] + "=value." + "xyzw"[component] + ";")
        return "\n".join(lines + ["    }"])
    if slot == 16:
        screen.require(fields["vector_opcode"] == 17 and fields["vector_mask"] == 1 and
                       fields["vector_destination"] == 4 and fields["scalar_opcode"] == 50 and not fields["scalar_mask"],
                       "Textured DOT2ADD destination changed")
        screen.require([rigid.operand(fields, index, "PS") for index in range(3)] ==
                       ["r6.zwww", "k255.zxxx", "k253.yyyy"], "Textured DOT2ADD operands changed")
        return "    { precise float value=dot(r6.zw,k255.zx)+k253.y; r4.x=value; }"
    emitted = rigid.issue(slot, row, "PS")
    if slot == 18:
        screen.require("r4.yyyy*r3.xyzz" in emitted, "Textured legacy normalization changed")
        emitted = emitted.replace("r4.yyyy*r3.xyzz", "rigidLegacyMultiply(r4.yyyy,r3.xyzz)")
    return re.sub(r" // slot[0-9]+", "", emitted)


def shader_source(image):
    decoded = records(image)
    rows = decoded["PS"]
    lines = ['#include "rigid_shader.hlsl"',
             'Texture2D<float4> rigidBase : register(t2);',
             'SamplerState rigidBaseSampler : register(s2);',
             'RigidOutput VSRigidTextured(RigidInput input) { return VSRigid(input); }',
             'float4 PSRigidTextured(RigidOutput input):SV_Target0 {']
    for register in range(4):
        values = ",".join("0x%08Xu" % value for value in LITERALS[4*register:4*register+4])
        lines.append("    const float4 k" + str(252+register) + "=asfloat(uint4(" + values + "));")
    lines.extend(['    precise float4 r0=float4(input.uv,0,0),r1=input.characterShadow,r2=input.worldShadow;',
                  '    precise float4 r3=float4(input.normal,0),r4=input.color,r5=0,r6=0,r7=0,output0=0;',
                  '    precise float ps=0;bool p0=false;'])
    for start, stop, suffix in ((10, 26, '    [branch] if(p0) {'), (26, 30, '    [branch] if(p0) {'),
                              (30, 50, '    }'), (50, 57, '    [branch] if(p0) {'), (57, 77, '    }'),
                              (77, 79, '    }'), (79, 83, '    return output0;')):
        lines.extend(issue(slot, rows[slot]) for slot in range(start, stop))
        lines.append(suffix)
    return "\n".join(lines + ['}', ''])


def inspect(image, source):
    screen.require(len(image) == screen.IMAGE_SIZE and rigid.sha(image) == screen.IMAGE_SHA256, "Retail image changed")
    screen.require(rigid.sha(four.UCODE.read_bytes()) == four.UCODE_SHA and
                   rigid.sha(four.XENOS.read_bytes()) == four.XENOS_SHA, "Instruction reference changed")
    screen.require(rigid.sha(image[0x168F8:0x168F8+12432]) ==
                   "20213b2d1c5c1c8e7dfce827e513cf236f928d7fd1808f91afec4cacaf21ca01", "Textured FX body changed")
    rigid.source_equivalence(image, (ROOT / "renderer/rigid_shader.hlsl").read_text(encoding="utf-8"))
    compact = lambda value: re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", value))
    screen.require(compact(source) == compact(shader_source(image)), "Textured native source differs from original slots")
    return dict(effect="820168F8", vertex="8201700C", pixel="82017658", source_sha256=rigid.sha(source.encode()),
                vertex_instructions="Identical to qualified rigid VS; distinct original owner and semantic association checked",
                pixel_instructions=73, texture_stages=[0, 1, 2], original_register_banks=[480, 800],
                native_entries=["VSRigidTextured", "PSRigidTextured"],
                scope="Opaque textured rigid pass only. Native texture ownership, material commit and scene integration require separate validation.")


def self_test(image, source):
    checks = 0
    for _, address, size, _, _, _, _ in PROFILES:
        for offset in range(0, size, 4):
            mutation = bytearray(image)
            mutation[address-screen.BASE+offset] ^= 1
            try:
                records(mutation)
            except (ValueError, struct.error):
                checks += 1
            else:
                raise ValueError("Accepted changed original shader word")
    for before, after in (("register(t2)", "register(t3)"), ("r6.zw,k255.zx", "r6.xy,k255.zx"),
                          ("rigidLegacyMultiply(r4.yyyy,r3.xyzz)", "r4.yyyy*r3.xyzz"),
                          ("0x3E99999Au", "0x3EC7AE14u"), ("shadowSampler0,r1.yz", "shadowSampler0,r1.xy")):
        screen.require(before in source, "Mutation target missing")
        try:
            inspect(image, source.replace(before, after))
        except ValueError:
            checks += 1
        else:
            raise ValueError("Accepted changed native shader expression")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    image = (ROOT / "analysis/simpsons.pe").read_bytes()
    if arguments.source:
        print(shader_source(image), end="")
        return
    source = (ROOT / "renderer/rigid_textured_shader.hlsl").read_text(encoding="utf-8")
    result = inspect(image, source)
    if arguments.self_test:
        result["rejected_mutations"] = self_test(image, source)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
