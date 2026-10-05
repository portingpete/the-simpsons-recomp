"""Opaque normalmap inventory only; no GPU execution or runtime admission."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import analyze_rigid_normalmap_shader as normalmap

class NormalmapInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = (ROOT/'analysis/simpsons.pe').read_bytes()
        cls.inventory = normalmap.inspect(cls.image)

    def test_exact_pair_and_complete_slots(self):
        for stage,address,pairs,slots in [('VS','0x8205855c',5,43),('PS','0x82058cbc',16,122)]:
            row = self.inventory[stage]
            self.assertEqual(row['address'],address)
            self.assertEqual(sorted(row['rows']),list(range(pairs,pairs+slots)))
            self.assertEqual(tuple(row['control']),normalmap.CF[stage])
        self.assertEqual(sum(r['fetch'] for r in self.inventory['VS']['rows'].values()),6)
        self.assertEqual(sum(r['fetch'] for r in self.inventory['PS']['rows'].values()),20)

    def test_all_alu_slots_emit(self):
        # Emission coverage only: issue() does not schedule control flow or
        # apply instruction predicates. Never use this as runtime admission.
        for stage,expected in (('VS',37),('PS',102)):
            count=0
            for slot,row in self.inventory[stage]['rows'].items():
                if row['fetch']:continue
                with self.subTest(stage=stage,slot=slot):
                    self.assertIn('// slot'+str(slot),normalmap.rigid.issue(slot,row,stage))
                count+=1
            self.assertEqual(count,expected)

    def test_ps_is_not_straight_line(self):
        self.assertEqual(sum(hi>>12==11 for lo,hi in normalmap.CF['PS']),5)
        self.assertTrue(any(r['fields'].get('predicated') for r in self.inventory['PS']['rows'].values()))

    def test_export_maps_exact(self):
        # Eight VS interpolators plus clip position; a single PS output.
        # Masks prove partial writes (7) versus full writes (15).
        self.assertEqual(normalmap.exports(self.inventory['VS']['rows'],'VS'),
                         normalmap.VS_EXPORTS)
        self.assertEqual(normalmap.exports(self.inventory['PS']['rows'],'PS'),
                         normalmap.PS_EXPORTS)
        self.assertEqual(sorted(k for k in normalmap.VS_EXPORTS if k != 62),
                         list(range(8)))

    def test_schedule_coverage_allocs_and_end(self):
        entries, covered = normalmap.schedule(normalmap.CF['VS'],'VS')
        self.assertEqual(covered,list(range(5,48)))
        kinds = [(e['index'],e['op']) for e in entries]
        self.assertEqual([k for k in kinds if k[1] != 'EXEC'],
                         [(1,'ALLOC'),(3,'ALLOC'),(9,'EXEC_END')])
        tail = next(e for e in entries if e['op'] == 'EXEC_END')
        self.assertEqual((tail['address'],tail['count']),(45,3))
        entries, covered = normalmap.schedule(normalmap.CF['PS'],'PS')
        self.assertEqual(covered,list(range(16,138)))
        tail = next(e for e in entries if e['op'] == 'EXEC_END')
        self.assertEqual((tail['address'],tail['count']),(134,4))
        self.assertEqual([e['index'] for e in entries if e['op'] == 'ALLOC'],[30])

    def test_jumps_are_forward_with_pinned_targets(self):
        entries, _ = normalmap.schedule(normalmap.CF['PS'],'PS')
        jumps = [(e['index'],e['raw'],e['target']) for e in entries if e['op'] == 'JUMP']
        self.assertEqual(tuple(jumps),normalmap.PS_JUMPS)
        for index, raw, target in jumps:
            with self.subTest(index=index):
                self.assertEqual(raw,normalmap.CF['PS'][index][0])
                self.assertGreater(target,index)
        _, covered_vs = normalmap.schedule(normalmap.CF['VS'],'VS')
        entries_vs, _ = normalmap.schedule(normalmap.CF['VS'],'VS')
        self.assertEqual([e for e in entries_vs if e['op'] == 'JUMP'],[])

    def test_predication_and_sources(self):
        self.assertEqual(normalmap.predication(self.inventory['PS']['rows']),
                         normalmap.PS_PREDICATED)
        self.assertEqual(normalmap.predication(self.inventory['VS']['rows']),{})
        for slot, sense in normalmap.PS_PREDICATED.items():
            self.assertTrue(sense,'Predicated slot must gate on p0, not !p0')
        self.assertEqual(normalmap.predicate_sources(self.inventory['PS']['rows']),
                         normalmap.PS_PREDICATE_SOURCES)
        self.assertEqual(normalmap.predicate_sources(self.inventory['VS']['rows']),())

    def test_vs_source_structure(self):
        source = normalmap.vs_source(self.image)
        self.assertIn('NormalmapVSOutput VSRigidNormalmap(NormalmapVSInput input)',source)
        self.assertEqual(source.count('// slot'),37)
        self.assertNotIn('FETCH',source)
        for marker in ('float4(input.uv,input.uv1)','float4(input.tangent,0)',
                       'float4(input.normal,0)','result.t6=output6.xyz'):
            self.assertIn(marker,source)

    def test_ps_fetches_exact(self):
        fetched = normalmap.ps_fetches(self.inventory['PS']['rows'])
        self.assertEqual(fetched['material'],((16,2,8),(28,3,10)))
        self.assertEqual([constant for constant, _ in fetched['shadow']],[1,0])
        for (_, taps), (constant, slots, _, _) in zip(
                fetched['shadow'],normalmap.PS_SHADOW_CLAUSES):
            self.assertEqual([slot for slot, _, _ in taps],list(slots))
            self.assertEqual([channel for _, _, channel in taps],
                             [channel for _, _, channel in normalmap.PS_TAPS])

    def test_ps_fetches_reject_mutations(self):
        import copy
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        slot = normalmap.PS_SHADOW_CLAUSES[0][1][0]
        raw = list(mutated[slot]['fields']['offset_fields'])
        raw[0] ^= 0x1
        mutated[slot]['fields']['offset_fields'] = raw
        with self.assertRaisesRegex(ValueError,'Normalmap shadow tap changed'):
            normalmap.ps_fetches(mutated)
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        slot = normalmap.PS_MATERIAL_FETCHES[0][0]
        mutated[slot]['fields']['fetch_constant_index'] ^= 0x1
        with self.assertRaisesRegex(ValueError,'Normalmap material fetch changed'):
            normalmap.ps_fetches(mutated)
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        mutated[slot]['fields']['anisotropy'] = 0
        with self.assertRaisesRegex(ValueError,'Normalmap fetch filter profile changed'):
            normalmap.ps_fetches(mutated)

    def test_ps_inputs_exact(self):
        self.assertEqual(normalmap.ps_inputs(self.inventory['PS']['rows']),
                         normalmap.PS_INPUT_FIRST_USE)
        self.assertEqual([reg for reg, _ in normalmap.PS_INPUT_FIRST_USE],
                         [0,1,2,3,4,6])

    def test_ps_inputs_reject_mutations(self):
        import copy
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        mutated[18]['fields']['sources'][0]['register'] = 7
        self.assertNotEqual(normalmap.ps_inputs(mutated),
                            normalmap.PS_INPUT_FIRST_USE)

    def test_input_mapping_exact(self):
        # Semantic entries joined with fetch destinations: normal -> r5,
        # tangent -> r2, texcoords -> r4.xy/r4.zw (first/second).
        self.assertEqual(normalmap.input_mapping(self.image),
                         ((5,'POSITION',1),(6,'NORMAL',5),(7,'TANGENT',2),
                          (8,'COLOR',3),(9,'TEXCOORD',4),(10,'TEXCOORD',4)))
        self.assertEqual(normalmap.USAGE_NAMES[6],'TANGENT')
        self.assertEqual(len(normalmap.INPUT_MAPPING),6)

    def test_input_mapping_rejects_mutations(self):
        va = normalmap.PROFILES[0][1]
        base = va-normalmap.screen.BASE
        original = normalmap.hashlib.sha256
        digests = {n: digest for _, _, n, _, _, _, digest in normalmap.PROFILES}
        def pinned(data):
            if len(data) in digests:
                class Digest:
                    def hexdigest(self): return digests[len(data)]
                return Digest()
            return original(data)
        # Retarget the tangent entry at slot 7 onto slot 6: duplicate link.
        changed = bytearray(self.image)
        entry_at = base+normalmap.SEMANTIC_OFFSET+2*4
        changed[entry_at+3] ^= 0x01
        with patch.object(normalmap.hashlib,'sha256',pinned):
            with self.assertRaisesRegex(ValueError,'semantic entry changed'):
                normalmap.input_mapping(changed)
        # Rewrite the tangent usage byte to a normal: role mismatch.
        changed = bytearray(self.image)
        changed[entry_at+2] ^= 0x50
        with patch.object(normalmap.hashlib,'sha256',pinned):
            with self.assertRaisesRegex(ValueError,'semantic entry changed'):
                normalmap.input_mapping(changed)

    def test_export_schedule_predication_pins_reject_mutations(self):
        import copy
        mutated = copy.deepcopy(self.inventory['VS']['rows'])
        mutated[25]['fields']['vector_mask'] = 0
        self.assertNotEqual(normalmap.exports(mutated,'VS'),normalmap.VS_EXPORTS)
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        slot, cond = next(iter(normalmap.PS_PREDICATED.items()))
        mutated[slot]['fields']['predicate_condition'] = not cond
        self.assertNotEqual(normalmap.predication(mutated),normalmap.PS_PREDICATED)
        mutated = copy.deepcopy(self.inventory['PS']['rows'])
        first = normalmap.PS_PREDICATE_SOURCES[0]
        mutated[first]['fields']['scalar_opcode'] = 0
        self.assertNotEqual(normalmap.predicate_sources(mutated),
                            normalmap.PS_PREDICATE_SOURCES)
        control = list(normalmap.CF['PS'])
        index, raw, _ = normalmap.PS_JUMPS[0]
        retargeted = list(control)
        retargeted[index] = (raw ^ 0x1,control[index][1])
        entries, _ = normalmap.schedule(retargeted,'PS')
        self.assertNotEqual(next(e['target'] for e in entries if e['op'] == 'JUMP'
                                 and e['index'] == index),
                            normalmap.PS_JUMPS[0][2])
        backward = list(control)
        backward[index] = (raw & ~0x3,control[index][1])
        with self.assertRaisesRegex(ValueError,'Unqualified normalmap jump'):
            normalmap.schedule(backward,'PS')
        flagged = list(control)
        flagged[index] = (raw | 0x8000,control[index][1])
        with self.assertRaisesRegex(ValueError,'Unqualified normalmap jump'):
            normalmap.schedule(flagged,'PS')

    def test_each_control_word_rejected_independently_of_digest(self):
        original = normalmap.hashlib.sha256
        digests = {size:digest for _,_,size,_,_,_,digest in normalmap.PROFILES}
        def pinned(data):
            if len(data) in digests:
                class Digest:
                    def hexdigest(self):return digests[len(data)]
                return Digest()
            return original(data)
        for stage,va,size,start,length,pairs,digest in normalmap.PROFILES:
            for offset in range(0,pairs*12,4):
                changed = bytearray(self.image);changed[va-normalmap.screen.BASE+start+offset+3] ^= 1
                with self.subTest(stage=stage,offset=offset),patch.object(normalmap.hashlib,'sha256',pinned):
                    with self.assertRaisesRegex(ValueError,'control flow changed'):
                        normalmap.inspect(changed)

    def test_corrupt_and_truncated_record(self):
        for stage,va,size,start,length,pairs,digest in normalmap.PROFILES:
            changed = bytearray(self.image);changed[va-normalmap.screen.BASE+start+pairs*12] ^= 1
            with self.assertRaisesRegex(ValueError,'record changed'):normalmap.inspect(changed)
            with self.assertRaisesRegex(ValueError,'record changed'):
                normalmap.inspect(self.image[:va-normalmap.screen.BASE+size-1])

if __name__ == '__main__':unittest.main()
