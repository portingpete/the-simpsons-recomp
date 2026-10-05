"""Bounded original-byte contract for the embedded loading texture bridge.

No guest execution, AOT generation, asset extraction, or GPU emulation. The only
write target is analysis/native-texture-bridge.json. Existing decoding work is
reused read-only; instruction words are checked against the pinned flat image.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
import extract_loading_art as loading

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_HASH = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
REPORT = ROOT / "analysis/native-texture-bridge.json"

# Extents absent from .pdata end at the independently inspected return/tail call.
LEAVES = {
    0x823F7988: 0x40,
    0x823FDEC8: 0x20, 0x823FE020: 0x54, 0x823FE078: 0x30,
    0x823FE0A8: 0xB4, 0x823FE2B0: 0x24, 0x823FF048: 0x20,
    0x82401AC8: 0x18, 0x8244E940: 0x4C,
    0x82737068: 0x24, 0x827370C0: 0x10, 0x82737160: 0x30,
}
FUNCTIONS = {
    0x823F03A0: "original standard callback installation",
    0x823F53D8: "mixed raster lock: saved dimensions and level index",
    0x823F5588: "mixed raster unlock: persistent CPU fields and SDK temporary release",
    0x823F62A0: "platform raster destroy: requires native owned-texture branch",
    0x823F65D0: "CPU format support query adapter, not resource construction",
    0x823F6E68: "raster format validation and CPU fields (reviewed type-4 path)",
    0x823F7070: "raster callback: only flags 0x384 path reviewed here",
    0x823F7DD0: "chunk header read",
    0x823F7988: "CPU four-byte endian conversion for dictionary struct",
    0x823F7F58: "find chunk and return size/version",
    0x823F90C0: "stream read (type-3 memory branch reviewed)",
    0x823F9398: "stream skip",
    0x823F94A0: "stream close",
    0x823F9598: "stream open",
    0x823FA2C8: "original extension read and plugin post-read dispatch",
    0x823FB328: "original plugin construction and failure unwind",
    0x823FB3A8: "original plugin destruction",
    0x823FD900: "texture final destruction",
    0x823FDC28: "dictionary CPU construction",
    0x823FDD58: "dictionary texture iteration",
    0x823FDE20: "texture CPU construction",
    0x823FDEC8: "texture reference decrement",
    0x823FDEF0: "texture name setter",
    0x823FDF88: "texture mask-name setter",
    0x823FE020: "dictionary insertion without reference increment",
    0x823FE078: "dictionary unlink",
    0x823FE0A8: "borrowed ASCII-case-insensitive texture lookup",
    0x823FE2B0: "texture plugin registration wrapper",
    0x823FEC80: "dictionary destruction",
    0x823FF048: "texture stream plugin registration wrapper",
    0x823FF778: "dictionary failure iteration release callback",
    0x823FF7A8: "native dictionary read and ownership transfer",
    0x82401940: "texture binding CPU cache plus forbidden SDK calls",
    0x82401AC8: "CPU stage raster lookup",
    0x82407BA8: "raster unlock dispatch wrapper",
    0x82407DC0: "raster plugin destruction and allocator wrapper",
    0x82408130: "raster allocation and plugin constructor wrapper",
    0x82408208: "raster lock dispatch (not retained for native upload)",
    0x8240A278: "native texture stream engine callback",
    0x8244E940: "CPU format table predicate",
    0x8244EA18: "SDK CPU format validation, no device allocation",
    0x82737068: "EA2F plugin copy callback",
    0x827370C0: "EA2F serialized-size callback",
    0x82737160: "EA2F plugin CPU constructor",
    0x82737190: "EA2F eight-byte stream read",
    0x827371F8: "EA2F stream write (evidence only)",
    0x82737260: "EA2F registration",
    0x82862A28: "loading dictionary open/read and borrowed frame lookup",
    0x82862B18: "loading dictionary cleanup",
}

# Pin exactly the critical calls/stores/branches used in the contract. The full
# image SHA pins all remaining words; these explicit pins make review practical.
PINS = [
    (0x82062B48, "1828014f10010000", "raster format-3 table entry and depth byte"),
    (0x8217F9A0, "6672616d653200006672616d653100", "frame2/frame1 original lookup names"),
    (0x8240A278, "7d8802a64863211d9421feb0", "engine callback entry"),
    (0x823FF8E8, "816b00b07d6903a64e800421", "E+B0 dispatch; LR=823FF8F4"),
    (0x8240A298, "4bfedcc1", "find inner struct", 0x823F7F58),
    (0x8240A2C4, "4bfeedfd", "read 0x48 header bytes", 0x823F90C0),
    (0x8240A2E8, "4bfeedd9", "read 0x10 raster header bytes", 0x823F90C0),
    (0x8240A334, "4bffddfd", "raster constructor for flags 0x384", 0x82408130),
    (0x8240A474, "48036105", "forbidden SDK texture allocation", 0x82440578),
    (0x8240A47C, "907f0000", "original resource publication X+0"),
    (0x8240A49C, "939f0018524a072e997f0008995f000a", "format/alpha/low-nibble fields"),
    (0x8240A6C4, "4bffdb45", "original lock dispatch; exclude", 0x82408208),
    (0x8240A6DC, "4bfee9e5", "read little-endian level size", 0x823F90C0),
    (0x8240A77C, "4bfee945", "read serialized level bytes", 0x823F90C0),
    (0x8240A79C, "816b0030", "direct SDK header dereference prevents ID substitution"),
    (0x8240A7D8, "48129a51", "console layout upload; exclude", 0x82534228),
    (0x8240A7E4, "4bffd3c5", "original unlock dispatch; exclude", 0x82407BA8),
    (0x823F5538, "915f002c917f0028", "lock preserves original dimensions in R+28/+2C"),
    (0x823F5574, "9b5e000b", "lock records mip zero in X+B"),
    (0x823F5604, "39600000815f0028813f002c917f0018917f0004", "unlock clears stride/pixels and restores dimensions"),
    (0x823F5680, "716b00f9997f0022", "unlock clears flags 2 and 4"),
    (0x8240A870, "4bff35b1", "texture constructor takes raster ownership", 0x823FDE20),
    (0x8240A8B4, "4bff363d", "original name setter", 0x823FDEF0),
    (0x8240A8C0, "4bff36c9", "original mask setter", 0x823FDF88),
    (0x8240A8C4, "3860000193f00000", "success then output publication"),
    (0x8240A8D4, "4bffd4ed38600000", "raster destruction on callback failure", 0x82407DC0),
    (0x823FF910, "4bffa9b9", "texture extensions before dictionary insertion", 0x823FA2C8),
    (0x823FF91C, "4182004c", "extension failure skips fresh texture insertion"),
    (0x823FF924, "4bffe6fd", "dictionary insertion", 0x823FE020),
    (0x823FF950, "4bffa979", "dictionary extensions", 0x823FA2C8),
    (0x823FF974, "4bffe3e5", "failure releases already inserted textures", 0x823FDD58),
    (0x824081BC, "4e800421", "E+58 raster platform callback"),
    (0x824081F4, "4bff3135", "original raster plugin constructors", 0x823FB328),
    (0x823FDEA4, "4bffd485", "original texture plugin constructors", 0x823FB328),
    (0x8273728C, "4bcc7025", "EA2F plugin registration", 0x823FE2B0),
    (0x827372B8, "4bcc7d91", "EA2F stream registration", 0x823FF048),
    (0x827371B8, "4bcc1f09", "EA2F original stream read", 0x823F90C0),
    (0x827371D8, "914b000081410054914b0004", "EA2F payload CPU stores"),
    (0x823FD960, "4800a461", "final texture release destroys raster", 0x82407DC0),
    (0x82407E00, "4e800421", "E+5C platform raster destroy"),
    (0x823F62DC, "4800b665", "matching stage unbind (mixed CPU/SDK)", 0x82401940),
    (0x823F6418, "4804b2f1", "forbidden SDK resource release", 0x82441708),
    (0x82862A68, "4bb96b31", "loading memory stream open", 0x823F9598),
    (0x82862A9C, "4bb9cd0d907f0004", "dictionary read and global publication", 0x823FF7A8),
    (0x82862AAC, "4bb969f5", "close temporary memory stream", 0x823F94A0),
    (0x82862ABC, "4bb9b5ed", "borrow frame1", 0x823FE0A8),
    (0x82862AD0, "4bb9b5d9", "borrow frame2", 0x823FE0A8),
    (0x82862B60, "4bb9c12139600000917f0004917ffffc917f0000", "destroy dictionary then clear globals", 0x823FEC80),
]


def hx(value):
    return f"0x{value:08X}"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def span(data, address, size):
    offset = address - BASE
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("Range outside original image")
    return data[offset:offset + size]


def branch(pc, word):
    if word >> 26 != 18:
        return None
    offset = word & 0x03FFFFFC
    if offset & 0x02000000:
        offset -= 0x04000000
    return ((offset if word & 2 else pc + offset) & 0xFFFFFFFF, bool(word & 1))


def verify_pin(data, pin):
    address, expected, label, *target = pin
    if span(data, address, len(expected) // 2) != bytes.fromhex(expected):
        raise ValueError(f"Instruction pin mismatch at {hx(address)}: {label}")
    if target and branch(address, int(expected[:8], 16)) != (target[0], True):
        raise ValueError(f"Direct call target mismatch at {hx(address)}")


def extents(data):
    if len(data) != IMAGE_SIZE or sha(data) != IMAGE_HASH:
        raise ValueError("Unsupported/modified original flat image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Bad PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    if machine != 0x1F2 or struct.unpack_from("<I", data, pe + 52)[0] != BASE:
        raise ValueError("Bad image architecture/base")
    sections = {}
    for i in range(count):
        name, size, rva = struct.unpack_from("<8sII", data, pe + 24 + opt_size + i * 40)
        sections[name.rstrip(b"\0")] = (BASE + rva, size)
    start, size = sections[b".pdata"]
    code, code_size = sections[b".text"]
    if size % 8:
        raise ValueError("Partial unwind record")
    result = {}
    for offset in range(start, start + size, 8):
        address, packed = struct.unpack(">II", span(data, offset, 8))
        if address == packed == 0:
            continue
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if address & 3 or address in result or not code <= address < address + length <= code + code_size:
            raise ValueError("Invalid unwind extent")
        result[address] = length
    return result


def disassemble(data, image, executable, address, size):
    lines = subprocess.check_output([str(executable), str(image), hx(BASE), hx(address), str(size // 4)], text=True).splitlines()
    instructions, calls = [], []
    for line in lines:
        m = re.fullmatch(r"([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+(.+?)\s*", line)
        if not m:
            raise ValueError("Unexpected disassembler output")
        pc, word = int(m[1], 16), int(m[2], 16)
        if pc != address + 4 * len(instructions) or struct.pack(">I", word) != span(data, pc, 4):
            raise ValueError("Disassembler address/word mismatch")
        instructions.append(f"{pc:08X} {word:08X} {m[3]}")
        edge = branch(pc, word)
        if edge and not address <= edge[0] < address + size:
            calls.append(dict(pc=hx(pc), target=hx(edge[0]), linked=edge[1]))
    if len(instructions) * 4 != size:
        raise ValueError("Incomplete disassembly")
    return instructions, calls


def asset_evidence(data):
    stream = span(data, BASE + loading.STREAM_OFFSET, loading.STREAM_SIZE)
    if sha(stream) != loading.STREAM_HASH:
        raise ValueError("Embedded dictionary identity mismatch")
    textures = loading.parse_dictionary(stream)
    root = list(loading.chunks(stream, 0, len(stream)))[0]
    children = list(loading.chunks(stream, root[1], root[2]))
    result = []
    for source, native in zip(textures, children[1:3]):
        chunks = list(loading.chunks(stream, native[1], native[2]))
        inner, extension = chunks
        header = source["header_offset"]
        ext_child = list(loading.chunks(stream, extension[1], extension[2]))[0]
        row = {k: v for k, v in source.items() if k != "payload"}
        row.update(native_chunk_payload_va=hx(BASE + loading.STREAM_OFFSET + native[1]),
                   native_chunk_bytes=native[2] - native[1],
                   struct_va=hx(BASE + loading.STREAM_OFFSET + header),
                   struct_bytes=inner[2] - inner[1],
                   struct_end_va=hx(BASE + loading.STREAM_OFFSET + inner[2]),
                   extension_header_va=hx(BASE + loading.STREAM_OFFSET + extension[1] - 12),
                   extension_payload_hex=stream[ext_child[1]:ext_child[2]].hex(),
                   extension_payload_be_words=[hx(w) for w in struct.unpack(">II", stream[ext_child[1]:ext_child[2]])],
                   header_hex=stream[header:header + 88].hex(),
                   serialized_payload_sha256=sha(source["payload"]))
        result.append(row)
    return dict(va=hx(BASE + loading.STREAM_OFFSET), size=loading.STREAM_SIZE,
                sha256=sha(stream), stamp=hx(loading.STAMP), textures=result)


def inspect(image, disassembler):
    data = image.read_bytes()
    pdata = extents(data)
    for pin in PINS:
        verify_pin(data, pin)
    functions = []
    for address, purpose in sorted(FUNCTIONS.items()):
        size = pdata.get(address, LEAVES.get(address))
        if size is None:
            raise ValueError(f"No verified extent for {hx(address)}")
        instructions, calls = disassemble(data, image, disassembler, address, size)
        functions.append(dict(address=hx(address), size=size, purpose=purpose,
                              extent_source="original_pdata" if address in pdata else "reviewed_leaf",
                              sha256=sha(span(data, address, size)), instructions=instructions, direct_edges=calls))
    return dict(
        schema="simpsons_native_texture_bridge_v1", image=dict(base=hx(BASE), size=len(data), sha256=sha(data)),
        tool_sha256=sha(Path(__file__).read_bytes()),
        reused_asset_parser_sha256=sha(Path(loading.__file__).read_bytes()),
        disassembler_sha256=sha(disassembler.read_bytes()),
        scope="Static embedded frame1/frame2 stream ownership; no guest execution, renderer implementation or camera/depth recovery",
        abi=dict(entry="0x8240A278", engine_offset="0xB0", caller="0x823FF8F0",
                 r3="stream", r4="writable BE32 texture output", r5="outer native chunk length; ignored by original callee",
                 success="r3=1; *r4=texture", failure="r3=0; output unchanged",
                 input_position="inner struct chunk header", output_position="immediately before texture extension chunk header"),
        ownership=dict(raster_constructor="0x82408130", texture_constructor="0x823FDE20",
                       texture_refcount_offset="0x54", texture_registry="0x82CD1DB8",
                       dictionary_registry="0x82CD1DD0", raster_registry="0x82CD1E28",
                       dictionary_insert="0x823FE020 (no addref)", lookup="0x823FE0A8 (borrowed)",
                       release="0x823FDEC8 -> 0x823FD900 -> 0x82407DC0 -> E+0x5C",
                       dictionary="0x82E071EC", frame1="0x82E071E4", frame2="0x82E071E8",
                       caveat="Original extension-failure path does not explicitly release the freshly returned texture before insertion"),
        native_cut=dict(replace_engine_callback="0x8240A278", retain_original_stream_and_dictionary=True,
                        required_native_raster_profile="type4; flags0x384 unallocated -> real BC3 resource -> flags0",
                        dynamic_raster_extension="R + BE32[0x82E3DC94]",
                        dynamic_texture_extension="T + BE32[0x82CF0600]",
                        forbidden=["0x8240A474 SDK allocation", "0x8240A79C SDK header read",
                                   "0x82408208/0x82407BA8 lock/unlock", "0x82534228 console upload",
                                   "0x823F6418 SDK release"],
                        texture_sampler="BE32[T+0x50]=0x1102; screen helper effective sampler is separate"),
        raster_fields=dict(
            after_wrapper_and_profile_callback={"R+0:BE32": "R", "R+4:BE32": 0, "R+8:BE32": 0,
                "R+C:BE32": 256, "R+10:BE32": 256, "R+14:BE32": 16,
                "R+1C:BE16": 0, "R+1E:BE16": 0, "R+20:u8": 4, "R+21:u8": 128,
                "R+22:u8": 0, "R+23:u8": 3, "X+0:BE32": 0, "X+4:BE32": 0,
                "X+8:u8": 0, "X+9:u8": 0, "X+A:u8": 0, "X+B:u8": 255,
                "X+C:BE32": 0, "X+18:BE32": 0},
            after_native_resource={"R+21:u8": 0, "R+23:u8": 3, "X+0:BE32": "checked native owned identity",
                "X+8:u8": 1, "X+9:u8": 0, "X+A:u8": 1, "X+18:BE32": "0x1A200154"},
            after_completed_upload_cpu_state={"R+4:BE32": 0, "R+18:BE32": 0, "R+22:u8": 0,
                "R+28:BE32": 256, "R+2C:BE32": 256, "X+B:u8": 0},
            native_temporary_slots="X+C surface, X+10 pitch, X+14 mapped pixels are unsupported SDK lock temporaries; native immutable service keeps them unavailable/zero, not fabricated SDK pointers or console pitch.",
            caveat="Do not zero unspecified fields; retain original raster plugin constructors. Type4/0x384 skips camera list helper 823F5DA8."),
        pins=[dict(address=hx(p[0]), evidence_hex=p[1], observation=p[2],
                   **({"call_target": hx(p[3])} if len(p) == 4 else {})) for p in PINS],
        embedded=asset_evidence(data), functions=functions,
        limits=["No runtime registry snapshot: retain original registered constructors/stream callbacks and validate dynamic offsets.",
                "Only unbound, root type-4 loading raster creation/destruction is specified; bound-stage cleanup needs CPU cache and native unbind.",
                "Other native formats, mips, cubes, palettes, subrasters and stream backends are not certified.",
                "No original SDK object layout, GPU packets, fake pointers, guest-ready result or game pixels are produced."])


class ProofTests(unittest.TestCase):
    def test_branches(self):
        self.assertEqual(branch(0x8240A474, 0x48036105), (0x82440578, True))
        self.assertEqual(branch(0x8240A870, 0x4BFF35B1), (0x823FDE20, True))
        self.assertEqual(branch(0xFFFFFFFC, 0x48000009), (4, True))
        self.assertEqual(branch(0x10, 0x4800000B), (8, True))
        self.assertIsNone(branch(0, 0x4E800421))

    def test_pins_and_bounds(self):
        data = bytes.fromhex("4800000960000000")
        verify_pin(data, (BASE, "48000009", "fixture", BASE + 8))
        with self.assertRaises(ValueError):
            verify_pin(data, (BASE, "48000005", "changed call"))
        with self.assertRaises(ValueError):
            verify_pin(data, (BASE, "48000009", "wrong target", BASE + 4))
        for address, size in [(BASE - 1, 1), (BASE + 7, 2), (BASE, -1)]:
            with self.assertRaises(ValueError):
                span(data, address, size)

    def test_identity_rejection(self):
        with self.assertRaises(ValueError):
            extents(bytes(64))

    def test_real_loading_profile(self):
        data = (ROOT / "analysis/simpsons.pe").read_bytes()
        extents(data)
        for pin in PINS:
            verify_pin(data, pin)
        assets = asset_evidence(data)
        # Original serialization order differs from the later lookup order.
        self.assertEqual([x["name"] for x in assets["textures"]], ["frame2", "frame1"])
        for row in assets["textures"]:
            self.assertEqual(row["struct_bytes"], 0x1005C)
            self.assertEqual(row["struct_end_va"], row["extension_header_va"])
        altered = bytearray(data)
        altered[0x40A474] ^= 1
        with self.assertRaises(ValueError):
            extents(altered)
        stream = bytearray(span(data, BASE + loading.STREAM_OFFSET, loading.STREAM_SIZE))
        struct.pack_into("<I", stream, 4, len(stream))
        with self.assertRaises(ValueError):
            loading.parse_dictionary(stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--disassembler", type=Path, default=ROOT / "build/generator-ninja/SimpsonsDisasm.exe")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--report", action="store_true", help="write only analysis/native-texture-bridge.json")
    mode.add_argument("--verify-report", action="store_true", help="compare deterministic report without writes")
    mode.add_argument("--self-test", action="store_true", help="run bounded proof/negative-input fixtures without writes")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ProofTests))
        return 0 if result.wasSuccessful() else 1
    report = inspect(args.image, args.disassembler)
    encoded = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    if args.report:
        if REPORT.resolve() != ROOT.resolve() / "analysis/native-texture-bridge.json":
            raise ValueError("Report target escapes owned output")
        REPORT.write_text(encoded, encoding="utf-8", newline="\n")
    elif args.verify_report:
        if REPORT.read_text(encoding="utf-8") != encoded:
            raise ValueError("Report differs from current verified inputs/tool")
    print(json.dumps(dict(functions=len(report["functions"]), pins=len(PINS),
                          instruction_words=sum(f["size"] // 4 for f in report["functions"]),
                          textures=[r["name"] for r in report["embedded"]["textures"]],
                          report="written" if args.report else "verified" if args.verify_report else "not written")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
