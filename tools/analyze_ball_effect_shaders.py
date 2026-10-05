"""Pinned, read-only evidence for the original Ball Homer distortion sprite.

This is an offline exact-record inspector, not runtime shader translation.
The CPU continues to calculate particle position, alpha and animation UVs.
"""
import argparse
import hashlib
import json
import struct

import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

need = screen.require
sha = lambda data: hashlib.sha256(data).hexdigest()
HLSL_SHA = "7df2e16ddbda6ad1eec8ed024d7291c7cbc3cf5db49cf68b821e973315a2b1b9"
PROFILES = (
    ("PSBallEffect", 0x82155D60, 0x1C4, 0x170, 2, 0x82CF2584,
     "ae476ee112be6bf2ae46c842758d3a27cce2a9d15ac218234d36acd43f54dec2",
     "afe75a1c52954d184ae9ec93ded42edfcc703510e6e0b43a62e92979de68ffe0"),
    ("VSBallEffect", 0x82156548, 0x220, 0x16C, 3, 0x82CF25B4,
     "2e3e12ad3ec9bf12b9834fef3217d5a2ea84ae99efe24ad054e1b71ffb84dcdd",
     "05dd82e368d6c46b9af7c6e67ebc68a0e63b853839feef54ba82317225fa0363"),
)
CPU = (
    (0x82771960, 0xB4, "bcd4714c80442ce5ac1026cdca3520c73d7a03a71949f2568fc2b78dfbc7df35"),
    (0x82771A18, 0x3BC, "ed7de4a5d74023ddf817d85c0dd1e98d05f3620961ac16034cf4915e17f0e434"),
    (0x82444D10, 0x20, "54bc77323704fb91297bb9f69ce0cd290c2c3f87b8ba5cd51ff35d71ed50c2bb"),
    (0x82444DF8, 0x20, "ecd9da647cb75b90db04a29e1d461063e1971dca09510d21c2a97d6d5d443b0f"),
)
CF = {
    "PSBallEffect": ((0x11002, 0x1200), (0, 0xC400), (0x3003, 0x2200), (0, 0)),
    "VSBallEffect": ((0x30052003, 0x1200), (0, 0xC200), (0x6005, 0x1200),
                     (0x100B, 0x1200), (0, 0xC400), (0x200C, 0x2200)),
}
EXPR = {
    "PSBallEffect": (
        "r0.xy=sample0(interpolator0.xy).ga",
        "r0.x=r0.x*r0.y (scalar MULs; swizzle 0x61 selects X and Y)",
        "r1.x=r0.x*r1.w (interpolator1.w is particle alpha)",
        "color0=saturate(r1.xxxx*c255.xxxx), c255.x=1.5",
    ),
    "VSBallEffect": (
        "r0.zw=fetch POSITION.xy", "r0.xy=fetch TEXCOORD0.xy",
        "r1.xyz=c8.zzz*c2.xzy+c3.xzy",
        "r1.xyz=c8.yyy*c1.zxy+r1.yxz",
        "r1.xyz=c8.xxx*c0.xzy+r1.yxz",
        "r0.zw=float2(r1.x,r1.z)+r0.zw",
        "r1=r1.yyyy*c6+c7", "r1=r0.wwww*c5+r1",
        "position=r0.zzzz*c4+r1",
        "interpolator1=float4(1,1,1,c8.w)", "interpolator0.xy=r0.xy",
    ),
}


def words(data, at=0, count=3):
    return struct.unpack_from(">" + str(count) + "I", data, at)


def program(code, profile):
    name, address, size, offset, pairs, handle, digest, code_digest = profile
    need(len(code) == size - offset and sha(code) == code_digest,
         "Ball effect executable identity differs")
    cf = []
    for i in range(pairs):
        a, b, c = words(code, i * 12)
        cf.extend(((a, b & 65535), ((b >> 16 | c << 16) & 0xFFFFFFFF, c >> 16)))
    need(tuple(cf) == CF[name], "Ball effect control flow differs")
    schedule = []
    for lo, hi in cf:
        if hi >> 12 in (1, 2):
            first, count, sequence = lo & 4095, lo >> 12 & 7, lo >> 16 & 4095
            need(0 < count <= 6 and sequence >> (2 * count) == 0,
                 "Ball effect issue schedule differs")
            schedule.extend((first+i, bool(sequence & (1 << (i*2)))) for i in range(count))
    need([i for i, _ in schedule] == list(range(pairs, len(code)//12 - 1)),
         "Ball effect has unaccounted instructions")
    need(len(schedule) == len(EXPR[name]), "Ball effect annotations incomplete")
    result = []
    for (slot, fetch), expression in zip(schedule, EXPR[name]):
        raw = words(code, slot*12)
        fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        if not fetch:
            need(not any(fields[key] for key in (
                "scalar_clamp", "absolute_constants", "vector_destination_relative",
                "predicated", "predicate_condition", "constant_address_register_relative",
                "constant_0_relative", "constant_1_relative")), "Ball ALU modifiers differ")
            need(all(not any(src[key] for key in ("absolute_temporary", "relative_temporary", "negated"))
                     for src in fields["sources"]), "Ball ALU operand modifiers differ")
        if name == "PSBallEffect" and slot == 3:
            swizzle = raw[1] & 255
            need(fields["scalar_opcode"] == 2 and fields["scalar_mask"] == 1 and
                 ((3 + (swizzle >> 6)) & 3, swizzle & 3) == (0, 1),
                 "Ball scalar multiplication operands differ")
        if name == "VSBallEffect" and slot == 12:
            need(fields["export"] and fields["vector_mask"] == 7 and fields["scalar_mask"] == 15 and
                 fields["scalar_opcode"] == 5 and fields["sources"][2]["register"] == 8,
                 "Ball RGB-one/particle-alpha export differs")
        result.append({"slot": slot, "words": [f"{word:08X}" for word in raw],
                       "fields": fields, "expression": expression})
    return result


def qualify(image, source):
    need(len(image) == screen.IMAGE_SIZE and sha(image) == screen.IMAGE_SHA256,
         "Ball original image identity differs")
    need(sha(source.replace("\r\n", "\n").encode()) == HLSL_SHA,
         "Reviewed Ball effect HLSL differs")
    for path, digest in ((four.UCODE, four.UCODE_SHA), (four.XENOS, four.XENOS_SHA)):
        need(sha(path.read_bytes()) == digest, "Ball declarative field reference differs")
    take = lambda address, size: image[address-screen.BASE:address-screen.BASE+size]
    for address, size, digest in CPU:
        need(sha(take(address, size)) == digest, "Ball CPU or constant-bank convention differs")
    declaration = words(take(0x82151724, 36), count=9)
    need(declaration == (0, 0x002C23A5, 0, 8, 0x002C23A5, 0x50000, 0xFF0000, 0xFFFFFFFF, 0),
         "Ball float2 position/UV declaration differs")
    result = []
    for profile in PROFILES:
        name, address, size, offset, pairs, handle, digest, code_digest = profile
        record = take(address, size)
        need(sha(record) == digest, "Ball shader record identity differs")
        need(words(take(handle, 12)) == (0, address, 0), "Ball original shader association differs")
        header = words(record, count=9)
        prefix = 0 if name.startswith("VS") else 64
        need(header[1]+header[2] == size and header[1]+prefix == offset and
             words(record, header[6], 2) == (prefix, size-offset), "Ball shader envelope differs")
        if prefix:
            need(words(record, header[1], 16) == (0,)*12+(0x3FC00000, 0, 0, 0),
                 "Ball pixel shader literal differs")
        result.append({"name": name, "address": f"{address:08X}", "bytes": size,
                       "sha256": digest, "instructions": program(record[offset:], profile)})
    return {"shaders": result, "constants": {
        "VS c0..3": "w2v, original 82DFEA60", "VS c4..7": "v2s, original 82DFEAA0",
        "VS c8": "world_particle_pos3_alpha, original stack+60; device+800",
        "PS c255.x": 1.5}, "vertices": "Original QUADLIST 13: 4 XYUV float4s, stride16",
        "pixel": "saturate(((sample.G * sample.A) * alpha) * 1.5), replicated RGBA",
        "limits": "Finite native arithmetic and sampler policy; no console rounding or rasterization parity claim."}


def self_test(image, source):
    checks = 0
    for profile in PROFILES:
        _, address, size, offset, _, _, _, _ = profile
        code = image[address-screen.BASE+offset:address-screen.BASE+size]
        for index in range(len(code)):
            changed = bytearray(code)
            changed[index] ^= 1
            try:
                program(changed, profile)
            except ValueError:
                checks += 1
            else:
                raise ValueError("Mutated Ball shader was accepted")
    for before, after in (("ballC[2].xzy", "ballC[2].xyz"),
                          ("sampled.g*sampled.a", "sampled.r*sampled.a"),
                          ("value=value*1.5", "value=value*1.0"),
                          ("ballC[8].w", "ballC[8].z"),
                          ("destination.rgb-product", "product-destination.rgb")):
        need(before in source, "Ball HLSL mutation anchor missing")
        try:
            qualify(image, source.replace(before, after))
        except ValueError:
            checks += 1
        else:
            raise ValueError("Mutated Ball HLSL was accepted")
    return checks


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    image = (screen.ROOT/"analysis/simpsons.pe").read_bytes()
    source = (screen.ROOT/"renderer/ball_effect.hlsl").read_text(encoding="utf-8")
    result = qualify(image, source)
    if not args.verify:
        print(json.dumps(result, indent=2))
    if args.self_test:
        print("PASS", self_test(image, source), "Ball effect shader/HLSL mutation checks")
