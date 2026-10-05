"""Read-only qualification of RenderShadowDepthAlpha's exact original records.

No arguments prints deterministic JSON; --verify checks the embedded pins without
printing JSON; --self-test adds mutation checks. No report/build files are written.
Only static UCODE fields are inspected, never executed. Main owns material mapping
and offline FXC/CMake integration; actual native PS output has a separate GPU test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

# Keep this support-file-only tool read-only even when invoked without python -B.
sys.dont_write_bytecode = True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four
import analyze_shadow_depth_shader as depth

ROOT = Path(__file__).resolve().parents[1]
need = screen.require
VS_VA, PS_VA = 0x820C1E6C, 0x820CA530
VS_RECORD_SHA = 'd2e8706e97a50c8e68e7ea4b4283189582d0fecfb9bd14b31a89a99a30362c10'
PS_RECORD_SHA = 'bab129e556228968094f00df964b9611c84dd3c53b9ccc29aa7eb6c658523859'
VS_CODE_SHA = 'dc166db9617589c3dcd618b03be63d0ea685fb4a8d513f2ecb969d20cb93aa1d'
VS_EXECUTABLE_SHA = '4331483f18a4e5cb418cc0d7ab9228f3b51724cbb6685e4c066e12becd6d5260'
PS_CODE_SHA = 'fb4364f3b02a68c1ebe10f4bf46e14c668104ceae4376f77f9fc81bdc81433b2'
PS_EXECUTABLE_SHA = 'f40fd20b0c212c81ec82cd781199649e1b055336abd3de17094ef21ec8c02a99'
VS_SOURCE_SHA = 'bbf59478a760ffcee6eeceb8d8d07dbb4321afaa5286cd3e7375e4038d229f85'
PS_SOURCE_SHA = '790fe2f4a5b83908cdebfa84b958eb711cd19d57eb5c33175fc56df68a33f030'
VS_LITERALS = (0,0,0,0,0,0,0,0,0,0x3F800000,0x40400000,0x447A0000,0x42700000,0,0,0)
PS_LITERALS = (0,0,0,0,0,0,0,0,0,0,0,0,0x3F800000,0,0,0)
CF = ((0x1003,0x1000),(0x4003,0xB000),(0x92004,0x1200),
      (0,0xC400),(0x1006,0x2200),(0,0))
# Every bit is pinned, including empty-mask ALU work, unused CF, fetch flags and
# the unexecuted final 12 bytes. These are big-endian words from the image.
PS_RAW = (
    (0x00001003,0x40031000,0xB0000000),
    (0x00092004,0x00001200,0xC4000000),
    (0x00001006,0x00002200,0x00000000),
    (0x70040000,0x006C6C6C,0x02FFFF29),
    (0x10080001,0x1F1FFEFF,0x00004000),
    (0xC8000000,0x00000000,0x02000000),
    (0xC80F8000,0x00C6C600,0xC2000000),
    (0x4E4A000B,0x01EA67DC,0xD36BD9F6),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def record(data, size, sha, literal_offset, literals):
    need(len(data) == size and digest(data) == sha, 'Original shader record changed')
    # The record header points to literals+instructions, not instructions alone.
    need(screen.words(data, 4, 2) == (literal_offset, size-literal_offset),
         'Original literal/code extent changed')
    need(struct.unpack_from('>16I', data, literal_offset) == literals,
         'Original 64-byte c252..c255 literal bank changed')
    return data[literal_offset+64:]


def pixel_code(code):
    need(len(code) == 96, 'PS instruction region must be exactly 96 bytes')
    raw = tuple(screen.words(code, i*12) for i in range(8))
    need(raw == PS_RAW and digest(code) == PS_CODE_SHA, 'Original PS raw code changed')
    need(digest(code[:84]) == PS_EXECUTABLE_SHA, 'Original PS executable bytes changed')
    fields = []
    for a,b,c in raw[:3]:
        fields.extend(((a,b & 0xFFFF), (((b >> 16) | (c << 16)) & 0xFFFFFFFF,c >> 16)))
    need(tuple(fields) == CF, 'Original PS control flow changed')
    slots = []
    for lo,hi in fields:
        if hi >> 12 in (1,2):
            start,count,sequence = lo & 4095,(lo >> 12) & 7,(lo >> 16) & 4095
            need(0 < count <= 6 and sequence >> (2*count) == 0, 'Invalid EXEC span')
            slots.extend((start+i, bool(sequence & (1 << (2*i))),
                          bool(sequence & (2 << (2*i)))) for i in range(count))
    need(slots == [(3,False,False),(4,True,False),(5,False,True),(6,False,False)],
         'Changed fetch/ALU/serialization schedule')
    lo,hi = fields[1]
    jump = {'target_cf':lo & 8191, 'unconditional':bool(lo & 8192),
            'predicated':bool(lo & 16384), 'condition':bool(hi & 1024),
            'bool_address':(hi >> 2) & 255, 'address_mode':(hi >> 11) & 1}
    need(jump == {'target_cf':3,'unconditional':False,'predicated':True,
                  'condition':False,'bool_address':0,'address_mode':0}, 'Changed predicate jump')
    rows = []
    for slot,fetch,serial in slots:
        words = raw[slot]
        decoded = screen.decode_fetch(words) if fetch else edge.alu(*words)
        if not fetch:
            for flag in ('vector_clamp','scalar_clamp','absolute_constants',
                         'vector_destination_relative','scalar_destination_relative_or_export_zero',
                         'predicated','predicate_condition','constant_address_register_relative',
                         'constant_1_relative','constant_0_relative'):
                need(not decoded[flag], 'Unproved ALU modifier: '+flag)
            for source in decoded['sources']:
                need(not any(source[k] for k in ('absolute_temporary','relative_temporary','negated')),
                     'Unproved source modifier')
        rows.append({'slot':slot,'va':f'{PS_VA+448+slot*12:08X}',
                     'words':[f'{w:08X}' for w in words], 'serialized':serial,'fields':decoded})
    init,fetch,nop,export = (row['fields'] for row in rows)
    need((init['vector_opcode'],init['vector_destination'],init['vector_mask'],
          init['scalar_opcode'],init['scalar_mask'],init['export']) == (2,0,4,28,0,False),
         'Expected MAX into r0.z and co-issued scalar PRED_SETNE')
    need([(s['bank'],s['register'],s['components']) for s in init['sources']] ==
         [('constant',255,[0,0,0,0]),('constant',255,[0,0,0,0]),('constant',41,[0,0,0,0])],
         'Expected c255.x literal operands and c41.x predicate')
    # Scalar PRED_SETNE uses src3.a (W of the relative-swizzled operand).
    need(init['sources'][2]['components'][3] == 0, 'Scalar predicate component is not X')
    need(fetch == {
        'kind':'texture_fetch','source_register':0,'destination_register':0,
        'destination_swizzle':[7,7,3,7],'fetch_constant_index':0,
        'normalized_coordinates':True,'source_components':[0,1,0],'dimension_field':1,
        'mag_filter':3,'min_filter':3,'mip_filter':3,'anisotropy':7,'arbitrary_filter':0,
        'volume_mag_filter':3,'volume_min_filter':3,'computed_lod':True,
        'register_lod':False,'register_gradients':False,'fetch_valid_only':True,
        'sample_location':0,'lod_bias_field':0,'offset_fields':[0,0,0]},
        'Expected sampler-driven normalized 2D alpha fetch0')
    need((nop['vector_mask'],nop['scalar_mask'],nop['scalar_opcode'],nop['export']) == (0,0,50,False),
         'Slot5 must have no writes or predicate work')
    need((export['vector_opcode'],export['vector_destination'],export['vector_mask'],
          export['scalar_opcode'],export['scalar_mask'],export['export']) == (2,0,15,50,0,True),
         'Expected unsaturated color0 vector export')
    need([(s['bank'],s['register'],s['components']) for s in export['sources'][:2]] ==
         [('temporary',0,[2,2,2,2]),('temporary',0,[2,2,2,2])], 'Expected r0.zzzz export')
    return {'control_flow':[[f'{lo:08X}',f'{hi:04X}'] for lo,hi in fields],
            'jump':jump,'instructions':rows,
            'static_paths':{'predicate_false':[3,6],'predicate_true':[3,4,5,6]},
            'trailer_words':[f'{w:08X}' for w in raw[7]]}


def vertex_reuse(alpha, original):
    code = record(alpha,4396,VS_RECORD_SHA,3624,VS_LITERALS)
    prior = record(original,4396,depth.RECORD_SHA,3624,VS_LITERALS)
    need(digest(code) == VS_CODE_SHA and digest(prior) == depth.CODE_SHA, 'VS raw code changed')
    need(len(code) == 708 and code[:696] == prior[:696] and
         digest(code[:696]) == VS_EXECUTABLE_SHA, 'VS executable reuse differs')
    need(alpha[3624:3688] == original[3624:3688], 'VS literal reuse differs')
    need(screen.words(code,696) == (0x4E4A000C,0x01EA67DC,0xD36BD9F6) and
         screen.words(prior,696) == (0x4E4A000A,0x01EA67DC,0xD36BD9F6), 'VS trailer tags changed')
    need(alpha[:4384] == original[:4384] and alpha[4388:] == original[4388:],
         'VS records differ outside the first trailer word')
    # Reuse the already qualified original static CF/fetch/ALU schedule.
    depth.schedule(prior)
    return {'record_va':f'{VS_VA:08X}','record_bytes':4396,'record_sha256':VS_RECORD_SHA,
            'literal_offset':3624,'literal_bytes':64,'literal_c252_to_c255':[f'{v:08X}' for v in VS_LITERALS],
            'code_offset':3688,'code_bytes_including_trailer':708,'code_sha256':VS_CODE_SHA,
            'executable_bytes':696,'executable_sha256':VS_EXECUTABLE_SHA,
            'reuse_record_va':f'{depth.VA:08X}','reuse_record_sha256':depth.RECORD_SHA,
            'native_entry':'VSShadowDepth','native_symbol':'kVSShadowDepth',
            'trailer_first_word':{'alpha':'4E4A000C','depth':'4E4A000A'},
            'only_record_difference':'First word of the unexecuted trailer, record offset 4384.'}


def source_pins(vertex_source, pixel_source):
    # read_text normalizes CRLF so source pins survive Windows checkout policy.
    need(digest(vertex_source.encode('utf-8')) == VS_SOURCE_SHA, 'Qualified VSShadowDepth source changed')
    need(digest(pixel_source.encode('utf-8')) == PS_SOURCE_SHA, 'Qualified shadow alpha HLSL source changed')


def inspect(image):
    need(len(image) == screen.IMAGE_SIZE and digest(image) == screen.IMAGE_SHA256,
         'Original image identity changed')
    for path,sha in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(digest(path.read_bytes()) == sha, 'Pinned local UCODE/enum reference changed')
    source_pins((ROOT/'renderer/shadow_depth_shader.hlsl').read_text(encoding='utf-8'),
                (ROOT/'renderer/shadow_alpha_shader.hlsl').read_text(encoding='utf-8'))
    take = lambda va,size: image[va-screen.BASE:va-screen.BASE+size]
    # The existing inspector independently qualifies the original VS literals,
    # control flow and decoded instructions; no translated backend is imported.
    depth.inspect(image)
    reuse = vertex_reuse(take(VS_VA,4396),take(depth.VA,4396))
    ps = record(take(PS_VA,544),544,PS_RECORD_SHA,384,PS_LITERALS)
    pixel = pixel_code(ps)
    # Pin the supplied caller return site. The BL at LR-4 targets the technique
    # selection wrapper; material/pass association is intentionally main-owned.
    need(take(0x82706148,8) == bytes.fromhex('4BFAFF3138A00001'), 'Caller return boundary changed')
    pixel.update({'record_va':f'{PS_VA:08X}','record_bytes':544,'record_sha256':PS_RECORD_SHA,
                  'literal_offset':384,'literal_bytes':64,
                  'literal_c252_to_c255':[f'{v:08X}' for v in PS_LITERALS],
                  'code_offset':448,'code_bytes_including_trailer':96,'code_sha256':PS_CODE_SHA,
                  'executable_bytes':84,'executable_sha256':PS_EXECUTABLE_SHA})
    return {'image_sha256':screen.IMAGE_SHA256,'caller_return_va':'8270614C',
            'caller_bl':{'va':'82706148','word':'4BFAFF31','target':'826B6078'},
            'references':{'ucode':{'path':str(four.UCODE),'sha256':four.UCODE_SHA,
                         'sections':'ControlFlowExec/CondJmp, TextureFetchInstruction, AluInstruction, kSetpNe'},
                          'xenos':{'path':str(four.XENOS),'sha256':four.XENOS_SHA,
                         'sections':'TextureFilter, AnisoFilter, FetchOpDimension, SampleLocation, ArbitraryFilter'}},
            'vertex_reuse':reuse,'pixel':pixel,
            'native':{'source':'renderer/shadow_alpha_shader.hlsl','source_sha256_lf':PS_SOURCE_SHA,
                      'reused_vs_source_sha256_lf':VS_SOURCE_SHA,
                      'entry':'PSShadowDepthAlpha','profile':'ps_5_0','symbol':'kPSShadowDepthAlpha',
                      'constants':'float4 c[42] at b0; only c41.x is read',
                      'resources':'Texture2D<float4> texture0 at t0; SamplerState sampler0 at s0',
                      'output':'c41.x == 0: (1,1,1,1); otherwise sample0(uv).aaaa',
                      'probe_entry':'VSShadowAlphaProbe','probe_profile':'vs_5_0'},
            'scope':['Static original-byte proof; separate real D3D11 test observes the authored PS.',
                     'Filters/anisotropy inherit the sampler; normalized 2D, computed LOD, zero offsets/bias.',
                     'Arbitrary-filter field 0 is deprecated per the pinned enum reference.',
                     'Original program output only; no fixed-function alpha test, depth adapter or runtime integration.',
                     'Finite inputs; console FP, interpolation, filtering and LOD bit equivalence are not claimed.']}


def self_test(image):
    count = 0

    def rejects(function, *args):
        nonlocal count
        try:
            function(*args)
        except ValueError:
            count += 1
            return
        raise ValueError('Static verifier accepted a mutated input')

    for va,size,sha,offset,literals in (
        (VS_VA,4396,VS_RECORD_SHA,3624,VS_LITERALS),
        (depth.VA,4396,depth.RECORD_SHA,3624,VS_LITERALS),
        (PS_VA,544,PS_RECORD_SHA,384,PS_LITERALS),
    ):
        data = image[va-screen.BASE:va-screen.BASE+size]
        for at in range(size):
            changed = bytearray(data)
            changed[at] ^= 1
            rejects(record,changed,size,sha,offset,literals)
        rejects(record,data[:-1],size,sha,offset,literals)
        rejects(record,data+b'\0',size,sha,offset,literals)
    code = image[PS_VA-screen.BASE+448:PS_VA-screen.BASE+544]
    for at in range(96):
        for bit in range(8):
            changed = bytearray(code)
            changed[at] ^= 1 << bit
            rejects(pixel_code,changed)
    rejects(pixel_code,code[:-1])
    rejects(pixel_code,code+b'\0')
    vs = (ROOT/'renderer/shadow_depth_shader.hlsl').read_text(encoding='utf-8')
    ps = (ROOT/'renderer/shadow_alpha_shader.hlsl').read_text(encoding='utf-8')
    rejects(source_pins,vs+'\n',ps)
    for old,new in (('c[41].x','c[40].x'),('!= 0.0f','> 0.0f'),('.a;','.r;'),
                    ('alpha.xxxx','float4(alpha,alpha,alpha,1)'),('alpha = 1.0f','alpha = 0.0f'),
                    ('register(t0)','register(t1)'),('register(s0)','register(s1)'),
                    ('register(b0)','register(b1)'),('float4 c[42]','float4 c[41]')):
        need(old in ps, 'Stale HLSL mutation fixture')
        rejects(source_pins,vs,ps.replace(old,new))
    print(f'PASS {count} record/code/source mutation and extent checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true',help='Check embedded pins; suppress JSON; never write files')
    parser.add_argument('--self-test',action='store_true',help='Reject changed bytes, extents and shader contracts')
    args = parser.parse_args()
    image = (ROOT/'analysis/simpsons.pe').read_bytes()
    report = inspect(image)
    if args.self_test:
        self_test(image)
    if not args.verify:
        print(json.dumps(report,indent=2))
    print('PASS exact RenderShadowDepthAlpha PS, c41.x branch, sampler-driven alpha, and VSShadowDepth reuse')


if __name__ == '__main__':
    main()
