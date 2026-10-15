"""FXC wrapper that also emits the Vulkan SPIR-V twin of each D3D11 shader.

Usage:
    vulkan_shader_compile.py --fxc FXC [--glslang GLSLANG] -- <fxc arguments>

FXC runs first with its arguments unchanged, so the D3D11 bytecode in the /Fh
header is byte-identical to a plain FXC build. When a glslang validator is
supplied, vertex and pixel shaders are compiled again from the same HLSL source
into SPIR-V. The DXBC interface (RDEF resource registers, constant-buffer
member offsets, input/output signatures) is parsed and every SPIR-V binding,
offset and stage variable is checked against it, so the Vulkan twin cannot
silently bind a different slot or read a differently packed constant.
The checked SPIR-V is appended to the header behind SIMPSONS_VULKAN_SPIRV and
self-registers under the DXBC checksum; renderer/vulkan resolves
CreateVertexShader/CreatePixelShader bytecode through that checksum.

Register mapping (one descriptor set per stage, patched at runtime):
    b#  -> binding #        s#  -> binding 16+#
    t#  -> binding 32+#     u#  -> binding 160+#

Post-processing applied to the glslang output:
  * every floating-point arithmetic result is decorated NoContraction,
    matching FXC /Gis (no fused or refactored arithmetic);
  * SV_StencilRef (unsupported by glslang's HLSL front end) is compiled as a
    marker semantic and rewritten to BuiltIn FragStencilRefEXT.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CB_SHIFT, SAMPLER_SHIFT, TEXTURE_SHIFT, UAV_SHIFT = 0, 16, 32, 160
STENCIL_MARKER = "SIMPSONS_STENCIL_REF"
SAMPLE_POSITION_MARKER = "SIMPSONS_SAMPLE_POSITION"
STAGES = {"vs": "vert", "ps": "frag"}


class Failure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Failure(message)


# ---------------------------------------------------------------- FXC arguments
def parse_fxc_arguments(arguments):
    parsed = {"profile": None, "entry": None, "header": None, "variable": None,
              "includes": [], "defines": [], "source": None}
    value_options = {"/T": "profile", "/E": "entry", "/Fh": "header", "/Vn": "variable"}
    index = 0
    while index < len(arguments):
        token = arguments[index]
        if token in value_options:
            require(index + 1 < len(arguments), f"FXC option {token} lacks a value")
            parsed[value_options[token]] = arguments[index + 1]
            index += 2
        elif token == "/I":
            parsed["includes"].append(arguments[index + 1])
            index += 2
        elif token == "/D":
            parsed["defines"].append(arguments[index + 1])
            index += 2
        elif token in ("/Fo", "/Fc", "/Fe", "/Fd"):
            index += 2
        elif token.startswith("/"):
            index += 1
        else:
            require(parsed["source"] is None, f"FXC arguments name two sources: {parsed['source']} and {token}")
            parsed["source"] = token
            index += 1
    for key in ("profile", "entry", "header", "variable", "source"):
        require(parsed[key], f"FXC arguments lack {key}")
    return parsed


def header_bytes(text, variable):
    match = re.search(r"const\s+BYTE\s+" + re.escape(variable) + r"\s*\[\]\s*=\s*\{([^}]*)\};", text)
    require(match, f"FXC header has no {variable} byte array")
    values = [int(token) for token in match.group(1).replace("\n", " ").split(",") if token.strip()]
    require(all(0 <= value <= 255 for value in values), "FXC header byte out of range")
    return bytes(values)


# ---------------------------------------------------------------- DXBC
def dxbc_chunks(data):
    require(len(data) >= 32 and data[:4] == b"DXBC", "Bytecode is not a DXBC container")
    total, count = struct.unpack_from("<II", data, 24)
    require(total == len(data), "DXBC container size differs from its header")
    chunks = {}
    for index in range(count):
        offset, = struct.unpack_from("<I", data, 32 + 4 * index)
        fourcc = data[offset:offset + 4].decode("ascii")
        size, = struct.unpack_from("<I", data, offset + 4)
        chunks[fourcc] = data[offset + 8:offset + 8 + size]
    return chunks


def c_string(data, offset):
    end = data.index(0, offset)
    return data[offset:end].decode("ascii")


def parse_rdef(chunk):
    cb_count, cb_offset, res_count, res_offset = struct.unpack_from("<IIII", chunk, 0)
    minor, major = chunk[16], chunk[17]
    require(major == 5, f"Unsupported RDEF shader model {major}.{minor}")
    resources = []
    for index in range(res_count):
        name, kind, _ret, dimension, _samples, bind, count, _flags = struct.unpack_from("<8I", chunk, res_offset + 32 * index)
        resources.append({"name": c_string(chunk, name), "type": kind, "dimension": dimension, "bind": bind, "count": count})
    cbuffers = []
    for index in range(cb_count):
        name, var_count, var_offset, size, _flags, kind = struct.unpack_from("<6I", chunk, cb_offset + 24 * index)
        variables = []
        for v in range(var_count):
            vname, start, vsize, _vflags, _type, _default, _t0, _t1, _s0, _s1 = struct.unpack_from("<10I", chunk, var_offset + 40 * v)
            variables.append({"name": c_string(chunk, vname), "offset": start, "size": vsize})
        cbuffers.append({"name": c_string(chunk, name), "size": size, "type": kind, "variables": variables})
    return resources, cbuffers


def parse_signature(chunk):
    count, _ = struct.unpack_from("<II", chunk, 0)
    elements = []
    for index in range(count):
        name, semantic_index, system_value, component, register, mask, used = struct.unpack_from("<5IBB", chunk, 8 + 24 * index)
        elements.append({"name": c_string(chunk, name).upper(), "index": semantic_index, "system": system_value,
                         "component": component, "register": register, "mask": mask, "used": used})
    return elements


# ---------------------------------------------------------------- SPIR-V
OP_NAME, OP_MEMBER_NAME, OP_EXTENSION, OP_CAPABILITY = 5, 6, 10, 17
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_STRUCT, OP_TYPE_POINTER = 21, 22, 23, 30, 32
OP_VARIABLE, OP_DECORATE, OP_MEMBER_DECORATE, OP_DECORATE_STRING = 59, 71, 72, 5632
DEC_BLOCK, DEC_NO_CONTRACTION, DEC_BUILTIN, DEC_LOCATION, DEC_BINDING, DEC_SET, DEC_OFFSET, DEC_SEMANTIC = 2, 42, 11, 30, 33, 34, 35, 5635
BUILTIN_FRAG_STENCIL_REF, BUILTIN_SAMPLE_POSITION = 5014, 19
CAP_STENCIL_EXPORT, CAP_SAMPLE_RATE_SHADING = 5013, 35
FLOAT_ARITHMETIC = {127, 129, 131, 133, 136, 140, 141, 142, 143, 144, 145, 146, 147, 148}
ANNOTATIONS = {OP_DECORATE, OP_MEMBER_DECORATE, 73, 74, 75, 332, OP_DECORATE_STRING, 5633}
# Capability/extension/import/memory model/entry/execution modes, then debug
# strings, sources, names, processed markers and line information.
PREAMBLE = {OP_CAPABILITY, OP_EXTENSION, 11, 14, 15, 16, 331, 7, 3, 4, 2, OP_NAME, OP_MEMBER_NAME, 330, 8, 317}


def spirv_words(blob):
    require(len(blob) % 4 == 0 and len(blob) >= 20, "SPIR-V blob is truncated")
    words = list(struct.unpack(f"<{len(blob) // 4}I", blob))
    require(words[0] == 0x07230203, "SPIR-V magic number is missing")
    return words


def instructions(words):
    index = 5
    while index < len(words):
        count, opcode = words[index] >> 16, words[index] & 0xFFFF
        require(count and index + count <= len(words), "SPIR-V instruction stream is malformed")
        yield index, opcode, words[index + 1:index + count]
        index += count


def literal_string(operands):
    raw = struct.pack(f"<{len(operands)}I", *operands)
    return raw[:raw.index(0)].decode("utf-8")


def encode_string(text):
    raw = text.encode("utf-8") + b"\0"
    raw += b"\0" * (-len(raw) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def reflect_spirv(words):
    names, member_names, decorations, member_decorations = {}, {}, {}, {}
    types, pointers, variables = {}, {}, []
    for _index, opcode, ops in instructions(words):
        if opcode == OP_NAME:
            names[ops[0]] = literal_string(ops[1:])
        elif opcode == OP_MEMBER_NAME:
            member_names[(ops[0], ops[1])] = literal_string(ops[2:])
        elif opcode == OP_DECORATE:
            decorations.setdefault(ops[0], {})[ops[1]] = ops[2:]
        elif opcode == OP_DECORATE_STRING:
            decorations.setdefault(ops[0], {})[ops[1]] = literal_string(ops[2:])
        elif opcode == OP_MEMBER_DECORATE:
            member_decorations.setdefault((ops[0], ops[1]), {})[ops[2]] = ops[3:]
        elif opcode == OP_TYPE_POINTER:
            pointers[ops[0]] = (ops[1], ops[2])
        elif opcode in (OP_TYPE_STRUCT, OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR):
            types[ops[0]] = (opcode, ops[1:])
        elif opcode == OP_VARIABLE:
            storage, pointee = pointers[ops[0]]
            variables.append({"id": ops[1], "storage": ops[2], "type": pointee})
    return names, member_names, decorations, member_decorations, types, variables


# Marker semantic -> (storage class, BuiltIn, capability, extension or None).
# glslang's HLSL front end has no SV_StencilRef and no sample-position input.
MARKERS = {
    STENCIL_MARKER: (3, BUILTIN_FRAG_STENCIL_REF, CAP_STENCIL_EXPORT, "SPV_EXT_shader_stencil_export"),
    SAMPLE_POSITION_MARKER: (1, BUILTIN_SAMPLE_POSITION, CAP_SAMPLE_RATE_SHADING, None),
}


def patch_spirv(words, stage):
    """Apply NoContraction and the marker-semantic builtin rewrites."""
    _names, _member_names, decorations, _member_decorations, _types, variables = reflect_spirv(words)
    builtins = {}
    for variable in variables:
        semantic = str(decorations.get(variable["id"], {}).get(DEC_SEMANTIC, "")).upper()
        if semantic in MARKERS:
            storage, builtin, capability, extension = MARKERS[semantic]
            require(variable["storage"] == storage, f"Marker semantic {semantic} has the wrong storage class")
            require(builtin not in [entry[0] for entry in builtins.values()], f"Shader declares {semantic} twice")
            builtins[variable["id"]] = (builtin, capability, extension)
    capabilities = {ops[0] for _index, opcode, ops in instructions(words) if opcode == OP_CAPABILITY}
    decorated = {target for target, values in decorations.items() if DEC_NO_CONTRACTION in values}
    results = []
    for _index, opcode, ops in instructions(words):
        if opcode in FLOAT_ARITHMETIC and ops[1] not in decorated:
            results.append(ops[1])
    out = words[:5]
    inserted_annotations = False
    added_preamble = False
    for index, opcode, ops in instructions(words):
        instruction = words[index:index + 1 + len(ops)]
        if not added_preamble and opcode != OP_CAPABILITY:
            for builtin, capability, extension in builtins.values():
                if capability not in capabilities:
                    out += [(2 << 16) | OP_CAPABILITY, capability]
                    capabilities.add(capability)
                if extension:
                    text = encode_string(extension)
                    out += [((1 + len(text)) << 16) | OP_EXTENSION] + text
            added_preamble = True
        if not inserted_annotations and opcode not in PREAMBLE and opcode not in ANNOTATIONS:
            for result in results:
                out += [(3 << 16) | OP_DECORATE, result, DEC_NO_CONTRACTION]
            for variable, (builtin, _capability, _extension) in builtins.items():
                out += [(4 << 16) | OP_DECORATE, variable, DEC_BUILTIN, builtin]
            inserted_annotations = True
        if opcode == OP_DECORATE and ops[0] in builtins and ops[1] in (DEC_LOCATION, 31):
            continue
        out += instruction
    require(inserted_annotations, "SPIR-V module has no type section")
    return out, BUILTIN_FRAG_STENCIL_REF in [entry[0] for entry in builtins.values()]


def semantic_key(text):
    text = str(text).upper()
    match = re.fullmatch(r"(.*?)(\d*)", text)
    return match.group(1), int(match.group(2) or 0)


def cross_check(dxbc, words, stage, name):
    chunks = dxbc_chunks(dxbc)
    resources, cbuffers = parse_rdef(chunks["RDEF"])
    names, member_names, decorations, member_decorations, types, variables = reflect_spirv(words)
    bound = {}
    for variable in variables:
        decoration = decorations.get(variable["id"], {})
        if DEC_BINDING in decoration:
            require(decoration.get(DEC_SET, [0])[0] == 0, f"{name}: SPIR-V descriptor set is not zero")
            key = names.get(variable["id"], "")
            if not key or key.startswith("@"):
                key = names.get(variable["type"], "")
            bound[key] = decoration[DEC_BINDING][0]
    for resource in resources:
        shift = {0: CB_SHIFT, 2: TEXTURE_SHIFT, 3: SAMPLER_SHIFT}.get(resource["type"])
        require(shift is not None, f"{name}: unsupported D3D resource type {resource['type']} for {resource['name']}")
        require(resource["count"] == 1, f"{name}: resource arrays are not mapped ({resource['name']})")
        require(resource["name"] in bound, f"{name}: SPIR-V lacks D3D resource {resource['name']}")
        require(bound[resource["name"]] == shift + resource["bind"],
                f"{name}: {resource['name']} binds {bound[resource['name']]} in SPIR-V, register {resource['bind']} in DXBC")
    blocks = {}
    for variable in variables:
        if variable["storage"] == 2 and DEC_BLOCK in decorations.get(variable["type"], {}):
            blocks[names.get(variable["type"], "")] = variable["type"]
    for cbuffer in cbuffers:
        if cbuffer["type"] != 0 or cbuffer["name"] not in bound:
            continue
        struct_id = blocks.get(cbuffer["name"])
        require(struct_id is not None, f"{name}: SPIR-V lacks the {cbuffer['name']} block")
        offsets = {member_names.get((struct_id, member)): values[DEC_OFFSET][0]
                   for (sid, member), values in member_decorations.items()
                   if sid == struct_id and DEC_OFFSET in values}
        for variable in cbuffer["variables"]:
            if variable["name"] in offsets:
                require(offsets[variable["name"]] == variable["offset"],
                        f"{name}: {cbuffer['name']}.{variable['name']} at {offsets[variable['name']]} in SPIR-V, {variable['offset']} in DXBC")
    semantics = {}
    for variable in variables:
        decoration = decorations.get(variable["id"], {})
        if variable["storage"] in (1, 3) and DEC_SEMANTIC in decoration and DEC_BUILTIN not in decoration:
            semantics[(variable["storage"], semantic_key(decoration[DEC_SEMANTIC]))] = decoration.get(DEC_LOCATION, [None])[0]
    if stage == "vs":
        for element in parse_signature(chunks["ISGN"]):
            if element["system"] == 0 and element["used"]:
                require((1, (element["name"], element["index"])) in semantics,
                        f"{name}: SPIR-V lacks vertex input {element['name']}{element['index']}")
    if stage == "ps":
        for element in parse_signature(chunks["OSGN"]):
            if element["name"] == "SV_TARGET":
                location = semantics.get((3, ("SV_TARGET", element["index"])))
                require(location == element["index"],
                        f"{name}: SV_Target{element['index']} is SPIR-V location {location}")


# ---------------------------------------------------------------- emission
def registration(variable, entry, checksum, words, tool_hash):
    lines = [
        "",
        f"// Vulkan SPIR-V twin of {entry}: glslang {tool_hash[:16]}, checked against the DXBC interface above.",
        f"#if defined(SIMPSONS_VULKAN_SPIRV) && !defined(SIMPSONS_SPIRV_{variable})",
        f"#define SIMPSONS_SPIRV_{variable}",
        '#include "renderer/vulkan/spirv_registration.h"',
        f"inline constexpr uint32_t {variable}_spirv[] = {{",
    ]
    for start in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{word:08X}" for word in words[start:start + 8]) + ",")
    lines.append("};")
    parts = ", ".join(f"0x{value:08X}u" for value in struct.unpack("<4I", checksum))
    lines.append(f"inline const ::Simpsons::Graphics::Vulkan::SpirvRegistration {variable}_spirvRegistration{{")
    lines.append(f'    "{entry}", {{{parts}}}, {variable}_spirv, sizeof({variable}_spirv) / sizeof(uint32_t)}};')
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def compile_spirv(glslang, parsed, stage, work):
    output = work / (parsed["entry"] + ".spv")
    command = [glslang, "-D", "-V", "--target-env", "vulkan1.2", "-S", STAGES[stage], "-e", parsed["entry"],
               "--hlsl-offsets", "--hlsl-iomap", "-fhlsl_functionality1", "--spirv-val",
               "--shift-UBO-binding", str(CB_SHIFT), "--shift-sampler-binding", str(SAMPLER_SHIFT),
               "--shift-texture-binding", str(TEXTURE_SHIFT), "--shift-uav-binding", str(UAV_SHIFT)]
    if stage == "ps":
        command += ["--hlsl-dx-position-w"]
    # Vertex compiles of a file can still parse its pixel output structs.
    # SIMPSONS_SPIRV selects the rare source variants FXC never sees.
    command += [f"-DSV_StencilRef={STENCIL_MARKER}", "-DSIMPSONS_SPIRV=1"]
    command += ["-I" + include for include in parsed["includes"]]
    command += ["-D" + define for define in parsed["defines"]]
    command += ["-o", str(output), parsed["source"]]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise Failure(f"glslang could not compile {parsed['entry']} from {parsed['source']}")
    return output.read_bytes()


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if "--" not in argv:
        raise SystemExit("usage: vulkan_shader_compile.py --fxc FXC [--glslang GLSLANG] -- <fxc arguments>")
    split = argv.index("--")
    parser = argparse.ArgumentParser()
    parser.add_argument("--fxc", required=True)
    parser.add_argument("--glslang")
    options = parser.parse_args(argv[:split])
    fxc_arguments = argv[split + 1:]
    result = subprocess.run([options.fxc] + fxc_arguments)
    if result.returncode:
        return result.returncode
    parsed = parse_fxc_arguments(fxc_arguments)
    stage = parsed["profile"].split("_")[0]
    if not options.glslang or stage not in STAGES:
        return 0
    try:
        header = Path(parsed["header"])
        text = header.read_text()
        dxbc = header_bytes(text, parsed["variable"])
        tool_hash = hashlib.sha256(Path(options.glslang).read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix="spirv-", dir=header.parent) as directory:
            blob = compile_spirv(options.glslang, parsed, stage, Path(directory))
        words, _stencil = patch_spirv(spirv_words(blob), stage)
        cross_check(dxbc, words, stage, parsed["entry"])
        header.write_text(text + registration(parsed["variable"], parsed["entry"], dxbc[4:20], words, tool_hash))
    except (Failure, OSError, KeyError, ValueError, struct.error) as error:
        # A partial header must not satisfy the next incremental build.
        try:
            Path(parsed["header"]).unlink()
        except OSError:
            pass
        sys.stderr.write(f"vulkan_shader_compile: {error}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
