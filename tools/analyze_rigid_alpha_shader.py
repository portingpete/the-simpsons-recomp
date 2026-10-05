"""Offline, exact CCB8 rigidalpha shader transcription; no runtime admission.

VS8200D734 has the same executable instructions as VS8201739C. PS8200E1BC
samples t0 RGB, selects a silhouette from the absolute view/normal cosine,
and exports its original opacity expression. Native binding remains separate.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import analyze_168f8alpha_shader as a168
import analyze_edge_shaders as edge
import analyze_post_effect_catalog as catalog
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x8200D734, 676, 424, 252, 3,
     'c1dc83387b4951de7e24b779ef3c10dcd45c7df72a229f60bfbf52f1d64ea13d'),
    ('PS', 0x8200E1BC, 724, 532, 192, 2,
     '3962cab675bbf80c79137e58f13633ab5438e2100b113af4c41baabb73103161'),
)
HEADERS = {'VS': (0x102A1101, 424, 252, 36, 116, 0, 316, 0, 0),
           'PS': (0x102A1100, 468, 256, 36, 116, 380, 420, 0, 0)}
CF = {'VS': a168.CF['VS'], 'PS': ((0x11002, 0x1200), (0, 0xC400),
                                (0x6003, 0x1200), (0x6009, 0x2200))}
LITERALS = (0,) * 12 + (0x3F800000, 0, 0, 0)


def inspect(image):
    screen.require(len(image) == screen.IMAGE_SIZE and
                   hashlib.sha256(image).hexdigest() == screen.IMAGE_SHA256, 'Original image changed')
    original = a168.inspect(image)
    fx = image[0xCCB8:0xCCB8 + 12000]
    screen.require(hashlib.sha256(fx).hexdigest() == rigid.FX_SHA, 'Rigid effect changed')
    metadata = catalog.inspect_blob(fx, 0x8200CCB8)
    technique = metadata['techniques'][1]
    p = technique['passes'][0]
    screen.require(technique['name'] == 'rigidalpha' and technique['handle'] == '0x0007FFFC'
                   and p['handle'] == '0x0007FFFE' and p['context_offset'] == 0x2910,
                   'Rigid alpha technique changed')
    screen.require([(s['sdk_id'], s['value']) for s in p['scalar_states']] == [(40, 1), (48, 1)],
                   'Rigid alpha depth states changed')
    screen.require([(s['stage'], s['sdk_id'], s['value']) for s in p['sampler_states']] ==
                   [(0, k, v) for k, v in zip((0, 4, 8, 16, 20, 24), (0, 0, 0, 1, 1, 1))],
                   'Rigid alpha sampler states changed')
    body = fx[12:]
    constant_maps = {}
    expected = {
        'private': {2: (0xC0004, 0xC0C, 0, 0), 14: (0x48001C, 0, 49, 0),
                    15: (0x4C001E, 0, 40, 0), 16: (0x500020, 0, 0, 0)},
        'shared': {0: (0x40001, 0xC00, 0, 0), 1: (0x80003, 0, 4, 0)},
    }
    for space, name, count, start in ((0, 'private', 22, 0x29F0), (1, 'shared', 11, 0x2B50)):
        screen.require(struct.unpack_from('>I', body, 0x2910 + 64 + 4 * space)[0] == start and
                       metadata['parameters'][name]['leaf_count'] == count, 'Alpha map bounds changed')
        rows = {i: struct.unpack_from('>4I', body, start + 16 * i) for i in range(count)}
        rows = {i: value for i, value in rows.items() if any(value)}
        screen.require(rows == expected[name], 'Alpha constant/resource map changed')
        constant_maps[name] = rows
    result = {'technique': technique, 'constant_maps': constant_maps,
              'consumed_inputs': {'vertex': 'view-projection c0..3, object c12..15',
                                  'pixel': 'world eye c4.xyz, object ID c40.w, custom lines c49.xy',
                                  'texture': 'private g_BaseSampler leaf16, stage0; no shadow sampler'}}
    for stage, va, size, start, length, pairs, digest in PROFILES:
        record = image[va - screen.BASE:va - screen.BASE + size]
        screen.require(hashlib.sha256(record).hexdigest() == digest, stage + ' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage], stage + ' header changed')
        screen.require(record[-12:].hex() == ('4e4a0001' if stage == 'VS' else '4e4a0000') +
                       '09a7ce5aa0f8c989', stage + ' trailer changed')
        code = record[start:start + length]
        control, schedule = a168.schedule(code, pairs)
        screen.require(tuple(control) == CF[stage], stage + ' control changed')
        screen.require(not any(select for _, _, select in schedule), 'Unexpected issue select')
        rows = {}
        for slot, fetch, _ in schedule:
            raw = screen.words(code, 12 * slot)
            fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            if not fetch:
                for k in ('absolute_constants', 'vector_destination_relative',
                          'scalar_destination_relative_or_export_zero', 'predicated', 'predicate_condition',
                          'constant_address_register_relative', 'constant_0_relative', 'constant_1_relative'):
                    screen.require(not fields[k], 'Unqualified ALU modifier ' + k)
                screen.require(not any(s['relative_temporary'] for s in fields['sources']), 'Relative temporary')
            rows[slot] = dict(fetch=fetch, raw=raw, fields=fields)
        if stage == 'VS':
            old = a168.PROFILES[0]
            other = image[old[1] - screen.BASE + old[3]:old[1] - screen.BASE + old[3] + old[4]]
            screen.require(code[:-12] == other[:-12] and rows == original['VS']['rows'],
                           'Shared alpha vertex executable differs')
        else:
            screen.require(struct.unpack_from('>16I', record, 468) == LITERALS, 'PS literals changed')
            screen.require([i for i, r in rows.items() if r['fetch']] == [2], 'PS fetch count changed')
            f = rows[2]['fields']
            screen.require(f['source_register'] == 0 and f['destination_register'] == 4 and
                           f['destination_swizzle'] == [0, 1, 2, 7], 'PS sample wiring changed')
            a168.a168_sample(f)  # Validates the entire sampler instruction contract.
            screen.require([i for i, r in rows.items() if r['fields'].get('export')] == [13, 14],
                           'PS exports changed')
        result[stage] = dict(address=f'{va:08X}', sha256=digest, code_sha256=hashlib.sha256(code).hexdigest(),
                             control=control, rows=rows)
    return result


def ps_issue(slot, row):
    if row['fetch']:
        return '    { // slot2\n' + a168.a168_sample(row['fields']) + '\n    }'
    text = rigid.issue(slot, row, 'PS')
    # Both normalizations require SM3 zero-product behavior when a length is
    # zero. Preserve the co-issued old-register reads in the shared emitter.
    if slot in (6, 7):
        a, b = (rigid.operand(row['fields'], i, 'PS') for i in (0, 1))
        before = 'precise float4 v=' + a + '*' + b + ';'
        screen.require(before in text, 'Normalization multiplication changed')
        text = text.replace(before, 'precise float4 v=rigidLegacyMultiply(' + a + ',' + b + ');')
    return text


def shader_source(image):
    inventory = inspect(image)
    # Reuse only the VS whose complete executable was compared above. The
    # separate PS below comes solely from the CCB8 original record.
    source = a168.shader_source(image).split('float4 PS168F8(', 1)[0]
    source = source[source.index('cbuffer RigidVertexConstants'):]
    source = source.replace('VS168F8', 'VSRigidAlpha').replace('A168', 'RigidAlpha')
    source = source.replace('// PS slot15 uses the original SM3 multiplication rule:',
                            '// Normalization uses the original SM3 multiplication rule:')
    source = '// VS8200D734 / PS8200E1BC; offline transcription.\n' + source
    lines = [source, 'float4 PSRigidAlpha(RigidAlphaOutput input):SV_Target0 {',
             '    const float4 k255=float4(1,0,0,0);',
             '    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=input.t3,r4=0;',
             '    precise float4 output0=0; precise float ps=0;']
    lines += [ps_issue(slot, row) for slot, row in inventory['PS']['rows'].items()]
    lines += ['    return output0;', '}']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    source = shader_source(image)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if args.emit_hlsl:
        args.emit_hlsl.write_text(source, encoding='utf-8')
    print('PASS original rigidalpha inventory: 17 VS slots, 13 PS slots; exact VS reuse, one t0 fetch')


if __name__ == '__main__':
    main()
