"""Exact offline transcription of the base skin alpha pass.

VS82008E20 is executable-identical to the qualified opaque skin VS82007C1C;
only the original record trailer differs. PS8200A4A4 is a separate stage-0
texture program and is transcribed independently.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import analyze_168f8alpha_shader as a168
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen
import analyze_skin_shader as skin

ROOT = Path(__file__).resolve().parents[1]
VS = (0x82008E20, 4604, 3800, 804,
      '11f2f256c463e01a385b05e91a97b8d393680e62154e17f4abcb082753f8a17c')
PS = (0x8200A4A4, 720, 528, 192,
      'a8bcfe5c5bea6cbbb2e1f3cd3b0d98827241b86b448dbf390e4fa1e12c4aeca4')
VS_HEADER = (0x102A1101, 3736, 868, 36, 116, 3556, 3596, 0, 0)
PS_HEADER = (0x102A1100, 464, 256, 36, 116, 376, 416, 0, 0)
VS_TRAILER = '4e4a0001b88abdbf816783b1'
PS_TRAILER = '4e4a0000b88abdbf816783b1'
PS_LITERALS = (0,) * 12 + (0x3F800000, 0, 0, 0)


def inspect(image):
    opaque = skin.inspect(image)
    va, size, start, length, digest = VS
    record = image[va-screen.BASE:va-screen.BASE+size]
    screen.require(hashlib.sha256(record).hexdigest() == digest, 'Skin alpha VS record changed')
    screen.require(struct.unpack_from('>9I', record) == VS_HEADER, 'Skin alpha VS header changed')
    screen.require(record[-12:].hex() == VS_TRAILER, 'Skin alpha VS trailer changed')
    opaque_record = image[0x82007C1C-screen.BASE:0x82007C1C-screen.BASE+4604]
    screen.require(record[start:start+length-12] == opaque_record[3800:3800+804-12],
                   'Skin alpha VS executable differs from opaque skin VS')

    va, size, start, length, digest = PS
    record = image[va-screen.BASE:va-screen.BASE+size]
    screen.require(hashlib.sha256(record).hexdigest() == digest, 'Skin alpha PS record changed')
    screen.require(struct.unpack_from('>9I', record) == PS_HEADER, 'Skin alpha PS header changed')
    screen.require(record[-12:].hex() == PS_TRAILER, 'Skin alpha PS trailer changed')
    screen.require(struct.unpack_from('>16I', record, PS_HEADER[1]) == PS_LITERALS,
                   'Skin alpha PS literals changed')
    code = record[start:start+length]
    control, schedule = a168.schedule(code, 2)
    screen.require(tuple(control) == ((0x11002, 0x1200), (0, 0xC400),
                                     (0x6003, 0x1200), (0x6009, 0x2200)),
                   'Skin alpha PS control flow changed')
    rows = {}
    for slot, fetch, predicated in schedule:
        screen.require(not predicated, 'Skin alpha PS predication changed')
        raw = screen.words(code, slot*12)
        rows[slot] = dict(fetch=fetch, raw=raw,
                          fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw))
    screen.require([slot for slot, row in rows.items() if row['fetch']] == [2],
                   'Skin alpha PS fetch set changed')
    sample = rows[2]['fields']
    # This is the same exact stage-0 sample contract as the qualified rigid
    # alpha program; only the following ALU differs.
    a168.a168_sample(sample)
    return {'VS': opaque['VS'], 'PS': {'rows': rows, 'control': control}}


def ps_issue(slot, row):
    if row['fetch']:
        return '    { // slot2\n' + a168.a168_sample(row['fields']).replace(
            'aTex0', 'shadow0').replace('aSampler0', 'shadowSampler0') + '\n    }'
    text = rigid.issue(slot, row, 'PS')
    f = row['fields']
    if f['vector_mask'] and f['vector_opcode'] == 1:
        a, b = [rigid.operand(f, i, 'PS') for i in range(2)]
        text = text.replace(a + '*' + b, 'rigidLegacyMultiply(' + a + ',' + b + ')')
    return text


def shader_source(image):
    rows = inspect(image)['PS']['rows']
    source = skin.shader_source(image)
    lines = [source.rstrip(),
             '// Base skin alpha PS8200A4A4. VS82008E20 reuses VSSkin exactly.',
             'float4 PSSkinAlpha(SkinOutput input):SV_Target0 {',
             '    const float4 k255=float4(1,0,0,0);',
             '    precise float4 r0=float4(input.t0,0,0),r1=float4(input.t1,0);',
             '    precise float4 r2=input.t2,r3=input.t3,r4=0,output0=0;',
             '    precise float ps=0; bool p0=false;']
    lines += [ps_issue(slot, rows[slot]) for slot in sorted(rows)]
    lines += ['    return output0;', '}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-hlsl', type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    # Force every PS ALU through the emitter during verification.
    for slot, row in result['PS']['rows'].items():
        ps_issue(slot, row)
    if args.emit_hlsl:
        args.emit_hlsl.write_text(shader_source(image), encoding='utf-8')
    print('PASS base skin alpha: VS exact executable reuse; 13 PS slots transcribed')


if __name__ == '__main__':
    main()
