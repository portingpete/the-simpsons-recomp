"""Verify the identified US executable's bounded renderer evidence, read-only.

This is an offline provenance tool, not a renderer or GPU command interpreter.
It requires the project's existing SimpsonsDisasm.exe and generated AOT source.
Only --report is written. Default inputs are never generated or modified here.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA256 = "6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0"
IMAGE_SIZE = 15466496

# Names below describe reviewed behavior, not recovered debugging symbols.
FUNCTIONS = {
    0x823EC490: "engine request adapter",
    0x823EC9E0: "engine start, request 2 and plugin initialization",
    0x823ECB98: "engine plugin registration",
    0x823ECE30: "engine open and driver callback installation",
    0x823ECF58: "engine open wrapper",
    0x823ED028: "engine core initialization",
    0x823EDF20: "platform renderer start",
    0x823EE1A8: "platform renderer stop",
    0x823EE6C8: "camera target and viewport selection",
    0x823EE7F0: "platform camera end",
    0x823F00C0: "platform camera begin",
    0x823F03A0: "29-slot engine standard callback installation",
    0x823F0630: "platform engine request dispatcher",
    0x823F1800: "camera end callback",
    0x823F1870: "camera begin callback",
    0x823F1A08: "camera end dispatch",
    0x823F1A18: "camera begin dispatch",
    0x823F1DB0: "camera object construction",
    0x823F62A0: "platform raster destruction",
    0x823F7070: "platform raster creation",
    0x823FD900: "texture final destruction",
    0x823FDD58: "texture list iteration",
    0x823FDEC8: "texture reference decrement",
    0x823FE020: "dictionary texture insertion",
    0x823FE0A8: "borrowed case-insensitive texture lookup",
    0x823FEC80: "texture dictionary destruction",
    0x823FF7A8: "native texture dictionary stream read",
    0x82407DC0: "raster destruction wrapper",
    0x82408130: "raster construction wrapper",
    0x8240A278: "native texture stream callback",
    0x82439F00: "culling setter",
    0x82439F60: "alpha test setter",
    0x8243A010: "replacement blend setter",
    0x8243A438: "alpha reference setter",
    0x8243A4A0: "alpha comparison setter",
    0x8243A6C8: "depth test setter",
    0x8243B3B0: "expanded target blend-format toggle",
    0x8243BA40: "sampler filter setter",
    0x8243BBD0: "sampler filter setter",
    0x8243CB80: "blend control setter",
    0x82451FB0: "aligned SDK device storage allocation",
    0x82452540: "console device creation",
    0x82466E48: "console engine initialization, forbidden replacement level",
    0x82466F30: "console device initialization",
    0x82533970: "linear source rows to tiled destination copy",
    0x82534228: "texture block dimensions and level upload adapter",
    0x82543948: "format block dimensions",
    0x827142D8: "camera and two raster construction",
    0x826F24A0: "ITXD release entry",
    0x826F24D8: "ITXD texture walk and ownership attachment",
    0x826F26B0: "ITXD load entry",
    0x826F7618: "resource owner attachment",
    0x826F8168: "resource owner group release",
    0x82711358: "typed resource release dispatcher",
    0x82711828: "typed resource load dispatcher",
    0x82736D50: "copied ITXD release",
    0x82736EA8: "copied ITXD relocation",
    0x82736F58: "ITXD allocation and copy",
    0x82740CE8: "pooled record allocation, not a draw",
    0x82751510: "copy selected viewport scale record",
    0x82752090: "select camera viewport scale record",
    0x82756480: "screen quad engine boundary",
    0x8285F640: "frontend loop object construction",
    0x8285FDA0: "frontend loop tick",
    0x8285FF40: "frontend stream request",
    0x82860790: "frontend activation",
    0x828609C8: "loop name key adapter",
    0x82860AA0: "loop registration",
    0x82860C08: "current loop tick dispatch",
    0x82861F48: "startup, renderer and loop registration",
    0x828625A0: "loading background and five animated quads",
    0x82862A28: "loading texture dictionary acquisition",
    0x82862B18: "loading texture dictionary release",
    0x82862D50: "frontend loading progress",
    0x82875BF0: "engine initialization orchestration",
}

FACTS = [
    ("frontend_registration", "static_verified", [0x828621EC, 0x828621FC, 0x82862210, 0x82860C28],
     "The 32-byte loop at startup SP+0xD0 is registered under FrontendMainLoop; its vtable +0x0C selects 8285FDA0."),
    ("quad_abi", "static_verified", [0x82756494, 0x827564A4, 0x827564AC, 0x827564B4, 0x82756518],
     "r3 camera; f1..f4 rectangle endpoints; f5..f8 UV endpoints; r8 float4 color; r9 blend selector; r10 optional texture wrapper."),
    ("quad_geometry", "static_verified", [0x827564C8, 0x827564D8, 0x82756520, 0x82756534, 0x827566E8, 0x827567E8, 0x82756810],
     "Signed raster offsets and selected engine scale form clip XY. Four vertices TL,TR,BL,BR; stride16 textured or8 flat; primitive code6; transient data finalized before return."),
    ("loading_resources", "static_verified", [0x82862A68, 0x82862A9C, 0x82862ABC, 0x82862AD0, 0x82862B60],
     "Loading artwork comes from the embedded stream at8215F820, dictionary lookup frame1/frame2, released as a dictionary. It is not the frontend_global.itxd sample."),
    ("allocation_correction", "static_verified", [0x82740D10, 0x82740D34, 0x82740D4C, 0x82740D54],
     "82740CE8 pops a free-list record, decrements its free count, calls initializer82740C28, returns the record; no draw claim."),
    ("engine_start_dispatch", "static_and_boot021", [0x823ECA04, 0x823ECA14, 0x823EC4C0, 0x823F0888, 0x823EE03C],
     "Engine start requests2 through the driver adapter. The observed dispatcher starts the platform renderer, which calls console device creation."),
    ("device_publication", "static_verified", [0x82452568, 0x824525DC, 0x824525E8],
     "SDK device output is cleared first and published only after successful initialization. Live allocated device at boot failure is not a published usable device."),
    ("raster_callbacks", "static_verified", [0x82408188, 0x824081BC, 0x82407DF8, 0x82407E00],
     "Raster create uses engine+0x58 (slot4) and destroy uses+0x5C (slot5). Slots2/3 are pixel conversion, not raster allocation."),
    ("stream_texture_callback", "static_verified", [0x823FF8DC, 0x823FF8E4, 0x823FF8E8, 0x823FF8F0, 0x823FF924],
     "Native stream reader slot26 at engine+0xB0 receives r3=stream,r4=&texture output,r5=chunk length; loader checks success/nonzero output before dictionary insertion."),
    ("serialized_linear_blocks", "static_verified", [0x8240A6DC, 0x8240A70C, 0x8240A758, 0x8240A764, 0x8240A77C, 0x8240A7D8, 0x82534474, 0x82533ACC, 0x82533BC4, 0x82533C44],
     "Native stream level size is byteswapped; compressed source pitch is level size divided by block rows. Payload enters a temporary buffer and a row/column-linear source copy with independently tiled destination addressing. Serialized stream layout is not ITXD native layout."),
    ("replacement_plan", "design_inference", [0x823ECA14, 0x82756480],
     "A native engine driver must establish real resource/target/state services and the screen-quad boundary; returning success from console GPU initialization does not satisfy this contract."),
]

UNRESOLVED = [
    "This report does not demonstrate dynamic execution of the frontend quad; boot021 stops during renderer initialization.",
    "The whole renderer and every platform callback have not been recovered. Names are behavior labels, not original symbols.",
    "Remaining inherited sampler/stencil/depth-write/color-mask state must be established or explicitly rejected by a native bridge.",
    "Screen shader Z/W output and exact arithmetic require their own original-byte proof before claiming pixel identity.",
    "ITXD relocation/loading and the embedded stream loader must not be conflated merely because both ultimately expose raster objects.",
]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def hx(value: int) -> str:
    return f"0x{value:08X}"


def span(image: bytes, address: int, size: int) -> bytes:
    offset = address - BASE
    if size < 0 or offset < 0 or offset + size > len(image):
        raise ValueError(f"Image range out of bounds: {hx(address)} + {size}")
    return image[offset:offset + size]


def u32(image: bytes, address: int) -> int:
    if address & 3:
        raise ValueError("Unaligned original word")
    return struct.unpack(">I", span(image, address, 4))[0]


def branch(address: int, word: int):
    """Decode only PPC direct I-form branches, never execute guest code."""
    if address & 3 or not 0 <= word <= 0xFFFFFFFF:
        raise ValueError("Invalid instruction address/word")
    if word >> 26 != 18:
        return None
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    return ((displacement if word & 2 else address + displacement) & 0xFFFFFFFF,
            bool(word & 1))


def pe_sections(image: bytes):
    if len(image) != IMAGE_SIZE or sha(image) != IMAGE_SHA256:
        raise ValueError("Unsupported or modified original flat PE (size/SHA256)")
    if image[:2] != b"MZ":
        raise ValueError("Missing MZ")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, count, _, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", image, pe + 4)
    opt = pe + 24
    if machine != 0x1F2 or struct.unpack_from("<H", image, opt)[0] != 0x10B:
        raise ValueError("Unexpected architecture")
    if struct.unpack_from("<I", image, opt + 28)[0] != BASE:
        raise ValueError("Unexpected image base")
    sections = {}
    for i in range(count):
        off = opt + optional_size + 40 * i
        name, size, rva = struct.unpack_from("<8sII", image, off)
        name = name.rstrip(b"\0").decode("ascii")
        flags = struct.unpack_from("<I", image, off + 36)[0]
        # The flat retail mapping omits part of discardable .reloc, while its
        # original PE section header retains the larger virtual extent.
        if not flags & 0x02000000:
            span(image, BASE + rva, size)
        if name in sections:
            raise ValueError("Duplicate PE section")
        sections[name] = (BASE + rva, size)
    return sections


def pdata_functions(image: bytes, sections):
    address, size = sections[".pdata"]
    if size % 8:
        raise ValueError("Partial .pdata entry")
    text, text_size = sections[".text"]
    result = {}
    for offset in range(0, size, 8):
        start, packed = struct.unpack(">II", span(image, address + offset, 8))
        if start == packed == 0:
            continue
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if start & 3 or not length or not text <= start < start + length <= text + text_size:
            raise ValueError("Invalid .pdata function range")
        if start in result:
            raise ValueError("Duplicate .pdata function")
        result[start] = length
    return result


def aot_index(root: Path):
    result = {}
    for path in sorted(root.glob("ppc_recomp.*.cpp")):
        source = path.read_text(encoding="utf-8")
        matches = list(re.finditer(r"PPC_FUNC_IMPL\(__imp__(\w+)\)", source))
        for i, match in enumerate(matches):
            end = matches[i + 1].start() if i + 1 < len(matches) else len(source)
            body = source[match.start():end]
            trace = re.search(r"PPC_TRACE_ENTRY\(ctx, (0x[0-9A-Fa-f]+)\)", body)
            if not trace:
                continue
            address = int(trace[1], 16)
            comments = re.findall(r"^\s*// (.+)$", body, re.M)
            if address in result:
                raise ValueError("Duplicate AOT function")
            result[address] = (path.name, len(comments) * 4, match[1])
    if not result:
        raise ValueError("No generated AOT source found")
    return result


def aot_comments(path: Path, address: int, symbol: str):
    source = path.read_text(encoding="utf-8")
    marker = f"PPC_FUNC_IMPL(__imp__{symbol})"
    start = source.find(marker)
    if start < 0:
        raise ValueError("AOT function disappeared during inspection")
    end = source.find("PPC_FUNC_IMPL(", start + len(marker))
    body = source[start:end if end >= 0 else len(source)]
    return re.findall(r"^\s*// (.+)$", body, re.M), source.count("\n", 0, start) + 1


def normalize_instruction(text):
    return re.sub(r"\s+", "", text).lower()


def verify_disassembly(image, start, size, lines, comments):
    if not size or size % 4 or len(lines) != size // 4 or len(comments) != len(lines):
        raise ValueError(f"Incomplete instruction coverage at {hx(start)}")
    result = []
    for i, (line, comment) in enumerate(zip(lines, comments)):
        match = re.fullmatch(r"([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+(.+?)\s*", line)
        if not match:
            raise ValueError("Malformed disassembler line")
        address, word = int(match[1], 16), int(match[2], 16)
        if address != start + i * 4 or word != u32(image, address):
            raise ValueError("Disassembly address/word differs from original bytes")
        instruction = match[3].strip()
        if normalize_instruction(instruction) != normalize_instruction(comment):
            raise ValueError(f"AOT comment differs from original disassembly at {hx(address)}: {comment!r} / {instruction!r}")
        result.append({"address": hx(address), "word": f"{word:08X}", "instruction": instruction})
    return result


def boot_frames(log: bytes):
    source = log.decode("utf-8")
    pattern = (r"\[GUEST FRAME\] depth=(\d+) sp=(0x[0-9A-Fa-f]+) "
               r"caller_sp=(0x[0-9A-Fa-f]+) saved_lr=(0x[0-9A-Fa-f]+)")
    frames = []
    previous = None
    for match in re.finditer(pattern, source):
        depth = int(match[1]); sp, caller, lr = (int(x, 16) for x in match.groups()[1:])
        if depth != len(frames) or sp & 15 or caller & 15 or caller <= sp or (previous is not None and sp != previous):
            raise ValueError("Malformed or discontinuous logged backchain")
        frames.append({"depth": depth, "sp": hx(sp), "caller_sp": hx(caller), "saved_lr": hx(lr)})
        previous = caller
    expected = [0x82466F88, 0x824525E0, 0x823EE040, 0x823F088C, 0x823EC4C4,
                0x823ECA18, 0x82875D0C, 0x828620AC, 0x823BC990, 0x82432418, 0]
    if [int(x["saved_lr"], 16) for x in frames] != expected:
        raise ValueError("Log is not the identified boot021 renderer backchain")
    if "unimplemented import __imp__VdInitializeEngines" not in source:
        raise ValueError("Missing identified boot failure")
    return frames


def boot_registers(log: bytes):
    source = log.decode("utf-8")
    prefixes = ("[AOT FAILURE]", "[ARGUMENTS]", "[GUEST STACK]")
    result = {}
    for prefix in prefixes:
        lines = [line for line in source.splitlines() if line.startswith(prefix)]
        if len(lines) != 1:
            raise ValueError(f"Missing or ambiguous boot record: {prefix}")
        pairs = re.findall(r"\b(pc|function|lr|sp|r\d+)=(0x[0-9A-Fa-f]+)", lines[0])
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate logged register")
            result[key] = int(value, 16)
    expected = {"pc": 0x82466E80, "function": 0x82466E48, "lr": 0x82466E80,
                "r3": 0x16000000, "r4": 0x82466430, "r5": 0,
                "r6": 0x8206DA30, "r7": 0x8206DEB0}
    if any(result.get(key) != value for key, value in expected.items()):
        raise ValueError("Boot registers differ from the original identified import call")
    if not result.get("r31") or result["r31"] & 0x7F:
        raise ValueError("Missing or unaligned observed device allocation")
    return {key: hx(value) for key, value in sorted(result.items())}


def standard_callbacks(image):
    """Recover only the literal pairs in this verified registration function.

    A bounded constant-expression reader for addi/addis/stw/cmpwi, not guest
    execution. Any unfamiliar instruction or unresolved stored value rejects.
    """
    registers = {}
    stack_words = {}
    for pc in range(0x823F03AC, 0x823F05D0, 4):
        word = u32(image, pc)
        op, rt, ra = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        immediate = word & 0xFFFF
        signed = immediate - 0x10000 if immediate & 0x8000 else immediate
        if op in (14, 15):
            value = 0 if ra == 0 else registers.get(ra)
            if value is None:
                raise ValueError("Unresolved callback literal expression")
            registers[rt] = (value + (signed << 16 if op == 15 else signed)) & 0xFFFFFFFF
        elif op == 36 and ra == 1:
            if rt not in registers or signed in stack_words:
                raise ValueError("Invalid/duplicate callback literal store")
            stack_words[signed] = (registers[rt], pc)
        elif op == 11:
            pass  # cmpwi reads but does not change a register or stored pair.
        else:
            raise ValueError(f"Unexpected instruction in callback literals: {hx(pc)}")
    result = []
    seen = set()
    for offset in range(-384, -168, 8):
        index, index_pc = stack_words[offset]
        target, target_pc = stack_words[offset + 4]
        if not 0 <= index < 29 or index in seen or not 0x82230000 <= target < 0x82CC3CE4 or target & 3:
            raise ValueError("Invalid callback index/target")
        seen.add(index)
        result.append({"index": index, "engine_offset": hex(0x48 + 4 * index), "target": hx(target),
                       "index_store": hx(index_pc), "target_store": hx(target_pc)})
    return sorted(result, key=lambda item: item["index"])


def pipeline_contract(image):
    shaders = []
    for handle, record, expected in [(0x82CF2340, 0x82152880, "VSTextured"),
                                      (0x82CF2334, 0x82152708, "PSTextured"),
                                      (0x82CF231C, 0x821525E8, "VSFlat"),
                                      (0x82CF2310, 0x821524C8, "PSFlat")]:
        if u32(image, handle + 4) != record:
            raise ValueError("Unexpected original screen shader record")
        raw = span(image, record + 0x2C, 128)
        name, separator, _ = raw.partition(b"\0")
        if not separator or not name.endswith(f"Screen_Xenon_{expected}.updb".encode()):
            raise ValueError("Unexpected original shader debug identity")
        shaders.append({"handle_storage": hx(handle), "record_address": hx(record),
                        "original_debug_path": name.decode("ascii"), "status": "identity_verified; arithmetic_unresolved"})
    return {
        "start": {"address": "0x823EDF20", "stop": "0x823EE1A8", "input": "global engine/video-mode state; incoming argument registers unused",
                  "presentation_block": "0x82E3DCE0", "presentation_bytes": 124,
                  "width_storage": "0x82E3DF84", "height_storage": "0x82E3DF88",
                  "device_storage": "0x82D0CAF8", "color_surface_storage": "0x82D0CB00", "depth_surface_storage": "0x82D0CAFC",
                  "texture_storages": ["0x82D0CF84", "0x82D0CF90", "0x82D0CF8C", "0x82D0CF88"],
                  "sdk_device_allocation_bytes": 0x5700, "sdk_device_alignment": 128,
                  "native_design_limit": "No small sufficient emulated SDK-device field set is proven. Replace engine services, not GPU initialization success."},
        "quad": {"address": "0x82756480", "abi": {"r3": "camera", "f1_f4": "x0,y0,x1,y1 endpoints", "f5_f8": "u0,v0,u1,v1",
                  "r8": "BE float32[4] color pointer", "r9": "blend selector", "r10": "texture wrapper or0"},
                 "vertex_order": ["TL", "TR", "BL", "BR"], "vertex_count": 4, "primitive_code": 6,
                 "textured_stride": 16, "flat_stride": 8,
                 "blend_words": ["00010106", "00010706", "00010186", "00010001"],
                 "alpha_early_out_float_bits": f"{u32(image, 0x821DD350):08X}",
                 "shaders": shaders, "resource_ownership": "borrowed during synchronous helper; deferred native work must retain/snapshot independently"},
        "raster_create": {"wrapper": "0x82408130", "abi": "r3 width,r4 height,r5 depth-like value,r6 flags",
                          "callback": "0x823F7070", "callback_abi": "r3=0,r4=initialized raster,r5=flags; return Boolean",
                          "plugin_offset_storage": "0x82E3DC94", "wrapper_return": "raster pointer or0"},
        "raster_destroy": {"wrapper": "0x82407DC0", "abi": "r3=raster", "callback": "0x823F62A0",
                           "callback_abi": "r3=0,r4=raster,r5=0; wrapper then returns object to original allocator"},
        "native_texture_read": {"callback": "0x8240A278", "slot": 26,
                                "abi": "r3=stream,r4=texture pointer output,r5=chunk length (overwritten by this callee); returns Boolean"},
    }


def inspect(image_path, aot_root, disassembler, boot_log):
    image = image_path.read_bytes()
    sections = pe_sections(image)
    pdata = pdata_functions(image, sections)
    index = aot_index(aot_root)
    log = boot_log.read_bytes()
    frames = boot_frames(log)
    registers = boot_registers(log)
    starts = sorted(index)
    selected = dict(FUNCTIONS)
    current = 0x82466E48
    for frame in frames:
        lr = int(frame["saved_lr"], 16)
        if not lr:
            continue
        pc = lr - 4
        pos = bisect.bisect_right(starts, pc) - 1
        if pos < 0:
            raise ValueError("Logged caller outside AOT index")
        caller = starts[pos]
        if pc >= caller + index[caller][1]:
            raise ValueError("Logged caller in gap, not a function (nearest .pdata is insufficient)")
        decoded = branch(pc, u32(image, pc))
        if decoded:
            target, linked = decoded
            if not linked or target != current:
                raise ValueError("Logged direct caller does not lead to inner frame")
            kind = "original_direct_bl"
        elif u32(image, pc) == 0x4E800421:
            target = current
            kind = "observed_indirect_bctrl; target also supported by driver table"
        else:
            raise ValueError("Logged LR does not follow a BL/BCTRL")
        frame.update(call_address=hx(pc), caller_function=hx(caller), callee=hx(target), evidence=kind)
        selected.setdefault(caller, "observed startup ancestor")
        current = caller
    instructions = {}
    functions = []
    for address, label in sorted(selected.items()):
        if address not in index:
            raise ValueError(f"Missing required AOT function {hx(address)}")
        file, size, symbol = index[address]
        comments, line = aot_comments(aot_root / file, address, symbol)
        aot_extent = size
        if address in pdata and size != pdata[address]:
            # Current recovery includes an adjacent tail after the entry. Use
            # original unwind extent for this explicitly identified exception.
            if address != 0x82432280 or size != 484 or pdata[address] != 448:
                raise ValueError(f"AOT extent differs from original .pdata: {hx(address)}")
            size = pdata[address]
            comments = comments[:size // 4]
        body = span(image, address, size)
        process = subprocess.run([str(disassembler), str(image_path), hx(BASE), hx(address), str(size // 4)],
                                 capture_output=True, text=True, check=True, timeout=60)
        decoded = verify_disassembly(image, address, size, process.stdout.splitlines(), comments)
        edges = []
        for record in decoded:
            pc = int(record["address"], 16)
            instructions[pc] = record
            direct = branch(pc, int(record["word"], 16))
            if direct:
                target, linked = direct
                if not address <= target < address + size:
                    edges.append({"call_address": hx(pc), "target": hx(target), "linked": linked})
        functions.append({"address": hx(address), "behavior_label": label, "size": size,
                          "aot_extent_bytes": aot_extent,
                          "extent_evidence": "original_pdata" if address in pdata else "AOT_extent_with_original_instruction_comparison",
                          "original_bytes_sha256": sha(body), "aot_file": file, "aot_symbol": symbol, "aot_line": line,
                          "aot_instruction_sha256": sha("\n".join(comments).encode()),
                          "external_direct_branches": edges, "instructions": decoded})
    facts = []
    for name, status, witnesses, description in FACTS:
        if any(address not in instructions for address in witnesses):
            raise ValueError(f"Uncovered claim witness: {name}")
        facts.append({"id": name, "status": status, "description": description,
                      "witnesses": [instructions[x] for x in witnesses]})
    table = [u32(image, 0x82CD1A78 + i * 4) for i in range(14)]
    if table[1] != 0x823F0630:
        raise ValueError("Unexpected platform driver dispatcher")
    requests = []
    for request, index_byte in enumerate(span(image, 0x82062AC0, 23)):
        target = 0x823F067C + 4 * index_byte
        if not 0x823F0630 <= target < 0x823F0914:
            raise ValueError("Dispatch target outside verified driver")
        requests.append({"request": request, "index_byte": index_byte, "target": hx(target)})
    vtable = [u32(image, 0x8215F698 + i * 4) for i in range(9)]
    if vtable[3] != 0x8285FDA0:
        raise ValueError("Unexpected frontend tick vtable")
    report = {
        "schema_version": 1,
        "scope": "US original renderer startup and loading quads; offline evidence only",
        "image": {"size": len(image), "sha256": sha(image), "base": hx(BASE), "mapping": "flat VA minus base; PE raw offsets are NOT used"},
        "tools": {"disassembler_sha256": sha(disassembler.read_bytes()), "analyzer_sha256": sha(Path(__file__).read_bytes())},
        "reviewed_reference_snapshots": [
            {"path": "K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h",
             "sha256": "7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227",
             "use": "declarative enum corroboration only; original byte verification does not execute this reference"},
            {"path": "K:/Simpsons/RexGlueCurrent/include/rex/graphics/registers.h",
             "sha256": "2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40",
             "use": "declarative state fields only; no backend copied"}],
        "verification": {"functions": len(functions), "instruction_words": len(instructions),
                         "all_selected_AOT_comments_equal_original_disassembly": True, "original_pdata_records": len(pdata)},
        "boot021": {"source": boot_log.name, "sha256": sha(log), "evidence_class": "observed_log_and_original_instruction_crosscheck",
                    "failure_function": "0x82466E48", "failure_call": "0x82466E7C", "reported_return_pc": "0x82466E80",
                    "device_address_this_run_only": registers["r31"], "registers": registers, "frames_inner_to_outer": frames,
                    "limitation": "Backchain establishes execution to the failure, not successful initialization or frontend drawing."},
        "engine_driver": {"template_address": "0x82CD1A78", "template_size": 56, "words": [hx(x) for x in table],
                          "switch_index_address": "0x82062AC0", "requests": requests,
                          "standard_callbacks": standard_callbacks(image),
                          "standard_default": "0x823EE438 returns0; slots0 and22 remain default",
                          "engine_pointer_storage": "0x82D0CA68", "device_pointer_storage": "0x82D0CAF8"},
        "frontend": {"vtable_address": "0x8215F698", "vtable_words": [hx(x) for x in vtable],
                     "name": span(image, 0x82001F10, 17).rstrip(b"\0").decode("ascii"),
                     "embedded_loading_stream": {"address": "0x8215F820", "size": 0x20150,
                                                  "sha256": sha(span(image, 0x8215F820, 0x20150))}},
        "claims": facts, "unresolved": UNRESOLVED, "functions": functions,
        "pipeline_contract": pipeline_contract(image),
    }
    return report


def self_test():
    count = 0
    def check(condition):
        nonlocal count
        if not condition:
            raise AssertionError("Analyzer self-test failed")
        count += 1
    check(branch(0x82466E7C, 0x4885BFC9) == (0x82CC2E44, True))
    check(branch(0x8286266C, 0x4BEF3E15) == (0x82756480, True))
    check(branch(0x1000, 0x48000003) == (0, True))
    check(branch(0x1000, 0x4BFFFFFE) == (0xFFFFFFFC, False))
    check(branch(0, 0x4E800421) is None)
    bad_cases = [lambda: span(b"1234", BASE - 1, 1), lambda: span(b"1234", BASE, 5),
                 lambda: span(b"1234", BASE, -1), lambda: u32(b"12345678", BASE + 1),
                 lambda: branch(1, 0), lambda: branch(0, 1 << 32),
                 lambda: pe_sections(b"MZ"), lambda: boot_frames(b""),
                 lambda: verify_disassembly(b"\x60\0\0\0", BASE, 4, ["82000000 60000000 nop"], ["blr"]),
                 lambda: verify_disassembly(b"\x60\0\0\0", BASE, 4, ["82000004 60000000 nop"], ["nop"]),
                 lambda: verify_disassembly(b"\x60\0\0\0", BASE, 4, ["82000000 00000000 nop"], ["nop"]),
                 lambda: verify_disassembly(b"\x60\0\0\0", BASE, 4, [], [])]
    for case in bad_cases:
        try:
            case()
        except ValueError:
            count += 1
        else:
            raise AssertionError("Malformed evidence was accepted")
    check(verify_disassembly(b"\x60\0\0\0", BASE, 4, ["82000000 60000000 nop "], ["nop"])[0]["word"] == "60000000")
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "analysis/simpsons.pe")
    parser.add_argument("--aot-root", type=Path, default=ROOT / "build/generated")
    parser.add_argument("--disassembler", type=Path, default=ROOT / "build/generator-ninja/SimpsonsDisasm.exe")
    parser.add_argument("--boot-log", type=Path, default=ROOT / "build/boot-021.log")
    parser.add_argument("--report", type=Path, help="Only this JSON output is written; omit for stdout")
    parser.add_argument("--self-test", action="store_true", help="Run in-memory verification/rejection checks and exit")
    args = parser.parse_args()
    if args.self_test:
        print(json.dumps({"self_tests_passed": self_test()}, sort_keys=True))
        return 0
    if args.report:
        target = args.report.resolve()
        protected = [args.image.resolve(), args.disassembler.resolve(), args.boot_log.resolve(), Path(__file__).resolve()]
        game = (ROOT / "Simpsons Game, The (USA)").resolve()
        if target in protected or target.is_relative_to(game) or target.is_relative_to(args.aot_root.resolve()):
            raise ValueError("Report would overwrite a protected input")
    report = inspect(args.image.resolve(), args.aot_root.resolve(), args.disassembler.resolve(), args.boot_log.resolve())
    output = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8", newline="\n")
        print(json.dumps(report["verification"], sort_keys=True))
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"render-boundary: {error}", file=sys.stderr)
        raise SystemExit(2)
