"""Coverage joins must never turn asset presence or partial logs into proof."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit
import audit_texture_runtime_coverage as textures


def effect(pass_handle=1):
    return dict(id='effect:' + str(pass_handle), kind='effect_pass', asset_group=None,
                source='0x82006348', vertex='0x82007C1C', pixel='0x8200A02C',
                technique_handle=2, pass_handle=pass_handle, coverage=audit.coverage('declared_selection'))


def encounter(**values):
    e = dict(schema=1, event='encounter', kind='effect_pass', asset='source:82006348',
             caller='82706394', parameters='source=82006348 stride=48',
             ownership='wrapper=guest:00100000', mission='loc', last_action='attack', sequence=1)
    e.update(values)
    return e


def map_boundary(phase, sequence, mission='loc', owner='0x00100000', generation=1, package='0x00300000'):
    if phase == 'map-load-request':
        return encounter(event='lifecycle', kind='map-lifetime', caller='828998C4', sequence=sequence,
            mission=mission, asset=mission+'/'+mission+'.str',
            parameters='phase=map-load-request operation=0 managerType=821822E8 mapType=1',
            ownership='snapshot=readable managerMatchesGlobal=true recordArgumentsMatch=true registeredCallbackMatches=true qualification=map-load-request originalFieldsUnchanged=true',
            instance='manager=0x00200000 globalManager=0x00200000 dispatchManager=0x00200000 requestedPackage='+package)
    if phase == 'map-ready':
        return encounter(event='lifecycle', kind='map-lifetime', caller='823BB5D8', sequence=sequence,
            mission=mission, asset=mission+'/'+mission+'.str', parameters='phase=map-ready',
            ownership='snapshot=readable ownerMatchesGlobal=true currentPackageIsMap=true ready=1 qualification=ready',
            instance='owner='+owner+' globalOwner='+owner+' manager=0x00200000 package='+package+' map='+package+' ownerGeneration='+str(generation))
    return encounter(event='lifecycle', kind='map-lifetime', caller='823BBCE0', sequence=sequence,
        mission=mission, asset=mission+'/'+mission+'.str', parameters='phase=cleanup-complete',
        ownership='cachedReady=true globalOwnerCleared=true',
        instance='owner='+owner+' globalOwner=0x00000000 ownerGeneration='+str(generation))


class MissionSupportAuditTests(unittest.TestCase):
    def test_map_lifecycle_has_no_retained_asset_encounter_or_rejection_credit(self):
        rows = [effect()]
        events, errors = audit.parse_encounters(json.dumps(encounter(event='lifecycle',
            kind='map', asset='loc', parameters='source=82006348 vs=82007C1C ps=8200A02C')))
        report = audit.join_encounters(rows, events)
        self.assertFalse(errors)
        self.assertEqual(len(report['unlinked']), 1)
        self.assertFalse(report['failure_groups'])
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])

    def test_movie_lifecycle_exact_filename_does_not_claim_use_or_decoder_completion(self):
        row = dict(id='movies/intro.bik', kind='movie-lifetime', coverage=audit.coverage())
        events, errors = audit.parse_encounters('\n'.join(json.dumps(encounter(event='lifecycle',
            kind='movie-lifetime', asset=row['id'], caller=caller, parameters='phase='+phase,
            ownership='global_match=1 filename=bounded callback=1 decoder=1'))
            for caller,phase in ((0x828291E0,'input-ready'),(0x8282D998,'decoder-stop-request'))))
        report = audit.join_encounters([row], events)
        self.assertFalse(errors)
        self.assertEqual(len(report['unlinked']), 2)
        self.assertFalse(report['failure_groups'])
        self.assertFalse(row['coverage']['encountered_runtime'])
        self.assertFalse(row['coverage']['encountered_gameplay'])
        self.assertFalse(row['coverage']['lifecycle_tested'])

    def test_geometry_use_proves_only_exact_registered_shader_pair(self):
        rows = [effect()]
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='geometry_use',
            parameters='vs=82007C1C ps=8200A02C primitive=6 baseVertex=0')))
        report = audit.join_encounters(rows, events)
        self.assertFalse(report['unlinked'])
        self.assertTrue(rows[0]['coverage']['encountered_runtime'])
        self.assertFalse(rows[0]['coverage']['lifecycle_tested'])

    def test_screen_replacement_proves_source_only(self):
        rows = [effect()]
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='screen_replacement',
            parameters='technique=0003FFFC declaration_match=1 vertex_match=0 pixel_match=0')))
        report = audit.join_encounters(rows, events)
        self.assertEqual(len(report['encountered_sources']), 1)
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])

    def test_cached_texture_profile_preserves_all_six_words_without_asset_credit(self):
        descriptor = ['81000002', '00000053', '0003E01F', '00000D10', '00000000', '00000200']
        rows = [dict(id='texture:one', kind='texture', name='bubble_01', storage_bytes=16384,
            descriptor=descriptor, metadata_sha256='a'*64, coverage=audit.coverage())]
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='texture_binding', asset='bubble_01',
            parameters='payload_bytes=16384 descriptor=' + ','.join(descriptor), ownership='ITXD-phase=3')))
        report = audit.join_encounters(rows, events)
        self.assertEqual(report['encountered_texture_profiles'][0]['candidate_catalog_ids'], ['texture:one'])
        self.assertFalse(report['encountered_texture_profiles'][0]['packaged_occurrence_proved'])
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])
        events[0]['parameters'] = 'payload_bytes=16384 descriptor=81000002'
        self.assertFalse(audit.join_encounters(rows, events)['encountered_texture_profiles'])

    def test_identical_payloads_in_one_entry_retain_distinct_original_offsets(self):
        chunks = [dict(type_name='VFX', name='lightning.vfx', payload_sha256='a' * 64,
                       payload_size=252, payload_decoded_offset=offset) for offset in (100, 400)]
        resources, _ = audit.resource_inventory(dict(files=[dict(extension='.str', path='loc/a.str',
            sha256='b' * 64, size=1000, inspection=dict(entries=[dict(index=1, chunks=chunks)]))]))
        self.assertEqual(len({r['id'] for r in resources}), 2)
        self.assertEqual([r['payload_decoded_offset'] for r in resources], [100, 400])

    def test_source_only_does_not_mark_every_pass_or_packaged_asset_encountered(self):
        rows = [effect(1), effect(2)]
        events, errors = audit.parse_encounters(json.dumps(encounter()))
        report = audit.join_encounters(rows, events)
        self.assertFalse(errors)
        self.assertEqual(len(report['encountered_sources']), 1)
        self.assertEqual(len(report['unlinked']), 1)
        self.assertTrue(all(not r['coverage']['encountered_gameplay'] for r in rows))

    def test_exact_pair_and_handles_join_only_proved_pass(self):
        rows = [effect(1), effect(2)]
        events, _ = audit.parse_encounters(json.dumps(encounter(parameters=dict(
            source='82006348', vs='82007C1C', ps='8200A02C', technique_handle=2, pass_handle=2))))
        report = audit.join_encounters(rows, events)
        self.assertFalse(report['unlinked'])
        self.assertFalse(rows[0]['coverage']['encountered_gameplay'])
        self.assertTrue(rows[1]['coverage']['encountered_runtime'])
        self.assertFalse(rows[1]['coverage']['tested'])

    def test_shared_pair_without_handles_does_not_mark_all_techniques(self):
        rows = [effect(1), effect(2)]
        events, _ = audit.parse_encounters(json.dumps(encounter(parameters='source=82006348 vs=82007C1C ps=8200A02C')))
        self.assertEqual(len(audit.join_encounters(rows, events)['unlinked']), 1)
        self.assertTrue(all(not r['coverage']['encountered_gameplay'] for r in rows))

    def test_native_hex_handles_disambiguate_shared_pair_and_invalid_handles_do_not_join(self):
        rows = [effect(1), effect(2)]
        events, _ = audit.parse_encounters(json.dumps(encounter(parameters=
            'source=82006348 vs=82007C1C ps=8200A02C technique=00000002 pass=00000002 context=00000008')))
        self.assertFalse(audit.join_encounters(rows, events)['unlinked'])
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])
        self.assertTrue(rows[1]['coverage']['encountered_runtime'])
        rows = [effect(1)]
        events, _ = audit.parse_encounters(json.dumps(encounter(parameters=
            'source=82006348 vs=82007C1C ps=8200A02C technique=00000003 pass=00000001')))
        self.assertEqual(len(audit.join_encounters(rows, events)['unlinked']), 1)
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])

    def test_shared_payload_requires_archive_and_entry_not_mission_guess(self):
        rows = [dict(id='vfx:' + source, kind='vfx', source=source, entry=1,
                     payload_sha256='a' * 64, coverage=audit.coverage()) for source in ('loc/a.str', 'brt/a.str')]
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='vfx', parameters=dict(payload_sha256='a' * 64))))
        self.assertEqual(len(audit.join_encounters(rows, events)['unlinked']), 1)
        self.assertTrue(all(not r['coverage']['encountered_gameplay'] for r in rows))
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='vfx', parameters=dict(
            payload_sha256='a' * 64, archive='brt/a.str', entry=1))))
        self.assertFalse(audit.join_encounters(rows, events)['unlinked'])
        self.assertTrue(rows[1]['coverage']['encountered_runtime'])

    def test_unique_payload_rejects_contradictory_provenance(self):
        for parameter, value in (('archive', 'brt/a.str'), ('entry', 2), ('payload_decoded_offset', 17)):
            row = dict(id='vfx:one', kind='vfx', source='loc/a.str', entry=1,
                       payload_decoded_offset=16, payload_sha256='a' * 64, coverage=audit.coverage())
            parameters = dict(payload_sha256='a' * 64, archive='loc/a.str', entry=1,
                              payload_decoded_offset=16)
            parameters[parameter] = value
            events, _ = audit.parse_encounters(json.dumps(encounter(kind='vfx', parameters=parameters)))
            self.assertEqual(len(audit.join_encounters([row], events)['unlinked']), 1)
            self.assertFalse(row['coverage']['encountered_runtime'])

    def test_bad_telemetry_line_does_not_hide_later_events(self):
        raw = '\n'.join(('not json', '[]', json.dumps(encounter()), json.dumps(dict(schema=1))))
        events, errors = audit.parse_encounters(raw)
        self.assertEqual(len(events), 1)
        self.assertEqual([e['line'] for e in errors], [1, 2, 4])

    def test_unknown_mission_is_runtime_evidence_not_gameplay_attribution(self):
        rows = [effect()]
        events, _ = audit.parse_encounters(json.dumps(encounter(mission='unknown', parameters=dict(
            source='82006348', vs='82007C1C', ps='8200A02C'))))
        audit.join_encounters(rows, events)
        self.assertTrue(rows[0]['coverage']['encountered_runtime'])
        self.assertFalse(rows[0]['coverage']['encountered_gameplay'])

    def test_map_request_ready_retirement_and_reused_owner_generation(self):
        rows = [effect(i) for i in range(1, 7)]
        raw = [map_boundary('map-load-request', 1), encounter(asset=rows[0]['id'], sequence=2),
            map_boundary('map-ready', 3, generation=7), encounter(asset=rows[1]['id'], sequence=4),
            map_boundary('cleanup-complete', 5, generation=7), encounter(asset=rows[2]['id'], sequence=6),
            map_boundary('map-load-request', 7, mission='brt', package='0x00400000'),
            encounter(asset=rows[3]['id'], sequence=8, mission='brt'),
            map_boundary('map-ready', 9, mission='brt', generation=8, package='0x00400000'),
            encounter(asset=rows[4]['id'], sequence=10, mission='brt'),
            encounter(asset=rows[5]['id'], sequence=11)]
        # File emission can interleave; original sequence orders the boundaries.
        events, errors = audit.parse_encounters('\n'.join(json.dumps(e) for e in reversed(raw)))
        report = audit.join_encounters(rows, events)
        self.assertFalse(errors)
        self.assertEqual([r['coverage']['encountered_gameplay'] for r in rows], [False, True, False, False, True, False])
        self.assertTrue(all(r['coverage']['encountered_runtime'] for r in rows))
        by_sequence = {e['sequence']: e for e in report['events']}
        self.assertEqual(by_sequence[2]['mission_attribution']['scope'], 'map_request')
        self.assertEqual(by_sequence[6]['mission_attribution']['scope'], 'post_retirement_unqualified')
        self.assertEqual(by_sequence[6]['mission'], 'loc')
        self.assertEqual(by_sequence[10]['mission_attribution']['active_owner']['generation'], '8')
        self.assertEqual(by_sequence[11]['mission'], 'loc')

    def test_unqualified_ready_and_foreign_retirement_do_not_reuse_old_owner(self):
        rows = [effect(i) for i in range(1, 4)]
        raw = [map_boundary('map-load-request', 1),
            map_boundary('map-ready', 2, package='0x00400000'), encounter(asset=rows[0]['id'], sequence=3),
            map_boundary('map-ready', 4), encounter(asset=rows[1]['id'], sequence=5),
            map_boundary('cleanup-complete', 6, generation=99), encounter(asset=rows[2]['id'], sequence=7)]
        events, _ = audit.parse_encounters('\n'.join(json.dumps(e) for e in raw))
        audit.join_encounters(rows, events)
        self.assertEqual([r['coverage']['encountered_gameplay'] for r in rows], [False, True, False])
        self.assertEqual(events[-1]['mission_attribution']['scope'], 'unqualified_owner_retirement')

    def test_legacy_mission_label_and_other_log_never_borrow_an_active_owner(self):
        rows = [effect(1), effect(2)]
        raw = [map_boundary('map-ready', 1), encounter(asset=rows[0]['id'], sequence=2),
               encounter(asset=rows[1]['id'], sequence=3)]
        events, _ = audit.parse_encounters('\n'.join(json.dumps(e) for e in raw))
        events[0]['log_path'] = events[1]['log_path'] = 'owned-map.jsonl'
        events[2]['log_path'] = 'legacy.jsonl'
        audit.join_encounters(rows, events)
        self.assertEqual([r['coverage']['encountered_gameplay'] for r in rows], [True, False])
        self.assertEqual(events[2]['mission_attribution']['scope'], 'legacy_label_only')
        self.assertEqual(events[2]['mission'], 'loc')

    def test_malformed_context_types_are_retained_and_later_events_execute(self):
        raw = '\n'.join(json.dumps(e) for e in (encounter(asset=[]), encounter(sequence=True), encounter()))
        events, errors = audit.parse_encounters(raw)
        self.assertEqual(len(events), 1)
        self.assertEqual([e['line'] for e in errors], [1, 2])

    def test_native_raw_unsigned_caller_is_valid_and_invalid_caller_is_retained(self):
        raw = '\n'.join(json.dumps(encounter(caller=c)) for c in (2184457356, -1, True, 0xffffffff + 1))
        events, errors = audit.parse_encounters(raw)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]['caller'], 2184457356)
        self.assertEqual([e['line'] for e in errors], [2, 3, 4])

    def test_shutdown_context_never_grants_asset_or_rejection_credit(self):
        rows = [effect()]
        events, errors = audit.parse_encounters(json.dumps(encounter(event='shutdown', reason='Native window closed',
            parameters='source=82006348 vs=82007C1C ps=8200A02C')))
        report = audit.join_encounters(rows, events)
        self.assertFalse(errors)
        self.assertFalse(report['failure_groups'])
        self.assertFalse(report['encountered_sources'])
        self.assertFalse(report['encountered_audio_cues'])
        self.assertFalse(rows[0]['coverage']['encountered_runtime'])

    def test_unvalidated_producer_candidate_retains_failure_without_pass_credit(self):
        row = effect()
        event = encounter(kind='effect_producer_entry', asset='source:82006348',
            parameters='source=82006348 requested_technique=00000001 candidate_vs=82007C1C candidate_ps=8200A02C candidate_pass=00000001 selection_state=cached-candidate-before-validation',
            ownership='cached_candidates=1 admission=unvalidated', instance='r3=FFFFFFFC typed_identity=00500001')
        failure = dict(event, event='failure', reason='Original camera differs', sequence=2)
        events, errors = audit.parse_encounters('\n'.join(json.dumps(e) for e in (event, failure)))
        report = audit.join_encounters([row], events)
        self.assertFalse(errors)
        self.assertEqual(len(report['unlinked']), 2)
        self.assertFalse(report['encountered_sources'])
        self.assertFalse(row['coverage']['encountered_runtime'])
        group = next(iter(report['failure_groups'].values()))
        self.assertEqual(group['first']['instance'], 'r3=FFFFFFFC typed_identity=00500001')

    def test_direct_particle_sdk_pair_is_distinct_from_registered_fx(self):
        pair = dict(vertex='0x821570E0', pixel='0x82156770', vertex_policy='ordinary', dual=False, projected=False)
        rows = audit.direct_particle_inventory(dict(direct_particles=dict(matrix=[pair])))
        self.assertEqual(rows[0]['kind'], 'particle_sdk_pass')
        self.assertFalse(rows[0]['coverage']['tested'])
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='particle_sdk_pass',
            asset='direct SDK', parameters='vs=821570E0 ps=82156770')))
        self.assertFalse(audit.join_encounters(rows, events)['unlinked'])
        self.assertTrue(rows[0]['coverage']['encountered_runtime'])

    def test_cached_resident_identity_matches_exact_cue_and_header_hash(self):
        cue = dict(id='cue:resident', kind='sound_cue', catalog='resident', source='loc/a.str', entry=2,
            bank='bank.sbk', header_offset=123, header_hex='0300bb800000189a', payload_sha256='a' * 64,
            coverage=audit.coverage())
        event = encounter(kind='audio-source', asset='EAAC:0300bb800000189a bank=loc/a.str name=bank.sbk headerOffset=123',
            caller=2184457356, parameters='fresh=1 control=0 payload_sha256=' + 'a' * 64,
            ownership='registeredMember=1 constructed=1 residentBank=1')
        events, errors = audit.parse_encounters(json.dumps(event))
        self.assertFalse(errors)
        report = audit.join_encounters([cue], events)
        self.assertFalse(report['unlinked'])
        self.assertTrue(cue['coverage']['encountered_runtime'])
        cue['coverage'] = audit.coverage()
        event['parameters'] = 'payload_sha256=' + 'b' * 64
        events, _ = audit.parse_encounters(json.dumps(event))
        self.assertEqual(len(audit.join_encounters([cue], events)['unlinked']), 1)
        self.assertFalse(cue['coverage']['encountered_runtime'])

    def test_amx_payload_identity_preserves_ambiguous_packaged_occurrences(self):
        rows = [dict(id='cue:' + archive, kind='sound_cue', catalog='amx', source=archive, entry=1,
            bank='char_sa_bart.amx', header_offset=78572, header_hex='0300bb802008e3b4', payload_sha256='a' * 64,
            coverage=audit.coverage()) for archive in ('loc/a.str', 'brt/a.str')]
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='audio-source',
            asset='EAAC:0300bb802008e3b4 amx=char_sa_bart.amx headerOffset=78572',
            ownership='registeredMember=1 constructed=1 amxOwner=1')))
        report = audit.join_encounters(rows, events)
        self.assertEqual(len(report['unlinked']), 1)
        self.assertEqual(len(report['encountered_audio_cues'][0]['candidate_catalog_ids']), 2)
        self.assertFalse(report['encountered_audio_cues'][0]['packaged_occurrence_proved'])
        self.assertTrue(all(not r['coverage']['encountered_runtime'] for r in rows))

    def test_cached_audio_provenance_is_resolved_before_occurrence_credit(self):
        rows = [dict(id='cue:' + str(entry), kind='sound_cue', catalog='resident', source='loc/a.str', entry=entry,
            bank='bank.sbk', header_offset=123, header_hex='0300bb800000189a', payload_sha256='a' * 64,
            coverage=audit.coverage()) for entry in (1, 2)]
        event = encounter(kind='audio-source',
            asset='EAAC:0300bb800000189a bank=loc/a.str name=bank.sbk headerOffset=123',
            parameters='payload_sha256=' + 'a' * 64 + ' archive=loc/a.str entry=2',
            ownership='registeredMember=1 constructed=1 residentBank=1')
        events, _ = audit.parse_encounters(json.dumps(event))
        report = audit.join_encounters(rows, events)
        self.assertFalse(report['unlinked'])
        identity = report['encountered_audio_cues'][0]
        self.assertEqual(identity['candidate_catalog_ids'], ['cue:1', 'cue:2'])
        self.assertEqual(identity['resolved_catalog_ids'], ['cue:2'])
        self.assertTrue(identity['packaged_occurrence_proved'])
        self.assertTrue(rows[1]['coverage']['encountered_runtime'])
        rows[1]['coverage'] = audit.coverage()
        event['parameters'] = 'payload_sha256=' + 'a' * 64 + ' archive=brt/a.str entry=2'
        events, _ = audit.parse_encounters(json.dumps(event))
        report = audit.join_encounters([rows[1]], events)
        self.assertEqual(len(report['unlinked']), 1)
        self.assertFalse(report['encountered_audio_cues'][0]['packaged_occurrence_proved'])
        self.assertEqual(report['encountered_audio_cues'][0]['resolved_catalog_ids'], [])
        self.assertFalse(rows[1]['coverage']['encountered_runtime'])

    def test_unregistered_audio_owner_and_header_only_stream_do_not_prove_catalog_owner(self):
        row = dict(id='stream:one', kind='sound_stream', header_hex='0300bb806041eb00', coverage=audit.coverage())
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='audio-source',
            asset='EAAC:0300bb806041eb00', ownership='registeredMember=1 constructed=1 encodedStream=1')))
        report = audit.join_encounters([row], events)
        self.assertEqual(report['encountered_audio_cues'][0]['candidate_catalog_ids'], ['stream:one'])
        self.assertFalse(row['coverage']['encountered_runtime'])
        events, _ = audit.parse_encounters(json.dumps(encounter(kind='audio-source',
            asset='EAAC:0300bb806041eb00', ownership='registeredMember=0 constructed=0 encodedStream=1')))
        self.assertFalse(audit.join_encounters([row], events)['encountered_audio_cues'])

    def test_failure_signature_retains_context_but_ignores_sequence(self):
        raw = '\n'.join(json.dumps(encounter(event='failure', sequence=i, last_action=action))
                        for i, action in ((1, 'attack'), (2, 'attack'), (3, 'checkpoint_reload')))
        events, _ = audit.parse_encounters(raw)
        groups = audit.join_encounters([effect()], events)['failure_groups']
        self.assertEqual(sorted(g['count'] for g in groups.values()), [1, 2])
        self.assertFalse(effect()['coverage']['lifecycle_tested'])

    def test_test_receipt_requires_proof_hash_and_complete_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            proof = root / 'proof.json'
            proof.write_text('{"result":"passed"}')
            rows = [effect()]
            receipt = dict(result='passed', catalog_id=rows[0]['id'], phases=['create'],
                           proof=dict(path='proof.json', sha256=audit.sha(proof.read_bytes())))
            audit.join_test_evidence(rows, [receipt], root)
            self.assertTrue(rows[0]['coverage']['tested'])
            self.assertFalse(rows[0]['coverage']['lifecycle_tested'])
            receipt['phases'] = ['create', 'use', 'release', 'malformed']
            audit.join_test_evidence(rows, [receipt], root)
            self.assertTrue(rows[0]['coverage']['lifecycle_tested'])
            self.assertTrue(rows[0]['coverage']['malformed_inputs_rejected'])
            proof.write_text('changed')
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                audit.join_test_evidence(rows, [receipt], root)

    def test_setup_receipt_does_not_claim_draw_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); proof = root / 'setup.log'; proof.write_text('passed original setup')
            rows = [effect()]
            receipt = dict(result='passed', scope='setup', catalog_id=rows[0]['id'],
                phases=['create', 'query', 'metadata_use', 'release', 'malformed'],
                proof=dict(path='setup.log', sha256=audit.sha(proof.read_bytes())))
            audit.join_test_evidence(rows, [receipt], root)
            self.assertTrue(rows[0]['coverage']['setup_tested'])
            self.assertFalse(rows[0]['coverage']['lifecycle_tested'])
            self.assertTrue(rows[0]['coverage']['malformed_inputs_rejected'])

    def test_partial_draw_receipts_do_not_combine_into_a_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); proof = root / 'draw.log'; proof.write_text('passed isolated phases')
            rows = [effect()]
            reference = dict(path='draw.log', sha256=audit.sha(proof.read_bytes()))
            audit.join_test_evidence(rows, [dict(result='passed', scope='draw', catalog_id=rows[0]['id'],
                phases=phases, proof=reference) for phases in (['create', 'use'], ['release'])], root)
            self.assertTrue(rows[0]['coverage']['draw_tested'])
            self.assertFalse(rows[0]['coverage']['lifecycle_tested'])

    def test_audio_lifecycle_receipt_does_not_claim_drawing_or_gameplay(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); proof = root/'audio.log'; proof.write_text('passed original PCM lifecycle')
            row = dict(id='sound_stream:exact', kind='sound_stream', coverage=audit.coverage())
            receipt = dict(result='passed', scope='lifecycle', catalog_id=row['id'],
                use_kind='pcm_mixer_service',
                phases=['create', 'use', 'release'],
                proof=dict(path='audio.log', sha256=audit.sha(proof.read_bytes())))
            audit.join_test_evidence([row], [receipt], root)
            self.assertTrue(row['coverage']['tested'])
            self.assertTrue(row['coverage']['lifecycle_tested'])
            self.assertFalse(row['coverage']['draw_tested'])
            self.assertFalse(row['coverage']['setup_tested'])
            self.assertFalse(row['coverage']['malformed_inputs_rejected'])
            self.assertFalse(row['coverage']['encountered_gameplay'])
            self.assertEqual(row['coverage']['use_kind'], 'pcm_mixer_service')
            row['coverage'] = audit.coverage()
            audit.join_test_evidence([row], [dict(receipt, phases=phases)
                for phases in (['create', 'use'], ['release'])], root)
            self.assertFalse(row['coverage']['lifecycle_tested'])
            bad = dict(receipt, use_kind='rendering')
            with self.assertRaisesRegex(ValueError, 'use kind'):
                audit.join_test_evidence([row], [bad], root)
            with self.assertRaisesRegex(ValueError, 'use kind'):
                audit.join_test_evidence([effect()], [dict(receipt, catalog_id=effect()['id'])], root)

    def test_receipt_build_inputs_are_revalidated_when_joined(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); proof = root / 'draw.log'; proof.write_text('passed')
            fixture = root / 'fixture.exe'; fixture.write_bytes(b'tested binary')
            receipt = dict(result='passed', catalog_id='effect:1', phases=['create', 'use', 'release'],
                proof=dict(path='draw.log', sha256=audit.sha(proof.read_bytes())),
                inputs=[dict(path='fixture.exe', sha256=audit.sha(fixture.read_bytes()))])
            audit.join_test_evidence([effect()], [receipt], root)
            fixture.write_bytes(b'changed build')
            with self.assertRaisesRegex(ValueError, 'Test input identity changed'):
                audit.join_test_evidence([effect()], [receipt], root)

    def test_synthetic_geometry_cannot_claim_a_packaged_mission_or_duplicate_identity(self):
        rows = [effect()]
        synthetic = dict(id='geometry:test', kind='geometry_regression', synthetic=True, asset_group=None)
        audit.add_synthetic_rows(rows, [synthetic])
        self.assertEqual(rows[-1]['coverage']['implemented'], 'declared_input_profile')
        self.assertFalse(rows[-1]['coverage']['encountered_runtime'])
        with self.assertRaisesRegex(ValueError, 'Duplicate synthetic'):
            audit.add_synthetic_rows(rows, [synthetic])
        with self.assertRaisesRegex(ValueError, 'Unsupported synthetic'):
            audit.add_synthetic_rows([], [dict(synthetic, asset_group='loc')])

    def test_dictionary_rejection_keeps_identity_and_later_cases_execute(self):
        entries = [(dict(index=i), dict(name=str(i), payload_sha256=str(i))) for i in range(3)]
        record = dict(name='valid', metadata_sha256='b' * 64)
        with patch.object(textures, 'original_records', side_effect=[ValueError('bad first'),
                          ([record], 256, 16), ValueError('bad last')]) as producer, \
             patch.object(textures, 'admission', return_value='admitted_metadata'):
            records, dictionaries, failures, expanded, consumed = textures.dictionary_cases(
                b'original', entries, 'loc/a.str', {})
        self.assertEqual(producer.call_count, 3)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]['entry'], 1)
        self.assertEqual([r['entry'] for r in failures], [0, 2])
        self.assertEqual((expanded, consumed), (256, 16))
        self.assertEqual(len(dictionaries), 3)

    def test_failed_shared_payload_is_retried_not_cached_as_success(self):
        entries = [(dict(index=i), dict(name=str(i), payload_sha256='same')) for i in range(2)]
        cache = {}
        with patch.object(textures, 'original_records', side_effect=ValueError('malformed')) as producer:
            _, _, failures, _, _ = textures.dictionary_cases(b'original', entries, 'loc/a.str', cache)
        self.assertEqual(producer.call_count, 2)
        self.assertEqual(len(failures), 2)
        self.assertFalse(cache)


if __name__ == '__main__':
    unittest.main()
