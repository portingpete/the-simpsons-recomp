"""Read-only, byte-pinned presentation evidence. No GPU/CPU execution backend.

The address search follows finite original control-flow paths and constant
address formation, not loaded aliases or arbitrary arithmetic. Its limits are
reported explicitly. Only analysis/native-present.json is writable by this CLI.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / 'tools/analyze_poststart_integration.py'
if hashlib.sha256(SUPPORT.read_bytes()).hexdigest() != 'fdcb3c06ae15041bcbf98ce59d07272e51dce345c7332b44475844d20251b434':
    raise RuntimeError('Frozen byte/PE validation dependency changed')
from analyze_poststart_integration import (BASE, IMAGE_SHA, branch, checked_decode,
                                          hx, layout, sha, span, validate_identity, word)

REPORT = ROOT / 'analysis/native-present.json'
FIELDS = tuple(range(0x82D0CF8C, 0x82D0CF9C, 4))
LEAVES = {0x823ED8E0, 0x823FA978, 0x823FC5B8, 0x8243BA10,
          0x824544E8, 0x82454BE8, 0x82454BF0, 0x82457708}
PINS = {
    0x823ED8E0: (0x4, '047e7a0eefe5d96b9516aad3c33dc1589e6ded842d433349e5a9198f35ef72ca'),
    0x823EDF20: (0x288, '575ebe22b19549ad84801306e347ca37feaf58da2b2c2f026f4fe54bd41cd1d6'),
    0x823EE1A8: (0x290, 'ba74567cf0aa3caa6d5f5d2ffceaf31abe0d9f8caf3ace3bfaf58cf66bc989a5'),
    0x823EE820: (0xAC, 'c07f3c115bd84242d3cf5caf790027b8e2711a231864d66c390ca16d8e326577'),
    0x823FA978: (0xA4, 'c22e20a543eea7380042e9cfd14a6d920812cb91dff3cc49155edbda9fed2faa'),
    0x823FC5B8: (0x3C, '4092121151844c62160e4a0e92d51fe0905f532c3536c6fd799aeb142b1c9040'),
    0x82408030: (0x54, '93d339abc97e4458c3022dd111164cf57fda91626cf2e57907ded881874c0195'),
    0x8243BA10: (0x8, 'a5aabdd7a855cff3a6671ea1c8998dc6f7a00e12868e1c0242f9d123cd6be7be'),
    0x82453DB0: (0xFC, '71e9e6c95b544d080ad4e778f3c74d707e276ff59ade870cf9f255cb6df7309a'),
    0x82453EB0: (0x194, 'd986e1ce1599a45298db8c95b7bfa28bce981b3988c316b73c452b04295a681a'),
    0x82454048: (0x1A8, '9cd0a7fdb2dc2a08cd680cfb04116333cc0d19b792acfb44d48e0703bc72a0ea'),
    0x824544E8: (0x8, '3269c5f7b6090fb63ec028ac28456592a46905af3844547e9180c160452bebfb'),
    0x824544F0: (0x594, '2187ab279e8ac47b4676e433f0758ecbac4dc0acbbcb11a72ae2e1eca6c63b28'),
    0x82454BE8: (0x8, 'af09bac82e657f976887ada2e9df15db935033e9cc3abd0057744f261fd37efa'),
    0x82454BF0: (0x8, '62b76e597d0c4692e846849d563aa585e9cdf96aa2850c9286037fec4744a1bc'),
    0x82455570: (0xE44, 'e3b994307e6e95ca72f3e9d7b184d4ec60d5e5e5cdc72978dd1c90cb66d0e26f'),
    0x82457210: (0xE8, 'f6956938261906b7c2f7dc240197964dca19d4b2baf3e07d9950eea0bec89624'),
    0x824574B8: (0xC4, '301ac0abbfe1e7c68fa371a9739c170743ec7fb0296586fe41822445c84d9604'),
    0x82457580: (0x170, 'd77a74f05a062c1bffd1c9e1eb6a912bab97b44170b11c54343b3dac33013a2d'),
    0x82457708: (0x1C, '19c357a6cd02bf7648dff6d6afd0a169848f038d821403b10546f017bfe14acd'),
    0x82457B40: (0x184, '959782745cc80834c887f449485cab7b3fd44840f1a814410cf9e40778c3e87d'),
    0x82457CC8: (0xFC, '29cb2db65debdead723d02eb4fba35c0b928489a868bd094d5a1dc347d2c6f4a'),
    0x82457E30: (0x34, '55c4bd651f053bad35b05a6e5095f6b84fe93119b13332690ca58cd6e555eb6d'),
    0x8246D3A0: (0xF0, '8910a71f1defd995bd2554bb3af183faf04dbc7bec2ebdf728998f7e618cb673'),
}
CALLS = {
    0x82408054: 0x823FA978, 0x823EE87C: 0x82455570,
    0x823EE88C: 0x82457E30, 0x823EE898: 0x824544E8,
    0x823EE8A8: 0x824544F0, 0x823EE8AC: 0x823FC5B8,
    0x82457E48: 0x82457CC8, 0x82457D58: 0x82457B40,
    0x82457C48: 0x82457210, 0x82454178: 0x82457580,
    0x824546CC: 0x8246D3A0, 0x8245473C: 0x82CC2CA4,
    0x824549D4: 0x82457CC8, 0x82454A08: 0x824574B8,
    0x82457544: 0x82452018,
}
# All manually reviewed exact memory uses found by the bounded address search.
EXPECTED_USES = {
    0x823EE130: ('store', 0x82D0CF90), 0x823EE158: ('store', 0x82D0CF8C),
    0x823EE3DC: ('load', 0x82D0CF8C), 0x823EE3F0: ('store', 0x82D0CF8C),
    0x823EE3F4: ('load', 0x82D0CF90), 0x823EE408: ('store', 0x82D0CF90),
    0x823EE854: ('load', 0x82D0CF8C), 0x823EE85C: ('load', 0x82D0CF90),
    0x823EE874: ('store', 0x82D0CF90), 0x823EE878: ('store', 0x82D0CF8C),
    0x823EE880: ('load', 0x82D0CF94), 0x823EE888: ('store', 0x82D0CF98),
    0x823EE890: ('store', 0x82D0CF94), 0x823EE8A0: ('load', 0x82D0CF8C),
}


def signed16(w):
    x = w & 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def u32(x):
    if type(x) is not int or not 0 <= x <= 0xFFFFFFFF:
        raise ValueError('Expected unsigned 32-bit integer')
    return x


def transfer(w, registers):
    """Static address propagation only; never reads or writes guest data.

    Unknown GPR-producing operations invalidate possible destinations. Tracked
    values descend from a selected LIS seed, not runtime inputs. No loaded
    pointer propagation, branch condition evaluation, or GPU packet decoding.
    """
    r = dict(registers)
    op, rt, ra, rb = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
    d, xo = signed16(w), w >> 1 & 1023
    refs = []

    def put(reg, value):
        r.pop(reg, None)
        if value is not None and 0x82D00000 <= (value & 0xFFFFFFFF) < 0x82D20000:
            r[reg] = value & 0xFFFFFFFF

    if op in (12, 13, 14, 15):
        v = registers.get(ra) if ra or op in (12, 13) else None
        put(rt, None if v is None else v + (d << (16 if op == 15 else 0)))
    elif op in (24, 25):
        v = registers.get(rt)
        put(ra, None if v is None else v | ((w & 0xFFFF) << (16 if op == 25 else 0)))
    elif 32 <= op <= 55 or op in (58, 62):
        disp = d & ~3 if op in (58, 62) else d
        ea = (registers[ra] + disp) & 0xFFFFFFFF if ra in registers else None
        load = op in (32, 33, 34, 35, 40, 41, 42, 43, 46, 48, 49, 50, 51, 58)
        if ea in FIELDS:
            refs.append(('load' if load else 'store', ea))
        if not load and op < 48 and registers.get(rt) in FIELDS:
            refs.append(('address_stored', registers[rt]))
        if op in (32, 33, 34, 35, 40, 41, 42, 43, 58):
            put(rt, None)
        if op == 46:
            for k in range(rt, 32):
                put(k, None)
        if (op <= 55 and op & 1 and op not in (46, 47)) or (op in (58, 62) and w & 3 == 1):
            put(ra, ea)
    elif op == 31:
        if xo == 444 and rt == rb:  # mr
            put(ra, registers.get(rt))
        elif xo in (0, 32, 144, 467, 598, 854, 54, 86, 246, 278, 470, 982, 1014,
                    151, 215, 407, 149, 663, 727, 231, 487):
            pass  # compare, mt*, barriers, cache ops, non-updating indexed stores
        elif xo in (183, 247, 439, 181, 695, 759):
            put(ra, None)  # indexed update stores
        elif xo in (24, 26, 28, 60, 124, 284, 316, 412, 444, 476, 536, 792, 824, 922, 954, 986):
            put(ra, None)
        else:
            put(rt, None)
            put(ra, None)  # conservative for unclassified X-form instructions
    elif op in (20, 21, 23, 26, 27, 28, 29, 30):
        put(ra, None)
    elif op in (7, 8):
        put(rt, None)
    elif op not in (3, 10, 11, 16, 18, 19, 59, 63):
        r.clear()  # unknown encoding: do not propagate addresses through it
    return r, refs


def address_search(b, text_address, text_size, pdata):
    starts = sorted(pdata)
    hits, seeds, limited, visited_count, backedges = set(), 0, [], 0, set()
    end = text_address + text_size
    for seed in range(text_address, end, 4):
        w = word(b, seed)
        if w >> 26 != 15 or w >> 16 & 31 or w & 0xFFFF not in (0x82D0, 0x82D1):
            continue
        seeds += 1
        i = bisect.bisect_right(starts, seed) - 1
        if i >= 0 and seed < starts[i] + pdata[starts[i]][0]:
            lo, hi = starts[i], starts[i] + pdata[starts[i]][0]
        else:
            lo, hi = seed, min(end, starts[i + 1] if i + 1 < len(starts) else end)
        work = [(seed + 4, {w >> 21 & 31: (w & 0xFFFF) << 16})]
        seen = set()
        while work:
            pc, r = work.pop()
            if not lo <= pc < hi or not r:
                continue
            key = (pc, tuple(sorted(r.items())))
            if key in seen:
                continue
            if len(seen) >= 8192:
                limited.append(hx(seed))
                break
            seen.add(key)
            ins = word(b, pc)
            op, xo = ins >> 26, ins >> 1 & 1023
            nxt, refs = transfer(ins, r)
            hits.update((pc, kind, target) for kind, target in refs)
            for reg, target in nxt.items():
                if target in FIELDS and r.get(reg) != target:
                    hits.add((pc, 'address_formed', target))
            call = op in (16, 18, 19) and ins & 1
            if call:
                for reg in range(3, 11):
                    if r.get(reg) in FIELDS:
                        hits.add((pc, 'address_call_argument', r[reg]))
                nxt = {k: v for k, v in nxt.items() if k >= 13}
                work.append((pc + 4, nxt))
            elif op == 18:
                dest = branch(pc, ins)[0]
                if dest > pc:
                    work.append((dest, nxt))
                else:
                    backedges.add(pc)
            elif op == 16:
                delta = ins & 0xFFFC
                delta -= 0x10000 if delta & 0x8000 else 0
                dest = (delta if ins & 2 else pc + delta) & 0xFFFFFFFF
                if dest > pc:
                    work.append((dest, dict(nxt)))
                else:
                    backedges.add(pc)
                work.append((pc + 4, nxt))
            elif op == 19 and xo in (16, 528):
                # Conditional return has a fallthrough; unconditional bctr/blr ends.
                if (ins >> 21 & 20) != 20:
                    work.append((pc + 4, nxt))
            else:
                work.append((pc + 4, nxt))
        visited_count += len(seen)
    return {'seeds': seeds, 'visited_states': visited_count, 'limited_seeds': limited,
            'backedges_not_unrolled': [hx(x) for x in sorted(backedges)],
            'references': [{'pc': hx(pc), 'word': hx(word(b, pc)), 'kind': kind, 'field': hx(a)}
                           for pc, kind, a in sorted(hits)]}


def check_pins(b):
    sections, pdata = layout(b)
    for a, (n, digest) in PINS.items():
        if sha(span(b, a, n)) != digest:
            raise ValueError('Changed pinned code at ' + hx(a))
        if a not in LEAVES and (a not in pdata or pdata[a][0] != n):
            raise ValueError('Changed .pdata extent at ' + hx(a))
    for pc, target in CALLS.items():
        if branch(pc, word(b, pc)) != (target, True):
            raise ValueError('Changed critical call at ' + hx(pc))
    if branch(0x824544EC, word(b, 0x824544EC)) != (0x82454048, False):
        raise ValueError('Changed synchronization tail branch')
    for a in FIELDS:
        if word(b, a):
            raise ValueError('Original global is not initially zero')
    return sections, pdata


def sdk_pending_comparison(current, completed, token):
    """Only the original unsigned comparison at 824574D0..F0, not a wait."""
    for x in (current, completed, token):
        u32(x)
    return token != 0 and ((current-token) & 0xFFFFFFFF) < ((current-completed) & 0xFFFFFFFF)


def internal_interval(request):
    """82454100..148 selector, not a host swap-chain timing implementation."""
    u32(request)
    choices = {0: 1, 1: 1, 2: 2, 4: 3, 0x80000000: 0}
    if request not in choices:
        raise ValueError('Unproved interval request')
    return choices[request]


def publication(cf8c, cf90, old_cf94, submission_receipt):
    """Value-only proposed publication fixture; a number cannot prove completion."""
    for x in (cf8c, cf90, old_cf94, submission_receipt):
        u32(x)
    if not cf8c or not cf90 or cf8c == cf90 or not submission_receipt:
        raise ValueError('Native proposal requires distinct live fronts and a receipt')
    return {'CF8C': cf90, 'CF90': cf8c, 'CF98': old_cf94, 'CF94': submission_receipt}


def require_output(path):
    if path.resolve() != REPORT.resolve():
        raise ValueError('Only analysis/native-present.json is an authorized output')
    if path.exists() and (path.is_symlink() or path.stat().st_nlink != 1):
        raise ValueError('Refusing linked output file')


def facts():
    def item(status, statement, *pcs):
        return {'status': status, 'statement': statement, 'evidence_pcs': [hx(p) for p in pcs]}
    return [
        item('verified', 'CF94 is loaded only to shift its prior value into CF98 among the reviewed static references. CF98 has no load in that set. No observed history-to-wait or application arithmetic use; not a whole-program alias proof.',
             0x823EE880, 0x823EE888, 0x823EE890),
        item('verified', 'Original image initializes all four globals to zero. Start creates distinct front owners, present swaps them, stop releases/zeros each. Start/stop do not explicitly reset the two histories.',
             0x823EE130, 0x823EE158, 0x823EE874, 0x823EE878, 0x823EE3DC, 0x823EE408),
        item('verified', 'CF94 receives device+2A9C captured before submission helper 82457CC8, after the resolve and before display synchronization/VdSwap. It is not a scanout-completion receipt.',
             0x823EE87C, 0x823EE88C, 0x82457E40, 0x82457E44, 0x82457E48, 0x823EE8A8),
        item('verified', 'Normal SDK submission writes its current token into its submission data and advances device+2A9C by two. Submission count is not one per frame; conditional SDK paths differ.',
             0x8245723C, 0x824572BC, 0x824572EC, 0x824572F0),
        item('verified', 'Wait helper treats zero as no wait and compares unsigned modular current-token against current-completed. Completion source is [device+2A90]+0. Polling can terminate through 82452018 returning zero.',
             0x824574D0, 0x824574D8, 0x824574EC, 0x82457544, 0x8245754C, 0x82457568),
        item('verified', '824544F0 captures a separate token at 824549D0, submits, waits using the prior device+3A44 token, and on the ordinary path publishes the captured token at +3A44. Neither CF94 nor CF98 supplies that wait.',
             0x824549D0, 0x824549D4, 0x82454A00, 0x82454A08, 0x82454A58),
        item('verified', '824544E8 queues synchronization/pacing work via 82454048 and 82457580; it does not itself prove CPU completion. Its callback address is 82453EB0. Interval requests 0 and 1 both select internal interval 1; 2/4/80000000 select 2/3/0.',
             0x824544EC, 0x82454100, 0x82454144, 0x82454160, 0x82454178, 0x824575E0),
        item('verified', 'Pacing callback can invoke device+4084 with a six-word stack record; the callback may change its final word before pacing continues. Setter 82454BF0 simply stores r4 into [r3+4084].',
             0x82453F70, 0x82453F80, 0x82453FAC, 0x82453FB0, 0x82454BF0),
        item('verified', 'Device timing callback 82453DB0 optionally invokes device+4088 with three stack words. Setter 82454BE8 stores r4 into [r3+4088]. Both callback registrations need native support or explicit rejection.',
             0x82453E6C, 0x82453E80, 0x82453E94, 0x82454BE8),
        item('verified', 'Present enters capture helper 8246D3A0, gated first by byte 82D55BCE. It also branches on SDK capture state +5404 before VdSwap. This is optional behavior, not unconditional game CPU work.',
             0x824546CC, 0x824546D0, 0x824546DC, 0x8246D3B4, 0x8246D3BC),
        item('verified', 'Original outer wrapper invokes CPU list splice 823FA978 before platform present. On normal platform return, 823FC5B8 resets all four cursors and advances D0DC modulo four. The helpers themselves perform no completion query.',
             0x82408054, 0x823EE8AC, 0x823FC5D0, 0x823FC5E0, 0x823FC5E4, 0x823FC5EC),
        item('proposal', 'Replace engine callback 823EE820, preserve wrapper/CPU helpers, validate live front identities and bound color role, perform real same-size packed transfer, associate native completion receipts and protect all resource reuse. Publish histories only for genuinely submitted work. Native receipt encoding is not the original SDK counter.',
             0x823EE820, 0x823EE87C, 0x823EE890, 0x823EE8AC),
        item('proposal', 'A conservative first service can wait for real native work before recycling resources; retain front owners through display use. Submission completion and host display acceptance are distinct. Failure after submission is not reversible guest-only rollback.'),
        item('unresolved', 'Indirect/computed aliases, arbitrary indexed bulk access and unseen callback registration are outside the address search. SDK consumer entries must remain guarded; native IDs must never enter SDK wait/object APIs.'),
        item('unresolved', 'Final gamma/output transfer, scaling/cropping/filter precision, display quantization, compositor/scanout timing and optional capture behavior are not certified. A real host Present is not proof of console display equivalence.'),
    ]


def literal_hits(b, target):
    needle, result = struct.pack('>I', target), []
    pos = b.find(needle)
    while pos >= 0:
        result.append(hx(BASE + pos))
        pos = b.find(needle, pos + 1)
    return result


def inspect(b, image, disassembler):
    sections, pdata = check_pins(b)
    search = address_search(b, *sections['.text'], pdata)
    found = {(int(r['pc'], 16), r['kind'], int(r['field'], 16))
             for r in search['references'] if r['kind'] in ('load', 'store')}
    expected = {(pc, kind, va) for pc, (kind, va) in EXPECTED_USES.items()}
    if found != expected or search['limited_seeds']:
        raise ValueError('Address search differs from reviewed reference inventory')
    functions = []
    for a, (size, digest) in sorted(PINS.items()):
        output = subprocess.run([str(disassembler), str(image), hx(BASE), hx(a), str(size//4)],
                                check=True, capture_output=True, text=True, timeout=30)
        functions.append({'address': hx(a), 'size': size, 'sha256': digest,
                          'extent_kind': 'reviewed leaf' if a in LEAVES else '.pdata',
                          'pdata_address': None if a in LEAVES else hx(pdata[a][1]),
                          'instructions': checked_decode(b, a, size, output.stdout)})
    targets = {0x823ED8E0, 0x82453DB0, 0x82453EB0, 0x824544E8, 0x824544F0,
               0x82454BE8, 0x82454BF0, 0x824574B8, 0x82457708, 0x82457E30}
    direct = {hx(t): [] for t in sorted(targets)}
    immediate = []
    ta, tn = sections['.text']
    for pc in range(ta, ta + tn, 4):
        w = word(b, pc)
        edge = branch(pc, w)
        if edge and edge[0] in targets:
            direct[hx(edge[0])].append({'pc': hx(pc), 'word': hx(w), 'linked': edge[1]})
        if w >> 26 in (14, 24, 25, 32, 36) and (w & 0xFFFF) in (0xCF8C, 0xCF90, 0xCF94, 0xCF98):
            immediate.append({'pc': hx(pc), 'word': hx(w),
                              'classification': 'reviewed target base' if pc in (0x823EE1BC, 0x823EE83C)
                              else 'low-word-only candidate; not an absolute target reference',
                              'preceding_words': [hx(word(b, a)) for a in range(max(ta, pc-32), pc, 4)]})
    search['method'] = ('All aligned .text words; LIS 82D0/82D1 address seeds; addi/addis/addic, '
                        'ori/oris, mr, D/DS memory addressing; forward control-flow paths, '
                        'ABI volatile invalidation at calls. Backedges are not unrolled.')
    search['limits'] = ('Loaded aliases and indirect/indexed memory are not resolved; unknown '
                        'GPR operations invalidate addresses. A negative search is not whole-program closure.')
    search['raw_global_pointer_occurrences'] = {hx(a): literal_hits(b, a) for a in FIELDS}
    search['low_immediate_candidates'] = immediate
    return {'schema': 1, 'image': {'base': hx(BASE), 'bytes': len(b), 'sha256': IMAGE_SHA},
            'facts': facts(), 'global_address_search': search,
            'global_initial_values': {hx(a): hx(word(b, a)) for a in FIELDS},
            'direct_branch_search': direct,
            'callback_pointer_literal_search': {hx(a): literal_hits(b, a) for a in (0x82454BE8, 0x82454BF0)},
            'functions': functions,
            'contract': {'boundary': '823EE820, retain 82408030 and CPU list/index helpers',
                         'history_meaning': 'submitted-copy history, not scanout completion',
                         'front_copy': 'distinct same-size single-sample packed 10:10:10:2 logical RGBA; preserve codes and stored alpha',
                         'front_sample_alpha': 1,
                         'original_present_interval_0_and_1': 'both internal interval 1; host mapping requires explicit pacing policy',
                         'native_receipts': 'real backend completion association, separate presentation resource lease',
                         'no_claim': 'no runtime implementation, no successful original frame, no certified display/timing match'}}


def run_tests(original):
    class EvidenceTests(unittest.TestCase):
        def test_pins_and_identity(self):
            validate_identity(original)
            self.assertEqual(len(check_pins(original)[1]), len(layout(original)[1]))

        def test_every_code_pin_rejects_change(self):
            for a in PINS:
                mutated = bytearray(original)
                mutated[a-BASE] ^= 1
                with self.assertRaises(ValueError):
                    check_pins(mutated)

        def test_pdata_corruption(self):
            _, pd = layout(original)
            mutated = bytearray(original)
            at = pd[0x823EE820][1] - BASE + 6
            mutated[at] ^= 1
            with self.assertRaises(ValueError):
                check_pins(mutated)

        def test_original_global_baseline_rejection(self):
            mutated = bytearray(original)
            mutated[FIELDS[-1]-BASE+3] = 1
            with self.assertRaises(ValueError):
                check_pins(mutated)

        def test_original_full_text_reference_inventory(self):
            sections, pd = check_pins(original)
            result = address_search(original, *sections['.text'], pd)
            got = {(int(r['pc'], 16), r['kind'], int(r['field'], 16)) for r in result['references']
                   if r['kind'] in ('load', 'store')}
            self.assertEqual(got, {(p, k, a) for p, (k, a) in EXPECTED_USES.items()})
            self.assertEqual(result['limited_seeds'], [])
            self.assertTrue(all(not literal_hits(original, a) for a in FIELDS))

        def test_negative_offset_and_nonadjacent_base(self):
            r, _ = transfer(0x3BEBCF94, {11: 0x82D10000})
            self.assertEqual(r[31], 0x82D0CF94)
            _, refs = transfer(0x817FFFF8, r)
            self.assertEqual(refs, [('load', 0x82D0CF8C)])

        def test_loaded_pointer_is_not_address(self):
            r, refs = transfer(0x817F0000, {31: 0x82D0CF94, 11: 0x82D0CF98})
            self.assertNotIn(11, r)
            self.assertEqual(refs, [('load', 0x82D0CF94)])

        def test_address_overwrite_and_unknown(self):
            self.assertNotIn(31, transfer(0x3BE00000, {31: 0x82D0CF94})[0])
            self.assertEqual(transfer(0, {31: 0x82D0CF94})[0], {})

        def test_call_clobbers_volatile_not_nonvolatile(self):
            # LIS r11; addi r31,r11,CF94; BL; LWZ volatile; LWZ nonvolatile; BLR.
            words = [0x3D6082D1, 0x3BEBCF94, 0x48000021, 0x806BCF94, 0x807F0000, 0x4E800020]
            data = struct.pack('>6I', *words)
            result = address_search(data, BASE, len(data), {BASE: (len(data), 0, 0)})
            loads = [r['pc'] for r in result['references'] if r['kind'] == 'load']
            self.assertEqual(loads, [hx(BASE+16)])

        def test_wait_zero_old_pending_and_wrap(self):
            self.assertFalse(sdk_pending_comparison(12, 8, 0))
            self.assertFalse(sdk_pending_comparison(12, 8, 8))
            self.assertFalse(sdk_pending_comparison(12, 8, 6))
            self.assertTrue(sdk_pending_comparison(12, 8, 10))
            self.assertTrue(sdk_pending_comparison(2, 0xFFFFFFFC, 0xFFFFFFFE))
            self.assertFalse(sdk_pending_comparison(2, 0xFFFFFFFE, 0xFFFFFFFE))
            with self.assertRaises(ValueError):
                sdk_pending_comparison(-1, 0, 0)

        def test_two_frames_keep_history_separate(self):
            a = publication(101, 202, 17, 31)
            self.assertEqual(a, {'CF8C': 202, 'CF90': 101, 'CF98': 17, 'CF94': 31})
            b = publication(a['CF8C'], a['CF90'], a['CF94'], 47)
            self.assertEqual(b, {'CF8C': 101, 'CF90': 202, 'CF98': 31, 'CF94': 47})
            for args in [(0, 2, 0, 1), (1, 1, 0, 1), (1, 2, 0, 0), (1, 2, 0, 1<<32)]:
                with self.assertRaises(ValueError):
                    publication(*args)

        def test_disassembly_mismatch(self):
            with self.assertRaises(ValueError):
                checked_decode(original, 0x82454BF0, 8, '82454BF0 00000000 wrong\n')

        def test_callback_and_interval_words(self):
            expected = {0x82454BF0: 0x90834084, 0x82454BE8: 0x90834088,
                        0x82453FAC: 0x4E800421, 0x82453E94: 0x4E800421,
                        0x82454100: 0x280A0000, 0x82454108: 0x2B0A0001,
                        0x82454144: 0x39400001}
            for pc, w in expected.items():
                self.assertEqual(word(original, pc), w)

        def test_interval_zero_is_not_immediate(self):
            self.assertEqual([internal_interval(x) for x in (0, 1, 2, 4, 0x80000000)], [1, 1, 2, 3, 0])
            for x in (3, -1, True, 1 << 32):
                with self.assertRaises(ValueError):
                    internal_interval(x)

        def test_output_scope_and_truncation(self):
            require_output(REPORT)
            for p in [ROOT/'analysis/simpsons.pe', ROOT/'build/native-present.json']:
                with self.assertRaises(ValueError):
                    require_output(p)
            with self.assertRaises(ValueError):
                validate_identity(original[:-1])

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceTests))
    return 0 if result.wasSuccessful() else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--disassembler', type=Path, default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.report:
        require_output(args.report)
    b = args.image.read_bytes()
    validate_identity(b)
    if args.self_test:
        return run_tests(b)
    result = inspect(b, args.image, args.disassembler)
    output = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.report:
        args.report.write_text(output, encoding='utf-8', newline='\n')
    else:
        print(output, end='')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('error: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
