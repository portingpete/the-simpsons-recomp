"""Synthetic receipt/replay tests; passing them is NOT gameplay verification."""
from pathlib import Path
import hashlib
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gameplay_evidence import EvidenceError, verify_gameplay_frame
from auto_start_native import Replay

SIZE=1280*720*4
BLACK=bytes(SIZE)
OPAQUE_BLACK=struct.pack('<I',0xC0000000)*(1280*720)
VISIBLE=struct.pack('<I',1)+BLACK[4:]


def metadata(**changes):
    return {'width':1280,'height':720,'format':'R10G10B10A2_UNORM_LE',
            'capture_source':'completed_front_renderer_readback','front_copy_completed':True,
            'display_accepted':True,'presentation':1203,'draws':1000,
            'rigid_mesh_draws':100,'skin_mesh_draws':5,'sky_mesh_draws':1,
            'scene_geometry_draws':106,'frame_scene_geometry_draws':3,**changes}


class GameplayEvidenceTests(unittest.TestCase):
    def verify(self,data=VISIBLE,meta=None):
        return verify_gameplay_frame(data,metadata() if meta is None else meta,minimum_presentation=1202)

    def test_accepts_complete_visible_scene_receipt(self):
        evidence=self.verify()
        self.assertEqual(evidence['nonzero_rgb_pixels'],1)
        self.assertEqual(evidence['frame_scene_geometry_draws'],3)
        self.assertEqual(evidence['pixel_sha256'],hashlib.sha256(VISIBLE).hexdigest())

    def test_all_rgb_channels_are_considered(self):
        for word in (1,1<<10,1<<20):
            with self.subTest(word=word):
                self.assertEqual(self.verify(struct.pack('<I',word)+BLACK[4:])['nonzero_rgb_pixels'],1)

    def test_black_is_not_visible(self):
        with self.assertRaisesRegex(EvidenceError,'nonzero RGB'):self.verify(BLACK)

    def test_opaque_alpha_does_not_make_black_visible(self):
        with self.assertRaisesRegex(EvidenceError,'nonzero RGB'):self.verify(OPAQUE_BLACK)

    def test_exact_size_required(self):
        for data in (b'',VISIBLE[:-1],VISIBLE+b'\x00'):
            with self.subTest(size=len(data)),self.assertRaises(EvidenceError):self.verify(data)

    def test_dimensions_and_format_required(self):
        for change in ({'width':640},{'height':360},{'format':'RGBA8'},{'width':True}):
            with self.subTest(change=change),self.assertRaises(EvidenceError):self.verify(meta=metadata(**change))

    def test_private_movie_target_is_not_front(self):
        with self.assertRaises(EvidenceError):self.verify(meta=metadata(capture_source='private_movie_target_renderer_readback'))

    def test_occluded_or_incomplete_present_rejected(self):
        for field in ('front_copy_completed','display_accepted'):
            for value in (False,0,1,'true',None):
                with self.subTest(field=field,value=value),self.assertRaises(EvidenceError):
                    self.verify(meta=metadata(**{field:value}))

    def test_occluded_front_scene_requires_opt_in_and_reports_false(self):
        occluded=metadata(display_accepted=False)
        with self.assertRaisesRegex(EvidenceError,'not accepted'):
            self.verify(meta=occluded)
        evidence=verify_gameplay_frame(VISIBLE,occluded,minimum_presentation=1202,
                                       allow_occluded=True)
        self.assertIs(evidence['display_accepted'],False)
        self.assertEqual(evidence['frame_scene_geometry_draws'],3)
        for change in ({'front_copy_completed':False},{'frame_scene_geometry_draws':0},
                       {'capture_source':'private_movie_target_renderer_readback'},
                       {'display_accepted':0}):
            with self.subTest(change=change),self.assertRaises(EvidenceError):
                verify_gameplay_frame(VISIBLE,metadata(**{'display_accepted':False,**change}),
                                      minimum_presentation=1202,allow_occluded=True)

    def test_presentation_must_be_newer_than_menu(self):
        for value in (0,1201,1202):
            with self.subTest(value=value),self.assertRaises(EvidenceError):self.verify(meta=metadata(presentation=value))

    def test_invalid_presentation_boundary_rejected(self):
        for boundary in (-1,True,'1202',None):
            with self.subTest(boundary=boundary),self.assertRaises(EvidenceError):
                verify_gameplay_frame(VISIBLE,metadata(),minimum_presentation=boundary)

    def test_cumulative_scene_draws_do_not_qualify_current_frame(self):
        with self.assertRaisesRegex(EvidenceError,'no new scene'):self.verify(meta=metadata(frame_scene_geometry_draws=0))

    def test_menu_movie_or_shadow_draws_alone_do_not_qualify(self):
        with self.assertRaises(EvidenceError):
            self.verify(meta=metadata(rigid_mesh_draws=0,skin_mesh_draws=0,sky_mesh_draws=0,
                scene_geometry_draws=0,frame_scene_geometry_draws=0,im2d_draws=10000,movie_draws=10000,shadow_mesh_draws=100))

    def test_frame_delta_cannot_exceed_total(self):
        with self.assertRaises(EvidenceError):self.verify(meta=metadata(frame_scene_geometry_draws=107))

    def test_counter_consistency_required(self):
        for change in ({'scene_geometry_draws':105},{'rigid_mesh_draws':99},{'draws':105}):
            with self.subTest(change=change),self.assertRaises(EvidenceError):self.verify(meta=metadata(**change))

    def test_missing_counters_fail_closed(self):
        for field in metadata():
            meta=metadata();del meta[field]
            with self.subTest(field=field),self.assertRaises(EvidenceError):self.verify(meta=meta)

    def test_invalid_numeric_types_fail_closed(self):
        for field in ('draws','presentation','scene_geometry_draws','frame_scene_geometry_draws','rigid_mesh_draws','skin_mesh_draws','sky_mesh_draws'):
            for value in (-1,True,'1',1.0,None):
                with self.subTest(field=field,value=value),self.assertRaises(EvidenceError):
                    self.verify(meta=metadata(**{field:value}))

    def test_non_object_metadata_rejected(self):
        for meta in ([],None,'{}'):
            with self.subTest(meta=meta),self.assertRaises(EvidenceError):
                verify_gameplay_frame(VISIBLE,meta,minimum_presentation=1202)


class FakeProcess:
    pid=123
    returncode=None
    def poll(self):return self.returncode


class ReplayEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)
        (self.folder/'game.log').write_text('', encoding="utf-8")
        self.process=FakeProcess()
        self.replay=Replay(self.process,self.folder,{
            'damaged-save':{'samples':[[0,99,99,99]],'minimum_match':1,'tolerance':0},
            'main-menu':{'samples':[[0,42,0,0]],'minimum_match':1,'tolerance':0},
        })
        self.addCleanup(self.replay.events.close)
        self.replay.main_menu_verified=True
        self.replay.input_sequence_completed=True
        self.replay.menu_presentation=1202
        self.capture=(self.folder/'captures/native-frame-1000.rgb10a2',metadata(),VISIBLE)
        self.replay.capture=lambda **kwargs:self.capture

    def test_replay_records_evidence_only_after_valid_capture(self):
        self.now=0.0
        self.enterContext(patch('auto_start_native.time.monotonic',lambda:self.now))
        def sleep(seconds):
            self.now+=seconds
        self.enterContext(patch('auto_start_native.time.sleep',sleep))
        self.replay.started=self.now
        counter=0
        def capture(**kwargs):
            nonlocal counter
            counter+=1
            self.now+=0.4 # Sustained presentations advance the controlled clock; no real sleeping.
            return (self.capture[0],metadata(presentation=1202+counter,rigid_mesh_draws=100+3*counter,
                scene_geometry_draws=106+3*counter),VISIBLE)
        self.replay.capture=capture
        self.replay.wait_gameplay(timeout=60)
        self.assertTrue(self.replay.gameplay_verified)
        self.assertGreaterEqual(self.replay.gameplay_evidence['observed_scene_frames'],30)
        self.assertGreaterEqual(self.replay.gameplay_evidence['observation_seconds'],10)
        self.assertEqual(self.replay.gameplay_evidence['verification'],'sustained_scene_rendering')
        self.assertFalse(self.replay.gameplay_evidence['character_control_verified'])
        self.assertEqual(self.replay.first_scene_frame['presentation'],1203)
        events=[json.loads(line) for line in (self.folder/'inputs.jsonl').read_text(encoding="utf-8").splitlines()]
        self.assertTrue(events[-1]['gameplay_verified'])

    def test_single_scene_then_stalled_capture_is_not_gameplay(self):
        calls=0
        def capture(**kwargs):
            nonlocal calls
            calls+=1
            if calls>1:raise TimeoutError('Renderer capture did not complete')
            return self.capture
        self.replay.capture=capture
        with self.assertRaisesRegex(TimeoutError,'capture did not complete'):
            self.replay.wait_gameplay(timeout=3)
        self.assertIsNotNone(self.replay.first_scene_frame)
        self.assertFalse(self.replay.gameplay_verified)

    def test_repeated_capture_cannot_establish_a_running_game(self):
        with self.assertRaisesRegex(TimeoutError,'No verified gameplay'):
            self.replay.wait_gameplay(timeout=.4)
        self.assertIsNotNone(self.replay.first_scene_frame)
        self.assertFalse(self.replay.gameplay_verified)

    def test_new_presentations_with_reused_scene_counters_are_rejected(self):
        counter=0
        def capture(**kwargs):
            nonlocal counter
            counter+=1
            return self.capture[0],metadata(presentation=1202+counter),VISIBLE
        self.replay.capture=capture
        with self.assertRaisesRegex(TimeoutError,'counters did not advance'):
            self.replay.wait_gameplay(timeout=.4)
        self.assertFalse(self.replay.gameplay_verified)

    def test_input_sequence_alone_cannot_report_gameplay(self):
        self.capture=(self.capture[0],metadata(frame_scene_geometry_draws=0),VISIBLE)
        with self.assertRaisesRegex(TimeoutError,'No verified gameplay'):
            self.replay.wait_gameplay(timeout=.001)
        self.assertFalse(self.replay.gameplay_verified)

    def test_unfinished_sequence_rejected(self):
        self.replay.input_sequence_completed=False
        with self.assertRaisesRegex(RuntimeError,'startup sequence'):self.replay.wait_gameplay(timeout=.001)

    def test_known_menu_rejected_even_with_incorrect_scene_counters(self):
        self.capture=(self.capture[0],metadata(),struct.pack('<I',42)+BLACK[4:])
        with self.assertRaisesRegex(TimeoutError,'startup screen: main-menu'):self.replay.wait_gameplay(timeout=.001)
        self.assertFalse(self.replay.gameplay_verified)

    def test_damaged_save_is_failure(self):
        word=99|(99<<10)|(99<<20)
        self.capture=(self.capture[0],metadata(),struct.pack('<I',word)+BLACK[4:])
        with self.assertRaisesRegex(RuntimeError,'damaged save'):self.replay.wait_gameplay(timeout=1)
        self.assertFalse(self.replay.gameplay_verified)

    def test_status_three_is_explicit_native_exception(self):
        self.process.returncode=3
        with self.assertRaisesRegex(RuntimeError,'SEH hardware exception'):self.replay.update()

    def test_failure_markers_stop_replay(self):
        for marker in ('[FAILURE]','[AOT FAILURE]','[THREAD FAILURE]','[TERMINATE]'):
            with self.subTest(marker=marker):
                (self.folder/'game.log').write_text(marker+' failure\n', encoding="utf-8")
                self.replay.offset=0;self.replay.partial=''
                with self.assertRaisesRegex(RuntimeError,'failure'):self.replay.update()

    def test_native_failure_during_capture_cannot_be_success(self):
        def capture(**kwargs):
            (self.folder/'game.log').write_text('[FAILURE] after capture\n', encoding="utf-8")
            return self.capture
        self.replay.capture=capture
        with self.assertRaisesRegex(RuntimeError,'after capture'):self.replay.wait_gameplay(timeout=1)
        self.assertFalse(self.replay.gameplay_verified)

    def test_uploaded_frontier_log_is_failure_not_gameplay(self):
        original=next((path for path in (ROOT/'logs/reach-game-212-game.log',
            ROOT/'build/automatic-startup/reach-game-212/game.log') if path.is_file()),None)
        if original is None:self.skipTest('Optional original frontier log not present')
        (self.folder/'game.log').write_bytes(original.read_bytes())
        with self.assertRaisesRegex(RuntimeError,'Unqualified original opaque rigid fallback entry'):self.replay.update()
        self.assertFalse(self.replay.gameplay_verified)


if __name__=='__main__':unittest.main()
