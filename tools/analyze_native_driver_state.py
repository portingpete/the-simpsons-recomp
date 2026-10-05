"""Offline evidence for the native driver state contract; no guest execution.

Only analysis/native-driver-state.json may be written. Uses the frozen driver
analyzer for original identity, .pdata and word-checked disassembly. The startup
ledger is manually reviewed, not an interpreter: each call PC and full original
body is retained for independent review. No native backend is exercised.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

import analyze_native_driver as original

ROOT = Path(__file__).resolve().parents[1]
BASE = original.BASE
hx = original.hx

# (call PC, scalar cache ID, value), reviewed from 824008E0. Repeated r4 is
# valid across 82400170: the entire leaf is checked, and does not modify r4.
SCALARS = [
    (0x824009C0,0x196,0), (0x824009CC,0x1A1,3),
    (0x824009D8,0x198,0), (0x824009E4,0x195,2),
    (0x824009FC,0x2C,6), (0x82400A08,0x30,1), (0x82400A10,0x28,1),
    (0x82400A28,0x6C,0), (0x82400A30,0x74,0), (0x82400A38,0x78,0),
    (0x82400A40,0x7C,0), (0x82400A4C,0x80,7), (0x82400A58,0x84,0),
    (0x82400A64,0x88,0xFFFFFFFF), (0x82400A6C,0x8C,0xFFFFFFFF),
    (0x82400C4C,0x48,6), (0x82400C58,0x4C,7), (0x82400C74,0x68,4),
    (0x82400C90,0x64,0), (0x82400C98,0x3C,0), (0x82400CA0,0x60,0),
    (0x82400CB4,0x19F,0), (0x82400CC0,0x38,2),
    (0x82400D08,0x1A5,0), (0x82400D10,0x1A6,0),
    (0x82400D18,0x1A7,0), (0x82400D20,0x197,0),
    (0x82400D28,0x1A3,0), (0x82400D34,0x1A0,0xFFFFFFFF),
    (0x82400D40,0x1A4,0),
]
SCALAR_MEANINGS = {
    0x28:"depth enable", 0x2C:"depth comparison", 0x30:"depth write enable",
    0x38:"cull front/back and front winding (three packed bits)",
    0x3C:"blend enable", 0x48:"source color blend factor",
    0x4C:"destination color blend factor", 0x60:"alpha test enable",
    0x64:"alpha reference / 255", 0x68:"alpha comparison",
    0x6C:"stencil enable", 0x74:"stencil fail", 0x78:"stencil depth fail",
    0x7C:"stencil pass", 0x80:"stencil comparison", 0x84:"stencil reference",
    0x88:"stencil read mask (low byte)", 0x8C:"stencil write mask (low byte)",
}
SAMPLERS = [(0x82400BC4,0,0), (0x82400BD4,4,0),
            (0x82400BEC,12,0), (0x82400C04,36,1)]
STAGES = [(0x82400C1C,"1..7",1,1), (0x82400C28,"1..7",4,1),
          (0x82400CD8,"0",1,3), (0x82400CE4,"0",3,0),
          (0x82400CF0,"0",4,3), (0x82400CFC,"0",6,0)]
FUNCTIONS = [
    0x823EDF20, 0x823FFE78, 0x82400040, 0x824001E0, 0x82400278,
    0x824008E0, 0x823F47F0, 0x8240EC28, 0x824025A8,
    0x8243F928, 0x8243FC38, 0x82440578, 0x82440698, 0x824408E0,
    0x82465948, 0x82466800, 0x82466A68,
]
LEAVES = {0x82400170:0x58, 0x824001C8:0x18, 0x82400258:0x20,
          0x823F4670:0x2C, 0x8240EBB0:0x1C, 0x8240EBD0:0x54,
          0x8240E960:0x148, 0x82401260:0x220}
# Explicit contiguous reviewed windows, NOT inferred function extents. They
# contain getters, setters and alignment padding as well as multiple leaves.
WINDOWS = [(0x82439590,0x2EA0,"SDK state/sampler getter/setter window"),
           (0x8243D0E8,0x10,"SDK scissor state tail wrapper")]
REFERENCES = [
    ("include/rex/graphics/xenos.h", "104-151; 298-365; 443-535; 677-731; 1174-1260"),
    ("include/rex/graphics/registers.h", "455-489; 681-733; 779-837"),
    ("include/rex/graphics/pipeline/render_target/cache.h", "45-78"),
    ("src/graphics/d3d12/texture_cache.cpp", "290-305; 419-433"),
    ("src/graphics/d3d12/render_target_cache.cpp", "1578-1604"),
    ("src/graphics/pipeline/shader/spirv_translator_rb.cpp", "26-215; 220-390"),
    ("src/graphics/pipeline/shader/dxbc_translator_om.cpp", "1619-1665"),
]


def decode_format(value):
    if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("Format must be a uint32")
    return {"value":hx(value), "surface_format":value & 63,
            "endian":(value >> 6) & 3, "tiled":(value >> 8) & 1,
            "sign_xyzw":[(value >> bit) & 3 for bit in (9,11,13,15)],
            "num_format":(value >> 17) & 1,
            "swizzle_xyzw":[(value >> bit) & 7 for bit in (18,21,24,27)],
            "unassigned_high_bits":value >> 30}


def decode_unsigned_float(code, mantissa_bits, exponent_bits, bias):
    """Mathematical format fixture, not a hardware rounding oracle."""
    if not 0 <= code < (1 << (mantissa_bits + exponent_bits)):
        raise ValueError("Out-of-range unsigned float encoding")
    exponent, fraction = code >> mantissa_bits, code & ((1 << mantissa_bits)-1)
    if not exponent:
        return math.ldexp(float(fraction), 1-bias-mantissa_bits)
    return math.ldexp(1 + fraction / (1 << mantissa_bits), exponent-bias)


def format_record(b, value):
    decoded = decode_format(value)
    surface = decoded["surface_format"]
    lookup = 7 if surface == 54 else surface  # 8243FC84..8C
    table = struct.unpack(">H",original.span(b,0x8206A028+2*lookup,2))[0]
    decoded.update({"surface_builder_lookup":lookup,
                    "table_address":hx(0x8206A028+2*lookup),
                    "table_halfword":f"0x{table:04X}",
                    "surface_kind":"depth" if surface in (22,23) else "color",
                    "render_target_format":(table >> 8) & 15})
    return decoded


def inspect(image, disassembler):
    b = image.read_bytes()
    pdata = original.extents(b)
    bodies = []
    for va in sorted(set(FUNCTIONS) | set(LEAVES)):
        n = pdata.get(va,LEAVES.get(va))
        if not n:
            raise ValueError(f"Unverified extent {hx(va)}")
        bodies.append({"address":hx(va), "size":n,
                       "extent":"original_pdata" if va in pdata else "reviewed_leaf",
                       "sha256":original.sha(original.span(b,va,n)),
                       "instructions":original.decode(b,va,n,disassembler,image)})
    for va,n,label in WINDOWS:
        bodies.append({"address":hx(va), "size":n, "extent":"reviewed_window_not_function",
                       "label":label,"sha256":original.sha(original.span(b,va,n)),
                       "instructions":original.decode(b,va,n,disassembler,image)})
    for entries,target in [(SCALARS,0x82400170),(SAMPLERS,0x82400278),(STAGES,0x824001E0)]:
        for pc,*_ in entries:
            if original.branch(pc,original.word(b,pc)) != (target,True):
                raise ValueError(f"Changed ledger call {hx(pc)}")
    sdk_tables = {}
    for name,va,count in [("scalar",0x82CD28B8,101),("sampler",0x82CD2D78,20)]:
        records=[]
        for i in range(count):
            get,set_,default=struct.unpack(">III",original.span(b,va+12*i,12))
            records.append({"id":hx(i*4), "address":hx(va+12*i),
                            "get":hx(get), "set":hx(set_), "default":hx(default)})
        sdk_tables[name]=records
    scalar_ledger=[]
    for pc,id_,value in SCALARS:
        initial=0 if id_ in (0x88,0x8C,0x198,0x1A0) else 0xFFFFFFFF
        row={"pc":hx(pc),"id":hx(id_),"value":hx(value),
             "pending_address":hx(0x82D0F3B0+id_*8),
             "applied_address":hx(0x82E3D580+id_*4),
             "enqueued_after_reset":initial != value,
             "meaning":SCALAR_MEANINGS.get(id_,"engine shader/pipeline shadow; not SDK-dispatched")}
        if id_<404:
            row["sdk_setter"] = sdk_tables["scalar"][id_//4]["set"]
        scalar_ledger.append(row)
    cpu_offsets={0x1240:1,0x1244:1,0x1248:0,0x124C:1,0x1250:1,0x1254:1,
                 0x1258:8,0x125C:0,0x1260:0xFFFFFFFF,0x1264:0xFFFFFFFF,
                 0x1268:0,0x126C:0,0x1270:2,0x1274:0,0x1278:1,
                 0x127C:0x3F800000,0x1280:0,0x1284:2,0x1348:5,0x134C:6,
                 0x1350:5,0x1354:1,0x2F90:0,0x2F94:0,0x2F98:0,
                 0x2FAC:0,0x2FB0:0,0x2FB4:0,0x2FB8:0}
    refs=[]
    for relative,lines in REFERENCES:
        path=Path("K:/Simpsons/RexGlueCurrent")/relative
        refs.append({"path":str(path),"reviewed_lines":lines,
                     "sha256":original.sha(path.read_bytes()),
                     "use":"read-only semantic reference; no renderer code imported"})
    return {
        "schema_version":1,
        "scope":"Static state/format contract, not a native rendering test or complete SDK state implementation",
        "image":{"path":str(image),"size":len(b),"sha256":original.sha(b)},
        "tools":{"analyzer_sha256":original.sha(Path(__file__).read_bytes()),
                 "frozen_helper_sha256":original.sha(Path(original.__file__).read_bytes()),
                 "disassembler_sha256":original.sha(disassembler.read_bytes())},
        "verification":{"function_count":len(FUNCTIONS)+len(LEAVES),"additional_windows":len(WINDOWS),
                        "instruction_words_checked":sum(len(q["instructions"]) for q in bodies),
                        "scalar_calls_checked":len(SCALARS),"sampler_call_sites_checked":len(SAMPLERS),
                        "stage_call_sites_checked":len(STAGES),
                        "all_disassembly_words_equal_original":True},
        "formats":[format_record(b,x) for x in (0x182801B6,0x28280136,0x1A220197,0x1A2201BF)],
        "format_provenance":"8243F928 constructs texture fields; 8243FC38 maps surface format through 8206A028. 1A2201BF is a contrast fixture, not a request-2 allocation.",
        "sdk_default_tables":sdk_tables,
        "startup_scalar_calls":scalar_ledger,
        "startup_sampler_calls":[{"pc":hx(pc),"stages":"0..7","id":id_,"value":value} for pc,id_,value in SAMPLERS],
        "startup_stage_calls":[{"pc":hx(pc),"stage":stage,"id":id_,"value":value} for pc,stage,id_,value in STAGES],
        "startup_cpu_words":[{"address":hx(0x82D0D170+offset),"value":hx(value)} for offset,value in sorted(cpu_offsets.items())],
        "startup_sampler_cpu_records":{"base":hx(0x82D0E3F8),"stride":24,"count":8,"words":[0,1,1,2,0,1]},
        "capability_filter_test":{"address":hx(0x8206AA70),"word":hx(original.word(b,0x8206AA70)),
                                  "mask":hx(0x200),"linear_path":bool(original.word(b,0x8206AA70)&0x200)},
        "float_fixtures":{"f10_7e3":[{"code":hx(x),"value":decode_unsigned_float(x,7,3,3)} for x in (0,1,127,128,384,1023)],
                          "depth_20e4":[{"code":hx(x),"value":decode_unsigned_float(x,20,4,15)} for x in (0,1,0xFFFFF,0x100000,0xE00000,0xF00000,0xFFFFFF)],
                          "limitation":"Encoding values only; original raster depth rounding mode not established"},
        "references":refs,
        "functions_and_windows":bodies,
    }


def self_test():
    checks=0
    def check(value):
        nonlocal checks
        if not value:
            raise AssertionError("Driver state fixture failed")
        checks+=1
    color=decode_format(0x182801B6); front=decode_format(0x28280136)
    depth=decode_format(0x1A220197); float10=decode_format(0x1A2201BF)
    check(color["surface_format"]==54 and color["num_format"]==0)
    check(color["swizzle_xyzw"]==[2,1,0,3] and color["endian"]==2)
    check(front["swizzle_xyzw"]==[2,1,0,5] and front["endian"]==0)
    check(depth["surface_format"]==23 and depth["num_format"]==1)
    check(float10["surface_format"]==63 and float10["num_format"]==1)
    check(all(decode_format(x)["tiled"] for x in (0x182801B6,0x28280136,0x1A220197)))
    f10=[decode_unsigned_float(x,7,3,3) for x in range(1024)]
    check(math.isclose(f10[1],2**-9,rel_tol=0,abs_tol=1e-12) and math.isclose(f10[128],0.25,rel_tol=0,abs_tol=1e-12) and math.isclose(f10[-1],31.875,rel_tol=0,abs_tol=1e-12))
    check(all(x<y for x,y in zip(f10,f10[1:])))
    check(all(struct.unpack("<e",struct.pack("<e",v))[0]==v for v in f10))
    check(decode_unsigned_float(1,20,4,15)==2**-34)
    check(decode_unsigned_float(0x100000,20,4,15)==2**-14)
    check(decode_unsigned_float(0xF00000,20,4,15)==1.0)
    check(decode_unsigned_float(0xFFFFFF,20,4,15)==2-2**-20)
    check(len({id_ for _,id_,_ in SCALARS})==len(SCALARS))
    check(dict((id_,v) for _,id_,v in SCALARS)[0x2C]==6)
    for bad in [lambda:decode_format(-1),lambda:decode_format(1<<32),
                lambda:decode_unsigned_float(1024,7,3,3),
                lambda:decode_unsigned_float(-1,20,4,15)]:
        try:
            bad()
        except ValueError:
            checks+=1
        else:
            raise AssertionError("Invalid fixture accepted")
    return checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test",action="store_true")
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    if args.self_test:
        print(json.dumps({"self_tests_passed":self_test()})); return
    if args.report and args.report.resolve() != (ROOT/"analysis/native-driver-state.json").resolve():
        raise ValueError("Only analysis/native-driver-state.json is an allowed output")
    report=inspect(ROOT/"analysis/simpsons.pe",ROOT/"build/generator-ninja/SimpsonsDisasm.exe")
    out=json.dumps(report,indent=2,ensure_ascii=True)+"\n"
    if args.report:
        args.report.write_text(out,encoding="utf-8",newline="\n")
        print(json.dumps(report["verification"]))
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    try:
        main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(f"native-driver-state: {error}",file=sys.stderr)
        raise SystemExit(1)
