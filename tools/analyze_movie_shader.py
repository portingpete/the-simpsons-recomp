"""Offline qualification of ONLY original vp6_y_cr_cb_Xenon_PS at 82152B68.

Stdlib, immutable inputs, no GPU/runtime translation. --write/--verify use only
build/movie-shader/movie-shader.json. Field decoders reject unproved forms;
the record and full image must also match independent SHA-256 pins.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "build/movie-shader/movie-shader.json"
BASE = screen.BASE
VA = 0x82152B68
SIZE = 0x21C
SHA256 = "48d052096755186f10dc040a2e0718544f2dea5692f59b6e2ca5a9435351deee"
PAYLOAD_OFFSET, PAYLOAD_SIZE = 0x164, 0xB8
CODE_OFFSET, CODE_SIZE = 0x1A4, 0x78
REF_ROOT = Path("K:/Simpsons/RexGlueCurrent")
REFERENCES = {
    "include/rex/graphics/format/ucode.h":
        "e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb",
    "include/rex/graphics/xenos.h":
        "7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227",
    "src/graphics/pipeline/shader/dxbc_translator_alu.cpp":
        "283e79061787be8a9ad4ea9d6afbede0a554dead65694d298c9e0991a882f575",
    "src/graphics/pipeline/shader/spirv_translator_alu.cpp":
        "dfe92d0b216c4d580ac7cbec8605b223d6af6c8ef38f87ec34f4db19c97a5f01",
    "src/graphics/command_processor.cpp":
        "413db8239f8dea0ca5390cfc44076c588fd7711d60383538eb0e4763410d56d6",
    "src/graphics/d3d12/command_processor.cpp":
        "1af3d15bb78c1a55092be530f7fdf871aa75fdc58918d4def033bb8209ea2701",
    "src/graphics/d3d12/texture_cache.cpp":
        "ebd1eb6fdcf509e7f7f9b476c24caeb8efaa1d7b80a05f51932bdb6963675d09",
    "src/graphics/pipeline/texture/cache.cpp":
        "525d74a5f492c881f7d5d9937645b4752bde6362b9aa812902705778e45e6489",
    "src/graphics/pipeline/shader/dxbc_translator_fetch.cpp":
        "9bbd02d52432fe55ec95d1b6f03a0cba46ad98a1ee4b974fe1fcaac4f6c47530",
}
ORIGINAL_SPANS = (
    ("PS creator", 0x82448178, 0x82448288, "d04313f290346e03d92960a98819179a0d16933c17dfbb55fe3d2c19e10ecb2e"),
    ("PS binder", 0x82445278, 0x82445434, "4241ad4dc5325e67142978e26f16173d38099fbb3fc04eb2bb6e04d198d77cb4"),
    ("draw dispatch", 0x8244C4BC, 0x8244C52C, "8c0f6c341f730906ffdebe01d8e228ec399e6f3fb1888c3045cb851f191b6b6f"),
    ("shader upload entry", 0x8245FA00, 0x8245FA88, "4d711b6a711a0c94b5a04e6e854bc096cdf643fbbcc7e61b2717374374ac94da"),
    ("PS constant and code uploads", 0x8245FB2C, 0x8245FBF8, "06cf2908beb36f454950a790b6dfef004c4dba9329eb31d34aca0400d84cd32a"),
    ("literal uploader", 0x8245F810, 0x8245F8C0, "c9e4216d700d717fba0ecdb0062e04db373bf231bef67e89e8bd96c0423aef60"),
    ("code accessors", 0x8245DF60, 0x8245DF98, "677d1fb0ae4152ac6f6d9998d60ead2847e925a7a7f10889bfdc121f210b1af6"),
    ("R8 builder", 0x8243F928, 0x8243FC20, "51a870ba275247748200d6aaa036bedea8ef940484a4e74d5babd480f4256f8a"),
    ("R8 factory", 0x82440560, 0x82440608, "26baf40f3785f5969c66be88997700a1d29ce1f9922887662cf69483c73b813d"),
    ("texture binding fields", 0x824408E0, 0x824409E8, "8b762616a5ccdfa56db9c0d4a69af0b388d641d74fe14e2f88a9c42cbfb1ef31"),
    ("movie shader and plane bindings", 0x8282E440, 0x8282E4D0, "1950b35b76c89c7156f7976a5c754b781552b537c234fcea9369bab6ff74c76c"),
)
EXPECTED_WORDS = (
    (0x10081001, 0x1F1FFFF8, 0x00004000),
    (0x10181001, 0x1F1FFFC7, 0x00004000),
    (0x10281001, 0x1F1FFE3F, 0x00004000),
    (0xC8070000, 0x00C01A00, 0x8001FD00),
    (0xC801C000, 0x00B0B06C, 0x9100FDFF),
    (0xC8028000, 0x0065C000, 0x9000FE00),
    (0xC8048000, 0x00C41A6C, 0x9100FEFF),
)
LITERAL_BITS = (
    (0, 0, 0, 0),
    (0x3F950A81, 0x3FCC4A9D, 0xBD800000, 0xBF000000),
    (0xBF501EAC, 0xBEC89507, 0x3F950A81, 0x40011A54),
    (0, 0, 0, 0),
)
require = screen.require
words = screen.words


def hex32(value):
    return f"{value:08X}"


def from_bits(value):
    return struct.unpack(">f", struct.pack(">I", value))[0]


def bits(value):
    return struct.unpack(">I", struct.pack(">f", value))[0]


def f32(value):
    return from_bits(bits(value))


def decode_alu(raw):
    a, b, c = screen.triplet(raw)
    op = (c >> 24) & 31
    require(op in (0, 16, 17), "Unproved movie ALU opcode")
    require(a >> 26 == 50 and (a >> 20) & 15 == 0,
            "Unproved scalar operation/write")
    require(a & 0x03003FC0 == 0 and b >> 24 == 0,
            "Unproved clamp/absolute/relative/negate/predicate modifier")
    export = bool(a & 0x8000)
    zero_unwritten = bool(a & 0x4000)
    require(not zero_unwritten or export, "Relative scalar destination outside export")
    require((a >> 16) & 15, "Empty ALU vector result")
    sources = []
    for shift, selector in ((16, 31), (8, 30), (0, 29))[:3 if op == 17 else 2]:
        reg = (c >> shift) & 255
        temporary = bool(c & (1 << selector))
        require((temporary and reg < 64) or (not temporary and 252 <= reg <= 255),
                "Unproved register bank/index or temporary modifier")
        sources.append({"bank": "temporary" if temporary else "constant", "register": reg,
                        "relative_swizzle": f"{(b >> shift) & 255:02X}",
                        "components": screen.relative_swizzle((b >> shift) & 255)})
    if op != 17:
        require(c & 0x200000FF == 0 and b & 255 == 0, "Unexpected unused source3")
    mask = (a >> 16) & 15
    return {"kind": "alu", "opcode": op, "operation": {0: "ADD", 16: "DP3", 17: "DP2ADD"}[op],
            "destination": a & 63, "export": export, "vector_mask": mask,
            "scalar_opcode": 50, "scalar_mask": 0, "scalar_operation": "RETAIN_PREV (no write)",
            "constant_zero_mask": ((~mask) & 15) if zero_unwritten else 0,
            "constant_one_mask": 0, "sources": sources, "clamp": False,
            "predicated": False, "negate": False, "absolute": False, "relative_addressing": False}


def decode_fetch(raw):
    a, b, c = screen.triplet(raw)
    result = screen.decode_fetch(raw)
    require(a & ~(31 << 20) == 0x10081001 and ((a >> 20) & 31) < 3,
            "Unproved movie fetch source/destination/coordinate/slot flags")
    require(b & 0xFFFFF000 == 0x1F1FF000 and c == 0x00004000,
            "Unproved movie fetch filter/LOD/offset/modifier")
    require(result["destination_swizzle"] in ([0, 7, 7, 7], [7, 0, 7, 7], [7, 7, 0, 7]),
            "Unproved movie fetch component writes")
    result.update({"sample_location_name": "centroid", "sample_component": "X only",
                   "selector_7": "preserve destination", "lod_bias": 0.0,
                   "offset_texels": [0.0, 0.0, 0.0], "inherited_sampler": True,
                   "arbitrary_filter_note": "0=k2x4Sym; deprecated field in declarative reference"})
    return result


def decode_program(code):
    require(len(code) == CODE_SIZE, "Changed executable extent")
    cf, scheduled = screen.decode_schedule(code, 2)
    require(cf == [(1, 2, 3, 21), (12, 2), (2, 5, 4, 0), (0,)],
            "Changed movie execution schedule")
    require(words(code, CODE_SIZE - 12) == (0x4E4A0000, 0x5F71F02E, 0xF5081E67),
            "Changed unexecuted trailer")
    instructions = []
    for index, is_fetch, serialize in scheduled:
        raw = words(code, index * 12)
        decoded = decode_fetch(raw) if is_fetch else decode_alu(raw)
        require(raw == EXPECTED_WORDS[index - 2], "Changed pinned movie instruction")
        instructions.append({"slot": index, "address": hex32(VA + CODE_OFFSET + index * 12),
                             "words": list(map(hex32, raw)), "serialize": serialize, **decoded})
    return {"control_flow": [list(x) for x in cf], "instructions": instructions, "executed_slots": list(range(2, 9)),
            "unexecuted_trailer": list(map(hex32, words(code, CODE_SIZE - 12))),
            "instruction_count": 7, "texture_fetch_count": 3, "alu_count": 4,
            "conditional_control_flow": False, "trailer_executed": False}


def decode_record(data):
    require(len(data) == SIZE, "Changed movie record extent")
    require(words(data, 0, 9) == (0x102A1100, 0x164, 0xB8, 0x24, 0x74, 0x118, 0x140, 0, 0),
            "Changed movie record header")
    require(words(data, 0x140, 9) == (0x40, 0x78, 0x10000100, 4, 0, 0x821, 0x10001, 1, 0x3050),
            "Changed movie program header")
    require(words(data, 0x118, 10) == (0, 1, 0, 0, 0x14, 0x01FC0010, 0, 0, 0, 0),
            "Changed movie constant metadata")
    # CTAB offsets are relative to the header after the block-size word.
    table = 0x78
    require(words(data, table, 7) == (0x1C, 0x92, 0xFFFF0300, 3, 0x1C, 0, 0x8B),
            "Changed sampler reflection header")
    samplers = []
    for index, (label, slot, name_offset) in enumerate(
            (("gTexture_Cb", 2, 0x58), ("gTexture_Cr", 1, 0x74), ("gTexture_Y", 0, 0x80))):
        entry = words(data, table + 0x1C + index * 20, 5)
        require(entry == (name_offset, 0x30000 | slot, 0x10000, 0x64, 0),
                "Changed sampler reflection binding")
        start = table + name_offset
        require(data[start:start + len(label) + 1] == label.encode() + b"\0",
                "Changed sampler reflection name")
        samplers.append({"label": label, "stage": slot, "entry_address": hex32(VA + table + 0x1C + index * 20)})
    literals = []
    for index, expected in enumerate(LITERAL_BITS):
        offset = PAYLOAD_OFFSET + index * 16
        values = words(data, offset, 4)
        require(values == expected, "Changed movie literal constants")
        literals.append({"register": 252 + index, "address": hex32(VA + offset),
                         "bits": list(map(hex32, values)), "float32": list(map(from_bits, values)),
                         "read_components": {252: [], 253: [0, 1, 2, 3], 254: [0, 1, 2, 3], 255: [0]}[252 + index]})
    program = decode_program(data[CODE_OFFSET:CODE_OFFSET + CODE_SIZE])
    require(hashlib.sha256(data).hexdigest() == SHA256, "Changed movie record SHA-256")
    return {"address": hex32(VA), "name": "vp6_y_cr_cb_Xenon_PS", "size_bytes": SIZE,
            "sha256": SHA256, "payload_offset": PAYLOAD_OFFSET, "payload_size": PAYLOAD_SIZE,
            "executable_offset": CODE_OFFSET, "executable_size": CODE_SIZE,
            "program_header_address": "82152CA8", "literal_payload_bytes": 64,
            "metadata": {"address": "82152C80", "words": list(map(hex32, words(data, 0x118, 10))),
                         "literal_descriptor_address": "82152C94", "literal_descriptor": "01FC0010",
                         "payload_dword_offset": 0, "dword_count": 16, "first_constant": 252,
                         "constant_bank": "pixel", "dynamic_c0_read": False},
            "literal_upload": decode_literal_upload(data[0x118:0x140], PAYLOAD_SIZE),
            "reflection_samplers": samplers, "literals": literals, **program}


def decode_literal_upload(metadata, payload_size):
    require(len(metadata) == 0x28 and payload_size == 0xB8, "Changed constant upload envelope")
    require(words(metadata, 0, 5) == (0, 1, 0, 0, 0x14), "Changed constant metadata flags/extent")
    destination, count = struct.unpack_from(">HH", metadata, 0x14)
    offset = words(metadata, 0x18, 1)[0]
    require(destination == 0x1FC and count == 16 and offset == 0,
            "Unproved constant upload destination/count/offset")
    require(words(metadata, 0x1C, 3) == (0, 0, 0), "Changed metadata terminators")
    index = destination << 2
    return {"original_uploader": "8245F810", "packet_header": "C0022F00",
            "source": "GPU address of PS object payload pointer + 0 (original source 82152CCC)",
            "offset_type": hex32(index), "dword_count": count,
            "hardware_register_start": hex32(0x4000 + index),
            "hardware_register_end_inclusive": hex32(0x4000 + index + count - 1),
            "pixel_constant_base": "00004400", "first_pixel_constant": (index - 0x400) // 4,
            "c0_consumed": False, "registers": [252, 253, 254, 255],
            "source_payload_runtime_address_required_for_original_sdk": True}


def r8_contract(format_word=0x28000002, exponent_adjust=0):
    require(format_word == 0x28000002 and exponent_adjust == 0, "Unqualified movie R8 descriptor request")
    signs = [(format_word >> shift) & 3 for shift in (9, 11, 13, 15)]
    swizzle = [(format_word >> shift) & 7 for shift in (18, 21, 24, 27)]
    number_format = (format_word >> 17) & 1
    low19 = number_format | (sum(c << (3 * i) for i, c in enumerate(swizzle)) << 1)
    return {"original_request": hex32(format_word), "hardware_format": format_word & 63,
            "endian": (format_word >> 6) & 3, "tiled": bool((format_word >> 8) & 1),
            "signs_xyzw": signs, "number_format": number_format,
            "resource_swizzle_xyzw": swizzle, "resource_word28_low19": hex32(low19),
            "binder_preserves_low19": True, "exponent_adjust": exponent_adjust,
            "gamma_decode": False, "host_view_format": "DXGI_FORMAT_R8_UNORM",
            "sample_X": "unsigned byte / 255, then effective texture filtering; no second normalization",
            "host_missing_GBA": "irrelevant: every original fetch selects X only"}


def original_provenance(image):
    spans = []
    for name, start, end, digest in ORIGINAL_SPANS:
        data = image[start - BASE:end - BASE]
        require(hashlib.sha256(data).hexdigest() == digest, "Changed original provenance span: " + name)
        spans.append({"purpose": name, "start": hex32(start), "end_exclusive": hex32(end),
                      "sha256": digest, "words": list(map(hex32, words(data, 0, len(data) // 4)))})
    return spans


def pack_color(color):
    """Requested native packed-target helper, separate from original PS."""
    return [round(f32(min(1.0, max(0.0, x)) * scale)) for x, scale in zip(color, (1023, 1023, 1023, 3))]


def evaluate_samples(program, samples, trace=False):
    """Execute decoded dataflow on finite [0,1] sampled scalars, RN binary32.

    Sampling/filter precision is deliberately NOT simulated here. Separate
    rounded products, then left-to-right sums implement the pinned translator
    policy. This is an offline numeric model, not a console precision claim.
    """
    require(len(samples) == 3 and all(math.isfinite(x) and 0 <= x <= 1 for x in samples),
            "Numeric fixture requires three finite normalized sampled values")
    constants = {r["register"]: list(map(lambda x: from_bits(int(x, 16)), r["bits"]))
                 for r in program["literals"]}
    registers = {0: [None] * 4, 1: [None] * 4}
    output = [None] * 4
    steps = []
    for ins in program["instructions"]:
        if ins["kind"] == "texture_fetch":
            value = f32(samples[ins["fetch_constant_index"]])
            destination = registers[ins["destination_register"]]
            for component, selector in enumerate(ins["destination_swizzle"]):
                if selector != 7:
                    require(selector == 0, "Numeric model only samples X")
                    destination[component] = value
        else:
            src = []
            for operand in ins["sources"]:
                bank = registers if operand["bank"] == "temporary" else constants
                src.append([bank[operand["register"]][c] for c in operand["components"]])
            dest = output if ins["export"] else registers[ins["destination"]]
            if ins["operation"] == "ADD":
                result = [f32(src[0][i] + src[1][i]) if ins["vector_mask"] & (1 << i) else None
                          for i in range(4)]
            else:
                count = 3 if ins["operation"] == "DP3" else 2
                products = [f32(src[0][i] * src[1][i]) for i in range(count)]
                value = products[0]
                for product in products[1:]:
                    value = f32(value + product)
                if count == 2:
                    value = f32(value + src[2][0])
                result = [value] * 4
            for component in range(4):
                if ins["vector_mask"] & (1 << component):
                    dest[component] = result[component]
                elif ins["constant_zero_mask"] & (1 << component):
                    dest[component] = 0.0
        if trace:
            steps.append({"slot": ins["slot"], "r0": registers[0].copy(),
                          "r1": registers[1].copy(), "color0": output.copy()})
    require(all(x is not None for x in output), "Undefined output component")
    return (output, steps) if trace else output


def numeric_fixtures(program):
    cases = [("exact_bias_origin", [0.0625, 0.5, 0.5]),
             ("byte_16_128_128_is_not_exact_black", [16/255, 128/255, 128/255]),
             ("byte_235_128_128", [235/255, 128/255, 128/255]),
             ("all_zero", [0, 0, 0]), ("all_one", [1, 1, 1]),
             ("cr_high_cb_low", [0.5, 1, 0]), ("cr_low_cb_high", [0.5, 0, 1]),
             ("asymmetric_bytes", [93/255, 211/255, 37/255])]
    return [{"name": name, "sample_order": "Y,Cr,Cb", "samples": list(map(f32, sample)),
             "output_rgba": evaluate_samples(program, sample),
             "native_helper_packed": pack_color(evaluate_samples(program, sample)),
             "output_bits": list(map(lambda x: hex32(bits(x)), evaluate_samples(program, sample)))}
            for name, sample in cases]


def inspect_image(image):
    require(len(image) == screen.IMAGE_SIZE, "Wrong original image size")
    require(hashlib.sha256(image).hexdigest() == screen.IMAGE_SHA256, "Wrong original image SHA-256")
    record = decode_record(image[VA - BASE:VA - BASE + SIZE])
    return {"schema": "simpsons-movie-shader-v1",
            "image": {"path": "analysis/simpsons.pe", "base": hex32(BASE), "size_bytes": len(image),
                      "sha256": screen.IMAGE_SHA256}, "record": record,
            "references": [{"path": str(REF_ROOT / name), "sha256": digest} for name, digest in REFERENCES.items()],
            "original_cpu_provenance": original_provenance(image), "r8_descriptor": r8_contract(),
            "primary_host_sources": [
                "https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dp2add---ps",
                "https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax",
                "https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules"],
            "contract": {
                "entry_point": "PSMovie", "shader_profile": "ps_5_0", "uv": "TEXCOORD0.xy",
                "resources": ["t0/s0=plane0/Y", "t1/s1=plane2/Cr", "t2/s2=plane1/Cb"],
                "view": "three independently owned Texture2D<float> R8_UNORM views; consume X only",
                "sampling": "normalized UV; computed LOD; no shader bias/offset; per-stage inherited samplers",
                "offsets": "r0.xyz=(Y,Cr,Cb)+c253.zww",
                "red": "DP2ADD(r0.xy,c253.xy,c255.x)",
                "green": "DP3(r0.yzx,c254.xyz)",
                "blue": "DP2ADD(r0.xz,c254.zw,c255.x)",
                "alpha": "+0 (first export's constant-zero mask 0xE, preserved by later exports)",
                "shader_clamp": False, "shader_gamma_conversion": False,
                "shader_alpha_test_or_discard": False, "shader_depth_export": False,
                "host_constant_buffer": "none; exact immutable literal bit patterns embedded in HLSL",
                "packed_entry_point": "PSMoviePacked: uint4(round(saturate(PSMovie(uv))*float4(1023,1023,1023,3)))",
                "cached_PS_binding": "PSMovie (float); packed helper is a distinct target-storage policy",
                "precision_policy": "float32; precise separate products and left-to-right additions, matching pinned translator policy",
                "limits": ["No claim of Xenos versus host FP bit identity, filter/LOD/interpolation precision or exceptional/subnormal equivalence.",
                           "Sampler addressing/mip/LOD/border/anisotropy and resource metadata remain caller-owned preconditions.",
                           "Target conversion/clamp/gamma/blend/color masks and rectangle rasterization are outside this PS.",
                           "Offline qualification and native HLSL only; no shared build, backend integration, game run or console pixel comparison."]},
            "numeric_fixtures": numeric_fixtures(record),
            "user_reported_boot150_fixture": {
                "source": "User-supplied live probe; not independently read by this offline inspector",
                "frame": "FF614918", "presenter": "E1ADC480", "width": 1280, "uv_mode": 1,
                "VS_handle": "E3E968F0", "PS_handle": "E3E9ABD0", "planes_uniform_bytes": [16, 128, 128],
                "all_three_samplers": {"clamp_UVW": [2, 2, 2], "min": 1, "mag": 1, "mip": 2,
                                       "bias": 0, "min_lod": 0, "max_lod": 13, "anisotropy": 1},
                "draw_policy": {"packed_blend": 1, "expanded": 0, "write_mask": "F"},
                "expected_native_helper_packed": [3, 0, 4, 0]}}


def verify_references():
    for name, digest in REFERENCES.items():
        require(hashlib.sha256((REF_ROOT / name).read_bytes()).hexdigest() == digest,
                "Changed pinned reference: " + name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--verify", action="store_true")
    parser.add_argument("--verify-references", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = inspect_image(args.image.read_bytes())
        if args.verify_references:
            verify_references()
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.write:
            REPORT.parent.mkdir(parents=True, exist_ok=True)
            REPORT.write_text(encoded, encoding="utf-8", newline="\n")
        elif args.verify:
            require(REPORT.read_text(encoding="utf-8") == encoded, "Saved movie evidence differs")
        else:
            print(encoded, end="")
    except (ValueError, OSError) as exc:
        print("movie-shader: " + str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
