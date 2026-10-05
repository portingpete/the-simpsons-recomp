"""Independent Xenos pixel-shader microcode reference for test fixtures.

Executes the decoded original instructions of a straight-line pixel shader
record directly; it never reads native HLSL. Semantics follow the pinned
ucode.h conventions (tools/analyze_fourtap_shaders.py pins the file hash):

- co-issued vector and scalar operations read all sources before writing;
- scalar operand a = src3 component (3 + swizzle[7:6]) & 3, b = swizzle[1:0];
- MUL/ADD/SUB_CONST take constant a and temporary b, with the temporary index
  (opcode & 1) | (src3_sel << 1) | (swizzle & 0x3C); abs/negate apply to both;
- ps is the last scalar result, updated even when no component is written;
- legacy multiply: +-0 times anything is +0 (mul, mad, dot);
- exports write the vector result to vector-only lanes, the scalar result to
  scalar-only lanes, one to lanes in both masks and zero to lanes in neither
  when the scalar-relative bit is set.

Sampling is supplied by the caller. Arithmetic is Python double precision, so
comparisons against native float32 results need a tolerance.
"""
from __future__ import annotations

import math
import struct

import analyze_edge_shaders as edge
import analyze_screen_shaders as screen

need = screen.require
NAN = float('nan')


VECTOR_ARITY = {0: 2, 1: 2, 2: 2, 3: 2, 11: 3, 12: 3, 13: 3, 14: 3, 15: 2, 16: 2}
SCALAR_OPS = {0, 1, 2, 3, 5, 6, 14, 19, 25, 42, 43, 44, 45, 46, 47, 50}


def record(image: bytes, address: int):
    base = address - screen.BASE
    header = struct.unpack_from('>9I', image, base)
    return parse(image[base:base + header[1] + header[2]], address)


def parse(data: bytes, address: int):
    header = struct.unpack_from('>9I', data, 0)
    prefix, code_bytes = struct.unpack_from('>2I', data, header[6])
    literals = struct.unpack_from('>%df' % (prefix // 4), data, header[1])
    code = data[header[1] + prefix:header[1] + prefix + code_bytes]
    constants = {}
    for index in range(len(literals) // 4):
        constants[256 - len(literals) // 4 + index] = tuple(literals[4 * index:4 * index + 4])
    words = [struct.unpack_from('>3I', code, 12 * i) for i in range(len(code) // 12)]
    schedule = []
    for pair, (a, b, c) in enumerate(words):
        ended = False
        for lo, hi in ((a, b & 0xFFFF), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)):
            opcode = hi >> 12
            if opcode in (1, 2):
                start, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
                schedule.extend((start + i, bool(sequence & (1 << (2 * i)))) for i in range(count))
                ended |= opcode == 2
            else:
                need(opcode in (0, 12), 'Reference supports straight-line EXEC/ALLOC only')
        if ended:
            break
    program = []
    for slot, fetch in schedule:
        raw = words[slot]
        program.append(('fetch', screen.decode_fetch(raw)) if fetch else ('alu', edge.alu(*raw), raw))
    for entry in program:
        if entry[0] == 'alu':
            f = entry[1]
            need(not f['vector_mask'] or f['vector_opcode'] in VECTOR_ARITY, 'Unsupported vector opcode %d' % f['vector_opcode'])
            need(f['scalar_opcode'] in SCALAR_OPS, 'Unsupported scalar opcode %d' % f['scalar_opcode'])
        else:
            f = entry[1]
            need(f['kind'] == 'texture_fetch' and f['normalized_coordinates'] and f['dimension_field'] == 1,
                 'Reference supports normalized 2D texture fetch only')
    return {'address': address, 'program': program, 'literals': constants}


def _mul(a: float, b: float) -> float:
    return 0.0 if a == 0.0 or b == 0.0 else a * b


def _sat(value: float) -> float:
    return 0.0 if not value > 0.0 else (1.0 if value > 1.0 else value)


def run(shader, constants: dict, sample, uv):
    """Return oC0 for one pixel. sample(stage, u, v) -> (x, y, z, w)."""
    regs = [[NAN] * 4 for _ in range(64)]
    regs[0][0], regs[0][1] = uv
    literals = shader['literals']
    ps = NAN
    out = [NAN] * 4

    def constant(index):
        if index in literals:
            return literals[index]
        need(index in constants, 'Shader read an unsupplied constant c%d' % index)
        return constants[index]

    for entry in shader['program']:
        if entry[0] == 'fetch':
            f = entry[1]
            need(f['kind'] == 'texture_fetch' and f['normalized_coordinates'] and f['dimension_field'] == 1,
                 'Reference supports normalized 2D texture fetch only')
            source = regs[f['source_register']]
            u, v = source[f['source_components'][0]], source[f['source_components'][1]]
            texel = sample(f['fetch_constant_index'], u, v)
            dest = regs[f['destination_register']]
            new = list(dest)
            for lane, selector in enumerate(f['destination_swizzle']):
                if selector < 4:
                    new[lane] = texel[selector]
                elif selector == 4:
                    new[lane] = 0.0
                elif selector == 5:
                    new[lane] = 1.0
                else:
                    need(selector == 7, 'Unsupported fetch destination selector')
            regs[f['destination_register']] = new
            continue
        f, raw = entry[1], entry[2]
        need(not any(f[k] for k in ('predicated', 'vector_destination_relative', 'constant_address_register_relative',
                                    'constant_0_relative', 'constant_1_relative')), 'Unsupported ALU addressing/predicate')

        def operand(index):
            s = f['sources'][index]
            if s['bank'] == 'temporary':
                need(not s['relative_temporary'], 'Relative temporary unsupported')
                values = regs[s['register']]
                absolute = s['absolute_temporary']
            else:
                values = constant(s['register'])
                absolute = f['absolute_constants']
            result = [values[c] for c in s['components']]
            if absolute:
                result = [abs(x) for x in result]
            if s['negated']:
                result = [-x for x in result]
            return result

        vop, sop = f['vector_opcode'], f['scalar_opcode']
        arity = VECTOR_ARITY
        if not f['vector_mask']:
            vop = None  # Unwritten vector results never read their operands here.
        else:
            need(vop in arity, 'Unsupported vector opcode %d' % vop)
            a, b = operand(0), operand(1)
            c = operand(2) if arity[vop] == 3 else None
        if vop is None:
            vector = [NAN] * 4
        elif vop == 0:
            vector = [a[i] + b[i] for i in range(4)]
        elif vop == 1:
            vector = [_mul(a[i], b[i]) for i in range(4)]
        elif vop == 2:
            vector = [a[i] if a[i] >= b[i] else b[i] for i in range(4)]
        elif vop == 3:
            vector = [a[i] if a[i] < b[i] else b[i] for i in range(4)]
        elif vop == 11:
            vector = [_mul(a[i], b[i]) + c[i] for i in range(4)]
        elif vop in (12, 13, 14):
            test = {12: lambda x: x == 0.0, 13: lambda x: x >= 0.0, 14: lambda x: x > 0.0}[vop]
            vector = [b[i] if test(a[i]) else c[i] for i in range(4)]
        elif vop == 15:
            vector = [sum(_mul(a[i], b[i]) for i in range(4))] * 4
        elif vop == 16:
            vector = [sum(_mul(a[i], b[i]) for i in range(3))] * 4
        if f['vector_clamp']:
            vector = [_sat(x) for x in vector]

        swizzle = raw[1] & 255
        src3 = f['sources'][2]
        component_a, component_b = ((swizzle >> 6) + 3) & 3, swizzle & 3
        if 42 <= sop <= 47:
            const_value = constant(raw[2] & 255)[component_a]
            temp_value = regs[(sop & 1) | (((raw[2] >> 29) & 1) << 1) | (swizzle & 0x3C)][component_b]
            if f['absolute_constants']:
                const_value, temp_value = abs(const_value), abs(temp_value)
            if src3['negated']:
                const_value, temp_value = -const_value, -temp_value
            scalar = {42: lambda: _mul(const_value, temp_value), 43: lambda: _mul(const_value, temp_value),
                      44: lambda: const_value + temp_value, 45: lambda: const_value + temp_value,
                      46: lambda: const_value - temp_value, 47: lambda: const_value - temp_value}[sop]()
        elif sop == 50:
            scalar = ps
        else:
            if src3['bank'] == 'temporary':
                values = regs[src3['register']]
                absolute = src3['absolute_temporary']
            else:
                values = constant(src3['register'])
                absolute = f['absolute_constants']
            sa, sb = values[component_a], values[component_b]
            if absolute:
                sa, sb = abs(sa), abs(sb)
            if src3['negated']:
                sa, sb = -sa, -sb
            if sop == 0:
                scalar = sa + sb
            elif sop == 1:
                scalar = sa + ps
            elif sop == 2:
                scalar = _mul(sa, sb)
            elif sop == 3:
                scalar = _mul(sa, ps)
            elif sop == 5:
                scalar = sa if sa >= sb else sb
            elif sop == 6:
                scalar = sa if sa < sb else sb
            elif sop == 14:
                scalar = 2.0 ** sa
            elif sop == 19:
                scalar = 1.0 if sa == 1.0 else (math.copysign(math.inf, sa) if sa == 0.0 else 1.0 / sa)
            elif sop == 25:
                scalar = sa - sb
            else:
                need(False, 'Unsupported scalar opcode %d' % sop)
        if f['scalar_clamp']:
            scalar = _sat(scalar)
        ps = scalar

        vmask, smask = f['vector_mask'], f['scalar_mask']
        if f['export']:
            need(f['vector_destination'] == 0, 'Reference supports oC0 only')
            for lane in range(4):
                bit = 1 << lane
                if vmask & bit and smask & bit:
                    out[lane] = 1.0
                elif vmask & bit:
                    out[lane] = vector[lane]
                elif smask & bit:
                    out[lane] = scalar
                elif f['scalar_destination_relative_or_export_zero']:
                    out[lane] = 0.0
        else:
            vdest, sdest = regs[f['vector_destination']], regs[f['scalar_destination']]
            vnew, snew = list(vdest), list(sdest)
            for lane in range(4):
                if vmask & (1 << lane):
                    vnew[lane] = vector[lane]
            regs[f['vector_destination']] = vnew
            snew = list(regs[f['scalar_destination']])
            for lane in range(4):
                if smask & (1 << lane):
                    snew[lane] = scalar
            regs[f['scalar_destination']] = snew
    return out
