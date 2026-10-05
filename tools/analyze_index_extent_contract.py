"""Pin the original independent R16 owner extent, selected fetch and publication.

Unused source bytes are ownership, not draw count. Native limits and native
create/use/release evidence must be established separately from these pins.
"""
import argparse
import json
from pathlib import Path

from analyze_geometry_extent_contract import BASE, IMAGE_SHA, ROOT, verify as verify_geometry
from analyze_submesh_base_contract import safe_selected_indices

PINS = {
    # Complete original constructor: r3 bytes -> r30 -> header+1C unchanged.
    # Conditional branches test r29 usage, not bytes; r28 is the format.
    0x82C1FB48: "7D8802A6 4BE1C879 9421FF80 7D1F4378 7C7E1B78 7C9D2378 7CBC2B78 38A00020 38800000 7FE3FB78 7CFB3B78 4BE1C8CD 39600001 39400002 93DF001C 917F0004 915F0000 57A9077B 41820010 3D600020 616B0002 917F0000 57AB05AD 41820010 817F0000 656B0040 917F0000 817F0000 578AE804 3D20FFFF 937F0018 7D4B5B78 913F0014 917F0000 38210080 4BE1C840",
    # Binding publishes the header itself. Extent is not reconstructed from
    # a later submesh count or forced to an aligned/even byte whitelist.
    0x8243C7EC: "93BF308C 38210080 485FFC28",
    # Count-only65535 splitting precedes R16 start*2 + actual header.data.
    # The small-count packet path does not load the independent header+1C.
    0x8244D5D8: "39602102 7E388B78 2B11FFFF 95630004 7C7B1B78 95FB0004 40990024 560B1838 3D400000 614AFFFF 7D6B902E 7D4A5B96 0CCB0000 554A003C 7F0A59D6 81760000 5707801E 895F2F93 5669083C 7CFEA378 81160018 20AA0000 7D675B78 7CA52910 7D5550AE 7D294214 5706023E 556B0001 54AB07FE 516A45EE 54EB0802 555A05FE 552A653E 552900FE 394A0200 575A06AE 554A04E6 7CDD5B78 7F8A4A14 4182002C 566A103A 57090A3C 7D4A4214 7D3D5B78 554B653E 554A00FE 396B0200 63DE0800 556B04E6 7F8B5214 897F2ABC 556B07FF 40820024 7EEBBB78 957B0004 975B0004 97DB0004 979B0004 97BB0004",
}


def verify(image, check_hash=True):
    verify_geometry(image, check_hash)
    for address, words in PINS.items():
        expected = bytes.fromhex(words)
        if image[address-BASE:address-BASE+len(expected)] != expected:
            raise ValueError(f"Original index owner instruction changed at {address:08X}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    args = parser.parse_args()
    image = args.image.read_bytes()
    verify(image)
    words = [0, 1, 2, 0xFFFF, 2, 1, 3]
    boundaries = []
    for size in (14, 15, 0x01000001, 0x01000002, 0x04000002, 0xFFFFFFFF):
        complete = size // 2
        # Keep the original selected count7; this establishes no new large
        # chunking, primitive parity or wrapped source-address contract.
        selected = safe_selected_indices(words, 0, 7, 65532, 65536)
        if complete < 7 or 2*7 > size or selected[-1] != 65535:
            raise ValueError("Complete original selected R16 prefix exceeds its byte owner")
        if 2*complete > size or 2*(complete+1) <= size:
            raise ValueError("Unused source tail acquired another complete R16 index")
        boundaries.append({"bytes": f"{size:08X}", "complete_words": complete, "unused_tail": size % 2})
    try:
        safe_selected_indices(words[:-1], 0, 7, 65532, 65536)
    except ValueError:
        pass
    else:
        raise ValueError("Truncated selected R16 prefix was admitted")
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
                raise ValueError("Changed original index owner producer was admitted")
    print(json.dumps({
        "image_sha256": IMAGE_SHA,
        "original_extent": "8273B7C8..DC passes format from geometry+18, full byte extent from geometry+14 and inline header geometry+58 independently.82C1FB58 keeps bytes in r30;82C1FB80 stores all32 bits at header+1C. No parity or16MiB condition exists in the complete pinned constructor.",
        "original_fetch": "8243C7EC binds the header. Small-count8244D5D8..D6C0 loads header type/data, adds2*startIndex for R16 and emits selected count;65535 comparison controls count splitting, not owner size. These regressions retain count7 and do not broaden chunk/wrap contracts.",
        "selected_safety": "R16 index i consumes bytes[2*i,2*i+2). The checked native view has floor(ownerBytes/2) words, requires start+count<=that view, skips restartFFFF before signed base and bounds every effective vertex. An unused odd owner byte is preserved in original source/cache snapshots.",
        "native_capacity": {"buffer_element_exponent": 27, "byte_width_type": "UINT", "installed_sdk_constant": "D3D11_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP", "old_unjustified_index_count": "02000000", "new_capacity_checks": "2^27 complete buffer elements and UINT ByteWidth; actualCreateBuffer must succeed before publication", "primary_source": "https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-limits"},
        "boundary_cases": boundaries,
        "mutation_checks_passed": mutations,
        "native_evidence": "Independent original allocator/header/create/selected7-worddraw/pairedCPUrelease tests for01000002,01000001 and04000002 passed in index-mono-frontier-tests.xml. This source verifier is not a native execution or current-source/executable identity receipt. Full32-bit source admission alone does not establish native whole-buffer support beyond real D3D11 capacity or actual device availability.",
    }, indent=2))


if __name__ == "__main__":
    main()
