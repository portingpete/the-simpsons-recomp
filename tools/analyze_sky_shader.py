"""Pinned offline inventory and HLSL transcription of both original sky passes.

VS82036F08 / PS820374E8 implement the unlit textured sky (technique
rigidalpha 0007FFFC/0007FFFE, context 0x27B0): two vertex fetches
(position, uv), a 3-row transform exporting clip (x,y,w,w) to place the sky
at the far plane, two uv transforms, four texture samples and gradient math.
This script pins headers, trailers, complete control flow and full
issue-slot coverage, trial-emits every ALU slot through the shared rigid
emitter (with sky-specific sampler/export forms), and transcribes the full
VS/PS pair to test-only HLSL. Runtime admission, GPU arithmetic
qualification and mesh integration require separate proof; transcription
alone does not enable rendering.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_post_effect_catalog as catalog

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x82036F08, 0x2D4, 0x220, 180, 3,
     'ad81441498042410ca110b1381397568c26b5c6d1743a1108bcf4c1738647147'),
    ('PS', 0x820374E8, 0x2F4, 0x21C, 216, 2,
     '383e24ec449a1659786be5cce4b15d0d4db2699f13d647f0882c098ca9da1f8a'),
)
OPAQUE_PROFILES = (
    ('VS', 0x82036C2C, 0x2D4, 0x220, 180, 3,
     'eb0ddeb766888ca36fdc88e56f32fef036f83650ddaadea2894c4e2cf199901d'),
    ('PS', 0x820371EC, 0x2F4, 0x21C, 216, 2,
     'bf5a01df4d7c45879b990449e46a498d3f3e5600271b67386bec8d1a49509586'),
)
VS_LINKAGE = bytes.fromhex('00000000000000b4003100020000000000000000000028840000000100000002000000040000029000100003003050040000305000013151000232520003f3530000100d0000100a0000100b0000100c')
PS_LINKAGE = bytes.fromhex('00000040000000d810000300000000040000000000002884000f000f000000210000305000003151000032520000f353')
HEADERS = {
    'VS': (0x102A1101, 0x220, 0xB4, 0x24, 0x74, 0, 0x1D0, 0, 0),
    'PS': (0x102A1100, 0x1DC, 0x118, 0x24, 0x74, 0x184, 0x1AC, 0, 0),
}
TRAILERS = {
    'VS': '4e4a0001055e4781c46fa554',
    'PS': '4e4a0000055e4781c46fa554',
}
OPAQUE_TRAILERS = {
    'VS': '4e4a0003055e4781c46fa554',
    'PS': '4e4a0002055e4781c46fa554',
}
# Pinned (lo, hi16) control pairs from the generic CF walk: ALLOC/EXEC shapes
# match the shared ISA reference; coverage below pins the full schedule.
CF = {
    'VS': ((0x30052003, 0x1200), (0, 0xC200), (0x4005, 0x1200),
           (0, 0xC400), (0x5009, 0x2200), (0, 0)),
    'PS': ((0x09006002, 0x1200), (0x00544008, 0x1200),
           (0, 0xC400), (0x500C, 0x2200)),
}
# Pinned PS literal bank (k252..k255): k254=(0.5,-0.5,0,1), k255=(~0.99,0,0,0).
PS_LITERALS = (0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x00000000, 0x00000000, 0x00000000, 0x00000000,
               0x3F000000, 0xBF000000, 0x00000000, 0x3F800000,
               0x3F7D70A4, 0x00000000, 0x00000000, 0x00000000)
VS_FETCHES = {
    3: dict(kind='vertex_fetch', source_register=0, destination_register=2,
           destination_swizzle=[0, 1, 2, 5], fetch_constant_index=95, source_component=0),
    4: dict(kind='vertex_fetch', source_register=0, destination_register=0,
           destination_swizzle=[0, 1, 7, 7], fetch_constant_index=95, source_component=0),
}
PS_FETCHES = {
    # slot: (const, source_reg, dest_reg, dest_swizzle).
    6: (3, 3, 0, [7, 7, 0, 7]),
    9: (0, 0, 0, [7, 0, 1, 2]),
    10: (1, 1, 3, [0, 1, 2, 3]),
    11: (2, 2, 1, [0, 1, 2, 3]),
}
VS_EXPORTS = {
    # slot: (destination, mask).
    8: (62, 15), 10: (1, 3), 11: (2, 3), 12: (3, 15), 13: (0, 3),
}


def schedule(code, pairs):
    """Generic CF walk (shared ISA reference): EXEC/EXEC_END/ALLOC/NOP."""
    screen.require(len(code) % 12 == 0 and 0 < pairs < len(code) // 12 - 1,
                   'Malformed sky instruction window')
    control, executed = [], []
    ended = False
    for pair in range(pairs):
        a, b, c = screen.words(code, pair * 12)
        for lo, hi in ((a, b & 65535), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)):
            op = hi >> 12
            screen.require(not ended or (lo == 0 and hi == 0), 'Non-NOP after sky EXEC_END')
            if op in (1, 2):
                address, count, sequence = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
                screen.require(not lo & 0x8000 and hi & 0x0FFC == 0x200, 'Unproved sky EXEC control flags')
                screen.require(0 < count <= 6 and sequence >> (2 * count) == 0, 'Malformed sky EXEC count/sequence')
                screen.require(pairs <= address < address + count <= len(code) // 12 - 1, 'Sky EXEC overlaps CF/trailer')
                for i in range(count):
                    flags = (sequence >> (i * 2)) & 3
                    executed.append((address + i, bool(flags & 1), bool(flags & 2)))
                ended = op == 2
            elif op == 12:
                screen.require(lo == 0 and hi in (0xC200, 0xC400), 'Unproved sky ALLOC shape')
            else:
                screen.require(op == 0 and lo == 0 and hi == 0, 'Unsupported sky control flow')
            control.append((lo, hi))
    slots = [e[0] for e in executed]
    screen.require(ended and sorted(slots) == list(range(pairs, len(code) // 12 - 1)) and len(set(slots)) == len(slots),
                   'Unaccounted or repeated sky scheduled slot')
    return control, executed


def sky_sample(f):
    """Emit one sky texture fetch as an HLSL Sample with pinned contract."""
    slot = f['fetch_constant_index']
    screen.require(slot in (0, 1, 2, 3), 'Unqualified sky sampler')
    screen.require(f['source_components'][:2] == [0, 1] and f['normalized_coordinates'] and f['dimension_field'] == 1 and
                   f['computed_lod'] and not f['register_lod'] and not f['register_gradients'],
                   'Unqualified sky texture instruction')
    screen.require(f['offset_fields'] == [0, 0, 0], 'Nonzero sky texture offset')
    screen.require([f[k] for k in ('mag_filter', 'min_filter', 'mip_filter')] == [3, 3, 3] and f['anisotropy'] == 7 and
                   f['fetch_valid_only'] and f['sample_location'] == 0 and f['lod_bias_field'] == 0,
                   'Unqualified sky sampler state contract')
    source = 'r' + str(f['source_register']) + '.xy'
    lines = ['        precise float4 v=skyTex%d.Sample(skySampler%d,%s);' % (slot, slot, source)]
    for lane, selector in enumerate(f['destination_swizzle']):
        if selector != 7:
            screen.require(selector < 4, 'Unqualified sky texture component literal')
            lines.append('        r' + str(f['destination_register']) + '.' + 'xyzw'[lane] + '=v.' + 'xyzw'[selector] + ';')
    return '\n'.join(lines)


def ps_issue(slot, row):
    """Emit one PS slot with sky sampler/export forms."""
    f = row['fields']
    if row['fetch']:
        return '    { // slot%d\n%s\n    }' % (slot, sky_sample(f))
    if slot == 8:
        # KILLGT has a pixel side effect even with a zero destination mask.
        # Slot7 leaves r0.z = literal .99 - the sampled line-mask red value.
        # The strict greater comparison discards exactly when mask.red > .99.
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        screen.require(f['vector_opcode'] == 25 and f['vector_mask'] == 0 and
                       f['scalar_opcode'] == 50 and f['scalar_mask'] == 0 and
                       not f['export'] and not f['predicated'] and
                       a == 'k254.zzzz' and b == 'r0.zzzz',
                       'Sky slot8 pixel kill changed')
        return '    { // slot8: KILLGT, independent of the destination mask\n        if (any(%s > %s)) discard;\n    }' % (a, b)
    if slot == 15:
        # Plain three-lane multiply (r0.xyz=r0.www*r0.xyz); unlike rigid PS
        # slot15 this is not the legacy zero-product form.
        screen.require(f['vector_opcode'] == 1 and f['vector_mask'] == 7 and
                       f['vector_destination'] == 0 and not f['export'] and f['scalar_opcode'] == 50,
                       'Sky slot15 shape changed')
        a, b = [rigid.operand(f, j, 'PS') for j in range(2)]
        return '    { // slot%d\n        precise float4 v=%s*%s;\n        r0.xyz=v.xyz;\n    }' % (slot, a, b)
    if slot == 16:
        # Final color export (r0.xyzw=mad(r1.www,r1.xyz,r0.xyz) to output0).
        # The scalar w-lane write targets dead r0.w (last slot, never read or
        # exported); only the vector export is observable.
        screen.require(row['raw'] == (0xC88FC000, 0x001BC0C0, 0xEB010100),
                       'Sky slot16 shape changed')
        a, b, c = [rigid.operand(f, j, 'PS') for j in range(3)]
        return ('    { // slot%d\n        precise float4 v=mad(%s,%s,%s);\n'
                '        output0.xyzw=v.xyzw;\n    }') % (slot, a, b, c)
    return rigid.issue(slot, row, 'PS')


def emit_vs_control(rows):
    lines = []
    for slot in sorted(rows):
        row = rows[slot]
        if row['fetch']:
            continue
        lines.append(rigid.issue(slot, row, 'VS'))
    return lines


def emit_ps_control(rows):
    lines = []
    for slot in sorted(rows):
        lines.append(ps_issue(slot, rows[slot]))
    return lines


def shader_source(image):
    """Transcribe sky VS+PS to test-only HLSL; GPU/runtime need separate proof."""
    inventory = inspect(image)
    opaque = inspect(image, opaque=True)
    whole_pass_proof(image)
    common = rigid.shader_source(image).split('struct RigidInput {', 1)[0]
    # Sky VS needs vc47 (uv transform); PS needs pc23 plus k254/k255.
    # Sky samples its own t0-t3/s0-s3; drop the shared shadow declarations
    # (same registers, unused by either sky shader).
    for stale in ('Texture2D<float4> shadow0 : register(t0);',
                  'Texture2D<float4> shadow1 : register(t1);',
                  'SamplerState shadowSampler0 : register(s0);',
                  'SamplerState shadowSampler1 : register(s1);'):
        screen.require(stale in common, 'Shared prologue shadow declaration changed')
        common = common.replace(stale + '\n', '')
    lines = ['// Exact original sky opaque VS82036C2C/PS820371EC and alpha VS82036F08/PS820374E8.',
             '// GPU arithmetic and runtime integration require separate qualification.',
             common.replace('float4 vc[30]', 'float4 vc[64]'),
             'Texture2D<float4> skyTex0 : register(t0);',
             'Texture2D<float4> skyTex1 : register(t1);',
             'Texture2D<float4> skyTex2 : register(t2);',
             'Texture2D<float4> skyTex3 : register(t3);',
             'SamplerState skySampler0 : register(s0);',
             'SamplerState skySampler1 : register(s1);',
             'SamplerState skySampler2 : register(s2);',
             'SamplerState skySampler3 : register(s3);',
             'struct SkyInput { float3 position:TEXCOORD0; float2 uv:TEXCOORD1; };',
             'struct SkyOutput { float4 position:SV_Position; float2 t0:TEXCOORD0;',
             '    float2 t1:TEXCOORD1; float2 t2:TEXCOORD2; float4 t3:TEXCOORD3; };']
    lines += ['SkyOutput VSSky(SkyInput input) {',
              '// Fetch mapping: r2=position (w=1 per swizzle 5), r0=uv.',
              '    precise float4 r2=float4(input.position,1),r0=float4(input.uv,0,0);',
              '    precise float4 r1=0;',
              '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0;']
    lines += emit_vs_control(inventory['VS']['rows'])
    lines += ['    SkyOutput result; result.position=output62;',
              '    result.t0=output0.xy; result.t1=output1.xy;',
              '    result.t2=output2.xy; result.t3=output3; return result;', '}']
    lines += ['float4 PSSky(SkyOutput input):SV_Target0 {']
    plits = struct.unpack_from('>16I', image, PROFILES[1][1] - screen.BASE + 0x1DC)
    screen.require(tuple(plits) == PS_LITERALS, 'Sky PS literal bank changed')
    for i in range(4):
        lines.append('    const float4 k%d=asfloat(uint4(%s));' %
                     (252 + i, ','.join('0x%08Xu' % v for v in plits[i * 4:i * 4 + 4])))
    lines += ['    precise float4 r0=float4(input.t0,0,0),r1=float4(input.t1,0,0);',
              '    precise float4 r2=float4(input.t2,0,0),r3=input.t3;',
              '    precise float4 output0=0;',
              '    precise float ps=0;']
    lines += emit_ps_control(inventory['PS']['rows'])
    lines += ['    return output0;', '}']
    # Emit the opaque records under their own artifact entry names. The full
    # original executable, fetch linkage, literals and pass contract are
    # independently pinned below; no unknown record is admitted as an alias.
    alpha_text = '\n'.join(lines) + '\n'
    function_start = alpha_text.index('SkyOutput VSSky(')
    opaque_text = alpha_text[function_start:].replace('VSSky(', 'VSSkyOpaque(').replace('PSSky(', 'PSSkyOpaque(')
    screen.require(opaque['VS']['rows'] == inventory['VS']['rows'] and opaque['PS']['rows'] == inventory['PS']['rows'],
                   'Sky opaque executable differs from the qualified alpha transcription')
    return alpha_text + '\n' + opaque_text


def inspect(image, opaque=False):
    """Validate pinned records and decode fields; does not qualify execution."""
    screen.require(hashlib.sha256(rigid.four.UCODE.read_bytes()).hexdigest() == rigid.four.UCODE_SHA,
                   'Pinned sky KILLGT instruction reference changed')
    result = {}
    for stage, va, size, start, length, pairs, digest in (OPAQUE_PROFILES if opaque else PROFILES):
        record = image[va - screen.BASE:va - screen.BASE + size]
        screen.require(hashlib.sha256(record).hexdigest() == digest,
                       'Sky ' + stage + ' record changed')
        screen.require(struct.unpack_from('>9I', record) == HEADERS[stage],
                       'Sky ' + stage + ' header changed')
        screen.require(record[-12:].hex() == (OPAQUE_TRAILERS if opaque else TRAILERS)[stage],
                       'Sky ' + stage + ' trailer changed')
        screen.require(record[HEADERS[stage][6]:HEADERS[stage][1]] == (VS_LINKAGE if stage == 'VS' else PS_LINKAGE),
                       'Sky ' + stage + ' fetch linkage changed')
        if stage == 'PS':
            screen.require(struct.unpack_from('>16I', record, HEADERS[stage][1]) == PS_LITERALS,
                           'Sky PS literal bank changed')
        code = record[start:start + length]
        control, executed = schedule(code, pairs)
        screen.require(tuple(control) == tuple((lo, hi) for lo, hi in CF[stage]),
                       'Sky ' + stage + ' control flow changed')
        rows = {}
        for slot, fetch, _select in executed:
            raw = screen.words(code, slot * 12)
            fields = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            rows[slot] = dict(fetch=fetch, raw=raw, fields=fields)
        if stage == 'VS':
            for slot, contract in VS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], 'Sky VS fetch slot changed')
                f = rows[slot]['fields']
                for key, value in contract.items():
                    screen.require(f.get(key) == value, 'Sky VS fetch contract changed')
            for slot, (destination, mask) in VS_EXPORTS.items():
                f = rows[slot]['fields']
                screen.require(not rows[slot]['fetch'] and f['vector_destination'] == destination and
                               f['export'] and f['vector_mask'] == mask, 'Sky VS export changed')
        else:
            for slot, (const, source, destination, swizzle) in PS_FETCHES.items():
                screen.require(slot in rows and rows[slot]['fetch'], 'Sky PS fetch slot changed')
                f = rows[slot]['fields']
                screen.require(f['kind'] == 'texture_fetch' and f['fetch_constant_index'] == const and
                               f['source_register'] == source and f['destination_register'] == destination and
                               f['destination_swizzle'] == swizzle, 'Sky PS fetch contract changed')
            exports = [slot for slot, row in rows.items() if row['fields'].get('export')]
            screen.require(exports == [16], 'Sky PS export set changed')
        result[stage] = dict(address=hex(va), control=[(hex(lo), hex(hi)) for lo, hi in control], rows=rows,
                             header=struct.unpack_from('>9I', record),
                             trailer=record[-12:].hex())
    return result


def whole_pass_proof(image):
    """Pin selection, complete used/unused maps, storage, masks and state."""
    fx = image[0x36448:0x36448 + 11712]
    screen.require(hashlib.sha256(fx).hexdigest() == 'b13b8d005119d97a16bac18a10824813997e02eb99ee2e12e2de55b37e41932b',
                   'Original whole sky effect changed')
    b = fx[12:]
    metadata = catalog.inspect_blob(fx, 0x82036448)
    profiles = (OPAQUE_PROFILES, PROFILES)
    for technique, context, shaders, name, handle in zip(metadata['techniques'], (0x2480, 0x27B0), profiles,
                                                         ('rigid', 'rigidalpha'), ('0x0003FFFC', '0x0007FFFC')):
        screen.require(technique['name'] == name and technique['handle'] == handle and len(technique['passes']) == 1,
                       'Original sky technique association changed')
        p = technique['passes'][0]
        screen.require(p['context_offset'] == context and p['scalar_block_offset'] == 0x13B0 and
                       p['sampler_block_offset'] == 0x1470 and
                       [(s['sdk_id'], s['value']) for s in p['scalar_states']] == [(0x28, 1), (0x30, 0)],
                       'Original sky pass state changed')
        for key, profile in zip(('vertex', 'pixel'), shaders):
            screen.require(p['shaders'][key]['shader_sha256'] == profile[-1] and
                           struct.unpack_from('>I', b, context + (0x48 if key == 'vertex' else 0x4C))[0] + 0x82036454 + 8 == profile[1],
                           'Original sky selected shader association changed')
        ids = (0, 4, 8, 0x10, 0x14, 0x18)
        expected = [(stage, sdk, (0 if lane < 3 else 1) if stage < 3 else (2 if lane < 3 else 0 if lane < 5 else 2))
                    for stage in range(4) for lane, sdk in enumerate(ids)]
        screen.require([(s['stage'], s['sdk_id'], s['value']) for s in p['sampler_states']] == expected,
                       'Original sky sampler ownership changed')
    spans = ((0x2560, 0x2890, 26*16, '489ec17a2859aec15ffa5dec0233d6c456f66a880db38cd86e493103249dd97c'),
             (0x2700, 0x2A30, 11*16, '098f439141032607ddeb0aee7dc0c4dac9be8d61f3463413193bad875a4d6ff0'),
             (0x24D8, 0x2808, 128, 'd61302e938854f190740c0f733aa460bfb3cf6b03f3b89ec8d8e040ee9d2602e'),
             (0x13B0, 0x13B0, 0x30, '106a5d02b9bc19ae5cd3cde7eeef666eecea2c255add6ba90d5070c06918054e'),
             (0x1470, 0x1470, 0x150, 'b9a1c9ef87936ecf7acfd868896133843670fe8428ef670a5318875bd8f89784'))
    for left, right, size, digest in spans:
        screen.require(b[left:left+size] == b[right:right+size] and hashlib.sha256(b[left:left+size]).hexdigest() == digest,
                       'Original sky complete pass maps/masks/state differ')
    private = {17:(0x00540022,47,0,0),18:(0x00580024,46,0,0),19:(0x005C0026,22,0,0),
               20:(0x00600028,0,0,0),21:(0x0064002A,0,0x400000,0),22:(0x0068002C,0,0x800000,0),
               24:(0x00700030,0,0xC00000,0),25:(0x00740032,0,23,0)}
    for leaf in range(26):
        screen.require(struct.unpack_from('>4I', b, 0x2560+16*leaf) == private.get(leaf,(0,0,0,0)),
                       'Original sky material register/texture map differs')
    for leaf in range(11):
        screen.require(struct.unpack_from('>4I', b, 0x2700+16*leaf) == ((0x00040001,0xC00,0,0) if leaf == 0 else (0,0,0,0)),
                       'Original sky shared register map differs')
    descriptors = struct.unpack_from('>I', b, 0x108)[0]
    storage = ((0xC00210,0x20017),(0xC00610,0x40018),(0x600000,0x10019),
               (0x60002C,0x1A),(0x60002C,0x1E),(0x60002C,0x22),(0x60002C,0x26),
               (0x60002C,0x2A),(0x600610,0x4002E))
    screen.require(struct.unpack_from('>I',b,0x138)[0] == 188*4,
                   'Original sky private storage extent differs')
    for leaf, row in enumerate(storage,17):
        screen.require(struct.unpack_from('>2I',b,descriptors+8*(leaf+4)) == row,
                       'Original sky material storage differs')
    # Shared rigid selector: low byte r7==0 loads typed+A8; otherwise +AC.
    selector = image[0x3CA730:0x3CA784]
    screen.require(hashlib.sha256(selector).hexdigest() == 'f11933824220ee756b1310e76441a49be7099cae2634ea290d531ff5c8ebaaf8',
                   'Original sky begin selector changed')
    return dict(source='82036448', contexts=['2480','27B0'], private_words=188,
                textures=[20,21,22,24], unused_texture_leaf=23,
                material_vertex_rows=[47,46], inherited_rows=['VS0..3','VS22','PS23'],
                depth_enable=1,depth_write=0, selector_sha256=hashlib.sha256(selector).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--emit-hlsl', type=Path)
    args = parser.parse_args()
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    result = inspect(image)
    if args.emit_hlsl:
        args.emit_hlsl.write_text(shader_source(image), encoding='utf-8')
    if args.output:
        args.output.write_text(json.dumps(result, indent=2, default=str) + '\n', encoding='utf-8')
    summary = {}
    for stage in ('VS', 'PS'):
        rows = result[stage]['rows']
        fetches = {slot: row['fields'] for slot, row in rows.items() if row['fetch']}
        emittable, failures = [], []
        for slot, row in rows.items():
            try:
                if stage == 'PS':
                    ps_issue(slot, row)
                elif row['fetch']:
                    continue
                else:
                    rigid.issue(slot, row, stage)
                emittable.append(slot)
            except Exception as error:
                failures.append((slot, str(error)[:110]))
        summary[stage] = dict(address=result[stage]['address'], slots=len(rows),
                              fetches={slot: dict(constant=f.get('fetch_constant_index'),
                                                  destination=f.get('destination_register'),
                                                  kind=f.get('kind')) for slot, f in fetches.items()},
                              emitted=len(emittable), failures=failures)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
