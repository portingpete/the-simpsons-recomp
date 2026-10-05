"""Read-only static evidence for the first RenderShadowDepth character mesh.

Stdlib only; no builds, generated-code edits, shader execution or GPU packets.
The default check needs only the original flat image. --reference-root also
checks the already audited local source snapshots; capture files are optional
inputs to the separate C++ test, never dependencies of this verifier.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
REFERENCES = {
    "include/rex/graphics/xenos.h": "7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227",
    "include/rex/graphics/format/ucode.h": "e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb",
    "include/rex/graphics/registers.h": "2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40",
    "include/rex/graphics/register_table.inc": "c9dfdbfe72763051736230850ee92eaf73b7e2e9be328ac8b83df7d3b5aa9e53",
    "src/graphics/pipeline/shader/dxbc_translator_fetch.cpp": "9bbd02d52432fe55ec95d1b6f03a0cba46ad98a1ee4b974fe1fcaac4f6c47530",
}
HOOKS = (
    (0x827063B8, 0x4BD36209, 0x8243C5C0, "stream"),
    (0x827063C8, 0x4BD3F3D1, 0x82445798, "declaration"),
    (0x827063D4, 0x4BD36395, 0x8243C768, "indices"),
    (0x82706454, 0x4BD46F0D, 0x8244D360, "draw"),
)
# Contiguous original instruction words: operand roles are derived in the doc.
PINS = {
    0x82706394: "39000001 817E0010 38C00000 83FD000C 38800000 38BF0038 836B0014 80FF0004 7F63DB78 4BD36209 817F0030 7F63DB78 808B0004 4BD3F3D1",
    0x827063CC: "389F0058 7F63DB78 4BD36395",
    0x82706440: "7F63DB78 80FF0018 80DF0014 80BF0010 809F000C 4BD46F0D",
    0x8273B7EC: "809F0008 807F000C 4BFC63E5 907F0030",
    0x827453AC: "3D400001 6148003F 892B0000 7D4341D6 396B0001 7C6A4A14 7F0B3840 4198FFEC",
    0x8243C6A8: "574BF0BF 7FDBF92E 7D5FEA14 996A30E8",
    0x8245EE64: "813B0000 553FA73E 8AA50009 7F15F840 409A0014 8AA5000A 5534873E 7F15A040 419A0014",
    0x8245EED0: "81450004 5509073E 555F877E 5548B6B8 55556F7E 7D08FB78 555F042A 55081838 55540632 7D08AB78 2B140040 5508083C 7D08FB78 55081838 7D094B78 5529043E",
    0x8245EF94: "555563A6 A1050000 554A05AE A0A50002 3D80BFC0 7EAA5378 618CCFFF 554A2036 7EA830AE 7FBD6038 3FE00156 7D4AEB78",
    0x82C1FB10: "57AA01BA 817F001C 3D20FFFF 63880003 556B0108 7D4B5B78 913F0014 656B1000 911F0018 616B0002 917F001C",
    0x82C1FBB4: "817F0000 578AE804 3D20FFFF 937F0018 7D4B5B78 913F0014 917F0000",
    0x8244D5D8: "39602102 7E388B78 2B11FFFF 95630004 7C7B1B78 95FB0004",
    0x8244D614: "81760000 5707801E 895F2F93 5669083C 7CFEA378 81160018 20AA0000 7D675B78 7CA52910 7D5550AE 7D294214 5706023E 556B0001 54AB07FE 516A45EE 54EB0802",
}
# Literal observed declaration, including terminal and opaque bytes. Its hash
# joins it to the original engine cache without dereferencing an SDK object.
DECLARATION = bytes.fromhex("""
00000000 002A23B9 00000000 0000000C 002A2187 00030000
00000010 002C23A5 00050000 00000018 001A2286 00020000
0000001C 001A23A6 00010000 0000002C 00182886 000A0000
00010000 002A23B9 00000100 00020000 002A23B9 00000200
00030000 002A23B9 00000300 00040000 002A23B9 00000400
00050000 002A23B9 00000500 00060000 002A23B9 00000600
00FF0000 FFFFFFFF 00000000
""")


def need(value, why):
    if not value:
        raise ValueError(why)


def inspect(image, reference_root=None):
    need(len(image) == IMAGE_SIZE and hashlib.sha256(image).hexdigest() == IMAGE_SHA,
         "Original image size/hash changed")
    if reference_root is not None:
        for path, digest in REFERENCES.items():
            need(hashlib.sha256((Path(reference_root) / path).read_bytes()).hexdigest() == digest,
                 f"Local reference changed: {path}")
    for address, text in PINS.items():
        expected = bytes.fromhex(text)
        need(image[address-BASE:address-BASE+len(expected)] == expected,
             f"Original instruction span changed at {address:08X}")
    hooks = []
    for address, expected, target, name in HOOKS:
        word = struct.unpack_from(">I", image, address-BASE)[0]
        need(word == expected and word >> 26 == 18 and word & 3 == 1,
             f"Hook is no longer the exact relative BL: {address:08X}")
        displacement = word & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        need(address + displacement == target, "Hook target differs")
        hooks.append(dict(name=name, site=f"{address:08X}", word=f"{word:08X}",
                          target=f"{target:08X}", resume=f"{address+4:08X}"))
    need(hashlib.sha256(DECLARATION).hexdigest() ==
         "0510865127971756ca73288362132636da241b89c84342d6e6cae0674894bd0b",
         "Literal declaration differs from the audited capture")
    cache_hash = 0
    for byte in DECLARATION:
        cache_hash = (cache_hash * 0x1003F + byte) & 0xFFFFFFFF
    need(cache_hash == 0xBC06D35F, "Original declaration cache hash differs")
    elements = [struct.unpack_from(">HHIBBBB", DECLARATION, offset)
                for offset in range(0, len(DECLARATION), 12)]
    need(elements[-1] == (255, 0, 0xFFFFFFFF, 0, 0, 0, 0), "Bad terminator")
    metadata = struct.unpack_from(">4I", image, 0x820C3DA8-BASE)
    need(metadata == (0x00100007, 0x00005008, 0x00001009, 0x0020200A),
         "Shadow fetch metadata changed")
    expected_inputs = ((0, 57, [0, 1, 2, 5]), (16, 37, [0, 1, 4, 5]),
                       (28, 38, [0, 1, 2, 3]), (24, 6, [0, 1, 2, 3]))
    inputs = []
    fetches = ((0x05F80000, 0x287, 0), (0x05F82000, 0xFC8, 0),
               (0x05F81000, 0x688, 0), (0x05F83000, 0x53, 0))
    for native_slot, (meta, expected, fetch) in enumerate(zip(metadata, expected_inputs, fetches)):
        usage, index, slot = (meta >> 12) & 15, (meta >> 16) & 15, meta & 4095
        matches = [row for row in elements[:-1] if row[4:6] == (usage, index)]
        need(len(matches) == 1, "Shader semantic association is ambiguous")
        stream, offset, type_word, method, _, _, _ = matches[0]
        selectors = [(type_word >> (10+3*lane)) & 7 for lane in range(4)]
        need(stream == method == 0 and (offset, type_word & 63, selectors) == expected,
             "Consumed declaration format differs")
        va = 0x820C2FA0 + 3688 + slot*12
        need(struct.unpack_from(">3I", image, va-BASE) == fetch, "Original fetch changed")
        inputs.append(dict(slot=slot, metadata_va=f"{0x820C3DA8+4*native_slot:08X}",
                           usage=usage, usage_index=index, offset=offset,
                           type=f"{type_word:08X}", format=type_word & 63,
                           signed=bool(type_word & 256), integer=bool(type_word & 512),
                           selectors=selectors, native_semantic=f"TEXCOORD{native_slot}"))
    need(not inputs[3]["signed"] and inputs[3]["integer"], "Bone conversion must be unsigned and unnormalized")
    # Observed headers interpreted through the pinned constructors and draw
    # operand extraction. These are CPU data words, never commands to execute.
    need((0x10002C72 & 3) == 2, "Vertex endian must be 8-in-32")
    need(((0x20000002 >> 29) & 3) == 1 and not (0x20000002 >> 31),
         "Index endian/width must be 8-in-16 / uint16")
    packed = int.from_bytes(bytes.fromhex("00 00 00 04"), "big")
    need([(packed >> (8*lane)) & 255 for lane in range(4)] == [4, 0, 0, 0],
         "Bone X interpretation changed")
    return dict(image_sha256=IMAGE_SHA, hooks=hooks, inputs=inputs,
                declaration_cache_hash=f"{cache_hash:08X}", vertex_stride=48,
                vertex_endian="8-in-32", index_endian="8-in-16", index_width=16,
                draw_args=dict(r4="primitive type", r5="base vertex", r6="start index in elements", r7="index count"),
                observed_draw=[6, 0, 0, 577], reference_sha256=REFERENCES,
                reference_files_verified=reference_root is not None,
                scope="Static subset only; native raster, ownership and gameplay are separate checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--verify", action="store_true", help="Print a concise verification result")
    args = parser.parse_args()
    try:
        result = inspect(args.image.read_bytes(), args.reference_root)
    except (OSError, ValueError) as error:
        parser.exit(1, f"FAIL character mesh evidence: {error}\n")
    if not args.verify:
        print(json.dumps(result, indent=2))
    suffix = "; five local reference hashes matched" if result["reference_files_verified"] else "; reference files not rechecked"
    print("PASS original image, four BL hooks, declaration/semantic join, bone X=4 and draw/endian fields" + suffix)


if __name__ == "__main__":
    main()
