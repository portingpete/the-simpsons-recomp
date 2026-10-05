"""Pinned, read-only post-start evidence; no guest or command-stream execution.

Semantic labels are reviewed observations anchored to original instructions,
not inferred symbols. The report includes byte-checked disassembly and bounded
searches, not an exhaustive indirect call graph or a proof of absent aliases.
Only analysis/native-poststart-integration.json is a permitted output file.
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

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
EXPORT_SHA = "efe1609d2609007e38a905ea8a18ce68228d15610d2bc85d08a6e832fe224950"
REPORT = ROOT / "analysis/native-poststart-integration.json"
REFERENCE = Path("K:/Simpsons/RexGlueCurrent/src/kernel/xboxkrnl/export_table.inc")

# Reviewed leaf extents, ending in BLR or an unconditional tail branch. These
# are explicitly distinguished from .pdata extents; no nearest-entry guessing.
LEAVES = {
    0x823C6EB0: 0xC, 0x824338F8: 0x10, 0x82433908: 0xC,
    0x8245DA50: 0xE0, 0x826B78F0: 0x10, 0x82718D48: 0x70,
    0x82718DB8: 0x1C, 0x82723658: 0xC, 0x82723968: 0xC,
    0x82875A30: 0xC, 0x8287B5B8: 8,
}
FUNCTIONS = sorted(set(LEAVES) | {
    0x8238E880, 0x8238EB00, 0x823EDD38, 0x823EE1A8, 0x823EE8F8,
    0x824337F8, 0x824520F0, 0x824565A8, 0x82456658, 0x82457CC8,
    0x82457EE8, 0x82458080, 0x82458260, 0x82460D38, 0x82460DC0,
    0x82461500, 0x824671B0, 0x8268DDA0, 0x8268DF90, 0x8268E138,
    0x8268E510, 0x8268E6E8, 0x8268E7F0, 0x826B08B0, 0x826B0BB0,
    0x826B0DF8, 0x827142D8, 0x82718DD8, 0x82723670, 0x82723978,
    0x82723C80, 0x82723D80, 0x82724038, 0x8273E9F8, 0x8273EA90,
    0x8273EBA0, 0x82867A48, 0x82867C18, 0x82875BF0, 0x82875E78,
    0x8287B4F8,
})
IMPORTS = {
    0x82CC2A44: (0xBA, "MmAllocatePhysicalMemoryEx"),
    0x82CC2A64: (0xC7, "MmSetAddressProtect"),
    0x82CC2A74: (0xBD, "MmFreePhysicalMemory"),
    0x82CC2CE4: (0x1DF, "KiApcNormalRoutineNop"),
    0x82CC2CF4: (0x1B6, "VdEnableRingBufferRPtrWriteBack"),
    0x82CC2D04: (0x1C3, "VdInitializeRingBuffer"),
    0x82CC2D14: (0xBE, "MmGetPhysicalAddress"),
    0x82CC2D24: (0x1D9, "VdSetSystemCommandBufferGpuIdentifierAddress"),
}


def hx(n):
    return f"0x{n:08X}"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def take(b, offset, size):
    if offset < 0 or size < 0 or offset > len(b) or size > len(b) - offset:
        raise ValueError("Out-of-image range")
    return b[offset:offset + size]


def span(b, va, size):
    return take(b, va - BASE, size)


def word(b, va):
    if va & 3:
        raise ValueError("Unaligned word")
    return struct.unpack(">I", span(b, va, 4))[0]


def validate_identity(b):
    if len(b) != IMAGE_SIZE or sha(b) != IMAGE_SHA:
        raise ValueError("Unsupported or modified original image")


def layout(b):
    """Validate the flat PE and original BE .pdata records, independently of hash."""
    if take(b, 0, 2) != b"MZ":
        raise ValueError("Invalid DOS signature")
    pe = struct.unpack("<I", take(b, 0x3C, 4))[0]
    if take(b, pe, 4) != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, count = struct.unpack("<HH", take(b, pe + 4, 4))
    opt = struct.unpack("<H", take(b, pe + 20, 2))[0]
    if machine != 0x1F2 or not 1 <= count <= 96 or opt < 32:
        raise ValueError("Invalid architecture or header bounds")
    optional = take(b, pe + 24, opt)
    if struct.unpack_from("<H", optional)[0] != 0x10B or struct.unpack_from("<I", optional, 28)[0] != BASE:
        raise ValueError("Unexpected PE format/base")
    sections = {}
    for i in range(count):
        name, size, rva = struct.unpack("<8sII", take(b, pe + 24 + opt + 40 * i, 16))
        name = name.rstrip(b"\0").decode("ascii")
        if not name or name in sections or not size:
            raise ValueError("Invalid/duplicate section")
        if rva + size > 0x100000000 - BASE:
            raise ValueError("Section address overflow")
        sections[name] = (BASE + rva, size)
    if not {".text", ".pdata"} <= sections.keys():
        raise ValueError("Missing code/unwind section")
    pa, pn = sections[".pdata"]
    ta, tn = sections[".text"]
    # This pinned derived flat image does not include the entire declared
    # relocation tail. Validate every range we read, not unconsumed tail bytes.
    span(b, pa, pn)
    span(b, ta, tn)
    if pn % 8:
        raise ValueError("Partial unwind record")
    extents = {}
    for pos in range(pa, pa + pn, 8):
        start, packed = struct.unpack(">II", span(b, pos, 8))
        if start == packed == 0:
            continue
        size = ((packed >> 8) & 0x3FFFFF) * 4
        if start & 3 or start in extents or not size or not ta <= start < start + size <= ta + tn:
            raise ValueError("Invalid or duplicate unwind extent")
        extents[start] = (size, pos, packed)
    return sections, extents


def branch(pc, w):
    if pc & 3 or not 0 <= pc <= 0xFFFFFFFF or not 0 <= w <= 0xFFFFFFFF:
        raise ValueError("Invalid branch input")
    if w >> 26 != 18:
        return None
    delta = w & 0x03FFFFFC
    if delta & 0x02000000:
        delta -= 0x04000000
    return ((delta if w & 2 else pc + delta) & 0xFFFFFFFF, bool(w & 1))


def checked_decode(b, a, size, output):
    if a & 3 or size <= 0 or size & 3:
        raise ValueError("Invalid disassembly range")
    span(b, a, size)
    rows = []
    for line in output.splitlines():
        if not line.strip():
            continue
        m = re.fullmatch(r"([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+(.+?)\s*", line)
        if not m:
            raise ValueError("Unexpected disassembler output")
        pc, w = int(m[1], 16), int(m[2], 16)
        if pc != a + len(rows) * 4 or len(rows) >= size // 4 or w != word(b, pc):
            raise ValueError("Disassembler word/PC mismatch")
        rows.append({"pc": hx(pc), "word": hx(w), "assembly": m[3]})
    if len(rows) != size // 4:
        raise ValueError("Incomplete disassembly")
    return rows


def fact(name, status, text, *pcs):
    return {"id": name, "status": status, "statement": text, "evidence_pcs": [hx(p) for p in pcs]}


def facts():
    return [
        fact("reset", "verified", "Getter invokes original CPU binding/cache reset, then loads CAF8. Native identity is a replacement contract, not original SDK data.", 0x823EE904, 0x823EE90C),
        fact("allocator_precondition", "verified", "Containing startup already resolves 82D57244 before engine start; allocation sites independently repeat the null check.", 0x82875C2C, 0x82875C38, 0x82875D68, 0x82875D74, 0x82875DB0, 0x82875DBC),
        fact("two_requests", "verified", "Allocator +20 receives the two exact requests; globals 82E0759C/A0 receive results without null checks.", 0x82875DA0, 0x82875DAC, 0x82875DE8, 0x82875E0C),
        fact("record", "verified", "24-byte BE descriptor is {0,0x20000,first,0x600000,second,0}; SDK result is ignored.", 0x82875DF0, 0x82875DF8, 0x82875E04, 0x82875E10, 0x82875E14, 0x82875E1C, 0x82875E20, 0x82875E24),
        fact("allocator_contract", "verified", "Static allocator +20/+24 is 8268E138/8268E6E8. First request uses game heap, page alignment, accounting and protection; second takes physical allocation branch.", 0x8268E548, 0x8268E168, 0x8268E1C8, 0x8268E1D8, 0x8268DF7C),
        fact("sdk_borrows", "verified", "External pointers occupy device +3A18/+3A20 and cursors. Owning +39DC/+39E0 stores occur only under null-input allocation branches.", 0x82458378, 0x82458390, 0x82458394, 0x824583BC, 0x824584AC, 0x824584B0),
        fact("sdk_controls", "verified", "SDK additionally allocates/zeros 0x60 and 0x20 bytes at +2A90/+2A94 for console submission controls.", 0x824583D8, 0x824583E0, 0x824583F0, 0x824583F8, 0x8245840C, 0x8245841C),
        fact("console_configuration", "verified", "Physical ring, read-pointer writeback, GPU identifier, ring-copy and command-cursor writes are visible; no direct application-service registration in this body.", 0x8245842C, 0x82458434, 0x8245846C, 0x82458524, 0x824585A8, 0x824585B8, 0x82458654, 0x82458658),
        fact("optional_observers", "verified", "Ring-copy helper has optional +5404 vtable +18/+1C and +5490 callbacks. SDK +5404 setup is reached by PIX-labelled capture paths; +5490 is set by 82461500 selector 0x22. Not unconditional game services.", 0x82456670, 0x8245668C, 0x82456694, 0x824566B0, 0x82456718, 0x8245673C, 0x82460D6C, 0x82460E18, 0x824615D4),
        fact("observer_limit", "unresolved", "No exhaustive indirect registration closure or original live values for optional SDK observers. Do not claim all possible transitive configure effects are console-only.", 0x8245668C, 0x824566B0),
        fact("aliases", "verified", "DA74 and D890 are borrowed aliases, with no retain at publication. 826B78F0 supplies DB78 as r3 but tail target ignores it and only stores r4 at D890.", 0x82875E2C, 0x82875E30, 0x826B78F8, 0x826B78FC, 0x8272396C),
        fact("device_release", "verified", "Reviewed engine stop -> SDK refcount release -> device destructor -> configure(NULL) frees SDK-owned slots, not external command bases.", 0x823EE420, 0x82452110, 0x8246731C, 0x824582E0, 0x824582EC, 0x82458314, 0x82458320),
        fact("external_release", "unresolved", "No matching caller-buffer free established. Reviewed game cleanup clears device aliases but does not read/clear 759C/A0. Bounded immediate/pointer searches are not exhaustive alias analysis.", 0x82875E90, 0x82875E9C, 0x82875EA0, 0x82875EA4, 0x82875EA8),
        fact("release_capability", "verified", "Allocator +24 can free heap or physical allocations. That capability is not evidence of an original call for these two buffers.", 0x8268E758, 0x8268E768, 0x8268E778, 0x82433910),
        fact("unrelated_allocation", "verified", "82E075A4 belongs to earlier 0x340000 allocation/service, released via object vtable +0C at shutdown; it is not either command buffer.", 0x82875CB0, 0x82875CC4, 0x82875EC8, 0x82875ED8),
        fact("following_service", "verified", "After original li-1 gate, 82867A48 allocates 0x94 CPU object; ctor records timebase and publishes 82D576A0, then camera/rasters, loading art and service registrations follow.", 0x82875E34, 0x82875E4C, 0x82867A64, 0x82867A74, 0x82718D58, 0x82718DA4, 0x82867A88, 0x82867B34, 0x82867BD4, 0x82867BE4, 0x82867BF4, 0x82867C04),
        fact("following_cleanup", "verified", "Application cleanup clears 82D57448, destroys camera, unregisters four services and invokes CPU object's destructor before engine teardown.", 0x82867C7C, 0x82867CF0, 0x82867D0C, 0x82867D14, 0x82867D1C, 0x82867D24, 0x82867D48),
        fact("native_boundary", "proposal", "Replace only exact call at 82875E20, validate live backend identity and original 24-byte record with genuine allocations, adopt reservations into native ownership metadata; leave following alias stores and application continuation AOT. CAF8 remains zero. No SDK object or command model.", 0x82875E20, 0x82875E24, 0x82875E2C, 0x82875E30, 0x82875E34),
        fact("native_lifetime", "proposal", "Archive compatibility reservations through guest-memory teardown while original free remains unmatched. This is explicit native ownership policy, not verified stop/restart equivalence.", 0x82875DAC, 0x82875E0C),
    ]


def inspect(image, disassembler, reference):
    b = image.read_bytes()
    validate_identity(b)
    sections, pdata = layout(b)
    ref = reference.read_bytes()
    if sha(ref) != EXPORT_SHA:
        raise ValueError("Unexpected reference export table identity")
    imports = []
    for a, (ordinal, name) in sorted(IMPORTS.items()):
        expected = struct.pack(">IIII", 0x01010000 | ordinal, 0x02010000 | ordinal, 0x7D6903A6, 0x4E800420)
        name_pattern = rb"XE_EXPORT\(xboxkrnl,\s*0x" + f"{ordinal:08X}".encode() + rb",\s*" + name.encode() + rb",\s*kFunction\)"
        if span(b, a, 16) != expected or not re.search(name_pattern, ref):
            raise ValueError("Import thunk/name mismatch")
        imports.append({"va": hx(a), "ordinal": hx(ordinal), "words": expected.hex(), "reference_name": name,
                        "limit": "ordinal 0x1DF reference naming is ambiguous; no semantic identity inferred" if ordinal == 0x1DF else None})
    functions = []
    known_pcs = set()
    for a in FUNCTIONS:
        size = pdata[a][0] if a in pdata else LEAVES[a]
        ta, tn = sections[".text"]
        if size & 3 or not ta <= a < a + size <= ta + tn:
            raise ValueError("Invalid reviewed extent")
        out = subprocess.run([str(disassembler), str(image), hx(BASE), hx(a), str(size // 4)],
                             check=True, capture_output=True, text=True, timeout=30)
        rows = checked_decode(b, a, size, out.stdout)
        calls, indirect = [], []
        for pc in range(a, a + size, 4):
            known_pcs.add(pc)
            w = word(b, pc)
            q = branch(pc, w)
            if q and (q[1] or not a <= q[0] < a + size):
                calls.append({"pc": hx(pc), "target": hx(q[0]), "linked": q[1]})
            if w & 0xFC00FFFE == 0x4C000420:
                indirect.append({"pc": hx(pc), "linked": bool(w & 1)})
        functions.append({"start": hx(a), "size": size, "extent_source": ".pdata" if a in pdata else "reviewed leaf",
                          "pdata_va": hx(pdata[a][1]) if a in pdata else None,
                          "pdata_packed": hx(pdata[a][2]) if a in pdata else None,
                          "sha256": sha(span(b, a, size)), "instructions": rows,
                          "direct_edges": calls, "indirect_branch_sites": indirect})
    ledger = facts()
    for f in ledger:
        if any(int(pc, 16) not in known_pcs for pc in f["evidence_pcs"]):
            raise ValueError("Fact cites outside selected verified extents: " + f["id"])
    pinned = {0x82875D60: 0x4BB78B99, 0x82875E20: 0x4BBE2441,
              0x82875E2C: 0x93CBDA74, 0x82875E30: 0x4BE41AC1}
    if any(word(b, a) != w for a, w in pinned.items()):
        raise ValueError("Critical boundary instruction mismatch")
    vtable = [word(b, 0x820B60B8 + i * 4) for i in range(10)]
    if (vtable[0], vtable[1], vtable[8], vtable[9]) != (0x8268DDA0, 0x8268DF90, 0x8268E138, 0x8268E6E8):
        raise ValueError("Allocator contract mismatch")
    strings = []
    for a, value in ((0x8206B1D4, b"crashdump.pix2\0"), (0x8206B1E4, b"unnamed.pix2\0")):
        if span(b, a, len(value)) != value:
            raise ValueError("Reviewed SDK capture string mismatch")
        strings.append({"va": hx(a), "bytes": value.hex(), "text": value[:-1].decode("ascii")})
    ta, tn = sections[".text"]
    candidates, setter_candidates = [], []
    for i, (w,) in enumerate(struct.iter_unpack(">I", span(b, ta, tn - tn % 4))):
        pc = ta + 4 * i
        if w >> 26 in (14, 15, 24, 25, 32, 36) and 0x7590 <= w & 0xFFFF <= 0x75AF:
            candidates.append({"pc": hx(pc), "word": hx(w)})
        if w >> 26 == 36 and w & 0xFFFF in (0x5404, 0x5490):
            setter_candidates.append({"pc": hx(pc), "word": hx(w)})
    literals = {hx(a): [] for a in (0x82E0759C, 0x82E075A0, 0x82D5DA74, 0x82D6D890)}
    for i, (w,) in enumerate(struct.iter_unpack(">I", b)):
        if w in (0x82E0759C, 0x82E075A0, 0x82D5DA74, 0x82D6D890):
            literals[hx(w)].append(hx(BASE + 4 * i))
    return {
        "schema": "native-poststart-evidence-v1", "image": {"base": hx(BASE), "bytes": len(b), "sha256": sha(b)},
        "analyzer_sha256": sha(Path(__file__).read_bytes()), "disassembler_sha256": sha(disassembler.read_bytes()),
        "reference_export_table_sha256": sha(ref), "pdata_records": len(pdata),
        "method_limits": ["Reviewed semantics; no guest execution or GPU command interpretation.",
                          "Linear disassembly of .pdata ranges can include embedded tables/padding; edge lists are candidate direct edges, not a recovered CFG.",
                          "Searches do not resolve all derived pointers, callbacks or aliases; absence is not proof of no consumers/free.",
                          "Optional SDK observer registrations are not exhaustively closed; their absence was not measured on original hardware."],
        "hook": {"callsite": hx(0x82875E20), "original_word": hx(pinned[0x82875E20]), "original_target": hx(0x82458260),
                 "continuation": hx(0x82875E24), "abi": "r3=checked native backend identity; r4=guest SP+0x70, six BE32 words; original return ignored",
                 "surrounding_span": {"start": hx(0x82875D60), "bytes": 0xD4, "sha256": sha(span(b, 0x82875D60, 0xD4))}},
        "allocations": [
            {"callsite": hx(0x82875DA0), "global": hx(0x82E0759C), "args_r4_r8": [hx(x) for x in (0x20000, 0xFFFFFFFF, 0x20, 0x404, 1)],
             "actual_original_path": "heap vtable+0; alignment 0x1000; rounded size 0x20000; protection 0x404; allocation accounting retained"},
            {"callsite": hx(0x82875DE8), "global": hx(0x82E075A0), "args_r4_r8": [hx(x) for x in (0x600000, 0xFFFFFFFF, 0x20, 0x404, 0x20000001)],
             "actual_original_path": "physical flags=0,size=0x600000,min=0,max=0xFFFFFFFF,alignment=0x20; protection initially 0x20000404, reduced to 0x404 by 82433820 if BE32[82D51528] != 0"}],
        "descriptor": {"size": 24, "word_order": "big-endian", "words": [0, 0x20000, "first allocation", 0x600000, "second allocation", 0],
                       "offset_0": "not read in selected configure body", "offset_20": "zero selects segment count 0x20"},
        "exact_configure_arithmetic": {"ring_init_r4_decimal": 14, "writeback_r4_decimal": 8, "ring_dword_mask": hx(0x7FFF),
                                       "segment_bytes": hx(0x30000), "secondary_dword_count": hx(0x180000), "reserve_end_from_base": hx(0x2FF60)},
        "allocator_vtable": {"va": hx(0x820B60B8), "entries": [hx(v) for v in vtable]},
        "imports": imports, "sdk_capture_strings": strings, "facts": ledger,
        "optional_observer_registration_guards": [
            {"entry": hx(0x82461500), "selector": hx(0x22), "store_pc": hx(0x824615D4),
             "field": hx(0x5490), "action": "Reject unsupported native SDK registration before any device access; full dispatcher guard is valid while unported."},
            {"entry": hx(0x82460D38), "store_pc": hx(0x82460D6C), "field": hx(0x5404),
             "action": "Reject before initial command flush and capture-object factory call."},
            {"entry": hx(0x82460DC0), "calls_to_registration": [hx(0x82460E44), hx(0x82460F44)],
             "clear_store_pcs": [hx(0x82460E9C), hx(0x82460F90)],
             "action": "Outer PIX capture-controller rejection before file/capture/state effects."}],
        "bounded_searches": {"low16_7590_75AF_candidates": candidates, "sdk_observer_offset_store_candidates": setter_candidates,
                             "aligned_BE_pointer_literals": literals, "warning": "Includes unrelated bases, data and non-device types; candidates require original-instruction review."},
        "functions": functions,
    }


def self_test(image):
    original = image.read_bytes()
    validate_identity(original)
    sections, pdata = layout(original)

    class Checks(unittest.TestCase):
        def test_original_layout(self):
            self.assertEqual(pdata[0x82458260][0], 0x410)
            self.assertEqual(pdata[0x82875BF0][0], 0x288)

        def test_identity_corruption(self):
            bad = bytearray(original); bad[-1] ^= 1
            with self.assertRaises(ValueError): validate_identity(bad)
            with self.assertRaises(ValueError): validate_identity(original[:-1])

        def test_finite_ranges(self):
            for offset, size in [(-1, 4), (len(original), 1), (1, -1), (2**64, 4)]:
                with self.assertRaises(ValueError): take(original, offset, size)
            with self.assertRaises(ValueError): word(original, BASE + 1)

        def test_pe_bounds_and_identity(self):
            with self.assertRaises(ValueError): layout(original[:32])
            bad = bytearray(original); struct.pack_into("<I", bad, 0x3C, len(bad) - 1)
            with self.assertRaises(ValueError): layout(bad)
            pe = struct.unpack_from("<I", original, 0x3C)[0]
            bad = bytearray(original); struct.pack_into("<I", bad, pe + 52, BASE + 4)
            with self.assertRaises(ValueError): layout(bad)

        def test_pdata_zero_extent(self):
            bad = bytearray(original); pos = pdata[0x82458260][1] - BASE
            struct.pack_into(">I", bad, pos + 4, 0)
            with self.assertRaises(ValueError): layout(bad)

        def test_pdata_duplicate(self):
            bad = bytearray(original); pos = pdata[0x82458260][1] - BASE
            bad[pos:pos + 8] = original[pos + 8:pos + 16]
            with self.assertRaises(ValueError): layout(bad)

        def test_pdata_out_of_text(self):
            bad = bytearray(original); pos = pdata[0x82458260][1] - BASE
            struct.pack_into(">I", bad, pos, BASE)
            with self.assertRaises(ValueError): layout(bad)

        def test_branch_sign_absolute_link(self):
            self.assertEqual(branch(0x82875E20, 0x4BBE2441), (0x82458260, True))
            self.assertEqual(branch(BASE, 0x4BFFFFFC), (BASE - 4, False))
            self.assertEqual(branch(BASE, 0x4BFFFFFE), (0xFFFFFFFC, False))
            self.assertEqual(branch(BASE, 0x48000013), (0x10, True))
            self.assertIsNone(branch(BASE, 0x60000000))
            with self.assertRaises(ValueError): branch(BASE + 1, 0)

        def test_decoder_rejects_truncation(self):
            with self.assertRaises(ValueError): checked_decode(original, 0x82875E20, 4, "")

        def test_decoder_rejects_pc_word_and_extra(self):
            line = "82875E20 4BBE2441 bl 0x82458260"
            self.assertEqual(len(checked_decode(original, 0x82875E20, 4, line)), 1)
            for bad in [line.replace("E20", "E24"), line.replace("2441", "2440"), line + "\n" + line, "garbage"]:
                with self.assertRaises(ValueError): checked_decode(original, 0x82875E20, 4, bad)

        def test_exact_record_arithmetic(self):
            # Independently calculate the expressions reviewed at 82458420..E0.
            ring, secondary, count = 0x20000, 0x600000, 0x20
            self.assertEqual(28 - (32 - ring.bit_length()), 14)
            self.assertEqual(min(19, (ring >> 9).bit_length() - 1), 8)
            self.assertEqual((ring >> 2) - 1, 0x7FFF)
            self.assertEqual(secondary // count, 0x30000)
            self.assertEqual((secondary // count & ~3) - 160, 0x2FF60)

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks))
    return result.wasSuccessful()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--disassembler", type=Path, default=ROOT / "build/generator-ninja/SimpsonsDisasm.exe")
    parser.add_argument("--reference-export-table", type=Path, default=REFERENCE)
    parser.add_argument("--report", type=Path, help="Only analysis/native-poststart-integration.json is writable")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.report and args.report.resolve() != REPORT:
            raise ValueError("Output restricted to owned analysis/native-poststart-integration.json")
        if args.self_test:
            if args.report:
                raise ValueError("Self-test does not write reports")
            return 0 if self_test(args.image) else 1
        result = inspect(args.image, args.disassembler, args.reference_export_table)
        output = json.dumps(result, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
        if args.report:
            args.report.write_bytes(output.encode("utf-8"))
            print(f"Wrote {len(result['functions'])} function records and {len(result['facts'])} facts to {args.report}")
        else:
            print(output, end="")
        return 0
    except (OSError, ValueError, struct.error, subprocess.SubprocessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
