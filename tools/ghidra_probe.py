"""Bounded, byte-checked Ghidra/ReAgent evidence export; no guest execution.

Uses an isolated project and settings directory. The source is an already-flat
memory image, so BinaryLoader is mandatory. Ordinary PE loading is incorrect.
The generic PowerPC ABI and untyped save/restore helpers are NOT verified Xenon
ABI recovery; exported C and high P-code are inspection evidence only.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
LANGUAGE = "PowerPC:BE:64:64-32addr"
SELECTED = (0x823EDF20, 0x823EE1A8, 0x823EE6C8, 0x82723C80, 0x82723D80, 0x82756480)


def annotate_exports(out, report):
    """Preserve raw exports and separately expose this probe's bounded coverage.

    Bridge 0.2.0 exports calls only to defined Ghidra functions. Recover direct
    references from already-checked original words; never invent indirect targets.
    """
    raw = out / "exports"
    dest = out / "annotated-exports"
    dest.mkdir(exist_ok=True)
    records = {f["address"].lower(): f for f in report["functions"]}
    index = json.loads((raw / "_index.json").read_text(encoding="utf-8"))
    audit = {"raw_exports_preserved": True, "functions": [],
             "scope": "Annotations reflect bounded static evidence, not recovered ABI or SDK semantics"}
    for key in index:
        source = json.loads((raw / f"{key}.json").read_text(encoding="utf-8"))
        annotated = copy.deepcopy(source)
        record = records.get(key)
        gaps = []
        def gap(reason, kind="unavailable", site=None):
            gaps.append({"function": key, "reason": reason,
                         "origin": "tools/ghidra_probe.py + original-byte report",
                         "kind": kind, "site": site.lower() if site else None})
        gap("Caller discovery is limited to the explicitly selected functions; zero callers is not complete coverage")
        gap("Generic PowerPC compiler ABI remains unverified; pointer signedness, parameter widths and external call effects need original-byte review")
        if record:
            known = {str(c["addr"]).lower(): c for c in source["callees"]}
            for call in record["direct_calls"]:
                target = call["target"].lower()
                if target not in known:
                    known[target] = {"addr": target, "name": f"original_{target}",
                                     "ref_type": "CALL", "origin": "verified original BL word"}
                if target not in index:
                    gap(f"Direct call {target} body is outside this bounded Ghidra export", "limit", call["pc"])
            annotated["callees"] = [known[a] for a in sorted(known)]
            for pc in record["indirect_calls"]:
                gap("Indirect bctrl target and effects remain unresolved", "unresolved_call", pc)
            for ins in record["instructions"]:
                word = int(ins["word"], 16)
                if word >> 26 == 18 and not word & 1:
                    for target in ins["flows"]:
                        if not int(key, 16) <= int(target, 16) < int(key, 16) + record["size"]:
                            gap(f"External tail branch {target}; helper ABI/effects require review", "unresolved_call", ins["pc"])
            audit["functions"].append({"address": key,
                "original_direct_call_sites": len(record["direct_calls"]),
                "raw_unique_callees": len(source["callees"]),
                "annotated_unique_callees": len(annotated["callees"]),
                "original_indirect_call_sites": len(record["indirect_calls"]),
                "explicit_gaps": len(gaps), "high_pcode_ops": len(source["pcode"]),
                "cfg_blocks": len(source["cfg"]),
                "pcode_errors": source["pcode_errors"], "cfg_errors": source["cfg_errors"]})
        annotated["gaps"] = gaps
        index[key]["num_callees"] = len(annotated["callees"])
        (dest / f"{key}.json").write_text(json.dumps(annotated, indent=2) + "\n", encoding="utf-8")
    (dest / "_index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    (out / "export-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")


def original():
    data = (ROOT / "analysis/simpsons.pe").read_bytes()
    if len(data) != IMAGE_SIZE or hashlib.sha256(data).hexdigest() != IMAGE_SHA256:
        raise ValueError("Original-derived flat image identity mismatch")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = {}
    for i in range(count):
        name, size, rva = struct.unpack_from("<8sII", data, pe + 24 + opt_size + 40*i)
        sections[name.rstrip(b"\0").decode("ascii")] = (rva, size)
    offset, size = sections[".pdata"]
    extents = {}
    for pos in range(offset, offset + size, 8):
        start, packed = struct.unpack_from(">II", data, pos)
        if start in SELECTED:
            if start in extents:
                raise ValueError("Duplicate original function extent")
            extents[start] = ((packed >> 8) & 0x3FFFFF)*4
    if set(extents) != set(SELECTED):
        raise ValueError("Missing original function extent")
    return data, extents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ghidra", type=Path, required=True)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--tag", default="ppc64-32addr-01")
    parser.add_argument("--inline-save-helper", action="store_true",
                        help="Analyze verified __savegprlr_28 body inline in the Ghidra database")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", args.tag):
        raise ValueError("Invalid output tag")
    data, extents = original()
    work = ROOT / "build/reagent-probe" / args.tag
    out = ROOT / "analysis/reagent" / args.tag
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / "exports").mkdir(exist_ok=True)
    os.environ["JAVA_HOME_OVERRIDE"] = str(args.java)
    os.environ["GHIDRA_INSTALL_DIR"] = str(args.ghidra)
    from pyghidra.launcher import HeadlessPyGhidraLauncher
    launcher = HeadlessPyGhidraLauncher(install_dir=args.ghidra)
    for kind in ("settings", "cache", "temp"):
        path = work / kind
        path.mkdir(exist_ok=True)
        launcher.vm_args.append(f"-Dapplication.{kind}dir={path}")
    launcher.vm_args.append("-Xmx2G")
    launcher.start()

    import pyghidra
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.task import TaskMonitor
    from ghidra_ai_bridge.exporters.runner import export_decompiled

    report = {
        "input_sha256": IMAGE_SHA256, "language": LANGUAGE,
        "loader": "BinaryLoader", "base": f"{BASE:08X}",
        "project": str(work / "project"), "selected_only": True,
        "whole_program_autoanalysis": False,
        "scope": "Static evidence only. Generic compiler ABI, untyped callees and Xenon extensions remain unverified.",
        "functions": [],
    }
    with pyghidra.open_program(
        ROOT / "analysis/simpsons.pe", project_location=work / "project",
        project_name="simpsons-bounded", program_name="simpsons-flat",
        analyze=False, language=LANGUAGE, compiler="default",
        loader="ghidra.app.util.opinion.BinaryLoader", nested_project_location=False,
    ) as api:
        program = api.getCurrentProgram()
        addr = program.getAddressFactory().getDefaultAddressSpace().getAddress
        tx = program.startTransaction("Bounded original .pdata evidence")
        commit = False
        try:
            if program.getImageBase().getOffset() == 0:
                program.setImageBase(addr(BASE), True)
            if program.getImageBase().getOffset() != BASE:
                raise ValueError("Ghidra flat image mapped to wrong base")
            if str(program.getLanguageID()) != LANGUAGE or program.getDefaultPointerSize() != 4:
                raise ValueError("Wrong Ghidra language or pointer size")
            memory, listing = program.getMemory(), program.getListing()
            fm, refs = program.getFunctionManager(), program.getReferenceManager()
            if args.inline_save_helper:
                helper, helper_size = 0x82A3C3C8, 24
                expected = bytes.fromhex("fb81ffd8fba1ffe0fbc1ffe8fbe1fff09181fff84e800020")
                if data[helper-BASE:helper-BASE+helper_size] != expected:
                    raise ValueError("Changed save helper; inline assumption rejected")
                helper_body = AddressSet(addr(helper), addr(helper+helper_size-1))
                command = DisassembleCommand(addr(helper), helper_body, False)
                if not command.applyTo(program, TaskMonitor.DUMMY):
                    raise ValueError("Cannot decode original save helper")
                function = fm.getFunctionAt(addr(helper))
                if function is None:
                    function = fm.createFunction("__savegprlr_28", addr(helper), helper_body,
                                                 SourceType.USER_DEFINED)
                function.setInline(True)
                report["inline_helper"] = {"address": f"{helper:08X}", "bytes": expected.hex(),
                    "reason": "Original helper only stores r28-r31 and r12; never writes r3."}
            for start in SELECTED:
                size = extents[start]
                body = AddressSet(addr(start), addr(start + size - 1))
                # Restrict disassembly to this exact original .pdata body.
                command = DisassembleCommand(addr(start), body, False)
                if not command.applyTo(program, TaskMonitor.DUMMY):
                    raise ValueError(f"Disassembly failed {start:08X}: {command.getStatusMsg()}")
                if fm.getFunctionAt(addr(start)) is None:
                    fm.createFunction(f"original_{start:08X}", addr(start), body, SourceType.USER_DEFINED)
                instructions, calls, indirect = [], [], []
                for pc in range(start, start + size, 4):
                    word = struct.unpack_from(">I", data, pc - BASE)[0]
                    mapped = memory.getInt(addr(pc)) & 0xFFFFFFFF
                    ins = listing.getInstructionAt(addr(pc))
                    if ins is None:
                        one = DisassembleCommand(addr(pc), AddressSet(addr(pc), addr(pc+3)), False)
                        one.applyTo(program, TaskMonitor.DUMMY)
                        ins = listing.getInstructionAt(addr(pc))
                    if mapped != word or ins is None or ins.getLength() != 4:
                        raise ValueError(f"Missing/mismatched original instruction {pc:08X}")
                    flows = [f"{a.getOffset():08X}" for a in ins.getFlows()]
                    instructions.append({"pc": f"{pc:08X}", "word": f"{word:08X}",
                                         "ghidra": str(ins), "flows": flows})
                    if word >> 26 == 18 and word & 1:
                        delta = word & 0x03FFFFFC
                        if delta & 0x02000000:
                            delta -= 0x04000000
                        target = (delta if word & 2 else pc + delta) & 0xFFFFFFFF
                        if f"{target:08X}" not in flows:
                            raise ValueError(f"Direct branch mismatch at {pc:08X}")
                        calls.append({"pc": f"{pc:08X}", "target": f"{target:08X}"})
                    if word == 0x4E800421:
                        indirect.append(f"{pc:08X}")
                # Independent decoder checks every word and position, with aliases reported.
                decoded = subprocess.run([
                    str(ROOT / "build/generator-ninja/SimpsonsDisasm.exe"),
                    str(ROOT / "analysis/simpsons.pe"), hex(BASE), hex(start), str(size // 4)
                ], text=True, capture_output=True, check=True, timeout=30).stdout
                (out / f"{start:08X}.original.txt").write_text(decoded, encoding="utf-8")
                rows = [re.fullmatch(r"([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+(\S+)(.*)", s)
                        for s in decoded.splitlines() if s.strip()]
                if len(rows) != len(instructions) or not all(rows):
                    raise ValueError("Independent decoder missing instructions")
                aliases = []
                for row, ins in zip(rows, instructions):
                    if row[1].upper() != ins["pc"] or row[2].upper() != ins["word"]:
                        raise ValueError("Independent decoder bytes/position mismatch")
                    if row[3].lower() != ins["ghidra"].split()[0].lower():
                        aliases.append({"pc": ins["pc"], "original": row[3], "ghidra": ins["ghidra"]})
                report["functions"].append({"address": f"{start:08X}", "size": size,
                    "sha256": hashlib.sha256(data[start-BASE:start-BASE+size]).hexdigest(),
                    "instructions": instructions, "direct_calls": calls,
                    "indirect_calls": indirect, "mnemonic_differences": aliases})
            report["pointer_bytes"] = program.getDefaultPointerSize()
            report["ghidra_version"] = str(launcher.app_info.version)
            commit = True
        finally:
            program.endTransaction(tx, commit)
        export_decompiled(program, fm, refs, str(out / "exports"), listing)
    report["all_original_words_match"] = True
    report["instruction_count"] = sum(len(f["instructions"]) for f in report["functions"])
    report["direct_call_count"] = sum(len(f["direct_calls"]) for f in report["functions"])
    report["indirect_call_count"] = sum(len(f["indirect_calls"]) for f in report["functions"])
    if original()[0] != data:
        raise ValueError("Input image changed during analysis")
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    annotate_exports(out, report)
    print(json.dumps({k: v for k, v in report.items() if k != "functions"}, indent=2))


if __name__ == "__main__":
    main()
