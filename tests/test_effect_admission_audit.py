"""Original-corpus admission inventory and strict association regression checks."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_effect_admission as audit


class EffectAdmissionAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image,cls.catalogs=audit.load_original()
        cls.compiler=(ROOT/'renderer/screen_pipeline.cpp').read_text()
        cls.artifacts=audit.artifact_inventory(cls.compiler)
        cls.selections=audit.declared_selection()

    def report(self,catalogs=None,artifacts=None,observed=None):
        return audit.build_report(catalogs or self.catalogs,self.image,
                                  self.artifacts if artifacts is None else artifacts,
                                  self.selections,observed or {})

    def test_all_original_passes_and_materials_covered(self):
        report=self.report();summary=report['summary']
        self.assertEqual((summary['effects'],summary['passes'],summary['registered_shaders'],summary['immutable_materials']),
                         (49,110,215,256))
        self.assertEqual(summary['admitted_passes']+summary['missing_artifact_passes'],110)
        self.assertEqual(summary['native_artifacts']+len(report['unsupported_materials']),256)
        self.assertEqual(len({(r['source'],r['technique_handle'],r['pass_handle'])for r in report['passes']}),110)
        self.assertEqual(summary['observed_passes'],0)

    def test_new_source_passes_are_declared_but_not_observed(self):
        pairs=((0x8205DABC,0x8205E5E0),(0x8205E0B8,0x8205EED4),
               (0x82051EEC,0x82052610),(0x82052358,0x82052DF4),
               (0x8202579C,0x82027C18),(0x820269E8,0x82028258),
               (0x8203D93C,0x82040118),(0x8203ED38,0x82040510),
               (0x8204BA4C,0x8204E2DC),(0x8204CE90,0x8204E75C))
        report=self.report()
        for pair in pairs:
            self.assertIn(pair,self.selections)
            row=next(r for r in report['passes']if (int(r['vertex'],16),int(r['pixel'],16))==pair)
            self.assertEqual(row['admission_status'],'declared_with_artifacts')
            self.assertIsNone(row['observed'])
        for source in ('0x820312E8','0x82032978'):
            row=next(r for r in report['passes']if r['source']==source)
            self.assertEqual(row['admission_status'],'missing_artifacts')
            self.assertIsNone(row['runtime_selection'])

    def test_reachability_is_independent_of_artifact_presence(self):
        log='[NATIVE RIGID DRAW] vs=8200D3AC ps=8200D9E8\n[untrusted] vs=8200D3AC ps=8200D9E8\n'
        observed=audit.observed_pairs(log);self.assertEqual(len(observed),1)
        report=self.report(observed=observed)
        row=next(r for r in report['passes']if r['vertex']=='0x8200D3AC')
        self.assertEqual(row['admission_status'],'observed_with_artifacts')
        self.assertEqual(row['observed']['line'],1)
        missing=next(r for r in report['passes']if r['missing_native_artifacts'])
        self.assertIsNone(missing['observed']);self.assertEqual(missing['admission_status'],'missing_artifacts')

    def test_mono_selection_does_not_claim_observed_alpha_or_zprepass_alpha(self):
        self.assertIn((0x82120C04,0x82122BD4),self.selections)
        self.assertIn((0x82121BE8,0x82122D38),self.selections)
        self.assertFalse(self.selections[(0x82121BE8,0x82122D38)]['native_lifecycle_tested'])
        alpha=next(r for r in self.report()['passes']if r['vertex']=='0x82121BE8')
        self.assertIsNone(alpha['observed'])
        self.assertEqual(alpha['admission_status'],'declared_with_artifacts')
        self.assertIn((0x8214A8A4,0),self.selections)
        self.assertNotIn((0x8214B9B8,0),self.selections)
        self.assertIn((0x8205BE70,0x8205C100),self.selections)

    def test_mono_alpha_literals_alone_do_not_add_selector_admission(self):
        old='0x82120C04 0x82122BD4 passes.front() !c.r31.u32; // rejected 0x82121BE8 0x82122D38'
        self.assertEqual(audit.mono_declared_pairs(old),((0x82120C04,0x82122BD4),))
        self.assertEqual(audit.mono_declared_pairs(old+' c.r31.u32&255; c.r5.u32==technique; passes[alpha?1u:0u]'),((0x82120C04,0x82122BD4),))
        selected=old+'\n c.r31.u32&255; c.r5.u32==technique; passes[alpha?1u:0u] 0x82121BE8 0x82122D38'
        self.assertEqual(audit.mono_declared_pairs(selected),((0x82120C04,0x82122BD4),(0x82121BE8,0x82122D38)))
        with self.assertRaisesRegex(ValueError,'opaque guard'):audit.mono_declared_pairs(selected.replace('0x82120C04','0x82120C08'))

    def test_malformed_pass_and_artifact_associations_are_rejected(self):
        catalogs=deepcopy(self.catalogs)
        p=catalogs[1]['rows'][1]['techniques'][0]['passes'][0]
        p['shaders']['vertex']['shader_sha256']=p['shaders']['pixel']['shader_sha256']
        with self.assertRaisesRegex(ValueError,'correct stage'):self.report(catalogs=catalogs)
        artifacts=deepcopy(self.artifacts);artifacts[0x8200D3AC]['stage']='pixel'
        with self.assertRaisesRegex(ValueError,'stage differs'):self.report(artifacts=artifacts)
        artifacts=deepcopy(self.artifacts);del artifacts[0x8200D3AC]
        with self.assertRaisesRegex(ValueError,'independent pinned'):self.report(artifacts=artifacts)

    def test_native_case_requires_real_creation_and_unique_identity(self):
        with self.assertRaisesRegex(ValueError,'Duplicate'):audit.artifact_inventory(
            self.compiler.replace('case 0x8200D3AC:', 'case 0x8200D3AC: case 0x8200D3AC:',1))
        with self.assertRaisesRegex(ValueError,'actual shader creation'):audit.artifact_inventory(
            'NativeBackend::createMaterialArtifact( case 0x8200D3AC: default: void NativeBackend::validateEdgeShaders(')

    def test_instruction_matches_are_review_candidates_only(self):
        report=self.report()
        for candidate in report['instruction_equivalence_review_candidates']:
            self.assertIn('no runtime admission or alias',candidate['qualification'])
            self.assertNotIn(int(candidate['shader'],16),self.artifacts)
            self.assertTrue(all(int(a,16)in self.artifacts for a in candidate['candidate_artifacts']))

    def test_direct_particle_registry_matrix_is_separate_from_reachability(self):
        report=audit.direct_particle_scope(self.image)
        self.assertEqual(len(report['matrix']),8)
        self.assertEqual(len({(row['vertex'],row['pixel']) for row in report['matrix']}),8)
        self.assertEqual(sum(row['dual'] and row['projected'] for row in report['matrix']),2)
        self.assertTrue(all(row['observed'] is None for row in report['matrix']))
        self.assertEqual(report['logical_to_native_texture_stages'],
                         {'base':0,'secondary':3,'depth':2,'packed_destination':1})
        self.assertIn('nonzero original r29 special path',report['guarded'])
        self.assertNotIn('requested projection without a live original projector',report['guarded'])
        fallback=report['null_projector_fallback']
        self.assertEqual(fallback['pixel_records'],['0x82156770','0x82156948'])
        self.assertEqual(fallback['consumed_exports'],['SV_POSITION','TEX0','TEX2'])
        self.assertEqual(fallback['proof_sha256'],audit.sha((ROOT/fallback['proof']).read_bytes()))
        self.assertIn('No shadow owner is invented',fallback['qualification'])


if __name__=='__main__':unittest.main()
