"""Pinned original evidence for the bounded loading camera pass.

Read-only disassembly and small contract fixtures, never a CPU/GPU interpreter.
Only analysis/native-camera-pass.json may be written. Annotations distinguish
original facts, native proposals, and unresolved numerical/display behavior.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import hashlib

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / 'tools/analyze_poststart_integration.py'
SUPPORT_SHA = 'fdcb3c06ae15041bcbf98ce59d07272e51dce345c7332b44475844d20251b434'
if hashlib.sha256(SUPPORT.read_bytes()).hexdigest() != SUPPORT_SHA:
    raise RuntimeError('Frozen PE/byte validation dependency changed')
from analyze_poststart_integration import (BASE, IMAGE_SHA, branch, checked_decode,
                                          hx, layout, sha, span, validate_identity, word)

REPORT = ROOT / 'analysis/native-camera-pass.json'
LEAVES = {0x823EE7F0, 0x823EE8D8, 0x823EE8E8, 0x823F1A08, 0x823F1A18,
          0x823F47E0, 0x823F8D60, 0x823FA978, 0x823FC5B8, 0x824544E8, 0x82751510}
PINS = {
    0x823EDB68: (0x50, 'f3d33a580ea766076d5a0eb31eba6335be2ff7b2f7f9d946d7c0d8c5caaebd03'),
    0x823EE6C8: (0x124, '8373877c58773ad93b526085205beddc52941f3bd66dfcaeab7b48186df10f03'),
    0x823EE7F0: (0x2C, 'd23069266cb233c2cbfd0be7eff686dfcc7c9b229b3d852898b9be48b0d42228'),
    0x823EE820: (0xAC, 'c07f3c115bd84242d3cf5caf790027b8e2711a231864d66c390ca16d8e326577'),
    0x823EE8D8: (0xC, 'efc70f689c29ff0d2b1c3943d8dd2b9a5f1bad5a0dd9a95067367a3f4d0d37a0'),
    0x823EE8E8: (0xC, '01a15a60ffabfe7801ede54abbee7209a0c896a564c79290e60e7305467c6c49'),
    0x823EE940: (0x124, '9ec21ebd4fb0b8d84b6866abb8fe171b4de01637131e664c657119f0af71e070'),
    0x823EF028: (0x194, '0bcbb0962bf824ef4955b57b5ca281ff0b1f4d31eca1138a6ebf6ea47d21d9e9'),
    0x823F00C0: (0x1B8, '626176a035d0fc6a11245efaf3ff3c71a10d8e59d1c0b118df47fd693fe9c03f'),
    0x823F1800: (0x70, '886ef0e290303ebeadcfa4e1b83d021498ee4b1ce20a1682995820be3c36d4ec'),
    0x823F1870: (0x78, '982bbabc87c40e1e76a0ffb3cc4652d287439ada716dd3ec5ab5e53080b291c7'),
    0x823F1A08: (0xC, '08c7bc66e9da655cdd9a4d11186b4d83ccfaa2ea414e7c5395cab447418c6524'),
    0x823F1A18: (0xC, 'a6e968fdc698a885dd432c58b24f4ea083199b7ec73bb04dd6ff5e54645ff58e'),
    0x823F1B80: (0x4C, '44e4fcbf82f819be9a1f8ce9215d7a996b092d32a6f58b48d1ffa16aa83fca34'),
    0x823F1BD0: (0x40, 'f1f4172ecc20988d2e37a89d05fdfaf16db3cd6a5585352e59310367fcef1cab'),
    0x823F2540: (0x68, 'bfb6fd6b6bc6d7bc6ed79a9ca3c5dd9975928bb52bafce3e391c3b594b704ac5'),
    0x823F3DC8: (0x68, 'cd0bd59028857404562ddceb949010babb287abfb43b94ed3cba86af73a01dd7'),
    0x823F47E0: (0x8, 'd4e9e7520ef19698837e384bfe1708ac4b91fcf26b16ec098ec21c1e344ad3db'),
    0x823F8D60: (0x4, 'f332ea5b5437103cbb6f1508679da89eec9288ad775c96c439a17fccabe3de8e'),
    0x823FA978: (0xA4, 'c22e20a543eea7380042e9cfd14a6d920812cb91dff3cc49155edbda9fed2faa'),
    0x823FC5B8: (0x3C, '4092121151844c62160e4a0e92d51fe0905f532c3536c6fd799aeb142b1c9040'),
    0x82408030: (0x54, '93d339abc97e4458c3022dd111164cf57fda91626cf2e57907ded881874c0195'),
    0x8240D5C0: (0x13C, '74f3bc54468a53957d6128d9a81f741a802975f5dfe70f460b1331e4b13e3cf6'),
    0x8243C430: (0xFC, 'f80def68036d41538781f8c1475743fb39bd0fb61b272348bbe7d8fcd3af3a51'),
    0x8243CE80: (0x264, '2357e29a9ef7b70dd588e77c61c5c70d1e00fa240e31069e2512872f91ca8850'),
    0x8243D0F8: (0x7C, '72ff9ed8c40b222d32b9e57d8b15518ec9bc812b95923dcbc3a71f0ccb602321'),
    0x82453478: (0x690, '1e79dd9650cfd5b443ea0ef8c2be15e09349240639f4a6cbb978bee90a1eaeaa'),
    0x82453B08: (0x124, '4424b231cbf712911faf19290ce1316244f486ff2424173f3c9db790292d6586'),
    0x82453C30: (0xBC, 'ee372e9a307afe333bba0160be3fbe970b180263ff46b3bccae302e992ae5700'),
    0x82454048: (0x1A8, '9cd0a7fdb2dc2a08cd680cfb04116333cc0d19b792acfb44d48e0703bc72a0ea'),
    0x824544E8: (0x8, '3269c5f7b6090fb63ec028ac28456592a46905af3844547e9180c160452bebfb'),
    0x824544F0: (0x594, '2187ab279e8ac47b4676e433f0758ecbac4dc0acbbcb11a72ae2e1eca6c63b28'),
    0x82455570: (0xE44, 'e3b994307e6e95ca72f3e9d7b184d4ec60d5e5e5cdc72978dd1c90cb66d0e26f'),
    0x82457E30: (0x34, '55c4bd651f053bad35b05a6e5095f6b84fe93119b13332690ca58cd6e555eb6d'),
    0x8246D3A0: (0xF0, '8910a71f1defd995bd2554bb3af183faf04dbc7bec2ebdf728998f7e618cb673'),
    0x82751510: (0x74, '6e4a43376155a2c7ab419f2370ebb2dcf5b53afa5d3872eedd6b85a63e6327e6'),
    0x82752090: (0x40, '37f6cef697b5ea4a5db891406d8d3954c3eb6837ab42612a18f16eba36ed549b'),
    0x82756480: (0x3DC, '7ac6f43c8dcf558cd9bbfb3789ff4fb94c62c45dc21f7d47552ac84d6d2fb0f9'),
    0x828625A0: (0x308, '8a5d1ea02ffe32ced0b461692dd005361cd88fa4e736f54c87a7b0aeba69a9f7'),
    0x82862D50: (0x18C, 'e29a51665df7ce4c1da3331cb8b7ad5efc3ef93ad191c90b0537fc6213d88c3b'),
}

CALLS = {
    0x823F00EC: 0x823F2540, 0x823F0100: 0x823F3DC8,
    0x823F015C: 0x823EF028, 0x823F0210: 0x823EF028,
    0x823F0224: 0x823EE6C8, 0x823F0230: 0x82400170, 0x823F023C: 0x82400170,
    0x823EF0E0: 0x823F47E0, 0x823EF190: 0x823F47E0,
    0x823EE744: 0x823EDB68, 0x823EE75C: 0x823EDB68,
    0x823EE77C: 0x8243D598, 0x823EE7E0: 0x8243D0F8,
    0x823EDBA0: 0x8243DED0, 0x823EE9B4: 0x823EE6C8,
    0x823EEA28: 0x8243D0F8, 0x823EEA50: 0x82453C30,
    0x823F1894: 0x8240D5C0, 0x823F18C0: 0x823F8D60,
    0x82408054: 0x823FA978, 0x823EE87C: 0x82455570,
    0x823EE88C: 0x82457E30, 0x823EE898: 0x824544E8,
    0x823EE8A8: 0x824544F0, 0x823EE8AC: 0x823FC5B8,
    0x8245473C: 0x82CC2CA4, 0x824546CC: 0x8246D3A0,
    0x828625C8: 0x823F1A18, 0x828625D8: 0x82752090,
    0x8286266C: 0x82756480, 0x82862880: 0x82756480,
    0x82862894: 0x823F1A08, 0x82862E40: 0x828625A0,
    0x82862E58: 0x823F1BD0, 0x82862EA0: 0x82453C30,
}

# pc, source GPR/FPR, base GPR, signed displacement, bytes, reviewed destination.
STORES = [
    (0x823F00E4,31,11,-8864,4,'82E3DD60 = camera'),
    (0x823F0220,11,10,-13516,4,'82D0CB34 = 0'),
    (0x823F0254,3,11,-13540,4,'82D0CB1C = 1 if previously zero'),
    (0x823EE804,10,11,-13540,4,'82D0CB1C = 0 at end if nonzero'),
    (0x823EE814,11,10,-8864,4,'82E3DD60 = 0 at end'),
    (0x823F1890,31,11,0,4,'E+00 = camera before platform callback'),
    (0x823F184C,10,11,0,4,'E+00 = 0 after successful end callback'),
    (0x823EE778,4,11,-12456,4,'82D0CF58 = depth identity'),
    (0x823EE874,11,31,-4,4,'82D0CF90 = old CF8C'),
    (0x823EE878,6,31,-8,4,'82D0CF8C = old CF90'),
    (0x823EE888,11,31,4,4,'82D0CF98 = old CF94'),
    (0x823EE890,3,31,0,4,'82D0CF94 = SDK submission token'),
    (0x8243D064,31,31,0x291C,4,'SDK Z offset = 1'),
    (0x8243D068,12,31,0x2918,4,'SDK Z scale = -1'),
    (0x823FC5D0,9,11,0,4,'82D0D0E0 = 0'),
    (0x823FC5D4,9,11,4,4,'82D0D0E4 = 0'),
    (0x823FC5D8,9,11,8,4,'82D0D0E8 = 0'),
    (0x823FC5E0,9,11,12,4,'82D0D0EC = 0'),
    (0x823FC5E4,8,10,0,4,'82D0D0DC = previous index + 1'),
    (0x823FC5EC,9,10,0,4,'82D0D0DC = 0 if increment >= 4'),
]
VIEW_STORES = [(0x823EE7AC,0x50), (0x823EE7B4,0x54), (0x823EE7BC,0x58),
               (0x823EE7C4,0x5C), (0x823EE7D4,0x60), (0x823EE7DC,0x64)]
CONSTANTS = {0x82000BB0:0x3F800000, 0x821DD0D8:0, 0x82000FB8:0x3F000000,
             0x8206AD58:0, 0x8206AD5C:0, 0x823F47E0:0x38600000,
             0x823F47E4:0x4E800020, 0x823F8D60:0x4E800020,
             0x82CC2CA4:0x0101025B, 0x82CC2CA8:0x0201025B,
             0x82862634:0x39200003, 0x82862840:0x39200000,
             0x82862E34:0x38800001, 0x82862E38:0x38A00001}
CLEAR_TABLE = (0,15,16,31,32,47,48,63)
REFERENCES = {
    'include/rex/graphics/xenos.h':'7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227',
    'src/kernel/xboxkrnl/export_table.inc':'efe1609d2609007e38a905ea8a18ce68228d15610d2bc85d08a6e832fe224950',
}


def uint(value, bits=32):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError('Expected bounded unsigned integer')
    return value


def store_shape(w):
    widths = {36:4, 38:1, 44:2, 52:4}  # stw/stb/sth/stfs only
    if w >> 26 not in widths:
        raise ValueError('Not a reviewed direct store')
    d = w & 0xFFFF
    return ((w >> 21) & 31, (w >> 16) & 31,
            d - 0x10000 if d & 0x8000 else d, widths[w >> 26])


def check_pins(b):
    _, pdata = layout(b)
    for a, (size, digest) in PINS.items():
        if sha(span(b,a,size)) != digest:
            raise ValueError('Changed code at ' + hx(a))
        if a not in LEAVES and (a not in pdata or pdata[a][0] != size):
            raise ValueError('Changed original .pdata extent at ' + hx(a))
    for pc,dest in CALLS.items():
        if branch(pc,word(b,pc)) != (dest,True):
            raise ValueError('Changed critical call at ' + hx(pc))
    for pc,rs,ra,d,n,_ in STORES:
        if store_shape(word(b,pc)) != (rs,ra,d,n):
            raise ValueError('Changed reviewed write at ' + hx(pc))
    for pc,d in VIEW_STORES:
        if store_shape(word(b,pc))[1:] != (1,d,4):
            raise ValueError('Changed viewport stack framing')
    for pc,w in CONSTANTS.items():
        if word(b,pc) != w:
            raise ValueError('Changed critical word at ' + hx(pc))
    if struct.unpack('>8I',span(b,0x82062AA0,32)) != CLEAR_TABLE:
        raise ValueError('Changed clear table')
    return pdata


def clear_arguments(selector, rgba, has_camera_depth):
    """Verified engine-to-SDK arguments, NOT a native clear implementation."""
    uint(selector,3)
    if type(has_camera_depth) is not bool:
        raise ValueError('Depth presence must be Boolean')
    mask = CLEAR_TABLE[selector]
    if not has_camera_depth:
        mask &= ~0x30
    packed = 0
    if selector & 1:
        if type(rgba) is not bytes or len(rgba) != 4:
            raise ValueError('Clear color requires four original RGBA bytes')
        r,g,b,a = rgba
        packed = a << 24 | r << 16 | g << 8 | b
    return {'mask':mask,'argb':packed,'depth_float_bits':0}


def root_screen_viewport(words, target_width, target_height):
    """Proposed bounded native eligibility fixture, not a generic SDK viewport."""
    if type(words) is not tuple or len(words) != 6:
        raise ValueError('Expected six original viewport words')
    for w in words:
        uint(w)
    uint(target_width); uint(target_height)
    x,y,w,h,z0,z1 = words
    if (x,y,z0,z1) != (0,0,0x3F800000,0) or not 0 < w <= 16384 or not 0 < h <= 16384:
        raise ValueError('Outside the zero-offset reversed-depth root subset')
    if (w,h) != (target_width,target_height):
        raise ValueError('Extent mismatch; no guessed clipping')
    return {'logical_words':list(words),'logical_z_scale':-1,'logical_z_offset':1,
            'host_viewport':[0,0,w,h,0,1],
            'draw_condition':'Only proved screen shaders with depth/stencil effectively disabled; other state also required'}


def present_rotation(cf90, cf8c, cf94, new_submission):
    """Original CPU publication order with caller-provided tokens; no completion oracle."""
    for v in (cf90,cf8c,cf94,new_submission): uint(v)
    if not cf90 or not cf8c or cf90 == cf8c:
        raise ValueError('Native proposal requires two distinct live front roles')
    return {'CF90':cf8c,'CF8C':cf90,'resolve_destination':cf90,
            'display_source':cf90,'CF98':cf94,'CF94':new_submission}


def fact(status, statement, *pcs):
    return {'status':status,'statement':statement,'pcs':[hx(p) for p in pcs]}


def facts():
    return [
        fact('verified','Begin r4=camera: stores DD60, updates CPU view/projection caches, clears CB34, calls target service, queues IDs199/19A and sets CB1C if zero.',0x823F00E4,0x823F015C,0x823F0210,0x823F0220,0x823F0224,0x823F0230,0x823F023C,0x823F0254),
        fact('verified','EF028 compares/copies 64-byte matrices and allocates from CB3C via E+138; its device-labelled matrix helper 823F47E0 is li r3,0 / blr.',0x823EF0B8,0x823EF0CC,0x823EF0E0,0x823F47E0),
        fact('verified','Begin wrapper publishes E+00 before CPU frame synchronization, whose object callbacks remain original; 823F8D60 is a genuine blr.',0x823F1890,0x823F1894,0x8240D624,0x8240D65C,0x8240D68C,0x823F8D60),
        fact('verified','Type2 parent selects CB00/CAFC even when camera depth pointer is absent; camera depth absence only strips depth/stencil clear bits. Type5/null-color path is outside bounded support.',0x823EE72C,0x823EE740,0x823EE74C,0x823EE9AC),
        fact('verified','Target identities are CPU caches CF5C slot0 / CF58 depth; bindings and viewport are mixed SDK calls. No camera generation counter is incremented in these bodies.',0x823EDB94,0x823EE778,0x823EE7E0),
        fact('verified','Viewport has four unsigned-converted words and two floats. SDK writes offset=1,scale=-1; it also updates effective scissor from viewport and requested scissor.',0x8243D104,0x8243D11C,0x8243D060,0x8243D064,0x8243D068,0x8243C4C0),
        fact('proposal','Keep original logical 1->0 and use reversible host 0->1 only for depth/stencil-inert proved screen draws. No clamp, world-draw permit, or raw depth-write-request rewrite.'),
        fact('verified','Engine clear selector0..7 maps to 0,F,10,1F,20,2F,30,3F; RGBA bytes become AARRGGBB, depth=0, stencil comes from CB14. Original lookup lacks bounds check.',0x823EE960,0x823EE994,0x823EEA38,0x823EEA4C),
        fact('verified','Loading tick passes background=1,animation=1. Background quad uses selector3 and black alpha1; conditional animation uses selector0. Selectors1/2 are not selected by this caller.',0x82862E34,0x82862E38,0x82862634,0x8286266C,0x82862840,0x82862880),
        fact('verified','No-animation tick branch bypasses camera clear and calls SDK clear with flagsF,color0,depth1; depth is not selected. Do not miss this separately guarded direct SDK call.',0x82862E88,0x82862E8C,0x82862EA0),
        fact('unresolved','Expanded selectors0..2 arithmetic remains unproved; selector3 leaves inherited expansion untouched. No complete animated loading frame authorized.',0x827565B0,0x827565C0,0x82756838,0x82756848),
        fact('verified','Present rotates CF90/CF8C, resolves into old CF90, shifts CF94 into CF98, captures SDK submission token, enters SDK display, then advances original four-slot CPU index.',0x823EE874,0x823EE878,0x823EE87C,0x823EE888,0x823EE88C,0x823EE8A8,0x823EE8AC),
        fact('verified','Raster presentation wrapper first splices/swaps CPU resource lists at E+[DC88]+20/+24 and clears +8; must retain this independently of native display.',0x82408054,0x823FA9F4,0x823FAA04,0x823FAA14),
        fact('verified','Zero resolve flags select bound color0; single-sample source normalizes selector to10. Null rect uses destination extent; null point is original zero pair. Format54 remaps7, no requested post-resolve clears.',0x824555D0,0x824555E4,0x82455714,0x82455738,0x824557BC,0x82455910,0x8245592C),
        fact('verified','SDK display calls original VdSwap import025B; 824544E8 tail-enters submission synchronization, not a no-op. No engine quad or expansion setter call in the present callback.',0x824544EC,0x8245473C,0x82CC2CA4),
        fact('unresolved','Final display scaling/gamma, platform callback modes and complete consumer closure for submission-history words remain unproved. A host Present or extra texture-copy draw is not certified equivalent.'),
        fact('proposal','Next implement root target/viewport boundary while retaining original begin/end/wrappers and CPU allocation/lists. Gate every draw independently, reject expanded animation and unproved present/clear modes.'),
        fact('prerequisite','Boot037 is blocked earlier at application scalar dispatcher82723D80. Its indirect SDK setter lookup cannot use the native opaque context; inherited pass state requires observed or rejected application updates. Dispatcher implementation is separate.'),
    ]


def inspect(image, disassembler, reference_root):
    b = image.read_bytes(); validate_identity(b); pdata = check_pins(b)
    refs = []
    for name,digest in REFERENCES.items():
        if sha((reference_root/name).read_bytes()) != digest:
            raise ValueError('Changed read-only reference ' + name)
        refs.append({'path':name,'sha256':digest,'authority':'reference definitions, not original hardware measurements'})
    records = []
    for a,(size,digest) in sorted(PINS.items()):
        output = subprocess.check_output([str(disassembler),str(image),hx(BASE),hx(a),str(size//4)],text=True)
        decoded = checked_decode(b,a,size,output)
        edges = []
        for row in decoded:
            pc=int(row['pc'],16); dest=branch(pc,int(row['word'],16))
            if dest and (dest[1] or not a <= dest[0] < a+size):
                edges.append({'pc':hx(pc),'target':hx(dest[0]),'linked':dest[1]})
        records.append({'entry':hx(a),'size':size,'sha256':digest,
                        'extent':'reviewed leaf' if a in LEAVES else '.pdata',
                        'pdata_va':None if a in LEAVES else hx(pdata[a][1]),
                        'direct_edges':edges,'instructions':decoded})
    return {'schema':'native-camera-pass-evidence-v1',
            'image':{'base':hx(BASE),'bytes':len(b),'sha256':IMAGE_SHA},
            'analyzer_sha256':sha(Path(__file__).read_bytes()),'support_sha256':SUPPORT_SHA,
            'disassembler_sha256':sha(disassembler.read_bytes()),'references':refs,
            'scope':'Static initial root camera/black-background pass evidence; no observed pass or GPU execution',
            'function_count':len(records),'instruction_words':sum(n//4 for n,_ in PINS.values()),
            'checked_direct_calls':len(CALLS),'checked_named_stores':len(STORES),
            'entry_abi':{'begin':'823F00C0(r3=0,r4=camera,r5=0)->nonzero',
                         'end':'823EE7F0(r3=0,r4=camera,r5=0)->1',
                         'target':'823EE6C8(r3=camera); ignored return',
                         'clear':'823EE940(r3=camera,r4=RGBAbytes,r5=selector)->1',
                         'present':'823EE820(r3=raster,r4=0,r5=1 observed caller arguments; body ignores inputs)->1'},
            'critical_calls':[{'pc':hx(pc),'target':hx(dst),'lr':hx(pc+4)} for pc,dst in sorted(CALLS.items())],
            'checked_writes':[{'pc':hx(pc),'word':hx(word(b,pc)),'destination':dest} for pc,*_,dest in STORES],
            'clear_table':list(CLEAR_TABLE),'facts':facts(),
            'contract_fixtures':{'asymmetric_clear':clear_arguments(7,bytes([0x12,0x34,0x56,0x78]),True),
                                 'root_viewport':root_screen_viewport((0,0,1280,720,0x3F800000,0),1280,720),
                                 'front_rotation':present_rotation(0xF00004,0xF00005,17,23)},
            'limits':['Hashes pin all recorded bytes; semantic annotations were reviewed, not automatically inferred.',
                      'Direct edges are not an exhaustive indirect call graph or proof of absent aliases.',
                      'Native resource generation, pass revision and completion tracking are proposed host contracts, not guest counter schemas.',
                      'No expanded-blend arithmetic, floating-depth parity, final display-transfer parity, or whole-frame readiness claimed.'],
            'functions':records}


def self_test(image):
    b=image.read_bytes(); validate_identity(b)
    class Checks(unittest.TestCase):
        def test_original_pins_and_framing(self): check_pins(b)
        def test_modified_and_truncated_image(self):
            for bad in (b[:-1],b[:0x100]+bytes([b[0x100]^1])+b[0x101:]):
                with self.assertRaises(ValueError): validate_identity(bad)
        def test_changed_code_rejected_independent_of_full_hash(self):
            bad=bytearray(b);bad[0x823F00E4-BASE]^=1
            with self.assertRaises(ValueError): check_pins(bad)
        def test_changed_clear_table(self):
            bad=bytearray(b);bad[0x82062AA7-BASE]^=1
            with self.assertRaises(ValueError): check_pins(bad)
        def test_invalid_pdata(self):
            bad=bytearray(b);p=layout(b)[1][0x823F00C0][1]
            struct.pack_into('>I',bad,p-BASE,0x823F00C1)
            with self.assertRaises(ValueError): check_pins(bad)
        def test_disassembly_mismatch(self):
            for out in ('823F47E0 38600001 li r3,1\n','823F47E4 38600000 li r3,0\n','bad\n',''):
                with self.assertRaises(ValueError): checked_decode(b,0x823F47E0,4,out)
        def test_all_clear_masks_and_asymmetric_channels(self):
            for i in range(8):
                p=clear_arguments(i,b'\x12\x34\x56\x78',True)
                self.assertEqual(p['mask'],CLEAR_TABLE[i])
                self.assertEqual(p['argb'],0x78123456 if i&1 else 0)
                self.assertEqual(clear_arguments(i,b'\x12\x34\x56\x78',False)['mask'],15 if i&1 else 0)
            self.assertEqual(clear_arguments(2,None,True)['argb'],0)
        def test_clear_rejections(self):
            for i in (-1,8,True,1.0):
                with self.assertRaises(ValueError): clear_arguments(i,b'abcd',True)
            for color in (None,b'abc',b'abcde',[1,2,3,4]):
                with self.assertRaises(ValueError): clear_arguments(1,color,True)
        def test_asymmetric_viewport_and_no_mutation(self):
            words=(0,0,1280,720,0x3F800000,0)
            v=root_screen_viewport(words,1280,720)
            self.assertEqual(v['host_viewport'],[0,0,1280,720,0,1])
            self.assertEqual(v['logical_words'],list(words))
            # Screen Z=0 differs numerically! Safety requires inactive depth.
            self.assertNotEqual(v['logical_z_offset'],v['host_viewport'][4])
        def test_uncertain_viewports_rejected(self):
            good=(0,0,1280,720,0x3F800000,0)
            for i,val in ((0,0xFFFFFFFF),(1,1),(2,0),(2,16385),(3,721),(4,0),(4,0x7FC00000),(5,0x3F800000),(0,True)):
                w=list(good);w[i]=val
                with self.assertRaises(ValueError): root_screen_viewport(tuple(w),1280,720)
            with self.assertRaises(ValueError): root_screen_viewport(good[:-1],1280,720)
        def test_rotation_and_history_are_distinct(self):
            r=present_rotation(101,202,17,23)
            self.assertEqual((r['CF90'],r['CF8C'],r['CF98'],r['CF94']),(202,101,17,23))
            s=present_rotation(r['CF90'],r['CF8C'],r['CF94'],41)
            self.assertEqual((s['display_source'],s['CF98'],s['CF94']),(202,23,41))
            for args in ((0,2,0,1),(1,1,0,1),(1,2,-1,1),(1,2,0,True)):
                with self.assertRaises(ValueError): present_rotation(*args)
        def test_leaf_and_store_classification(self):
            self.assertEqual(span(b,0x823F47E0,8),bytes.fromhex('386000004e800020'))
            self.assertEqual(store_shape(word(b,0x8243D064)),(31,31,0x291C,4))
            with self.assertRaises(ValueError): store_shape(0x60000000)
        def test_report_path_restriction(self):
            self.assertEqual(report_path(REPORT),REPORT.resolve())
            for p in (image,ROOT/'docs/native-camera-pass.md',ROOT/'analysis/other.json'):
                with self.assertRaises(ValueError): report_path(p)
    return unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks)).wasSuccessful()


def report_path(path):
    p=path.resolve()
    # Also reject an existing report link; do not follow it into an input.
    if p != REPORT.absolute() or path.is_symlink():
        raise ValueError('Only owned analysis/native-camera-pass.json may be written')
    return p


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--disassembler',type=Path,default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    parser.add_argument('--reference-root',type=Path,default=Path('K:/Simpsons/RexGlueCurrent'))
    parser.add_argument('--report',type=Path)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    try:
        outpath=report_path(args.report) if args.report else None
        if args.self_test:
            if outpath: raise ValueError('Self-tests write no report')
            return 0 if self_test(args.image) else 1
        r=inspect(args.image,args.disassembler,args.reference_root)
        out=json.dumps(r,indent=2,sort_keys=True)+'\n'
        if outpath:
            outpath.write_bytes(out.encode('utf-8'))
            print(f"Wrote {r['function_count']} byte-pinned extents / {r['instruction_words']} words; incomplete pass remains gated")
        else: print(out,end='')
        return 0
    except (OSError,ValueError,struct.error,subprocess.SubprocessError) as exc:
        print('error: '+str(exc),file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
