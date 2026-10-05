"""Bounded original-byte and rational raster-policy evidence, not a renderer.

Only analysis/native-screen-raster-policy.json may be written. The numerical
fixtures check a stated mathematical model, not Xenos hardware precision.
Uses Python's standard library and the existing offline disassembler only.
"""
from __future__ import annotations

import argparse
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SIZE = 15466496
IMAGE_SHA = '6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'
REPORT = ROOT / 'analysis/native-screen-raster-policy.json'
# Reviewed ranges; only the two wrappers and the quad are labelled complete
# pdata functions here. Other ranges intentionally include leaves or slices.
RANGES = {
    0x823F4618:(0x54,'f52220488725192b5b428eb38bc5c05a497bd61c4bed4eac4d333fe8138046b3'),
    0x823F46A0:(0x50,'93a520d478d509861e6388c5e7b004c33d25fd2967476a19bad931518df90fbe'),
    0x8243B718:(0x24,'8ac16a269350a5a6eb4b1b5c46e8d7efcf476d7788ade765304c45ca3fcc89be'),
    0x8243B810:(0x60,'9a327eae8b02cce49662d0344614d348bec31e3edab427ef8c6953408c098d7b'),
    0x824659DC:(0x48,'7516f0256a57d5bda171d26e77aadbd339ebb2fd761809d8d96e673e71104dfc'),
    0x8243B260:(0x40,'0fe7bd2083ebd2565773dd06b319c44c247498278529f2828d8284025bb99fe7'),
    0x8243D05C:(0x38,'d41e67734bf24225c06fa36fc31de9c7cbf03d2ad9be423f1d560444ed1379ab'),
    0x8243C430:(0xFC,'f80def68036d41538781f8c1475743fb39bd0fb61b272348bbe7d8fcd3af3a51'),
    0x82756480:(0x3DC,'7ac6f43c8dcf558cd9bbfb3789ff4fb94c62c45dc21f7d47552ac84d6d2fb0f9'),
    0x824529EC:(0x28,'fcadb2ff6377e3ad1f77bedbb10411241a7bb492ce965aae45cb7f8ec19f6153'),
    0x82452AE0:(0x24,'30047ac9e33bcf0dccc6cf84e9e720e3b2768d28674a2be22d90016b749c5186'),
    0x82444854:(0x54,'897f8fe74d362b1f98cff1f68aeb0794806095416ee223c599adfaccc1fe48ef'),
    0x826B09A0:(0x50,'bc41bd85c965b158ef3fda4253672cafbb61d70c5093e5b84f2b1df8d48dcbc8'),
    0x826B09F0:(0x30,'ddf58167cd13bb260f43c424ced6267f777bf4c0b78b06019e4a1d44d49f6e66'),
    0x82CD1B70:(0x218,'82fd8b9fe750b17d294f1b0eb8dff5555e197637ad1bc02dcb29521d6d1b3591'),
}
REFERENCES = {
    'include/rex/graphics/registers.h':'2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40',
    'include/rex/graphics/xenos.h':'7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227',
    'include/rex/graphics/register_table.inc':'c9dfdbfe72763051736230850ee92eaf73b7e2e9be328ac8b83df7d3b5aa9e53',
    'src/graphics/util/draw.cpp':'bffc630c0b7bed2e3f2c8dc1c05ecc7025be289f2bde41eac03a66742ba7a5a1',
}
WORDS = {
    0x823F4634:0x57EB1838, 0x823F465C:0x2B1F0043,
    0x823F46E8:0x4BFFFF30, 0x8243B720:0x5164003C,
    0x8243B728:0x908329C0, 0x8243B820:0xD00329CC,
    0x8243B850:0xD00329C4, 0x824659DC:0x39200004,
    0x82465A00:0x913F29C0, 0x82444854:0x39402302,
    0x82444858:0x39200004, 0x826B09C0:0x38800001,
    0x826B0A04:0x38800001, 0x821DD0E4:0x40000000,
    0x82000BB0:0x3F800000, 0x82000FB8:0x3F000000,
    0x821DD0D8:0, 0x82CD1D80:0x144, 0x82CD1D84:0,
}
CALLS = {0x826B09B8:0x823F46A0,0x826B09D4:0x8243B718,
         0x826B09FC:0x823F46F8,0x826B0A0C:0x8243B718}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def hx(x):
    return f'0x{x:08X}'


def span(b, a, n):
    p = a - BASE
    if p < 0 or n < 0 or p + n > len(b):
        raise ValueError('Out-of-image range')
    return b[p:p+n]


def word(b, a):
    return struct.unpack('>I', span(b, a, 4))[0]


def validate_bytes(b):
    if len(b) != IMAGE_SIZE or sha(b) != IMAGE_SHA:
        raise ValueError('Unexpected original image identity')
    for a, (n, digest) in RANGES.items():
        if sha(span(b, a, n)) != digest:
            raise ValueError(f'Original byte range changed at {hx(a)}')
    for a, w in WORDS.items():
        if word(b, a) != w:
            raise ValueError(f'Original instruction/data changed at {hx(a)}')
    for a, target in CALLS.items():
        w = word(b, a)
        delta = w & 0x03FFFFFC
        if delta & 0x02000000:
            delta -= 0x04000000
        if w >> 26 != 18 or w & 3 != 1 or (a + delta) & 0xFFFFFFFF != target:
            raise ValueError(f'Original branch changed at {hx(a)}')
    rows = [struct.unpack('>II', span(b, 0x82CD1B70+8*i, 8)) for i in range(67)]
    if rows[-1] != (0x144, 0) or any(s in (0x158, 0x15C) for s, _ in rows):
        raise ValueError('Reset raster-state contract changed')
    return rows


def checked_decode(b, a, n, output):
    rows = []
    for line in output.splitlines():
        if not line.strip():
            continue
        m = re.fullmatch(r'([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+(.+?)\s*', line)
        if not m:
            raise ValueError('Malformed disassembler output')
        pc, w = int(m[1], 16), int(m[2], 16)
        if pc != a + 4*len(rows) or pc >= a+n or w != word(b, pc):
            raise ValueError('Disassembler address/word mismatch')
        rows.append({'pc':hx(pc),'word':hx(w),'assembly':m[3]})
    if len(rows)*4 != n:
        raise ValueError('Incomplete disassembler output')
    return rows


def host_offset(mode):
    if mode not in (0, 1):
        raise ValueError('Unknown pixel-center mode')
    return F(1-mode, 2)


def coverage(left, right, center, count=4):
    """Axis-aligned top/left inclusive interval; rational mathematical fixture."""
    return [i for i in range(count) if left <= i+center < right]


def quantize(p, bits):
    """Ideal nearest-even model only, not a claim of measured GPU rounding."""
    return F(round(p * (1 << bits)), 1 << bits)


def canonical_xy(vertices):
    return len(vertices) == 4 and all(-1 <= x <= 1 and -1 <= y <= 1 for x,y in vertices)


def numerical_report():
    return {
        'authority':'Exact rational derivation, not original hardware/native GPU test',
        'observed_half_sequence':[1,0,1],
        'draw_half':1,'draw_guard_bits':[hx(0x3F800000)]*2,
        'mode0_host_origin':['1/2','1/2'],'mode1_host_origin':['0','0'],
        'mode0_quarter_edge_pixels':coverage(F(1,4),F(13,4),F(0)),
        'mode1_quarter_edge_pixels':coverage(F(1,4),F(13,4),F(1,2)),
        'mode0_pixel0_u_for_0_to_4':'0','mode1_pixel0_u_for_0_to_4':'1/8',
        'half1_quantization_counterexample':{
            'left_edge':'17/32','original_Q4':str(quantize(F(17,32),4)),
            'native_Q8':str(quantize(F(17,32),8)),
            'original_pixel0_on_left_edge':True,'native_pixel0_excluded':True},
        'scope':'Four-vertex in-volume root-viewport strip; separate common-grid precision gate. No world/MSAA/expanded-blend proof',
    }


def output_path(value):
    p = Path(value).resolve()
    if p != REPORT.resolve():
        raise ValueError('Only the owned analysis/native-screen-raster-policy.json may be written')
    return p


def inspect(image, reference_root, disassembler):
    b = image.read_bytes()
    resets = validate_bytes(b)
    for name, digest in REFERENCES.items():
        if sha((reference_root/name).read_bytes()) != digest:
            raise ValueError(f'Read-only reference changed: {name}')
    bodies = []
    for a, (n, digest) in sorted(RANGES.items()):
        entry = {'address':hx(a),'size':n,'sha256':digest}
        if a != 0x82CD1B70:
            result = subprocess.run([str(disassembler),str(image),hx(BASE),hx(a),str(n//4)],
                                    check=True,capture_output=True,text=True, timeout=60)
            entry['instructions'] = checked_decode(b,a,n,result.stdout)
            entry['extent_kind'] = ('complete_pdata_function' if a in
                (0x82756480,0x826B09A0,0x826B09F0) else 'reviewed_range')
        else:
            entry['extent_kind'] = 'data_table'
        bodies.append(entry)
    return {'image':{'base':hx(BASE),'size':len(b),'sha256':sha(b)},
            'ranges':bodies,'reference_sha256':REFERENCES,
            'calls':[{'pc':hx(a),'target':hx(t),'lr':hx(a+4)} for a,t in CALLS.items()],
            'reset_rows':[{'address':hx(0x82CD1B70+8*i),'id':hx(s),'value':hx(v)}
                          for i,(s,v) in enumerate(resets)],
            'numerical_fixtures':numerical_report()}


class Fixtures(unittest.TestCase):
    def test_sequence_and_guards(self):
        state = {0x144:1,0x158:0x3F800000,0x15C:0x3F800000}
        state.update(validate_bytes((ROOT/'analysis/simpsons.pe').read_bytes()))
        self.assertEqual(state[0x144],0)
        state[0x144] = 1  # Byte-pinned wrapper LI r4,1 + direct BL setter.
        self.assertEqual([state[k] for k in (0x144,0x158,0x15C)], [1,0x3F800000,0x3F800000])

    def test_image_mutation_rejected(self):
        b=bytearray((ROOT/'analysis/simpsons.pe').read_bytes()); b[0x6B09C3] ^= 1
        with self.assertRaises(ValueError): validate_bytes(b)

    def test_translation_coverage(self):
        for mode in (0,1):
            d=host_offset(mode)
            for l,r in ((F(0),F(4)),(F(1,4),F(13,4)),(F(1),F(2))):
                self.assertEqual(coverage(l,r,F(mode,2)),coverage(l+d,r+d,F(1,2)))

    def test_wrong_unshifted_mode0(self):
        self.assertEqual(coverage(F(1,4),F(13,4),F(0)),[1,2,3])
        self.assertEqual(coverage(F(1,4),F(13,4),F(1,2)),[0,1,2])

    def test_interpolation(self):
        for mode in (0,1):
            d=host_offset(mode)
            for i in range(4):
                self.assertEqual((F(i)+F(mode,2))/4,(F(i)+F(1,2)-d)/4)

    def test_precision_counterexample(self):
        self.assertEqual(quantize(F(17,32),4),F(1,2))
        self.assertEqual(quantize(F(17,32),8),F(17,32))
        self.assertEqual(quantize(F(3,32),4),F(1,8))

    def test_shared_grid(self):
        for i in range(257):
            p=F(i,16)
            self.assertEqual(quantize(p,4),quantize(p,8))
            self.assertEqual(quantize(p+F(1,2),8),p+F(1,2))

    def test_canonical_bounds(self):
        corners=[(-1,1),(1,1),(-1,-1),(1,-1)]
        self.assertTrue(canonical_xy(corners))
        self.assertFalse(canonical_xy(corners[:3]))
        self.assertFalse(canonical_xy([(F(10001,10000),1)]+corners[1:]))
        self.assertFalse(canonical_xy([(float('nan'),1)]+corners[1:]))

    def test_modes_and_output_path(self):
        with self.assertRaises(ValueError): host_offset(2)
        with self.assertRaises(ValueError): output_path(ROOT/'runtime/engine_driver.cpp')
        self.assertEqual(output_path(REPORT),REPORT.resolve())

    def test_disassembly_mismatch(self):
        b=(ROOT/'analysis/simpsons.pe').read_bytes()
        good='826B09C0 38800001 li r4,1'
        self.assertEqual(len(checked_decode(b,0x826B09C0,4,good)),1)
        for text in ('',good.replace('38800001','38800000'),good.replace('09C0','09C4')):
            with self.assertRaises(ValueError): checked_decode(b,0x826B09C0,4,text)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--reference-root',type=Path,default=Path('K:/Simpsons/RexGlueCurrent'))
    parser.add_argument('--disassembler',type=Path,default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    parser.add_argument('--report',type=output_path)
    parser.add_argument('--verify-report',action='store_true')
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    if args.self_test:
        result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Fixtures))
        if not result.wasSuccessful(): return 1
        if not (args.report or args.verify_report): return 0
    value=inspect(args.image.resolve(),args.reference_root,args.disassembler.resolve())
    serialized=json.dumps(value,indent=2,sort_keys=True)+'\n'
    if args.verify_report:
        if REPORT.read_text(encoding='utf-8') != serialized: raise ValueError('Frozen report differs')
        print('Report verified; no files written')
    elif args.report:
        args.report.write_text(serialized,encoding='utf-8',newline='\n')
        print(f'Wrote {args.report.name}: {sum(len(x.get("instructions",[])) for x in value["ranges"])} checked words')
    else:
        print(serialized,end='')
    return 0


if __name__ == '__main__':
    sys.exit(main())
