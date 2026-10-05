"""Read-only proof of original submesh base-offset transport.

This pins original producer instructions and exercises their load/copy/store
dataflow. It establishes no mission encounter or native rendering support.
The full signed offset word is transported; vertex ownership still determines
which offsets are safe for a particular selected R16 index range.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
PINS = {
    # r7=count, r6=start, r5=base, r4=primitive; submesh rows are 36 bytes.
    0x827013A0: "80FF0018 80DF0014 80BF0010 809F000C 4BD4BFB1",
    0x827015B0: "80FF0018 80DF0014 80BF0010 809F000C 4BD4BDA1",
    0x827018B0: "80FE0018 80DE0014 80BE0010 809E000C 4BD4BAA1",
    0x82706444: "80FF0018 80DF0014 80BF0010 809F000C 4BD46F0D",
    0x826FF574: "80EB0018 80CB0014 80AB0010 808B000C 4BD4DDDD",
    0x82700460: "80FF0018 80DF0014 80BF0010 809F000C 4BD4CEF1",
    # SDK preserves the base separately from start and count.
    0x8244D370: "7C902378 7CAF2B78 7CD33378 7CF13B78",
    # Count is compared with 65535; the base word is written unchanged.
    0x8244D5D8: "39602102 7E388B78 2B11FFFF 95630004 7C7B1B78 95FB0004",
    # R16 address is descriptor.data + (startIndex << 1).
    0x8244D614: "81760000 5707801E 895F2F93 5669083C 7CFEA378 81160018",
    0x8244D63C: "7D294214",
}
PRODUCERS = {
    "rigid_sky_immediate": 0x827013A0,
    "rigid_sky_recorded": 0x827015B0,
    "skinned": 0x827018B0,
    "character_shadow": 0x82706444,
    "static_zprepass_mono": 0x826FF574,
    "skinned_mono": 0x82700460,
}


def verify(image, check_hash=True):
    if check_hash and hashlib.sha256(image).hexdigest() != IMAGE_SHA:
        raise ValueError("Original image hash changed")
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original submesh instruction changed at {address:08X}")


def loaded_arguments(image, address, row):
    """Evaluate only the four pinned original lwz instructions."""
    result = {}
    for lane in range(4):
        word = int.from_bytes(image[address-BASE+4*lane:address-BASE+4*lane+4], "big")
        if word >> 26 != 32:
            raise ValueError("Original submesh argument is not loaded by lwz")
        target = (word >> 21) & 31
        displacement = word & 0xFFFF
        result[target] = struct.unpack_from(">I", row, displacement)[0]
    return result


def safe_selected_indices(indices, start, count, base_vertex, vertex_count):
    """Proposed native ownership bound, separate from the original transport."""
    if not -(1 << 31) <= base_vertex < (1 << 31):
        raise ValueError("Base offset is outside the native signed argument")
    if not indices or not vertex_count or start < 0 or count < 0 or start + count > len(indices):
        raise ValueError("Selected index extent exceeds owned storage")
    effective = []
    for index in indices[start:start+count]:
        if not 0 <= index <= 0xFFFF:
            raise ValueError("Input is not an R16 index")
        if index == 0xFFFF:
            effective.append(None)
            continue
        addressed = index + base_vertex
        if not 0 <= addressed < vertex_count:
            raise ValueError("Effective index exceeds owned vertices")
        effective.append(addressed)
    return effective


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    offsets = [-(1 << 31), -2, -1, 0, 1, 2, 65535, (1 << 31)-1]
    transport_checks = 0
    for address in PRODUCERS.values():
        for offset in offsets:
            row = bytearray(36)
            struct.pack_into(">IiII", row, 12, 6, offset, 1, 7)
            actual = loaded_arguments(image, address, row)
            expected = {4: 6, 5: offset & 0xFFFFFFFF, 6: 1, 7: 7}
            if actual != expected:
                raise ValueError("Original producer altered submesh arguments")
            # Pinned mr r15,r5 followed by stwu r15,4(r27) transports this word.
            retained = actual[5]
            if struct.unpack(">I", struct.pack(">I", retained))[0] != expected[5]:
                raise ValueError("SDK base-offset word did not round-trip")
            transport_checks += 1
    # Unselected words do not belong to this draw's effective vertex range.
    fixture = [65534, 0, 1, 2, 65535, 2, 1, 3, 65534]
    if safe_selected_indices(fixture, 1, 7, 2, 6) != [2, 3, 4, None, 4, 3, 5]:
        raise ValueError("Positive offset/restart fixture differs")
    # Negative offsets require subtracting before validating a raw R16 word.
    if safe_selected_indices([2, 3, 4], 0, 3, -2, 3) != [0, 1, 2]:
        raise ValueError("Signed effective-range ownership model differs")
    malformed = [([0, 1, 2], 0, 3, -1, 3),
                 ([0, 1, 2], 0, 3, 2, 4),
                 ([0, 1, 2], 1, 3, 0, 3),
                 ([0, 1, 2], 0, 3, (1 << 31)-1, 3),
                 ([0, 1, 2], 0, 3, -(1 << 31), 3)]
    for case in malformed:
        try:
            safe_selected_indices(*case)
        except ValueError:
            pass
        else:
            raise ValueError("Malformed effective index model was admitted")
    mutations = 0
    for address, words in PINS.items():
        for at in range(len(bytes.fromhex(words))):
            changed = bytearray(image)
            changed[address-BASE+at] ^= 1
            try:
                verify(changed, check_hash=False)
            except ValueError:
                mutations += 1
            else:
                raise ValueError("Changed original submesh contract accepted")
    print(json.dumps({"image_sha256": IMAGE_SHA,
        "producer_sites": {name: f"{address:08X}" for name, address in PRODUCERS.items()},
        "submesh_fields": {"primitive": 12, "base_offset": 16, "start_index": 20, "index_count": 24},
        "sdk_base_capture": "8244D374 mr r15,r5",
        "sdk_base_publication": "8244D5EC stwu r15,4(r27)",
        "sdk_count_split_site": "8244D5E0 compares r17 (indexCount), not baseOffset or vertexCount",
        "transport_checks_passed": transport_checks,
        "mutation_checks_passed": mutations,
        "positive_regression": {"base_vertex": 2, "vertex_count": 6, "start_index": 1, "index_count": 7,
                                "indices": fixture, "effective": [2, 3, 4, None, 4, 3, 5]},
        "ownership_model_negative_checks": len(malformed),
        "scope": "Original producer word transport plus proposed CPU ownership model; native create/use/release and signed hardware interpretation remain to be qualified."}, indent=2))


if __name__ == "__main__":
    main()
