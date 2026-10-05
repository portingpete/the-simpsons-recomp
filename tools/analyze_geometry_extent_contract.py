"""Read-only original owned-geometry byte extent and signed R16 source proof.

This establishes producer/header transport bounds. Native execution and
mission encounters are separate evidence; wrapped header aliases are unproved.
"""
import argparse
import json
import struct
from pathlib import Path

from analyze_submesh_base_contract import (
    BASE, IMAGE_SHA, ROOT, safe_selected_indices, verify as verify_base,
)

PINS = {
    # Asset callback validates its type and passes loaded metadata unchanged.
    0x826FED8C: "3D403C43 7C6B1B78 614AA23D 7F045040 409A0028 7CC33378 808B0004 4803C9B9",
    # Original geometry setup supplies extent and owned addresses independently.
    0x8273B78C: "83FD000C 39600000 38C00000 3BDF0038 38A00000 38800000 7FC7F378 807F0000 917F0034 484E4301 7FC3F378 809F0010 484E572D 3BDF0058 38E00000 80BF0018 38C00000 807F0014 38800000 7FC8F378 484E436D 7FC3F378 809F001C 484E5701 809F0008 807F000C 4BFC63E5 907F0030",
    # Morph geometry/target table uses the same byte constructor and publisher.
    # r30+8 supplies each vertex count; multiplication by12 is independent
    # of the base geometry stride. The destination is the selected32-byte header.
    0x8270DB24: "815E0018 38C00000 817E0008 38A00000 38800004 814A0008 7D6BE82E 7D4AE82E 7CEAF82E 556A083C 7D6B5214 5563103A 48511F5D 817E0018 815E000C 816B0008 7D4AE82E 7D6BE82E 7C8AF82E 7C6BF82E 48513375",
    # Vertex resource initializer keeps the complete r3 byte extent in r29.
    0x82C1FABC: "7CFF3B78 7C7D1B78 7C9E2378",
    # The byte field masks aligned bits6..29 (03FFFFFC), not16-bit count.
    0x82C1FB10: "57AA01BA 817F001C 3D20FFFF 63880003 556B0108 7D4B5B78 913F0014 656B1000 911F0018 616B0002 917F001C",
    # Original index header stores its full byte extent separately from format.
    0x82C1FB54: "7D1F4378 7C7E1B78 7C9D2378 7CBC2B78",
    0x82C1FB78: "39600001 39400002 93DF001C 917F0004 915F0000",
    0x82C1FBB4: "817F0000 578AE804 3D20FFFF 937F0018 7D4B5B78 913F0014 917F0000",
    # Publish guest data into the inline header, retaining its low type bits.
    0x82C20F30: "817F0018 556A003A 7D4AF214 516A07BE 915F0018",
    0x82C20F98: "817F0018 7D6BF214 917F0018",
    # Paired original allocator and retirement vtable calls.
    0x8269BFA0: "7FA5EB78 7FC4F378 816B0000 7D6903A6 4E800421",
    0x8269BF40: "81630000 38A00000 7FC4F378 816B0004 7D6903A6 4E800421",
}
MAX_BYTES = 0x03FFFFFC


def verify(image, check_hash=True):
    verify_base(image, check_hash)
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original geometry extent instruction changed at {address:08X}")


def mask_from_rlwinm(word):
    if word >> 26 != 21 or ((word >> 11) & 31):
        raise ValueError("Original extent field is not an unshifted rlwinm")
    begin, end = (word >> 6) & 31, (word >> 1) & 31
    if begin > end:
        raise ValueError("Wrapped original extent bit mask is not qualified")
    return sum(1 << (31-bit) for bit in range(begin, end+1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    instruction = struct.unpack_from(">I", image, 0x82C1FB10-BASE)[0]
    mask = mask_from_rlwinm(instruction)
    if mask != MAX_BYTES:
        raise ValueError("Original resource byte-field extent differs")
    stride_checks = 0
    for stride in range(4, 1021, 4):
        byte_extent = 65536 * stride
        if not 0 < byte_extent <= mask or byte_extent & ~mask:
            raise ValueError("Representable65536 owner did not round-trip its original header")
        largest = mask // stride * stride
        if largest & ~mask or largest + stride <= mask:
            raise ValueError("Original full-record byte bound differs")
        stride_checks += 1
    indices = [0, 1, 2, 0xFFFF, 2, 1, 3]
    effective = safe_selected_indices(indices, 0, len(indices), 65532, 65536)
    if effective != [65532, 65533, 65534, None, 65534, 65533, 65535]:
        raise ValueError("Large original owned selected range differs")
    # All addresses are below2^24; this proves no wrapped24-bit alias policy.
    if max(i for i in effective if i is not None) >= 1 << 24:
        raise ValueError("Large fixture acquired an unproved hardware wrap")
    # Independent selected-fetch inequalities, including unused partial tails
    # and the last record's absent padding. No divisibility assertion applies.
    span_checks = 0
    for end in (12, 20, 28, 44, 48):
        for size, expected in ((65536*64, 65536), (65536*64+4, 65536),
                               (65535*64+end, 65536), (65535*64+end-4, 65535)):
            count = 1+(size-end)//64 if size >= end else 0
            if count != expected or (count-1)*64+end > size or count*64+end <= size:
                raise ValueError("Consumed original byte-owner boundary differs")
            span_checks += 1
    if 65536*28+12 > mask or (65536*28+12) & ~mask:
        raise ValueError("Original partial-tail fixture byte extent did not round-trip its header")
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
                raise ValueError("Changed original geometry producer was admitted")
    print(json.dumps({
        "image_sha256": IMAGE_SHA,
        "vertex_resource_byte_mask": f"{mask:08X}",
        "vertex_extent_contract": "Nonzero aligned byte extent4..03FFFFFC; upper-bit aliases remain unqualified. Original826FED80/8273B760 do not require the byte extent to be divisible by stride.",
        "fetched_record_contract": "For a consumed attribute end E and independent stride S, every decoded vertex must satisfy vertex*S+E<=ownedBytes. Count1+(ownedBytes-E)//S when ownedBytes>=E. Unused padding/partial tails do not justify asset rejection.",
        "index_header_extent_contract": "Original82C1FB80 stores the full32-bit byte extent at resource+1C; R16 specifies element width, not vertex ownership.",
        "morph_header_extent_contract": "Original8270DAE8/8270DB24..74 builds the selected float3 table header with12*vertexCount through the same82C1FAB0/82C20EE8. Its unaliased aligned byte field is also03FFFFFC; the native consumed span may use a prefix of that owner.",
        "original_setup": "8273B760 initializes inline vertex/index headers, publishes owned data, and constructs the cached declaration at82701BD8.",
        "large_owner": {"vertices": 65536, "stride": 28, "vertex_bytes": "001C0000", "base_vertex": 65532, "indices": indices, "effective": effective},
        "all_stride_owner_checks_passed": stride_checks,
        "consumed_span_checks_passed": span_checks,
        "mutation_checks_passed": mutations,
        "native_scope": "CPU predicates check owned byte extents and UINT ByteWidth/2^27 buffer-element representability. D3D11 CreateBuffer remains responsible for actual per-device allocation limits. Independent original large/partial-tail owner and CPU decoder cases passed in producer-ranges-frontier-tests.xml; this source verifier is not a native execution or current-source/executable identity receipt.",
        "cache_scope": "Existing512-entry/64MiB exact-content cache residency limits may evict or bypass entries; they must not become asset-admission whitelists.",
    }, indent=2))


if __name__ == "__main__":
    main()
