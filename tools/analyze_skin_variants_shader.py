"""Hash-pinned skin_gloss / skin_flipbook original instruction transcription.

Each pass retains its own record, input fetch wiring, control flow, literals,
register maps and output masks. No executable alias is used for alpha records.
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
from skin_variants_pins import PINS

ROOT = Path(__file__).resolve().parents[1]
ATTRIBUTES = ('position', 'normal', 'uv', 'uv1', 'indices', 'weights', 'color',
              'morph1', 'morph2', 'morph3', 'morph4', 'morph5', 'morph6')

# Only the atlas vertex instructions need bit-exact reciprocal rounding: their
# result feeds FRAC/TRUNC. DIV is permitted to use an approximate reciprocal.
# Use it as an estimate, then round an exact integer remainder. Pixel programs
# retain ordinary division rather than paying this vertex-only correction cost.
ATLAS_RECIPROCAL = r'''// Float32 RNE reciprocal for original atlas scalar opcode19.
float atlasReciprocalIEEE(float value) {
    uint bits=asuint(value),sign=bits&0x80000000u,m=bits&0x007FFFFFu;
    int e=int((bits>>23)&255u);
    if(e==255) return asfloat(m==0u?sign:(bits|0x00400000u));
    if(e==0) {
        if(m==0u) return asfloat(sign|0x7F800000u);
        uint shift=23u-firstbithigh(m);m<<=shift;e=1-int(shift);
    } else m|=0x00800000u;
    // Common unit/power-of-two atlas counts require no approximate arithmetic.
    if(m==0x00800000u) {
        int power=254-e;
        if(power>=255) return asfloat(sign|0x7F800000u);
        return asfloat(sign|(power>0?(uint(power)<<23):(1u<<uint(power+22))));
    }
    int resultExponent=253-e;
    if(resultExponent>=255) return asfloat(sign|0x7F800000u);
    // Round subnormal results directly, avoiding a second normal-then-shift rounding.
    uint k=resultExponent>0?47u:uint(46+resultExponent);
    uint numeratorHigh=1u<<(k-32u);
    precise float estimate=asfloat((k+127u)<<23)/float(m);
    uint q=uint(estimate);
    // Exact 24x24 (including an adjacent 25-bit estimate) product using SM5 uints.
    uint qLow=q&65535u,mLow=m&65535u;
    uint p0=qLow*mLow,cross=(q>>16)*mLow+qLow*(m>>16);
    uint lo=p0+(cross<<16),hi=(q>>16)*(m>>16)+(cross>>16)+uint(lo<p0);
    [unroll] for(uint correction=0u;correction<4u;++correction) {
        if(hi>numeratorHigh||(hi==numeratorHigh&&lo>0u)) {
            --q;uint old=lo;lo-=m;hi-=uint(old<m);
        } else {
            uint nextLow=lo+m,nextHigh=hi+uint(nextLow<lo);
            if(nextHigh<numeratorHigh||(nextHigh==numeratorHigh&&nextLow==0u)) {
                ++q;lo=nextLow;hi=nextHigh;
            }
        }
    }
    uint nextLow=lo+m,nextHigh=hi+uint(nextLow<lo),remainder=0u;
    if(hi>numeratorHigh||(hi==numeratorHigh&&lo>0u)||
        nextHigh<numeratorHigh||(nextHigh==numeratorHigh&&nextLow==0u)) {
        // Exact bounded fallback if an implementation's estimate is outside the
        // corrected bracket. All arithmetic remains <=25 bits; no FP64 feature.
        q=0u;remainder=1u<<(k-24u);
        [loop] for(uint bit=0u;bit<24u;++bit) {
            remainder<<=1;q<<=1;
            if(remainder>=m) {remainder-=m;q|=1u;}
        }
    } else remainder=0u-lo;
    uint twice=remainder<<1;
    if(twice>m||(twice==m&&(q&1u)!=0u)) ++q;
    if(resultExponent<=0) return asfloat(sign|q);
    if(q==0x01000000u) {q>>=1;++resultExponent;}
    if(resultExponent>=255) return asfloat(sign|0x7F800000u);
    return asfloat(sign|(uint(resultExponent)<<23)|(q&0x007FFFFFu));
}'''


def decode_record(record, pin):
    stage, _, size, start, length, pairs, digest = pin['profile']
    screen.require(len(record) == size and hashlib.sha256(record).hexdigest() == digest,
                   'Skin variant record extent/hash changed')
    screen.require(struct.unpack_from('>9I', record) == pin['header'], 'Skin variant header changed')
    screen.require(struct.unpack_from('>16I', record, pin['header'][1]) == pin['literals'],
                   'Skin variant literals changed')
    screen.require(struct.unpack_from('>2I', record, pin['header'][6]) == (64, length),
                   'Skin variant code bounds changed')
    screen.require(record[-12:].hex() == pin['trailer'], 'Skin variant trailer changed')
    if stage.startswith('VS'):
        screen.require(struct.unpack_from('>' + str(len(pin['semantics'])) + 'I', record,
                       pin['header'][6] + 40) == pin['semantics'], 'Skin variant semantic associations changed')
    code = record[start:start + length]; control = []
    for i in range(pairs):
        a, b, c = screen.words(code, 12 * i)
        control += [(a, b & 65535), (((b >> 16) | (c << 16)) & 0xFFFFFFFF, c >> 16)]
    screen.require(control == pin['cf'], 'Skin variant control flow changed')
    rows = {}; exports = {}
    for ci, (lo, hi) in enumerate(control):
        op = hi >> 12
        if op == 11:
            screen.require(lo & 0x6000 in (0x2000, 0x4000) and hi == 0xB000 and
                           ci < (lo & 4095) < len(control), 'Skin variant jump contract changed')
        if op not in (1, 2):
            screen.require(op in (0, 11, 12), 'Unqualified skin variant control opcode'); continue
        first, count, seq = lo & 4095, (lo >> 12) & 7, (lo >> 16) & 4095
        screen.require(0 < count <= 6 and seq >> (2 * count) == 0, 'Skin variant issue sequence changed')
        for n in range(count):
            slot = first + n; screen.require(slot not in rows, 'Repeated skin variant issue')
            raw = screen.words(code, slot * 12); fetch = bool(seq & (1 << (2 * n)))
            f = screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            rows[slot] = dict(raw=raw, fetch=fetch, fields=f, control_index=ci)
            if not fetch:
                screen.require(not any(f[k] for k in ('absolute_constants', 'vector_destination_relative',
                    'scalar_destination_relative_or_export_zero', 'constant_1_relative')) and
                    not any(s['relative_temporary'] for s in f['sources']), 'Unqualified skin variant modifier')
                if f['constant_address_register_relative']:
                    screen.require(stage.startswith('VS') and f['constant_0_relative'] and
                        f['sources'][1]['bank'] == 'constant' and f['sources'][1]['register'] in (52, 53, 54),
                        'Unqualified skin variant bone addressing')
                if f['export']:
                    exports[f['vector_destination']] = exports.get(f['vector_destination'], 0) | f['vector_mask'] | f['scalar_mask']
    screen.require(sorted(rows) == list(range(pairs, length // 12 - 1)), 'Incomplete skin variant slot coverage')
    screen.require(exports == pin['exports'], 'Skin variant output masks changed')
    screen.require([(i, r['fields']) for i, r in rows.items() if r['fetch']] == pin['fetches'],
                   'Skin variant original fetch changed')
    return rows


def inspect(image, family):
    pin = PINS[family]; blob = image[pin['source'] - screen.BASE:pin['source'] - screen.BASE + pin['size']]
    screen.require(hashlib.sha256(blob).hexdigest() == pin['sha'], 'Skin variant source changed')
    body = blob[12:]; word = lambda at: struct.unpack_from('>I', body, at)[0]
    for context, spaces in pin['maps'].items():
        for space, expected in spaces.items():
            offset = word(context + 64 + 4 * space)
            for leaf, row in enumerate(expected):
                usage = sum(((struct.unpack_from('>Q', body, word(context + 32 * space + 4 * k) + 8 * (leaf // 64))[0]
                              >> (63 - leaf % 64)) & 1) << k for k in range(8))
                screen.require((usage, *struct.unpack_from('>4I', body, offset + 16 * leaf)) == row,
                               'Skin variant selected register/resource map changed')
    return {stage: decode_record(image[p['profile'][1] - screen.BASE:
            p['profile'][1] - screen.BASE + p['profile'][2]], p) for stage, p in pin['records'].items()}


def operand(f, index, vertex, size=4):
    source = f['sources'][index]; reg = source['register']
    if source['bank'] == 'temporary': name = 'r' + str(reg)
    elif reg >= 252: name = 'k' + str(reg)
    else:
        relative = f['constant_address_register_relative'] and index == 1
        name = ('vc' if vertex else 'pc') + '[' + ('a0+' if relative else '') + str(reg) + ']'
    value = name + '.' + ''.join('xyzw'[n] for n in source['components'][:size])
    if source['absolute_temporary']: value = 'abs(' + value + ')'
    return '-' + value if source['negated'] else value


def scalar(row, vertex):
    f = row['fields']; op = f['scalar_opcode']; c = operand(f, 2, vertex)
    a, b = '(' + c + ').w', '(' + c + ').x'
    if 42 <= op <= 47:
        sw = row['raw'][1] & 255; reg = (op & 1) | (((row['raw'][2] >> 29) & 1) << 1) | (sw & 0x3C)
        constant = row['raw'][2] & 255
        name = 'k' + str(constant) if constant >= 252 else ('vc' if vertex else 'pc') + '[' + str(constant) + ']'
        a = name + '.' + 'xyzw'[((sw >> 6) + 3) & 3]; b = 'r' + str(reg) + '.' + 'xyzw'[sw & 3]
        if f['sources'][2]['negated']: a, b = '-(' + a + ')', '-(' + b + ')'
    # Original kRcp19 is 1.0/src (SDK RECIP_IEEE). Correct vertex rounding before
    # the original discrete atlas operations; preserve ordinary pixel division.
    expressions = {0: a + '+' + b, 1: a + '+ps', 2: 'legacyProduct(' + a + ',' + b + ')',
        3: 'legacyProduct(' + a + ',ps)', 5: 'max(' + a + ',' + b + ')', 6: 'min(' + a + ',' + b + ')',
        8: '(' + a + '>0?1.0:0.0)', 9: '(' + a + '>=0?1.0:0.0)', 10: '(' + a + '!=0?1.0:0.0)',
        11: 'frac(' + a + ')', 12: 'trunc(' + a + ')', 13: 'floor(' + a + ')',
        14: 'exp2(' + a + ')', 16: 'log2(abs(' + a + '))',
        19: ('atlasReciprocalIEEE(' + a + ')' if vertex else '(1.0/(' + a + '))'),
        22: 'rsqrt(abs(' + a + '))', 23: a, 27: '(' + a + '==0)', 28: '(' + a + '!=0)', 29: '(' + a + '>0)',
        42: 'legacyProduct(' + a + ',' + b + ')', 43: 'legacyProduct(' + a + ',' + b + ')',
        44: a + '+' + b, 46: a + '-' + b, 47: a + '-' + b}
    screen.require(op in expressions, 'Unqualified skin variant scalar ' + str(op)); return expressions[op]


def issue(slot, row, vertex):
    f = row['fields']; lines = ['    { // original slot' + str(slot)]
    if row['fetch']:
        screen.require(not vertex and f['fetch_constant_index'] == 0 and f['source_components'][:2] == [0, 1]
                       and f['offset_fields'] == [0, 0, 0], 'Skin variant base sample contract changed')
        lines += [f'        precise float4 v=skinVariantBase.Sample(skinVariantSampler,r{f["source_register"]}.xy);']
        for lane, selector in enumerate(f['destination_swizzle']):
            if selector < 4: lines += [f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[selector]};']
    else:
        op, mask, sop, sm = f['vector_opcode'], f['vector_mask'], f['scalar_opcode'], f['scalar_mask']
        if mask:
            a, b, c = (operand(f, i, vertex) for i in range(3))
            if op == 0: expr = a + '+' + b
            elif op == 1: expr = 'legacyMultiply(' + a + ',' + b + ')'
            elif op in (2, 3): expr = ('max' if op == 2 else 'min') + '(' + a + ',' + b + ')'
            elif op in (5, 6): expr = 'float4(' + a + ('>' if op == 5 else '>=') + b + ')'
            elif op in (8, 9, 10): expr = {8: 'frac', 9: 'trunc', 10: 'floor'}[op] + '(' + a + ')'
            elif op == 11: expr = 'legacyMultiply(' + a + ',' + b + ')+' + c
            elif op in (12, 13): expr = 'float4(' + ','.join('(' + a + ').' + n + ('==0?' if op == 12 else '>=0?') + '(' + b + ').' + n + ':(' + c + ').' + n for n in 'xyzw') + ')'
            elif op in (15, 16, 17):
                lines += ['        precise float4 product=legacyMultiply(' + a + ',' + b + ');']
                lanes = 'xyzw' if op == 15 else 'xyz' if op == 16 else 'xy'
                lines += ['        precise float sum=product.x;'] + ['        sum=sum+product.' + n + ';' for n in lanes[1:]]
                expr = ('(sum+(' + c + ').x)' if op == 17 else 'sum') + '.xxxx'
            else: raise ValueError('Unqualified skin variant vector ' + str(op))
            lines += ['        precise float4 v=' + ('saturate(' + expr + ')' if f['vector_clamp'] else expr) + ';']
        if sop != 50:
            expr = scalar(row, vertex)
            if sop in (27, 28, 29): lines += ['        bool predicate=' + expr + ';', '        precise float s=predicate?0.0:1.0;']
            else: lines += ['        precise float s=' + ('saturate(' + expr + ')' if f['scalar_clamp'] else expr) + ';']
            if sop == 23: lines += ['        int nextAddress=int(clamp(floor(s+0.5),-256.0,255.0));']
        for lane, name in enumerate('xyzw'):
            vm, scalar_mask = mask & (1 << lane), sm & (1 << lane)
            if f['export']:
                if vm or scalar_mask: lines += [f'        output{f["vector_destination"]}.{name}=' + ('1.0' if vm and scalar_mask else 'v.' + name if vm else 's') + ';']
            else:
                if vm: lines += [f'        r{f["vector_destination"]}.{name}=v.{name};']
                if scalar_mask: lines += [f'        r{f["scalar_destination"]}.{name}=s;']
        if sop != 50: lines += ['        ps=s;']
        if sop == 23: lines += ['        a0=nextAddress;']
        if sop in (27, 28, 29): lines += ['        p0=predicate;']
    text = '\n'.join(lines + ['    }'])
    if not row['fetch'] and f['predicated']: text = '    [branch] if(' + ('' if f['predicate_condition'] else '!') + 'p0) {\n' + text + '\n    }'
    return text


def control_source(pin, rows, vertex):
    # All pinned jumps are forward. Each conditional arm either rejoins at its
    # false target, or ends in a forward unconditional jump past the false arm.
    # Preserve that graph as static HLSL branches; no shader-side interpreter.
    control = pin['cf']
    def region(first, stop):
        lines = []; ci = first
        while ci < stop:
            lo, hi = control[ci]; op = hi >> 12
            if op in (1, 2):
                lines += [issue(slot, rows[slot], vertex) for slot in range(lo & 4095, (lo & 4095) + ((lo >> 12) & 7)) if not (vertex and rows[slot]['fetch'])]
                if op == 2: break
            elif op == 11:
                target = lo & 4095
                if lo & 0x2000: ci = target; continue
                joins = [a & 4095 for a, b in control[ci + 1:target] if b >> 12 == 11 and a & 0x2000]
                join = max(joins, default=target)
                screen.require(target <= join <= stop, 'Skin variant structured join differs')
                lines += [f'    [branch] if(p0) {{ // Original CF{ci}, false target CF{target}'] + region(ci + 1, target) + ['    }']
                if join > target: lines += ['    else {'] + region(target, join) + ['    }']
                ci = join; continue
            ci += 1
        return lines
    return region(0, len(control))


def shader_source(image, family):
    rows = inspect(image, family); pins = PINS[family]['records']; prefix = 'Skin' + family.title()
    lines = ['// Complete immutable retail source %08X; no pass executable aliases.' % PINS[family]['source'],
        'cbuffer SkinVariantVertexConstants:register(b0){float4 vc[256];};',
        'cbuffer SkinVariantPixelConstants:register(b0){float4 pc[64];};',
        'float legacyProduct(float a,float b){precise float p=a*b;return ((asuint(a)&0x7F800000u)==0u||(asuint(b)&0x7F800000u)==0u)?0.0:p;}',
        'float4 legacyMultiply(float4 a,float4 b){return float4(legacyProduct(a.x,b.x),legacyProduct(a.y,b.y),legacyProduct(a.z,b.z),legacyProduct(a.w,b.w));}',
        ATLAS_RECIPROCAL,
        'Texture2D<float4> skinVariantBase:register(t0);SamplerState skinVariantSampler:register(s0);',
        'struct SkinVariantInput {float3 position:TEXCOORD0;float3 normal:TEXCOORD1;float4 weights:TEXCOORD2;float4 indices:TEXCOORD3;float4 color:TEXCOORD4;float2 uv:TEXCOORD5;float3 morph1:TEXCOORD6;float3 morph2:TEXCOORD7;float3 morph3:TEXCOORD8;float3 morph4:TEXCOORD9;float3 morph5:TEXCOORD10;float3 morph6:TEXCOORD11;float2 uv1:TEXCOORD12;};',
        'struct SkinVariantAlphaInput {float3 position:TEXCOORD0;float3 normal:TEXCOORD1;float4 weights:TEXCOORD2;float4 indices:TEXCOORD3;float4 color:TEXCOORD4;float2 uv:TEXCOORD5;float3 morph1:TEXCOORD6;float3 morph2:TEXCOORD7;float3 morph3:TEXCOORD8;float3 morph4:TEXCOORD9;float3 morph5:TEXCOORD10;float3 morph6:TEXCOORD11;};',
        'struct SkinVariantOutput {float4 position:SV_Position;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;float4 t4:TEXCOORD4;};',
        '#include "shadow_mesh.hlsl"',
        'struct SkinVariantDrawOutput {float4 color:SV_Target0;float depth:SV_Depth;};',
        'cbuffer SkinVariantProbe:register(b2){float4 skinVariantProbe[5];};']
    for alpha in (False, True):
        name = prefix + ('Alpha' if alpha else ''); stage = 'VSA' if alpha else 'VS'; pin = pins[stage]
        input_type = 'SkinVariantAlphaInput' if alpha else 'SkinVariantInput'
        lines += [f'SkinVariantOutput VS{name}({input_type} input) {{']
        lines += [f'    const float4 k{252+i}=asfloat(uint4(' + ','.join('0x%08Xu' % v for v in pin['literals'][4*i:4*i+4]) + '));' for i in range(4)]
        lines += ['    precise float4 ' + ','.join(f'r{i}=0' for i in range(16)) + ';', '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0;precise float ps=0;bool p0=false;int a0=0;']
        attrs = [n for n in ATTRIBUTES if not (alpha and n == 'uv1')]
        for (_, f), attribute in zip(pin['fetches'], attrs):
            width = 2 if attribute in ('uv', 'uv1') else 4 if attribute in ('weights', 'indices', 'color') else 3
            for lane, selector in enumerate(f['destination_swizzle']):
                if selector < 4: value = 'input.' + attribute + '.' + 'xyzw'[selector] if selector < width else '0.0'
                elif selector in (4, 5): value = str(float(selector - 4))
                else: continue
                lines += [f'    r{f["destination_register"]}.{"xyzw"[lane]}={value};']
        lines += control_source(pin, rows[stage], True) + ['    SkinVariantOutput o;o.position=output62;o.t0=output0;o.t1=output1;o.t2=output2;o.t3=output3;o.t4=output4;return o;}',
            f'[maxvertexcount(1)] void GS{name}Probe(point SkinVariantOutput input[1],inout PointStream<SkinVariantOutput> stream){{stream.Append(input[0]);}}',
            f'SkinVariantOutput VS{name}PixelProbe(uint id:SV_VertexID){{SkinVariantOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);o.t0=skinVariantProbe[0];o.t1=skinVariantProbe[1];o.t2=skinVariantProbe[2];o.t3=skinVariantProbe[3];o.t4=skinVariantProbe[4];return o;}}']
        stage = 'PSA' if alpha else 'PS'; pin = pins[stage]
        lines += [f'float4 PS{name}(SkinVariantOutput input):SV_Target0 {{']
        lines += [f'    const float4 k{252+i}=asfloat(uint4(' + ','.join('0x%08Xu' % v for v in pin['literals'][4*i:4*i+4]) + '));' for i in range(4)]
        lines += ['    precise float4 r0=input.t0,r1=input.t1,r2=input.t2,r3=input.t3,r4=' + ('0' if alpha else 'input.t4') + ',r5=0,r6=0,r7=0;precise float4 output0=0;precise float ps=0;bool p0=false;']
        lines += control_source(pin, rows[stage], False) + ['    return output0;}', f'SkinVariantDrawOutput PS{name}Draw(SkinVariantOutput input){{SkinVariantDrawOutput o;o.color=PS{name}(input);o.depth=PSShadowMeshDepth(input.position);return o;}}']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('family', choices=PINS)
    parser.add_argument('--emit-hlsl', type=Path); parser.add_argument('--verify-hlsl', type=Path)
    parser.add_argument('--output', type=Path); args = parser.parse_args()
    image = (ROOT / 'analysis/simpsons.pe').read_bytes(); inventory = inspect(image, args.family)
    source = shader_source(image, args.family)
    if args.emit_hlsl: args.emit_hlsl.write_text(source, encoding='utf-8')
    if args.verify_hlsl: screen.require(args.verify_hlsl.read_text(encoding='utf-8') == source, 'Skin variant generated HLSL differs')
    if args.output: args.output.write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
    print('PASS skin_' + args.family + ': four exact records/maps; thirteen opaque/twelve alpha inputs; one original stage0 sample')


if __name__ == '__main__': main()
