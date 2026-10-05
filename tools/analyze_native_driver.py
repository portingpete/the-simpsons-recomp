"""Read-only original-byte evidence for native driver startup (no guest execution).

Only --report analysis/native-driver.json may be written. No AOT generation,
runtime/build outputs, reference projects, or game assets are changed. Labels
describe reviewed behavior, not recovered symbols. Literal tracking below is a
conservative static expression reader, not a CPU interpreter: branches, loads
and unfamiliar instructions discard facts; callbacks are never executed.
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

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"

# All other extents come from the original .pdata. These reviewed leaf extents
# end in their own original BLR or direct tail branch, not the preceding unwind
# record. The pinned image identity also protects their instruction boundaries.
LEAVES = {
    0x823EC5A8: 0x24, 0x823ED930: 0x70, 0x823ED9A0: 0x14,
    0x823EDA10: 0x88, 0x823EE5D0: 4, 0x823EFBD0: 4,
    0x823EFC38: 12, 0x823F0918: 12, 0x823F2038: 0x38,
    0x823F52B8: 4, 0x823F52C0: 4, 0x823F8D80: 0x2C,
    0x823F9618: 0x3C, 0x823FBEA8: 16, 0x823FBEB8: 4,
    0x82400170: 0x58, 0x824001E0: 0x74, 0x82408348: 0x18,
    0x8240EBD0: 0x54, 0x824103C0: 0x24,
    0x823E6E58: 0x3C, 0x823E1590: 0x38,
    0x823E6E98: 12, 0x823E15C8: 12,
}
SELECTED = [
    0x823EC490, 0x823EC5A8, 0x823EC950, 0x823EC9E0, 0x823ECAA8,
    0x823ECB98, 0x823ECE30, 0x823ECF58, 0x823ED028,
    0x823ED930, 0x823ED9A0, 0x823EDA10, 0x823EDB68, 0x823EDD38,
    0x823EDF20, 0x823EE1A8, 0x823EE440, 0x823EE5D0, 0x823EE8F8,
    0x823EF838, 0x823EFA18, 0x823EFB78, 0x823EFBD0, 0x823EFC38,
    0x823EFFD8, 0x823F0630, 0x823F0918,
    0x823F4780, 0x823F47A8, 0x823F52B8, 0x823F52C0, 0x823F52C8,
    0x823F69E0, 0x823F6A20, 0x823FB098, 0x823FB328, 0x823FB3A8,
    0x823FB708, 0x823FBD20, 0x823FBEA8, 0x823FBEB8,
    0x823FC6D8, 0x823FCAD8, 0x823FCD58, 0x823FCF60,
    0x823FFE78, 0x82400040, 0x82400170, 0x824001E0, 0x82400278,
    0x824008E0, 0x82403D78, 0x82406560, 0x82407600, 0x82407660,
    0x82408E30, 0x82409A90, 0x8240EBD0, 0x8240EC28, 0x8240EC68,
    0x824103C0, 0x824103E8, 0x82416430, 0x824164B0, 0x824164F8,
    0x82416588, 0x82416BC8, 0x82416C58,
    0x8244E5A8, 0x8244ED90, 0x82875BF0, 0x8287B688,
    0x82C743A8, 0x82CB33C8, 0x82CB35A8, 0x82CB3828,
    0x82CB3258, 0x82CB3310, 0x823E6E58, 0x823E1590,
    0x823E6E98, 0x823E15C8, 0x82A67C48, 0x82A6CF80,
    0x8269E240, 0x8269BE40, 0x827417C0,
]
CORE_CTORS = [
    0x823F9618, 0x823EC248, 0x82408348, 0x823F4360,
    0x823F2038, 0x823F8D80, 0x823F1960, 0x82406680,
    0x82408278, 0x823FEE48, 0x823F8CE0, 0x82407710,
    0x82407660, 0x823FA7B0,
]


def hx(v):
    return f"0x{v:08X}"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def span(b, a, n):
    o = a - BASE
    if n < 0 or o < 0 or o + n > len(b):
        raise ValueError(f"Out-of-image range {hx(a)} + {n}")
    return b[o:o+n]


def word(b, a):
    if a & 3:
        raise ValueError("Unaligned instruction/data word")
    return struct.unpack(">I", span(b, a, 4))[0]


def branch(pc, w):
    if pc & 3 or not 0 <= w <= 0xFFFFFFFF:
        raise ValueError("Invalid branch input")
    if w >> 26 != 18:
        return None
    displacement = w & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    return ((displacement if w & 2 else pc + displacement) & 0xFFFFFFFF,
            bool(w & 1))


def extents(b):
    if len(b) != IMAGE_SIZE or sha(b) != IMAGE_SHA256:
        raise ValueError("Unsupported/modified original flat image")
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[:2] != b"MZ" or b[pe:pe+4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, count = struct.unpack_from("<HH", b, pe+4)
    opt_size = struct.unpack_from("<H", b, pe+20)[0]
    if machine != 0x1F2 or struct.unpack_from("<I", b, pe+52)[0] != BASE:
        raise ValueError("Unexpected image architecture/base")
    sections = {}
    for i in range(count):
        name, n, rva = struct.unpack_from("<8sII", b, pe+24+opt_size+40*i)
        sections[name.rstrip(b"\0")] = (BASE+rva, n)
    a, n = sections[b".pdata"]
    text_a, text_n = sections[b".text"]
    if n % 8:
        raise ValueError("Partial unwind record")
    result = {}
    for pos in range(a, a+n, 8):
        start, packed = struct.unpack(">II", span(b, pos, 8))
        if start == packed == 0:
            continue
        size = ((packed >> 8) & 0x3FFFFF)*4
        if start in result or start & 3 or not size or not text_a <= start < start+size <= text_a+text_n:
            raise ValueError("Invalid original function extent")
        result[start] = size
    return result


def literal_step(regs, pc, w):
    """Only proven addi/addis/ori/or literals; no loads, branches or execution."""
    op, rt, ra, rb = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    imm = w & 0xFFFF
    signed = imm - 0x10000 if imm & 0x8000 else imm
    direct = branch(pc, w)
    if direct:
        if direct[1]:
            # ABI volatile GPRs are never propagated across calls.
            for k in list(regs):
                if k < 14:
                    del regs[k]
        else:
            regs.clear()
    elif op in (14, 15):
        value = 0 if ra == 0 else regs.get(ra)
        regs[rt] = None if value is None else (value + (signed << (16 if op == 15 else 0))) & 0xFFFFFFFF
    elif op == 24:
        value = regs.get(rt)
        regs[ra] = None if value is None else value | imm
    elif op == 31 and (w >> 1) & 1023 == 444:
        left, right = regs.get(rt), regs.get(rb)
        regs[ra] = None if left is None or right is None else left | right
    elif op in (10, 11, 36, 38, 44):
        pass  # compares and non-updating stores do not write a GPR
    else:
        regs.clear()  # conservative, including control-flow joins/unknown ops


def decode(b, a, n, disassembler, image):
    lines = subprocess.check_output([str(disassembler), str(image), hx(BASE), hx(a), str(n//4)], text=True).splitlines()
    output = []
    for line in lines:
        m = re.fullmatch(r"([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+(.+?)\s*", line)
        if not m:
            if line.strip():
                raise ValueError("Unexpected disassembler output")
            continue
        pc, w = int(m[1], 16), int(m[2], 16)
        if pc != a+4*len(output) or w != word(b, pc):
            raise ValueError("Disassembler word/PC mismatch")
        output.append(f"{pc:08X} {w:08X} {m[3]}")
    if len(output) != n//4:
        raise ValueError("Incomplete disassembly")
    return output


def inspect(image, disassembler):
    b = image.read_bytes()
    pdata = extents(b)
    attachments = [word(b, 0x821B5688+4*i) for i in range(25)]
    # A table entry may attach more than one object registry; record only direct
    # calls to the ENGINE registration wrapper. This is not a dynamic registry.
    selected = set(SELECTED + CORE_CTORS + attachments)
    functions, registrations = [], []
    for a in sorted(selected):
        n = pdata.get(a, LEAVES.get(a))
        if n is None:
            raise ValueError(f"No verified extent for {hx(a)}")
        instructions = decode(b, a, n, disassembler, image)
        calls, regs = [], {}
        for pc in range(a, a+n, 4):
            w = word(b, pc)
            q = branch(pc, w)
            if q and not a <= q[0] < a+n:
                args = {f"r{k}": hx(regs[k]) for k in range(3, 9) if regs.get(k) is not None}
                calls.append({"pc": hx(pc), "target": hx(q[0]), "linked": q[1], "proven_literal_args": args})
                if q[0] == 0x823EC5A8 or (a == 0x823ECB98 and q[0] == 0x823FB098):
                    fields = (4, 5, 6, 7) if a == 0x823ECB98 else (3, 4, 5, 6)
                    if any(regs.get(k) is None for k in fields):
                        raise ValueError(f"Unresolved registration literal {hx(pc)}")
                    size, plugin, ctor, dtor = (regs[k] for k in fields)
                    registrations.append({"registration_function": hx(a), "call": hx(pc),
                                          "size": size, "id": hx(plugin), "constructor": hx(ctor),
                                          "destructor": hx(dtor), "scope": "core" if a == 0x823ECB98 else "direct_attachment_table_call"})
            literal_step(regs, pc, w)
        functions.append({"address": hx(a), "size": n, "extent": "original_pdata" if a in pdata else "reviewed_leaf",
                          "sha256": sha(span(b, a, n)), "instructions": instructions, "direct_edges": calls})
    core = [x for x in registrations if x["scope"] == "core"]
    if [int(x["constructor"], 16) for x in core] != CORE_CTORS:
        raise ValueError("Unexpected core constructor order")
    caps = span(b, 0x8206AA30, 0x130)
    if word(b, 0x8206AA4C) != 0x01F91FC0:
        raise ValueError("Unexpected original resource capability")
    return {
        "schema_version": 1,
        "scope": "Original open/start/stop and bounded constructor dependencies; no runtime execution or complete renderer claim",
        "image": {"path": str(image), "size": len(b), "sha256": sha(b), "base": hx(BASE), "mapping": "flat VA minus base"},
        "tools": {"analyzer_sha256": sha(Path(__file__).read_bytes()), "disassembler_sha256": sha(disassembler.read_bytes())},
        "verification": {"functions": len(functions), "instruction_words": sum(x["size"]//4 for x in functions),
                         "core_registrations": len(core), "direct_attachment_registrations": len(registrations)-len(core),
                         "all_disassembly_words_equal_original": True},
        "static_data": {
            "driver_descriptor_82CD1A78": [hx(word(b, 0x82CD1A78+4*i)) for i in range(14)],
            "requests": [{"request": i, "case": hx(0x823F067C+4*v)} for i, v in enumerate(span(b, 0x82062AC0, 23))],
            "attachment_table_821B5688": [hx(x) for x in attachments],
            "mode_formats_82062A40": [hx(word(b, 0x82062A40+4*i)) for i in range(7)],
            "caps_8206AA30": {"bytes": len(caps), "sha256": sha(caps), "words": [hx(word(b, 0x8206AA30+4*i)) for i in range(len(caps)//4)]},
            "pipeline_node_records": [{"address": hx(a), "words": [hx(word(b, a+4*i)) for i in range(16)]}
                                      for a in (0x82CD1E58, 0x82CD1EB8, 0x82CD17E8, 0x82CD1788)],
        },
        "registrations": registrations,
        "limitations": ["Direct-call literal evidence does not prove indirect-call closure or dynamic constructor execution.",
                        "Direct attachment entries are not the complete registry: nested attachments must also be retained.",
                        "No shader arithmetic translation, DXGI mapping, renderer implementation, or boot success is claimed."],
        "functions": functions,
    }


def self_test():
    checks = 0
    def check(ok):
        nonlocal checks
        if not ok:
            raise AssertionError("Native driver analyzer self-test failed")
        checks += 1
    check(branch(0x823EE03C, 0x48064505) == (0x82452540, True))
    check(branch(0x1000, 0x4BFFFFFE) == (0xFFFFFFFC, False))
    check(branch(0, 0x4E800421) is None)
    regs = {}
    literal_step(regs, 0, 0x3D608240)  # lis r11,0x8240
    literal_step(regs, 4, 0x38CB9618)  # addi r6,r11,-27112
    check(regs[6] == 0x823F9618)  # signed low half matters
    regs[31] = 0x82CD1930
    literal_step(regs, 8, 0x48000001)
    check(6 not in regs and regs[31] == 0x82CD1930)
    literal_step(regs, 12, 0x41820004)
    check(not regs)  # no propagation through a control-flow join
    regs[11] = 0xFFFFFFFF
    literal_step(regs, 16, 0x396B0001)
    check(regs[11] == 0)
    literal_step(regs, 20, 0x816B0000)
    check(not regs)  # a load is unknown, not an original-data execution
    for bad in [lambda: span(b"1234", BASE-1, 1), lambda: span(b"1234", BASE, 5),
                lambda: span(b"1234", BASE, -1), lambda: word(b"1234", BASE+1),
                lambda: branch(1, 0), lambda: extents(b"MZ")]:
        try:
            bad()
        except ValueError:
            checks += 1
        else:
            raise AssertionError("Invalid evidence accepted")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT/"analysis/simpsons.pe")
    parser.add_argument("--disassembler", type=Path, default=ROOT/"build/generator-ninja/SimpsonsDisasm.exe")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        print(json.dumps({"self_tests_passed": self_test()}))
        return
    if args.report and args.report.resolve() != (ROOT/"analysis/native-driver.json").resolve():
        raise ValueError("Only analysis/native-driver.json is an allowed output")
    report = inspect(args.image.resolve(), args.disassembler.resolve())
    output = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8", newline="\n")
        print(json.dumps(report["verification"]))
    else:
        sys.stdout.write(output)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"native-driver: {error}", file=sys.stderr)
        raise SystemExit(1)
