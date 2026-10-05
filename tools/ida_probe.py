"""Bounded headless IDAPython evidence; never executes guest code.

Run inside idat -A -a -t -pppc with -S".../ida_probe.py TAG ppc64" and a new
database under build/ida-probe/TAG/. Only analysis/ida/TAG and that database
are written. The input is the pinned, already-derived flat memory image, NOT
a disk-layout PE: loading it through the ordinary PE loader would be wrong.
PPC64 pseudocode has a host-sized pointer model and untyped callees/helpers;
it is inspection evidence, not recovered executable C or a verified guest ABI.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import traceback

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
SELECTED = (0x823EE6C8, 0x823EDF20, 0x823EE1A8, 0x82756480)


def hx(value):
    return f"0x{value:08X}"


def image_evidence():
    data = (ROOT / "analysis/simpsons.pe").read_bytes()
    if len(data) != IMAGE_SIZE or hashlib.sha256(data).hexdigest() != IMAGE_SHA256:
        raise ValueError("Original-derived image identity mismatch")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0":
        raise ValueError("Invalid image header")
    machine, count = struct.unpack_from("<HH", data, pe+4)
    optional_size = struct.unpack_from("<H", data, pe+20)[0]
    if machine != 0x1F2 or struct.unpack_from("<I", data, pe+52)[0] != BASE:
        raise ValueError("Wrong original architecture/base")
    sections = {}
    for n in range(count):
        name, size, rva = struct.unpack_from("<8sII", data, pe+24+optional_size+40*n)
        sections[name.rstrip(b"\0").decode("ascii")] = (rva, size)
    offset, size = sections[".pdata"]
    extents = {}
    if size % 8:
        raise ValueError("Partial original pdata record")
    for pos in range(offset, offset+size, 8):
        start, packed = struct.unpack_from(">II", data, pos)
        if start in SELECTED:
            if start in extents:
                raise ValueError("Duplicate original pdata entry")
            extents[start] = ((packed >> 8) & 0x3FFFFF)*4
    if set(extents) != set(SELECTED):
        raise ValueError("Selected original extent missing")
    for start, size in extents.items():
        if not size or start & 3 or not BASE <= start < start+size <= BASE+len(data):
            raise ValueError("Invalid selected extent")
    return data, extents


def direct_branch(pc, word):
    if word >> 26 != 18:
        return None
    disp = word & 0x03FFFFFC
    if disp & 0x02000000:
        disp -= 0x04000000
    return ((disp if word & 2 else pc+disp) & 0xFFFFFFFF, bool(word & 1))


def compare(tag):
    """Independent installed offline decoder against every exported IDA word."""
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", tag):
        raise ValueError("Invalid comparison run tag")
    output = ROOT / "analysis/ida" / tag
    report = json.loads((output / "report.json").read_text(encoding="utf-8"))
    data, extents = image_evidence()
    summary = {"input_sha256": IMAGE_SHA256, "functions": [], "instruction_count": 0,
               "direct_calls": 0, "indirect_calls": 0, "all_original_words_match": True,
               "all_direct_call_refs_match": True,
               "scope": "static instruction/call evidence; not semantic or ABI equivalence"}
    if {int(f["address"], 16) for f in report["functions"]} != set(SELECTED):
        raise ValueError("Incomplete or changed function selection")
    for f in report["functions"]:
        start = int(f["address"], 16)
        expected = extents[start] // 4
        disasm = subprocess.run([str(ROOT / "build/generator-ninja/SimpsonsDisasm.exe"),
                                str(ROOT / "analysis/simpsons.pe"), hx(BASE), hx(start), str(expected)],
                               capture_output=True, text=True, check=True, timeout=15)
        (output / f"{start:08X}.original.txt").write_text(disasm.stdout, encoding="utf-8")
        rows = [re.fullmatch(r"([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+(\S+)(.*)", line)
                for line in disasm.stdout.splitlines() if line.strip()]
        if len(rows) != expected or len(f["instructions"]) != expected or not all(rows):
            raise ValueError("Missing instruction evidence")
        differences = []
        direct, indirect = [], []
        for i, (row, instruction) in enumerate(zip(rows, f["instructions"])):
            pc, word = int(row[1], 16), int(row[2], 16)
            original = struct.unpack_from(">I", data, pc-BASE)[0]
            if pc != start+4*i or pc != int(instruction["pc"], 16) or word != original or word != int(instruction["word"], 16):
                raise ValueError("Original/IDA/independent decoder word mismatch")
            ida_mnemonic = instruction["ida"].split()[0]
            if ida_mnemonic.lower() != row[3].lower():
                differences.append({"pc": hx(pc), "word": f"{word:08X}", "original_decoder": row[3],
                                    "ida": ida_mnemonic})
            edge = direct_branch(pc, word)
            if edge and edge[1]:
                direct.append({"pc": hx(pc), "target": hx(edge[0])})
                if hx(edge[0]) not in instruction["code_refs"]:
                    raise ValueError("IDA direct-call reference does not match original branch")
            if word == 0x4E800421:
                indirect.append(hx(pc))
        if direct != [{"pc": c["pc"], "target": c["target"]} for c in f["direct_calls"]]:
            raise ValueError("Changed direct-call list")
        if indirect != [c["pc"] for c in f["indirect_calls"]]:
            raise ValueError("Changed indirect-call list")
        summary["functions"].append({"address": hx(start), "instructions": expected,
            "original_function_sha256": hashlib.sha256(data[start-BASE:start-BASE+extents[start]]).hexdigest(),
            "direct_calls": len(direct), "indirect_calls": len(indirect), "mnemonic_differences": differences})
        summary["instruction_count"] += expected
        summary["direct_calls"] += len(direct)
        summary["indirect_calls"] += len(indirect)
    (output / "comparison.json").write_text(json.dumps(summary, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


def run():
    import idc
    import ida_auto
    import ida_bytes
    import ida_funcs
    import ida_hexrays
    import ida_ida
    import ida_idaapi
    import ida_idp
    import ida_kernwin
    import ida_lines
    import ida_loader
    import ida_name
    import ida_pro
    import ida_segment
    import ida_typeinf
    import ida_ua
    import idautils

    tag = idc.ARGV[1] if len(idc.ARGV) in (2, 3) else ""
    mode = idc.ARGV[2] if len(idc.ARGV) == 3 else "ppc64"
    if mode not in ("ppc64", "ilp32", "ppc32"):
        raise ValueError("Mode must be ppc64, ilp32 or ppc32")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", tag):
        raise ValueError("Supply one lowercase run tag")
    output = (ROOT / "analysis/ida" / tag).resolve()
    db_root = (ROOT / "build/ida-probe" / tag).resolve()
    db_path = Path(idc.get_idb_path()).resolve()
    if not db_path.is_relative_to(db_root):
        raise ValueError("Refusing to change a database outside this probe's run directory")
    output.mkdir(parents=True, exist_ok=True)
    if (output / "report.json").exists():
        raise ValueError("Use a fresh run tag to preserve previous evidence")
    report = {"tag": tag, "status": "started", "ida_version": ida_kernwin.get_kernel_version(),
              "python_version": sys.version, "database": str(db_path),
              "input": str(ROOT / "analysis/simpsons.pe"), "input_sha256": IMAGE_SHA256,
              "base": hx(BASE), "mode": mode, "functions": [], "analysis_scope": "four original pdata extents"}

    def checkpoint():
        (output / "report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")

    code = 1
    try:
        checkpoint()
        data, extents = image_evidence()
        report["initial_64bit"] = ida_ida.inf_is_64bit()
        report["initial_32bit"] = ida_ida.inf_is_32bit_exactly()
        if ida_segment.get_segm_qty():
            raise ValueError("Probe requires a new empty database")
        ida_ida.inf_set_auto_enabled(False)
        ida_ida.inf_set_af(ida_ida.AF_TRACE)
        ida_ida.inf_set_64bit(mode == "ppc64")
        ida_ida.inf_set_32bit(True)
        ida_ida.inf_set_ilp32(mode == "ilp32")
        ida_ida.inf_set_be(True)
        ida_typeinf.set_compiler_id(ida_typeinf.COMP_MS)
        report["processor"] = ida_ida.inf_get_procname()
        report["processor_id"] = ida_idp.ph.id
        report["big_endian"] = ida_ida.inf_is_be()
        if ida_idp.ph.id != ida_idp.PLFM_PPC or not report["big_endian"]:
            raise ValueError("Need big-endian PowerPC processor")
        segment = ida_segment.segment_t()
        segment.start_ea, segment.end_ea = BASE, BASE+len(data)
        segment.bitness = 2 if mode == "ppc64" else 1
        # Conservative mutable data: globals share this flat evidence segment.
        # Marking all bytes readonly would incorrectly fold runtime state.
        segment.perm = ida_segment.SEGPERM_READ | ida_segment.SEGPERM_WRITE | ida_segment.SEGPERM_EXEC
        if not ida_segment.add_segm_ex(segment, "flat_image", "CODE", ida_segment.ADDSEG_NOAA):
            raise ValueError("Cannot create derived-image segment")
        if not ida_loader.mem2base(data, BASE, 0):
            raise ValueError("Cannot load original-derived bytes")
        report["database_64bit"] = ida_ida.inf_is_64bit()
        report["database_32bit"] = ida_ida.inf_is_32bit_exactly()
        report["database_ilp32"] = ida_ida.inf_is_ilp32()
        report["compiler"] = {"id": ida_ida.inf_get_cc_id(), "int": ida_ida.inf_get_cc_size_i(),
                              "long": ida_ida.inf_get_cc_size_l(), "long_long": ida_ida.inf_get_cc_size_ll()}
        if ida_bytes.get_bytes(BASE, len(data)) != data:
            raise ValueError("IDA byte mapping differs from original image")
        report["loaded_bytes_match"] = True
        # No PE loader, signatures, recursive function discovery, or whole-image
        # analysis. Only these four explicit pdata extents become instructions.
        for start, size in extents.items():
            for pc in range(start, start+size, 4):
                if ida_ua.create_insn(pc) != 4:
                    raise ValueError("IDA instruction decode failed at "+hx(pc))
                edge = direct_branch(pc, struct.unpack_from(">I", data, pc-BASE)[0])
                if edge and not ida_name.get_name(edge[0]):
                    ida_name.set_name(edge[0], f"original_{edge[0]:08X}", ida_name.SN_NOWARN)
            if not ida_funcs.add_func(start, start+size):
                raise ValueError("Cannot define original function "+hx(start))
            ida_name.set_name(start, f"original_{start:08X}", ida_name.SN_NOWARN)
        ida_auto.auto_cancel(0, ida_idaapi.BADADDR)
        ida_ida.inf_set_auto_enabled(True)
        for start, size in extents.items():
            ida_auto.plan_and_wait(start, start+size)
        report["function_count"] = ida_funcs.get_func_qty()
        report["plugin_loaded"] = bool(ida_loader.load_plugin("hexppc"))
        report["decompiler_initialized"] = bool(ida_hexrays.init_hexrays_plugin())
        if report["decompiler_initialized"]:
            report["decompiler_version"] = ida_hexrays.get_hexrays_version()
        checkpoint()
        decompiler_broken = False
        for start in SELECTED:
            size = extents[start]
            result = {"address": hx(start), "size": size, "extent_source": "original .pdata",
                      "instructions": [], "direct_calls": [], "indirect_calls": []}
            report["functions"].append(result)
            asm = []
            for pc in range(start, start+size, 4):
                word = struct.unpack_from(">I", data, pc-BASE)[0]
                text = ida_lines.tag_remove(ida_lines.generate_disasm_line(pc, 0) or "")
                refs = list(idautils.CodeRefsFrom(pc, False))
                row = {"pc": hx(pc), "word": f"{word:08X}", "ida": text,
                       "code_refs": [hx(ea) for ea in refs]}
                result["instructions"].append(row)
                asm.append(f"{pc:08X}  {word:08X}  {text}")
                edge = direct_branch(pc, word)
                if edge and edge[1]:
                    result["direct_calls"].append({"pc": hx(pc), "target": hx(edge[0]),
                        "ida_ref_matches": edge[0] in refs})
                if word == 0x4E800421:
                    result["indirect_calls"].append({"pc": hx(pc), "kind": "bctrl", "ida": text})
            (output / f"{start:08X}.asm.txt").write_text("\n".join(asm)+"\n", encoding="utf-8")
            try:
                if decompiler_broken:
                    raise RuntimeError("Not retried after an internal decompiler failure in this process")
                if not report["decompiler_initialized"]:
                    raise RuntimeError("PPC decompiler did not initialize")
                failure = ida_hexrays.hexrays_failure_t()
                cfunc = ida_hexrays.decompile(start, failure)
                if cfunc is None:
                    decompiler_broken = failure.code == -1
                    raise RuntimeError(f"{failure.code} at {hx(failure.errea)}: {failure.desc()}")
                pseudocode = "\n".join(ida_lines.tag_remove(line.line) for line in cfunc.get_pseudocode())+"\n"
                (output / f"{start:08X}.c.txt").write_text(pseudocode, encoding="utf-8")
                result["decompiled"] = True
                result["pseudocode_lines"] = len(pseudocode.splitlines())
            except Exception as error:
                if "Internal error" in str(error):
                    decompiler_broken = True
                result["decompiled"] = False
                result["decompile_error"] = str(error)
            checkpoint()
        report["database_saved"] = bool(ida_loader.save_database(str(db_path), 0))
        report["status"] = "complete"
        code = 0 if all(f.get("decompiled") for f in report["functions"]) else 3
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
    finally:
        report["exit_code"] = code
        checkpoint()
        print("IDA_PROBE", report["status"], "exit", code, str(output))
        ida_pro.qexit(code)


if __name__ == "__main__":
    if "--check-image" in sys.argv:
        data, extents = image_evidence()
        print(IMAGE_SHA256, {hx(pc): hx(size) for pc, size in extents.items()})
    elif len(sys.argv) == 3 and sys.argv[1] == "--compare":
        compare(sys.argv[2])
    else:
        run()
