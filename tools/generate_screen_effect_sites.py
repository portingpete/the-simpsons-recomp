"""Derive the screen-effect bridge site table from the original pass code.

For the five original passes 82754288 (Dof), 82754C90 (Blur), 82755508 (Bloom),
8276C930 (Fog) and 8276FE60 (Sat), every call into the console SDK becomes a
bounded native endpoint, and every `lwz r11,-13576(r31)` console-device load
becomes a shadow-device endpoint whose later stores are declared here. The
argument expectations come from the instructions that load r4..r7 before
each call. Writes runtime/screen_effect_sites.h and the matching midasm hooks.
--check verifies both committed outputs match the image.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
DEVICE_LOAD = 0x817FCAF8  # lwz r11,-13576(r31)
# pass index, first/end address translated, frame bytes, return addresses.
# Pass 5 is the 82756268 letterbox; pass 6 is only the query prefix of the
# 82755FD0 overlay, whose native overlay endpoint closes the scope.
PASSES = (
    (0, 0x82754288, 0x82754644, 0xD0, (0x82751764,)),
    (1, 0x82754C90, 0x82754F7C, 0xC0, (0x827517C0,)),
    (2, 0x82755508, 0x827558A8, 0xF0, (0x82755B04, 0x826C1C10)),
    (3, 0x8276C930, 0x8276CCD0, 0xB0, (0x8275175C,)),
    (4, 0x8276FE60, 0x82770140, 0xD0, (0x827517B8,)),
    (5, 0x82756268, 0x82756480, 0x80, (0x8276E3FC, 0x826C2370)),
    (6, 0x82756034, 0x827560A4, 0xA0, (0x8276E354, 0x8276DFC8, 0x826C24F4)),
)
SCALARS = {0x82439F00: 0x38, 0x82439F60: 0x60, 0x8243A010: 0x3C, 0x8243A6C8: 0x28, 0x8243A708: 0x30,
           0x8243B3B0: 0x134, 0x8243B718: 0x144}
SAMPLERS = {0x8243BA40: 0x14, 0x8243BBD0: 0x10}
OPS = {0x8243CB80: 'PackedBlend', 0x82455570: 'Resolve', 0x82445578: 'VertexShader', 0x82445278: 'PixelShader',
       0x824408E0: 'Texture', 0x82445798: 'Declaration', 0x82444DF8: 'Constants', 0x8244C450: 'Begin', 0x8244C8F0: 'End'}


def disassemble(start, end):
    image = ROOT / 'analysis/simpsons.pe'
    out = subprocess.run([str(ROOT / 'build/generator-ninja/SimpsonsDisasm.exe'), str(image), hex(BASE), hex(start),
                          str((end - start) // 4)], capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        m = re.match(r'([0-9A-F]{8})\s+([0-9a-f]{8})\s+(\S+)\s*(.*)', line.strip())
        if m:
            rows.append((int(m[1], 16), int(m[2], 16), m[3], m[4].replace(' ', '')))
    return rows


def signed(value):
    return value - 0x10000 if value & 0x8000 else value


def argument(rows, index, register):
    """Static source of `register` at the call in rows[index]: ('imm', v) / ('field', a) / ('stack', d) / ('reg', r)."""
    value = None
    for pc, word, op, args in reversed(rows[max(0, index - 80):index]):
        parts = args.split(',')
        if not parts or parts[0] != register or op.startswith('st'):
            if op == 'bl':
                break
            continue
        if op == 'li':
            return ('imm', signed(int(parts[1], 0)) & 0xFFFFFFFF) if value is None else ('imm', (signed(int(parts[1], 0)) | value) & 0xFFFFFFFF)
        if op == 'lis':
            base = (int(parts[1], 0) << 16) & 0xFFFFFFFF
            return ('imm', base | (value or 0))
        if op in ('ori', 'oris') and parts[1] == register:
            imm = int(parts[2], 0)
            value = (value or 0) | (imm << 16 if op == 'oris' else imm)
            continue
        m = re.match(r'lwz', op)
        if m:
            d, base = re.match(r'(-?\d+)\((r\d+)\)', parts[1]).groups()
            if base in ('r29', 'r28'):  # Both hold the 82DFEA20 viewport row in these passes.
                return ('field', 0x82DFEA20 + int(d))
            if base == 'r11':
                # Preceding lis r11 (plus any addi r11,r11) establishes the address.
                offset = int(d)
                for pc2, _, op2, args2 in reversed(rows[max(0, index - 80):index]):
                    p2 = args2.split(',')
                    if pc2 >= pc or p2[0] != 'r11':
                        continue
                    if op2 == 'addi' and p2[1] == 'r11':
                        offset += int(p2[2], 0)
                        continue
                    if op2 == 'lis':
                        return ('field', ((int(p2[1], 0) << 16) + offset) & 0xFFFFFFFF)
                    return ('dynamic', op2 + ' ' + args2)
            return ('dynamic', args)
        if op == 'addi' and parts[1] == 'r1':
            return ('stack', int(parts[2], 0))
        if op == 'mr':
            return ('reg', int(parts[1][1:]))
        return ('dynamic', op + ' ' + args)
    return ('unknown', None)


def shader_handle(rows, index):
    d = int(re.match(r'r4,(-?\d+)\(r11\)', rows[index][3])[1])
    for pc, _, op, args in reversed(rows[max(0, index - 8):index]):
        parts = args.split(',')
        if op == 'lis' and parts[0] == 'r11':
            return ((int(parts[1], 0) << 16) + d) & 0xFFFFFFFF
    raise ValueError('Shader handle without lis r11 at %08X' % rows[index][0])


def device_block(rows, index):
    """Declared shadow writes after a device load: fetch fields and PS constant lanes."""
    fetch, lanes = set(), set()
    last_rlwimi = {}
    for pc, word, op, args in rows[index + 1:]:
        parts = args.split(',')
        if word == DEVICE_LOAD or op in ('bl', 'b', 'blr') or (parts and parts[0] == 'r11' and op not in ('stw', 'stfs', 'std')):
            break
        if op == 'rlwimi':
            last_rlwimi[parts[0]] = int(parts[2])
        m = re.match(r'(-?\d+)\(r11\)', parts[1]) if len(parts) > 1 else None
        if not m:
            continue
        offset = int(m[1])
        if op == 'stw' and 0x480 <= offset < 0x480 + 0x18 * 16 and (offset - 0x480) % 0x18 == 0:
            shift = last_rlwimi.get(parts[0])
            assert shift in (11, 14), (hex(pc), shift)
            fetch.add(((offset - 0x480) // 0x18) * 2 + (0 if shift == 11 else 1))
        elif op == 'stfs':
            assert 0x1780 <= offset < 0x1780 + 16 * 16 and offset % 4 == 0, hex(pc)
            lanes.add((offset - 0x1780) // 4)
        elif op == 'lwz' and 0x480 <= offset < 0x480 + 0x18 * 16 and (offset - 0x480) % 0x18 == 0:
            pass  # Read half of the fetch-word read-modify-write.
        elif op in ('ld', 'std'):
            assert offset in (8, 0x18), hex(pc)
        else:
            raise ValueError('Unclassified device access %s %s at %08X' % (op, args, pc))
    return sum(1 << f for f in fetch), sum(1 << lane for lane in lanes)


def sites():
    result = []
    for pass_index, start, end, frame, returns in PASSES:
        rows = disassemble(start, end)
        order = 0
        for index, (pc, word, op, args) in enumerate(rows):
            if word == DEVICE_LOAD:
                fetch, lanes = device_block(rows, index)
                assert fetch or lanes, hex(pc)
                result.append(dict(site=pc, pass_=pass_index, order=order, op='Device', a=fetch, b=lanes & 0xFFFFFFFF, c=lanes >> 32, d=0, jump=pc + 4))
                order += 1
                continue
            if op != 'bl':
                continue
            target = int(args, 0)
            if not (0x82438000 <= target < 0x82462000):
                continue
            a = b = c = d = 0
            arg = lambda r: argument(rows, index, r)
            def imm(r):
                kind, value = arg(r)
                assert kind == 'imm', (hex(pc), r, kind, value)
                return value
            def field(r):
                kind, value = arg(r)
                assert kind == 'field', (hex(pc), r, kind, value)
                return value
            if target in SCALARS:
                name = 'Scalar'; a, b = SCALARS[target], imm('r4')
            elif target in SAMPLERS:
                name = 'Sampler'; a, b, c = SAMPLERS[target], imm('r4'), imm('r5')
            else:
                name = OPS[target]
                if name == 'PackedBlend':
                    a, b = imm('r4'), imm('r5')
                elif name == 'Resolve':
                    a, b = field('r6'), imm('r4')
                elif name in ('VertexShader', 'Declaration'):
                    a = field('r4')
                elif name == 'PixelShader':
                    # Fog selects one of three handles on separate paths.
                    previous = max(i for i in range(index) if rows[i][2] == 'bl')
                    handles = sorted({shader_handle(rows, i) for i in range(previous + 1, index)
                                      if rows[i][2] == 'lwz' and rows[i][3].startswith('r4,') and rows[i][3].endswith('(r11)')})
                    assert 1 <= len(handles) <= 3, (hex(pc), handles)
                    a, b, c = (handles + [0, 0])[:3]
                elif name == 'Texture':
                    a, b, c = imm('r4'), field('r5'), imm('r6')
                elif name == 'Constants':
                    kind, value = arg('r5')
                    assert kind in ('stack', 'reg'), (hex(pc), kind, value)
                    a, b, c, d = imm('r4'), imm('r6'), (0 if kind == 'stack' else 1), value
                elif name == 'Begin':
                    a, b, c = imm('r4'), imm('r5'), imm('r6')
            result.append(dict(site=pc, pass_=pass_index, order=order, op=name, a=a, b=b, c=c, d=d, jump=pc + 4))
            order += 1
    return result


def header(rows):
    lines = ['// Generated by tools/generate_screen_effect_sites.py from the original pass code; do not edit.',
             '// Device sites: a = fetch fields (bit stage*2+{0:U,1:V}), b/c = PS constant lanes 0..31/32..63.',
             '#pragma once', '#include <array>', '#include <cstdint>', '', 'namespace Simpsons::ScreenEffects {',
             'enum class Op : uint8_t {Scalar,Sampler,PackedBlend,Resolve,VertexShader,PixelShader,Texture,Declaration,Constants,Begin,End,Device};',
             'struct Site {uint32_t site;uint8_t pass,order;Op op;uint32_t a,b,c,d;};',
             'struct Pass {uint32_t entry,frame;std::array<uint32_t,3> returns;};',
             'inline constexpr std::array<Pass,%d> passes{{' % len(PASSES)]
    for _, start, _, frame, returns in PASSES:
        r = list(returns) + [0, 0]
        lines.append('    {0x%08X,0x%X,{0x%08X,0x%08X,0x%08X}},' % (start, frame, r[0], r[1], r[2]))
    lines.append('}};')
    lines.append('inline constexpr std::array<Site,%d> sites{{' % len(rows))
    for row in rows:
        lines.append('    {0x%08X,%d,%d,Op::%s,0x%X,0x%X,0x%X,0x%X},' % (row['site'], row['pass_'], row['order'], row['op'],
                                                                     row['a'], row['b'], row['c'], row['d']))
    lines += ['}};', '}', '']
    return '\n'.join(lines)


def hooks(rows):
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    out = ['# Original 82754288/82754C90/82755508/8276C930/8276FE60 screen effects, the',
           '# 82756268 letterbox and the 82755FD0 overlay query prefix:',
           '# generated by tools/generate_screen_effect_sites.py. SDK calls and console',
           '# device loads become bounded native endpoints; all CPU math stays original.', '']
    for row in rows:
        evidence = image[row['site'] - BASE:row['jump'] - BASE].hex()
        out += ['[[midasm_hook]]', 'address = 0x%08X' % row['site'], 'name = "SimpsonsNativeScreenEffect%08X"' % row['site'],
                'registers = ["ctx", "base"]', 'jump_address = 0x%08X' % row['jump'], 'evidence_hex = "%s"' % evidence, '']
    return '\n'.join(out)


def exports(rows):
    return ''.join('void SimpsonsNativeScreenEffect%08X(PPCContext& c,uint8_t* b){SnapshotHostState fp;snapshotDriver(b).screenEffectOperation(c,b,0x%08X);}\n'
                   % (row['site'], row['site']) for row in rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true')
    p.add_argument('--print', choices=('hooks', 'exports', 'table'))
    args = p.parse_args()
    rows = sites()
    text = header(rows)
    target = ROOT / 'runtime/screen_effect_sites.h'
    if args.print == 'hooks':
        print(hooks(rows)); return 0
    if args.print == 'exports':
        print(exports(rows), end=''); return 0
    if args.print == 'table':
        for row in rows:
            print('%08X pass%d #%02d %-12s a=%X b=%X c=%X d=%X' % (row['site'], row['pass_'], row['order'], row['op'], row['a'], row['b'], row['c'], row['d']))
        return 0
    if args.check:
        if target.read_text(encoding='utf-8') != text:
            raise SystemExit('runtime/screen_effect_sites.h differs from the original pass code')
        toml = (ROOT / 'config/simpsons.toml').read_text(encoding='utf-8')
        if hooks(rows) not in toml:
            raise SystemExit('config/simpsons.toml lacks the generated screen-effect hooks')
        print('PASS %d screen-effect sites match the original pass code, header and hooks' % len(rows)); return 0
    target.write_text(text, encoding='utf-8', newline='\n')
    print('wrote', target, len(rows), 'sites')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
