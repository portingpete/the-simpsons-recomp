"""Read-only proof of four exact original Screen_Xenon shader records.

Stdlib only; JSON goes to stdout. This is a deliberately bounded offline
instruction-field inspector, not a shader translator or GPU command processor.
Unknown records, altered bytes and unproved instruction forms are rejected.
See docs/screen-shaders.md for declarative reference provenance and limitations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

BASE = 0x82000000
ROOT = Path(__file__).resolve().parents[1]
IMAGE_SIZE = 15466496
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
PROFILES = {
    "PSFlat": (0x821524C8, 0xFC, 0x24,
               "bd254ae4adf48d461ab2ddc1958156ca0dafa3d2a1bc4ed67d1a9a3238ae1709",
               ((12, 2), (2, 1, 1, 0))),
    "VSFlat": (0x821525E8, 0xD4, 0x48,
               "58a5e69479abbfb250270590a3c2254720aa1dc04b5a60858c6fc3f05376cccc",
               ((1, 3, 1, 1), (12, 1), (1, 4, 1, 0), (12, 2),
                (2, 3, 0, 0), (0,))),
    "PSTextured": (0x82152708, 0x138, 0x3C,
                   "3257ec7a18a3daa21fe5c570da1d9cdac2941f33c1e7f7cf4df72248b4f83780",
                   ((1, 2, 1, 1), (12, 2), (2, 3, 1, 0), (0,))),
    "VSTextured": (0x82152880, 0xE4, 0x60,
                   "475f31982d13ae15928e0498686bcd9bdb6da902e5c5479228f4a390efe678a9",
                   ((1, 3, 2, 5), (12, 1), (1, 5, 1, 0), (12, 2),
                    (2, 6, 1, 0), (0,))),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def words(data, offset, count=3):
    require(offset >= 0 and offset % 4 == 0 and count > 0,
            "Invalid/alignment-violating word range")
    require(offset + count * 4 <= len(data), "Truncated word range")
    return struct.unpack_from(">" + str(count) + "I", data, offset)


def triplet(value):
    require(len(value) == 3 and all(type(x) is int and 0 <= x <= 0xFFFFFFFF
                                   for x in value), "Invalid instruction words")
    return value


def destination_swizzle(value):
    return [(value >> (3 * i)) & 7 for i in range(4)]


def relative_swizzle(value):
    return [((value >> (2 * i)) + i) & 3 for i in range(4)]


def decode_alu(value):
    a, b, c = triplet(value)
    opcode = (c >> 24) & 31
    require(opcode in (1, 2), "Unproved vector ALU opcode")
    require((a >> 15) & 1, "Expected an export ALU")
    require((a >> 26) == 50 and ((a >> 20) & 15) == 0,
            "Unproved scalar operation/write")
    require(a & 0x030040C0 == 0 and b >> 24 == 0,
            "Unproved clamp, absolute, relative, negate or predicate flag")
    require((a >> 16) & 15, "Empty vector export mask")
    require(c & 0x200000FF == 0, "Unproved third source")
    sources = []
    for bit, shift in ((31, 16), (30, 8)):
        reg = (c >> shift) & 255
        # The high bit of a temporary source also has modifier meaning.
        require(reg < 128, "Unproved source register/modifier")
        sources.append({"bank": "temporary" if (c >> bit) & 1 else "constant",
                        "register": reg,
                        "swizzle": relative_swizzle((b >> shift) & 255)})
    return {"kind": "alu", "operation": {1: "MUL", 2: "MAX"}[opcode],
            "export_register": a & 63, "vector_mask": (a >> 16) & 15,
            "scalar_opcode": 50, "scalar_mask": 0, "sources": sources,
            "clamp": False, "predicated": False}


def decode_fetch(value):
    a, b, c = triplet(value)
    opcode = a & 31
    require(opcode in (0, 1), "Unproved fetch opcode")
    require(a & ((1 << 11) | (1 << 18)) == 0, "Relative fetch register")
    require(b >> 31 == 0 and c >> 31 == 0, "Predicated fetch")
    result = {"kind": "vertex_fetch" if opcode == 0 else "texture_fetch",
              "source_register": (a >> 5) & 63,
              "destination_register": (a >> 12) & 63,
              "destination_swizzle": destination_swizzle(b & 4095)}
    require(6 not in result["destination_swizzle"], "Unproved destination selector")
    if opcode == 0:
        result.update({"fetch_constant_index": ((a >> 20) & 31) * 3 + ((a >> 25) & 3),
                       "source_component": a >> 30, "format_field": (b >> 16) & 63,
                       "stride_dwords": c & 255, "offset_field": (c >> 8) & 0x7FFFFF,
                       "runtime_declaration_patch_verified": False})
    else:
        result.update({"fetch_constant_index": (a >> 20) & 31,
                       "normalized_coordinates": not bool((a >> 25) & 1),
                       "source_components": [(a >> (26 + 2 * i)) & 3 for i in range(3)],
                       "dimension_field": (c >> 14) & 3,
                       "mag_filter": (b >> 12) & 3, "min_filter": (b >> 14) & 3,
                       "mip_filter": (b >> 16) & 3, "anisotropy": (b >> 18) & 7,
                       "arbitrary_filter": (b >> 21) & 7,
                       "volume_mag_filter": (b >> 24) & 3,
                       "volume_min_filter": (b >> 26) & 3,
                       "computed_lod": bool((b >> 28) & 1),
                       "register_lod": bool((b >> 29) & 1),
                       "register_gradients": bool(c & 1),
                       "fetch_valid_only": bool((a >> 19) & 1),
                       "sample_location": (c >> 1) & 1,
                       "lod_bias_field": (c >> 2) & 127,
                       "offset_fields": [(c >> shift) & 31 for shift in (16, 21, 26)]})
    return result


def decode_schedule(code, pair_count):
    """Only straight-line EXEC/ALLOC schedules proven in these records."""
    require(len(code) % 12 == 0 and pair_count > 0, "Malformed instruction window")
    require(pair_count < len(code) // 12, "Control-flow region consumes code")
    schedule, executed = [], []
    ended = False
    for pair in range(pair_count):
        a, b, c = words(code, pair * 12)
        for lo, hi in ((a, b & 65535), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)):
            opcode = (hi >> 12) & 15
            require(not ended or (lo == 0 and hi == 0), "Instructions after EXEC_END")
            if opcode in (1, 2):
                address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
                require(count <= 6, "EXEC sequence count exceeds field capacity")
                # Original compiler sets predicate-clean; high word0 nibble
                # contains vertex-cache hints, not execution addresses.
                require(lo & 0x8000 == 0 and hi & 0x0FFF == 0x200,
                        "Unproved EXEC flags")
                require(sequence >> (2 * count) == 0, "Unused EXEC sequence bits")
                require(address >= pair_count and address + count < len(code) // 12,
                        "EXEC overlaps CF, trailer or out-of-bounds code")
                schedule.append((opcode, address, count, sequence))
                for i in range(count):
                    flags = (sequence >> (2 * i)) & 3
                    executed.append((address + i, bool(flags & 1), bool(flags & 2)))
                ended = opcode == 2
            elif opcode == 12:
                require(lo == 0 and hi in (0xC200, 0xC400), "Unproved ALLOC shape")
                schedule.append((12, (hi >> 9) & 3))
            elif opcode == 0:
                require(lo == 0 and hi == 0, "Nonzero NOP")
                schedule.append((0,))
            else:
                raise ValueError("Unproved control flow opcode")
    require(ended and executed, "Missing EXEC_END or empty shader")
    indices = [x[0] for x in executed]
    require(len(set(indices)) == len(indices), "Repeated execution outside proved profile")
    require(sorted(indices) == list(range(pair_count, len(code) // 12 - 1)),
            "Unaccounted executable slots")
    require(words(code, len(code) - 12)[0] == 0x4E4A0000, "Unrecognized trailer")
    return schedule, executed


def inspect_record(data, name):
    require(name in PROFILES, "Unknown shader profile")
    va, expected_offset, expected_size, digest, expected_schedule = PROFILES[name]
    offset, size = words(data, 4, 2)
    require((offset, size) == (expected_offset, expected_size), "Changed record header")
    require(len(data) == offset + size, "Record truncated or has trailing bytes")
    code = data[offset:]
    schedule, executed = decode_schedule(code, len(expected_schedule) // 2)
    require(tuple(schedule) == expected_schedule, "Changed execution schedule")
    instructions = []
    for index, is_fetch, serialize in executed:
        raw = words(code, index * 12)
        decoded = decode_fetch(raw) if is_fetch else decode_alu(raw)
        instructions.append({"address": f"0x{va+offset+index*12:08X}",
                             "words": [f"{x:08X}" for x in raw],
                             "serialize": serialize, **decoded})
    actual = hashlib.sha256(data).hexdigest()
    require(actual == digest, "Changed record bytes/hash")
    return {"name": name, "record_address": f"0x{va:08X}",
            "record_size": len(data), "sha256": actual,
            "instruction_offset": offset, "instruction_size": size,
            "control_flow": schedule, "instructions": instructions,
            "trailer_executed": False}


def inspect_image(image):
    require(len(image) == IMAGE_SIZE, "Wrong original image size")
    require(hashlib.sha256(image).hexdigest() == IMAGE_SHA256, "Wrong original image hash")
    records = []
    for name, (va, offset, size, _, _) in PROFILES.items():
        start = va - BASE
        records.append(inspect_record(image[start:start+offset+size], name))
    return {"schema_version": 1, "source_sha256": IMAGE_SHA256,
            "source_size": IMAGE_SIZE, "records": records,
            "verification": {"records": 4, "executed_fetches": 4, "executed_alus": 5},
            "limits": ["Only these four pinned original records are accepted.",
                       "Embedded vertex fetch format/stride/offset require runtime declaration patching.",
                       "Sampler, texture-view, target and inherited draw state are not supplied here.",
                       "Static evidence only; no native compile or pixel identity claim."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    args = parser.parse_args(argv)
    try:
        result = inspect_image(args.image.read_bytes())
    except (OSError, ValueError) as exc:
        print(f"screen-shaders: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
