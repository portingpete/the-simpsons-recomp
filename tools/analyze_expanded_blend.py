"""Bounded, read-only expanded-blend evidence; deterministic JSON on stdout.

No renderer, GPU command processor, shader translator, or claimed hardware blend
emulator. Arithmetic witnesses compare explicitly hypothetical rounding choices.
Only integer storage packing and the original descriptor toggle are modeled.
"""
from __future__ import annotations

import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

BASE = 0x82000000
ROOT = Path(__file__).resolve().parents[1]
IMAGE_SIZE = 15466496
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
REFERENCE_ROOT = Path(os.environ.get("SIMPSONS_REFERENCE_ROOT", "K:/Simpsons/RexGlueCurrent"))
# Hashes are of ORIGINAL bytes, not generated AOT source.
WINDOWS = (
    ("startup", 0x823EDF20, 0x288,
     "575ebe22b19549ad84801306e347ca37feaf58da2b2c2f026f4fe54bd41cd1d6"),
    ("expanded_target0_setter", 0x8243B3B0, 0xA4,
     "0dfbb7660d281a739c05d8fa53fc229cba95d81f59fb65668d0afffd96aab126"),
    ("screen_quad", 0x82756480, 0x3DC,
     "7ac6f43c8dcf558cd9bbfb3789ff4fb94c62c45dc21f7d47552ac84d6d2fb0f9"),
    ("expanded_request_getter", 0x8243B458, 8,
     "16399cd05c5414663be1b7e43fd2b48cb747e5bfd56c8067b74ec5b053c0fd6b"),
    ("surface_descriptor_builder", 0x8243FC38, 0x254,
     "a7e37fda962359f84cfbf7d6b1c9fd2d309e422a6b0547e7f60bb50520081413"),
    ("surface_create", 0x82440698, 0x128,
     "3d4ab515322504c948ccc5306e7db17f99670afb09fe22175360f6c6833d34c7"),
    ("engine_target_bind", 0x823EDB68, 0x50,
     "f3d33a580ea766076d5a0eb31eba6335be2ff7b2f7f9d946d7c0d8c5caaebd03"),
    ("target_bind_thunk", 0x8243DED0, 4,
     "cfe2436c3aaacdedf2283088aff8e3b6726336c86e43b6c53e35bc44481b8dcd"),
    ("target_bind", 0x8243D230, 0x364,
     "a7dc50b02df0dd1005afa476a0149404e3230f22e05d0546ec539ff2f144c232"),
    ("camera_end", 0x823EE7F0, 0x2C,
     "d23069266cb233c2cbfd0be7eff686dfcc7c9b229b3d852898b9be48b0d42228"),
    ("present_callback", 0x823EE820, 0xAC,
     "c07f3c115bd84242d3cf5caf790027b8e2711a231864d66c390ca16d8e326577"),
    ("loading_quads", 0x828625A0, 0x308,
     "8a5d1ea02ffe32ced0b461692dd005361cd88fa4e736f54c87a7b0aeba69a9f7"),
    ("loading_present", 0x82862D50, 0x18C,
     "e29a51665df7ce4c1da3331cb8b7ad5efc3ef93ad191c90b0537fc6213d88c3b"),
    ("camera_show", 0x823F1BD0, 0x40,
     "f1f4172ecc20988d2e37a89d05fdfaf16db3cd6a5585352e59310367fcef1cab"),
    ("engine_show_dispatch", 0x82408030, 0x54,
     "93d339abc97e4458c3022dd111164cf57fda91626cf2e57907ded881874c0195"),
    ("resolve_boundary", 0x82455570, 0xE44,
     "e3b994307e6e95ca72f3e9d7b184d4ec60d5e5e5cdc72978dd1c90cb66d0e26f"),
)
# Pinned read-only reference snapshots; these describe software implementations,
# not a hardware conformance oracle. No implementation text is emitted/copied.
REFERENCES = (
    ("include/rex/graphics/xenos.h",
     "7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227",
     ((298, 359, "format values, storage equivalence and 32-bit width"),
      (569, 582, "bitwise-compatible resolve texture formats"))),
    ("include/rex/graphics/pipeline/render_target/cache.h",
     "b81f11c9b74fa107266dc06853f885d3f5b3c9ddc8aa207c23f3efa61c377145",
     ((34, 74, "host path explicitly excludes internal blend precision fidelity"),
      (95, 117, "fixed-point flags shared by formats 2 and 10"),
      (225, 238, "storage resource key excludes blend precision"))),
    ("src/graphics/pipeline/render_target/cache.cpp",
     "5d0a91ab992c4085886d6f29fb0cbef3b2fefc443d3a45c9f4e9bd0db4324118",
     ((63, 75, "same clamp range and component write masks"),
      (484, 494, "resource format strips blending modifier"))),
    ("src/graphics/pipeline/shader/dxbc_translator_om.cpp",
     "d8704c6bcc04da97a7250f768e65c9f0b25e8ecbce378142722c28a07a046677",
     ((1110, 1128, "same 10/10/10/2 unpack"),
      (1261, 1280, "same scaled-plus-half/truncate pack"),
      (1981, 2015, "same fixed-point source clamping"))),
    ("src/graphics/pipeline/shader/spirv_translator_rb.cpp",
     "a4aaf9e6b8ab5fdce92ab19e1aa228cd439375f302739bc61c89d481d5699a24",
     ((2345, 2349, "half-unit pack offset"),
      (2428, 2457, "same 10/10/10/2 pack block"),
      (2742, 2765, "same unpack block"))),
    ("src/graphics/d3d12/render_target_cache.cpp",
     "6e91b6231b0063eadf8cb547b0495c1bd6ab6fdb1d9947f61bd32b711df30425",
     ((1580, 1593, "both formats use R10G10B10A2_UNORM backing"),
      (3255, 3277, "same-format transfer or packed conversion"),
      (5547, 5558, "packed transfer retains two-bit alpha"))),
    ("src/graphics/util/draw.cpp",
     "bffc630c0b7bed2e3f2c8dc1c05ecc7025be289f2bde41eac03a66742ba7a5a1",
     ((1114, 1135, "single-sample zero-bias bitwise-compatible resolve path"),)),
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def uint(value, bits, name="value"):
    require(type(value) is int and 0 <= value < (1 << bits),
            f"{name} must be an unsigned {bits}-bit integer")
    return value


def original_range(image, va, size):
    uint(va, 32, "address")
    require(type(size) is int and size > 0 and size % 4 == 0 and va % 4 == 0,
            "Invalid original instruction range/alignment")
    offset = va - BASE
    require(offset >= 0 and offset <= len(image) and size <= len(image) - offset,
            "Original range outside image")
    return image[offset:offset + size]


def word(image, va):
    return struct.unpack(">I", original_range(image, va, 4))[0]


def verify_windows(image):
    result = []
    for name, va, size, expected in WINDOWS:
        actual = hashlib.sha256(original_range(image, va, size)).hexdigest()
        require(actual == expected, f"Changed original evidence window: {name}")
        result.append({"name": name, "address": f"0x{va:08X}",
                       "bytes": size, "sha256": actual})
    return result


def verify_image(image):
    require(len(image) == IMAGE_SIZE, "Wrong original flat image size")
    require(hashlib.sha256(image).hexdigest() == IMAGE_SHA256,
            "Original flat image hash mismatch")


def verify_reference(data, relative_path):
    profile = next((entry for entry in REFERENCES if entry[0] == relative_path), None)
    require(profile is not None, "Unknown reference identity")
    actual = hashlib.sha256(data).hexdigest()
    require(actual == profile[1], f"Changed reference source: {relative_path}")
    lines = data.decode("utf-8").splitlines()
    windows = []
    for first, last, claim in profile[2]:
        require(1 <= first <= last <= len(lines), "Truncated reference evidence range")
        windows.append({"first_line": first, "last_line": last, "claim": claim})
    return {"path": relative_path, "sha256": actual, "windows": windows,
            "authority": "local reference implementation, not hardware measurement"}


def load_reference(root, relative_path):
    path = Path(root) / relative_path
    require(0 < path.stat().st_size <= 1024 * 1024, "Reference source exceeds bounded extent")
    return verify_reference(path.read_bytes(), relative_path)


def expanded_format(format_id, request):
    """Exact Boolean subset of original 8243B3B0's four-bit format update."""
    uint(format_id, 4, "format")
    uint(request, 1, "expanded request")
    if format_id not in (2, 3, 10, 12) or ((format_id >> 3) & 1) == request:
        return format_id
    return (format_id + 3) * 2 if request else format_id // 2 - 3


def update_descriptor(descriptor, request):
    """Only surface+1C bits 16..19 change. None means no bound surface.

    The original separately retains the request even with no surface. This pure
    helper does not own a surface, emulate device state, or touch stored pixels.
    """
    uint(request, 1, "expanded request")
    if descriptor is None:
        return None
    uint(descriptor, 32, "descriptor")
    changed = expanded_format((descriptor >> 16) & 15, request)
    return (descriptor & ~0xF0000) | (changed << 16)


def storage_format(format_id):
    require(type(format_id) is int and format_id in (2, 10),
            "Only proven integer formats 2 and 10 have this storage contract")
    return 2


def pack_storage_codes(codes, format_id=2):
    """Pack already-quantized render-target component codes; no float rounding.

    This abstract reference word is NOT serialized texture endian/swizzle or a
    resolve-address algorithm. X/Y/Z/W occupy low-to-high 10/10/10/2 bits.
    """
    storage_format(format_id)
    require(isinstance(codes, (tuple, list)) and len(codes) == 4,
            "Exactly four storage component codes required")
    for value, bits in zip(codes, (10, 10, 10, 2)):
        uint(value, bits, "component code")
    return codes[0] | codes[1] << 10 | codes[2] << 20 | codes[3] << 30


def unpack_storage_codes(packed, format_id=2):
    storage_format(format_id)
    uint(packed, 32, "packed component word")
    return tuple((packed >> shift) & mask
                 for shift, mask in ((0, 1023), (10, 1023), (20, 1023), (30, 3)))


def hypothetical_unorm_code(value, bits, rounding):
    """Exact rational counterexample math, NOT a chosen hardware conversion."""
    require(type(value) is Fraction and 0 <= value <= 1,
            "Witness input must be an exact Fraction in [0,1]")
    require(type(bits) is int and bits in (2, 10), "Witness precision must be 2 or 10")
    require(rounding in ("half_up", "nearest_even", "truncate"), "Unknown witness rounding")
    scaled = value * ((1 << bits) - 1)
    quotient, remainder = divmod(scaled.numerator, scaled.denominator)
    if rounding == "truncate":
        return quotient
    twice = remainder * 2
    return quotient + int(twice > scaled.denominator or
                          (twice == scaled.denominator and
                           (rounding == "half_up" or quotient % 2 == 1)))


def precision_witnesses():
    # Ordinary finite sample/color ranges, synthetic -- not a captured game pixel.
    source, alpha = Fraction(175, 255), Fraction(2, 255)
    alpha_codes = {bits: hypothetical_unorm_code(alpha, bits, "half_up") for bits in (2, 10)}
    out = {f"alpha_first_quantized_to_{bits}_bits": hypothetical_unorm_code(
               source * Fraction(code, (1 << bits) - 1), 10, "half_up")
           for bits, code in alpha_codes.items()}
    out["unquantized_source_alpha"] = hypothetical_unorm_code(source * alpha, 10, "half_up")
    half_code = Fraction(1, 2046)
    stored = Fraction(0)
    for _ in range(4):
        stored = Fraction(hypothetical_unorm_code(stored + Fraction(1, 4092), 10, "half_up"), 1023)
    return {
        "authority": "synthetic rational counterexamples; none selects the actual Xenos arithmetic",
        "selector0_source_alpha_precision": {
            "source_rgb_component": str(source), "source_alpha": str(alpha),
            "destination_code": 0, "assumed_final_rounding": "half_up", "results": out},
        "rgb_half_code_tie": {
            "value": str(half_code), "results": {
                mode: hypothetical_unorm_code(half_code, 10, mode)
                for mode in ("half_up", "nearest_even", "truncate")}},
        "persistent_storage_vs_deferred_quantization": {
            "per_write_increment": "1/4092", "writes": 4, "assumed_rounding": "half_up",
            "quantize_each_write_code": int(stored * 1023),
            "quantize_only_after_four_writes_code": hypothetical_unorm_code(Fraction(1, 1023), 10, "half_up")},
    }


def analyze(image, reference_root=REFERENCE_ROOT):
    verify_image(image)
    windows = verify_windows(image)
    references = [load_reference(reference_root, path)
                  for path, _, _ in REFERENCES]
    # Independently checked original SDK default row for scalar ID 0x134.
    row = 0x82CD28B8 + (0x134 // 4) * 12
    require(word(image, row + 4) == 0x8243B3B0 and word(image, row + 8) == 0,
            "Unexpected original expanded-blend default/setter")
    require(word(image, 0x827565C0) == 0x4BCE4DF1 and word(image, 0x82756848) == 0x4BCE4B69,
            "Quad no longer brackets with expanded blend setter")
    return {
        "schema_version": 1,
        "scope": "loading quad expanded blending, integer target formats 2/10 only",
        "image": {"base": "0x82000000", "bytes": len(image), "sha256": IMAGE_SHA256},
        "original_windows": windows, "reference_sources": references,
        "verified_original_instruction_words": sum(size // 4 for _, _, size, _ in WINDOWS),
        "facts": [
            {"authority": "original instructions", "claim": "8243B3B0 retains the request and changes only target format metadata plus dirty state; no allocation/copy/clear/resolve is called"},
            {"authority": "original instructions", "claim": "canonical request 1 maps format 2 to 10; request 0 maps 10 to 2; selectors 0..2 bracket their draw with those requests"},
            {"authority": "original instructions", "claim": "target binding 8243D230 reapplies the saved expanded request; cleanup resets the request rather than restoring prior state"},
            {"authority": "local reference", "claim": "formats 2/10 share 32-bit X10 Y10 Z10 W2 normalized storage; alpha has four persistent codes"},
            {"authority": "local reference", "claim": "both formats have the same packing and ordinary native R10G10B10A2 mapping; this does not prove identical blend arithmetic"},
            {"authority": "local reference", "claim": "resolve to texture formats 7/54 is bitwise-compatible when single-sample selection and zero exponent bias permit; layout/endian/channel handling remains separate"},
        ],
        "format_transition_table": [
            {"format": f, "request0": expanded_format(f, 0), "request1": expanded_format(f, 1)}
            for f in range(16)],
        "storage": {"bits_per_pixel": 32, "component_widths": [10, 10, 10, 2],
                    "component_shifts": [0, 10, 20, 30], "normalization_denominators": [1023, 1023, 1023, 3],
                    "toggle_converts_storage": False, "persistent_expanded_alpha_bits": False},
        "blend_algebra_before_unproved_quantization": {
            "domain": "finite normalized components; arithmetic equations alone do not specify hardware evaluation",
            "selector0": "RGB = sourceRGB * sourceAlpha + destinationRGB; A = sourceAlpha",
            "selector1": "RGB = sourceRGB * sourceAlpha + destinationRGB * (1-sourceAlpha); A = sourceAlpha",
            "selector2": "RGB = destinationRGB - sourceRGB * sourceAlpha; A = sourceAlpha",
            "reference_clamp_range": [0, 1]},
        "precision_witnesses": precision_witnesses(),
        "loading_resolve_boundary": {
            "original_call_chain": ["82862D50", "823F1BD0", "82408030", "823EE820", "82455570"],
            "resolve_call_pc": "0x823EE87C", "destination": "previous 82D0CF90 texture, now held by 82D0CF8C",
            "other_argument_registers": "r4=r5=r7=r8=r9=r10=0; f1=0; stack +5C/+64 zero",
            "presentation_call_pc": "0x823EE8A8", "presentation_callee": "0x824544F0",
            "limit": "call/argument boundary only; no command execution or complete resolve/display arithmetic proof"},
        "native_exact_blend_contract_proven": False,
        "unresolved": [
            "format-2 versus format-10 source/factor conversion precision",
            "fixed-point scale, multiplication/accumulation widths and intermediate rounding",
            "hardware final quantization/tie rules and exact equivalence to host fixed-function blending",
            "non-finite and out-of-range input behavior outside the finite bounded witnesses",
            "complete loading resolve conversion/rounding and display transform beyond the proven call boundary",
        ],
        "decision": "Retain the unsupported expanded-blend gate; these checks do not authorize selectors 0..2 on the native backend.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--reference-root", type=Path, default=REFERENCE_ROOT)
    args = parser.parse_args(argv)
    try:
        # Bounded reads: reject unexpected extents before reading full source data.
        require(args.image.stat().st_size == IMAGE_SIZE, "Wrong original flat image size")
        result = analyze(args.image.read_bytes(), args.reference_root)
    except (ValueError, OSError, UnicodeError) as error:
        print(f"expanded-blend evidence rejected: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
