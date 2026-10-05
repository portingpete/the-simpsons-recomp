"""Read-only original-byte evidence for startup root camera/shared-depth rasters.

No raster execution, renderer, GPU command model or image synthesis. Semantic
labels below are reviewed evidence, not automatically recovered symbols. Only
analysis/native-camera-rasters.json is writable; self-tests use memory only.
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
SUPPORT = ROOT / "tools/analyze_poststart_integration.py"
SUPPORT_SHA = "fdcb3c06ae15041bcbf98ce59d07272e51dce345c7332b44475844d20251b434"
if hashlib.sha256(SUPPORT.read_bytes()).hexdigest() != SUPPORT_SHA:
    raise RuntimeError("Frozen byte/PE validation dependency changed")
from analyze_poststart_integration import (BASE, IMAGE_SHA, branch, checked_decode,
                                           hx, layout, sha, span, validate_identity, word)

REPORT = ROOT / "analysis/native-camera-rasters.json"
LEAVES = {0x823F5048: 0x270, 0x823F52B8: 4, 0x823F52C0: 4,
          0x823F5E10: 0x7C, 0x82401AC8: 0x18, 0x82407EC8: 0x24}
FUNCTIONS = sorted(set(LEAVES) | {
    0x823F52C8, 0x823F5DA8, 0x823F61F8, 0x823F62A0, 0x823F69E0,
    0x823F6A20, 0x823F6E68, 0x823F7070, 0x82401940, 0x82407DC0,
    0x82408130, 0x82714220, 0x827142D8, 0x823EE6C8,
    0x823FB098, 0x823FB328, 0x823FB3A8,
})

# pc, base-register, displacement, width, destination, reviewed value.
# These describe writes; no instruction stream is executed or interpreted.
COMMON = [
    (0x823F70A0, 31, 8, 4, "R+08", "0"),
    (0x823F70A8, 31, 0x21, 1, "R+21", "flags & 0xF8"),
    (0x823F70AC, 31, 4, 4, "R+04", "0"),
    (0x823F70B0, 31, 0x20, 1, "R+20", "flags & 7"),
    (0x823F70B4, 30, 0, 4, "X+00", "0"),
    (0x823F70B8, 30, 4, 4, "X+04", "0"),
    (0x823F70BC, 30, 0xB, 1, "X+0B", "0xFF"),
    (0x823F70C0, 30, 0xC, 4, "X+0C", "0"),
    (0x823F70C4, 30, 8, 1, "X+08", "0"),
    (0x823F70C8, 30, 9, 1, "X+09", "0"),
    (0x823F70CC, 30, 0xA, 1, "X+0A", "0"),
    (0x823F70D0, 30, 0x18, 4, "X+18", "0"),
]
NORMALIZE = [
    (0x823F6E88, 29, 0x21, 1, "R+21", "flags & 0xF8"),
    (0x823F6E90, 29, 0x20, 1, "R+20", "flags & 7"),
    (0x823F6ED0, 29, 0x14, 4, "R+14", "type2: BE32[82E3DF94]; type1: 0x20"),
    (0x823F7030, 29, 0x23, 1, "R+23", "type2 with 182801B6: 0x0B; type1: 9"),
]
CAMERA = [
    (0x823F7194, 31, 0x18, 4, "R+18", "0"),
    (0x823F719C, 31, 4, 4, "R+04", "0"),
    (0x823F71A0, 31, 0x28, 4, "R+28", "width"),
    (0x823F71A4, 31, 0x2C, 4, "R+2C", "height"),
    (0x823F71A8, 31, 0x14, 4, "R+14", "0x20"),
    (0x823F71B0, 31, 0x21, 1, "R+21", "0x80"),
    (0x823F71BC, 30, 0x18, 4, "X+18", "BE32[82E3DCE8]"),
]
DEPTH = [
    (0x823F71E4, 11, 0x1D88, 4, "82CD1D88", "0"),
    (0x823F71F0, 30, 0x18, 4, "X+18", "0x1A220197"),
    (0x823F71F8, 30, 0, 4, "X+00", "BE32[82D0CAFC], borrowed"),
]
LIST = [
    (0x823F5DEC, 3, 0, 4, "newNode+00", "R"),
    (0x823F5DF0, 31, 0, 4, "82D0D01C", "newNode"),
    (0x823F5DF4, 3, 4, 4, "newNode+04", "previous head"),
    (0x823F5E5C, 9, 4, 4, "predecessor+04", "removedNode.next"),
    (0x823F5E70, 10, -4, 4, "82D0D01C", "removedNode.next if head matched"),
]
WRAPPER = [
    (0x82408190, 31, 0xC, 4, "R+0C", "width"),
    (0x82408194, 31, 0x10, 4, "R+10", "height"),
    (0x82408198, 31, 0x14, 4, "R+14", "input depth"),
    (0x8240819C, 31, 0, 4, "R+00", "R"),
    (0x824081A0, 31, 0x22, 1, "R+22", "0"),
    (0x824081A4, 31, 0x21, 1, "R+21", "0"),
    (0x824081A8, 31, 0x1C, 2, "R+1C", "0"),
    (0x824081AC, 31, 0x1E, 2, "R+1E", "0"),
    (0x824081B0, 31, 4, 4, "R+04", "0"),
    (0x824081B4, 31, 8, 4, "R+08", "0"),
]
REGISTRATION = [
    (0x823FB24C, 3, 0, 4, "record+00", "previous registry total = plugin offset"),
    (0x823FB254, 3, 4, 4, "record+04", "requested unrounded size"),
    (0x823FB258, 3, 8, 4, "record+08", "plugin ID"),
    (0x823FB2B0, 3, 0x20, 4, "record+20", "ctor or default callback"),
    (0x823FB2CC, 3, 0x24, 4, "record+24", "dtor or default callback"),
    (0x823FB2E4, 3, 0x28, 4, "record+28", "copy or default callback"),
    (0x823FB2EC, 3, 0x30, 4, "record+30", "0 (next)"),
    (0x823FB2F0, 3, 0x38, 4, "record+38", "registry owner"),
    (0x823FB2F4, 3, 0x34, 4, "record+34", "0 (previous before append)"),
    (0x823FB304, 28, 0x10, 4, "registry+10", "new record if list empty"),
    (0x823FB310, 11, 0x30, 4, "oldTail+30", "new record"),
    (0x823FB318, 3, 0x34, 4, "record+34", "old tail"),
    (0x823FB31C, 28, 0x14, 4, "registry+14", "new record"),
]


def store_shape(w):
    """Decode only D-form store shape to check reviewed write assertions."""
    widths = {36: 4, 38: 1, 44: 2}
    if w >> 26 not in widths:
        raise ValueError("Expected direct non-updating integer store")
    d = w & 0xFFFF
    return ((w >> 16) & 31, d - 0x10000 if d & 0x8000 else d, widths[w >> 26])


def checked_writes(b, rows):
    result = []
    for pc, ra, disp, width, dest, value in rows:
        w = word(b, pc)
        if store_shape(w) != (ra, disp, width):
            raise ValueError("Reviewed write shape mismatch at " + hx(pc))
        result.append({"pc": hx(pc), "word": hx(w), "width": width,
                       "destination": dest, "reviewed_value": value})
    return result


def parse_boot(data):
    text = data.decode("utf-8", errors="strict")
    lines = [s for s in text.splitlines() if s.startswith("[NATIVE RASTER] original create ")]
    if len(lines) != 1:
        raise ValueError("Expected exactly one observed raster-create line")
    m = re.fullmatch(r"\[NATIVE RASTER\] original create raster=([0-9A-F]{8}) flags=([0-9A-F]{8}) extent=(\d+)x(\d+) depth_field=(\d+) plugin_offset=([0-9A-F]+)", lines[0])
    if not m:
        raise ValueError("Malformed raster observation")
    raster, flags = int(m[1], 16), int(m[2], 16)
    w, h, depth, offset = int(m[3]), int(m[4]), int(m[5]), int(m[6], 16)
    if not raster or raster & 3 or flags != 2 or (w, h, depth, offset) != (1280, 720, 0, 0x34):
        raise ValueError("Observation outside the reviewed boot033 startup scope")
    guard = "[FAILURE] Unimplemented native engine graphics boundary 0x823F7070, caller 0x824081C0"
    if guard not in text or "[NATIVE ENGINE] native submission registered" not in text:
        raise ValueError("Missing observation boundary/context")
    return {"sha256": sha(data), "line": lines[0], "raster": hx(raster), "flags": flags,
            "width": w, "height": h, "input_depth": depth, "extension_offset": offset,
            "status": "entry observed; callback and following depth create not executed"}


def check_registration(offset, total, raster):
    """Finite bounds for a proposed native preflight, not a recovered schema."""
    if not all(type(x) is int for x in (offset, total, raster)):
        raise ValueError("Expected integer bounds")
    if offset < 0x34 or offset & 3 or total < offset + 0x20 or total > 0xFFFFFFFF:
        raise ValueError("Invalid extension/allocation range")
    if raster <= 0 or raster & 3 or raster + total > 0x100000000:
        raise ValueError("Invalid raster address range")


def inspect(image, disassembler, boot):
    b = image.read_bytes(); validate_identity(b)
    _, pdata = layout(b)
    functions = []
    for a in FUNCTIONS:
        size = pdata[a][0] if a in pdata else LEAVES[a]
        run = subprocess.run([str(disassembler), str(image), hx(BASE), hx(a), str(size // 4)],
                             check=True, capture_output=True, text=True, timeout=30)
        rows = checked_decode(b, a, size, run.stdout)
        edges = []
        for pc in range(a, a + size, 4):
            q = branch(pc, word(b, pc))
            if q and (q[1] or not a <= q[0] < a + size):
                edges.append({"pc": hx(pc), "target": hx(q[0]), "linked": q[1]})
        functions.append({"va": hx(a), "size": size, "extent": ".pdata" if a in pdata else "reviewed leaf",
                          "pdata_va": hx(pdata[a][1]) if a in pdata else None,
                          "sha256": sha(span(b, a, size)), "instructions": rows, "direct_edges": edges})
    calls = {0x823F70D4: 0x823F6E68, 0x823F7268: 0x823F5DA8,
             0x823F6368: 0x823F5E10, 0x823F642C: 0x823F5E10,
             0x823F717C: 0x82440698, 0x823F7208: 0x823F61F8,
             0x823F6418: 0x82441708, 0x82401AB8: 0x824408E0,
             0x8271431C: 0x82408130, 0x82714338: 0x82408130,
             0x82714288: 0x82407DC0, 0x8271429C: 0x82407DC0}
    for pc, dest in calls.items():
        if branch(pc, word(b, pc)) != (dest, True):
            raise ValueError("Critical call mismatch")
    for pc, expected in {0x82401ACC: 0x1D430018, 0x82401AD0: 0x396BE3B0,
                         0x82401AD4: 0x396B0048, 0x82401AD8: 0x7C6A582E}.items():
        if word(b, pc) != expected:
            raise ValueError("Stage getter address expression changed")
    if word(b, 0x82CD1D88) != 1 or word(b, 0x82CD1E28) != 0x34 or word(b, 0x823F52B8) != 0x4E800020 or word(b, 0x823F52C0) != 0x4E800020:
        raise ValueError("Shared-flag/plugin callback evidence changed")
    # Deliberately bounded search, not an indirect alias/free proof.
    sa, sn = layout(b)[0][".text"]
    candidates = [{"pc": hx(sa + i * 4), "word": hx(w)}
                  for i, (w,) in enumerate(struct.iter_unpack(">I", span(b, sa, sn - sn % 4)))
                  if w >> 26 in (14, 15, 24, 25, 32, 36) and w & 0xFFFF == 0x1D88]
    return {
        "schema": "native-camera-raster-evidence-v1",
        "image": {"base": hx(BASE), "sha256": IMAGE_SHA, "bytes": len(b)},
        "analyzer_sha256": sha(Path(__file__).read_bytes()), "support_analyzer_sha256": SUPPORT_SHA,
        "disassembler_sha256": sha(disassembler.read_bytes()), "observation": parse_boot(boot.read_bytes()),
        "scope": "Positive-size root type2 and first shared-depth type1; other branches rejected, not generalized",
        "entry_abi": {"create": "823F7070(r3=0,r4=raster,r5=flags)->Boolean",
                      "destroy": "823F62A0(r3=0,r4=raster,r5=0)->Boolean; wrapper ignores return"},
        "extension": {"offset_global": hx(0x82E3DC94), "registered_bytes": 32,
                      "plugin_id": hx(0x040C), "original_registry_base_bytes": 0x34,
                      "untouched_ranges": ["X+10..17", "X+1C..1F"]},
        "writes": {"wrapper_before_platform": checked_writes(b, WRAPPER),
                   "common_platform": checked_writes(b, COMMON), "safe_normalization_branches": checked_writes(b, NORMALIZE),
                   "camera_type2": checked_writes(b, CAMERA), "first_shared_depth_type1": checked_writes(b, DEPTH),
                   "cpu_list_insert_or_remove": checked_writes(b, LIST),
                   "plugin_registration": checked_writes(b, REGISTRATION)},
        "raster_plugin_registry": {"va": hx(0x82CD1E28), "total_size_offset": 0,
                                   "head_offset": 0x10, "tail_offset": 0x14,
                                   "record_readable_bytes": 0x3C,
                                   "record_fields": {"offset": 0, "size": 4, "id": 8,
                                                     "ctor": 0x20, "dtor": 0x24, "copy": 0x28,
                                                     "next": 0x30, "previous": 0x34, "registry": 0x38},
                                   "040C_expected": {"size": 0x20, "ctor": hx(0x823F52B8), "dtor": hx(0x823F52C0),
                                                     "offset": "BE32[82E3DC94]", "registry": hx(0x82CD1E28)}},
        "stage_getter": {"entry": hx(0x82401AC8), "base": hx(0x82D0E3F8), "stride_bytes": 0x18,
                         "native_preflight_indices": list(range(8)), "original_bounds_check": False},
        "helper_call_contracts": [
            {"entry": hx(0x823F6E68), "args": "r3=R,r4=flags", "lr": hx(0x823F70D8), "return": "Boolean in r3"},
            {"entry": hx(0x823F5DA8), "args": "r3=R", "lr": hx(0x823F726C),
             "return": "new node pointer from allocator, NOT Boolean; verify head==result and node={R,oldHead}",
             "internal_allocator_lr": hx(0x823F5DE8)},
            {"entry": hx(0x823F5E10), "args": "r3=R", "type2_lr": hx(0x823F6430), "shared_type1_lr": hx(0x823F636C),
             "return": "not Boolean: unchanged R on early absent return, or E+13C tail-callback result; validate list postcondition"},
            {"entry": hx(0x82401AC8), "args": "r3=stage", "lr": hx(0x823F62CC), "return": "borrowed raster pointer or zero"}],
        "facts": [
            {"status": "verified", "claim": "Type2 allocates no SDK surface; X+00 stays zero; original CPU list insertion still allocates a node.", "pcs": [hx(x) for x in (0x823F70B4, 0x823F7190, 0x823F7268, 0x823F5DE4)]},
            {"status": "verified", "claim": "Type1 shared flag is consumed, default depth borrowed without AddRef; matching destroy skips release and does not re-arm flag.", "pcs": [hx(x) for x in (0x823F71D4, 0x823F71E4, 0x823F71F8, 0x823F6378, 0x823F637C)]},
            {"status": "verified", "claim": "CPU normalization for types2/1; type2 presentation lookup writes scratch 82D0D000, with 182801B6 producing bytes 01200B00.", "pcs": [hx(x) for x in (0x823F6EEC, 0x823F505C, 0x823F50D0, 0x823F50E0, 0x823F52A4)]},
            {"status": "verified", "claim": "Destructor queries 8 stages; matching raster unbind is mixed CPU/SDK, not safe wholesale AOT.", "pcs": [hx(x) for x in (0x823F62C8, 0x823F62DC, 0x82401A6C, 0x82401AB8)]},
            {"status": "verified", "claim": "Non-root type2 removes list node; other child types skip platform release. Complete child creation/refcount semantics remain outside scope.", "pcs": [hx(x) for x in (0x823F62EC, 0x823F62F8, 0x823F6420, 0x823F642C)]},
            {"status": "verified", "claim": "Non-type5 camera color parent selects CB00/CAFC, not X+00. SDK target/viewport function stays guarded.", "pcs": [hx(x) for x in (0x823EE730, 0x823EE740, 0x823EE74C, 0x823EE77C, 0x823EE7E0)]},
            {"status": "unresolved", "claim": "Following depth create has not executed; live shared flag and native role/dimensions must be validated. No original flag reset proved by bounded search."},
            {"status": "proposal", "claim": "Native associations borrow existing driver roles; preflight before CPU writes, retain list helpers, reject bound stages/private depth/type5/children, and forbid premature driver teardown."},
        ],
        "cpu_helpers": {"normalize": hx(0x823F6E68), "format_scratch": hx(0x823F5048),
                        "list_insert": hx(0x823F5DA8), "list_remove": hx(0x823F5E10), "stage_get": hx(0x82401AC8),
                        "scope_limit": "Normalize only flags2/1; all helpers require valid original allocation/pool/list and checked ABI."},
        "required_original_lifetimes": {"object": "E+120/E+124 and all plugins retained in wrappers",
                                        "list": "8-byte pool nodes at D01C; D020 pool; insert E+138/remove E+13C",
                                        "targets": "driver-owned CB00/CAFC; no new native surfaces for these two rasters"},
        "guarded_paths": {"type5_sdk_create": hx(0x823F717C), "private_depth_helper": hx(0x823F61F8),
                          "private_sdk_release": hx(0x823F6418), "mixed_stage_unbind": hx(0x82401940),
                          "sdk_target_selection": hx(0x823EE6C8)},
        "shared_flag_low16_candidates": candidates,
        "limitations": ["No guest execution; reviewed write expressions are annotations, not an emulator.",
                        "Original CPU allocation failure and plugin rollback are not made infallible by this evidence.",
                        "No complete subraster, private-depth, format, locking or texture universe.",
                        "No draw, viewport or depth precision authorization; native implementation not modified or certified."],
        "functions": functions,
    }


def self_test(image, boot):
    b = image.read_bytes(); validate_identity(b)
    log = boot.read_bytes()

    class Checks(unittest.TestCase):
        def test_all_reviewed_store_shapes(self):
            for rows in (WRAPPER, COMMON, NORMALIZE, CAMERA, DEPTH, LIST, REGISTRATION):
                self.assertEqual(len(checked_writes(b, rows)), len(rows))

        def test_changed_store_opcode(self):
            bad = bytearray(b); struct.pack_into(">I", bad, 0x823F70B4 - BASE, 0x60000000)
            with self.assertRaises(ValueError): checked_writes(bad, COMMON)

        def test_changed_store_offset(self):
            bad = bytearray(b); struct.pack_into(">I", bad, 0x823F71F8 - BASE, word(b, 0x823F71F8) + 4)
            with self.assertRaises(ValueError): checked_writes(bad, DEPTH)

        def test_dynamic_extension_bounds(self):
            check_registration(0x34, 0x54, 0xE1A52960)
            check_registration(0x60, 0x90, 0x10000)
            for args in [(0,0x54,0x10000),(0x35,0x80,0x10000),(0x34,0x53,0x10000),
                         (0x34,0x54,0xFFFFFFF0),(0x34,0x54,0),(True,0x54,0x10000)]:
                with self.assertRaises(ValueError): check_registration(*args)

        def test_observed_camera(self):
            p = parse_boot(log)
            self.assertEqual((p['flags'],p['width'],p['height'],p['extension_offset']), (2,1280,720,0x34))

        def test_changed_observation_rejected(self):
            for bad in [log.replace(b'flags=00000002 extent=',b'flags=00000005 extent='),
                        log.replace(b'extent=1280x720 depth_field=',b'extent=0x720 depth_field='),
                        log.replace(b'plugin_offset=34',b'plugin_offset=ZZ'),
                        log.replace(b'caller 0x824081C0',b'caller 0x824081C4')]:
                with self.assertRaises(ValueError): parse_boot(bad)

        def test_missing_duplicate_observation(self):
            with self.assertRaises(ValueError): parse_boot(b'')
            with self.assertRaises(ValueError): parse_boot(log + b'\n' + log)

        def test_no_invented_extension_writes(self):
            camera = COMMON + CAMERA
            self.assertFalse(any(ra == 30 and 0x10 <= disp < 0x18 for _,ra,disp,_,_,_ in camera))
            self.assertEqual([(v,n) for _,ra,d,n,_,v in camera if ra == 30 and d == 0], [('0',4)])
            self.assertTrue(any(dest == '82CD1D88' and val == '0' for *_,dest,val in DEPTH))

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks))
    return result.wasSuccessful()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    p.add_argument('--disassembler',type=Path,default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    p.add_argument('--boot-log',type=Path,default=ROOT/'build/boot-033.log')
    p.add_argument('--report',type=Path)
    p.add_argument('--self-test',action='store_true')
    args = p.parse_args()
    try:
        if args.report and args.report.resolve() != REPORT:
            raise ValueError('Report destination outside owned analysis/native-camera-rasters.json')
        if args.self_test:
            if args.report: raise ValueError('Self-test writes no report')
            return 0 if self_test(args.image,args.boot_log) else 1
        r = inspect(args.image,args.disassembler,args.boot_log)
        out = json.dumps(r,indent=2,sort_keys=True) + '\n'
        if args.report:
            args.report.write_bytes(out.encode())
            print(f"Wrote {len(r['functions'])} original extents; observed type2, predicted shared type1 remains conditional")
        else: print(out,end='')
        return 0
    except (OSError,ValueError,struct.error,subprocess.SubprocessError) as exc:
        print(f'error: {exc}',file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
