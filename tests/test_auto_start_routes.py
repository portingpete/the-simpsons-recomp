"""Synthetic startup routes/capture ownership; no game or controller is launched."""
from pathlib import Path
import contextlib
import io
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import auto_start_native
from auto_start_native import Replay, build_command, create_parser


def scene(presentation=11,frame=4,display='accepted',completed=1):
    return (f'[NATIVE PRESENT] copy={presentation} receipt=123 completed={completed} display={display} '
            f'scene_geometry_draws=40 frame_scene_geometry_draws={frame}\n')


class Process:
    pid=123
    returncode=None
    def poll(self):return self.returncode


class RouteTests(unittest.TestCase):
    def setUp(self):
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.folder=Path(temporary.name);(self.folder/'captures').mkdir()
        (self.folder/'game.log').write_text('', encoding="utf-8")
        self.now=0.;self.tick=lambda:None
        self.enterContext(patch('auto_start_native.time.monotonic',lambda:self.now))
        self.enterContext(patch('auto_start_native.time.sleep',self.sleep))
        self.replay=Replay(Process(),self.folder,{})
        self.addCleanup(self.replay.events.close)
        self.replay.main_menu_verified=True;self.replay.menu_presentation=10
        self.sent=[];self.after_continue='';self.verified=False
        self.replay.send=self.send
        self.replay.wait_gameplay=self.verify

    def sleep(self,seconds):
        self.now+=seconds;self.tick()

    def append(self,text):
        with (self.folder/'game.log').open('a', encoding="utf-8") as stream:stream.write(text)

    def events(self):
        return [json.loads(row) for row in (self.folder/'inputs.jsonl').read_text(encoding="utf-8").splitlines()]

    def send(self,button,screen):
        after=self.replay.log_serial
        self.sent.append((button,screen))
        self.replay.event('delivered',button=button,screen=screen)
        self.append(self.after_continue if button=='A' else '[NATIVE MOVIE SKIP] original decoder released\n')
        return after

    def verify(self):
        self.assertEqual(self.events()[-1]['event'],'sequence-complete')
        self.assertTrue(self.replay.input_sequence_completed)
        self.assertFalse(self.replay.gameplay_verified)
        self.assertIsNone(self.replay.gameplay_evidence)
        self.verified=True # Existing receipt verification was invoked, not bypassed.

    def test_real_send_baseline_excludes_events_during_neutral_delay(self):
        self.replay.next_input=.04
        sent=False;old=False
        def poll():
            nonlocal sent,old
            if not old:
                old=True;self.append('name=VideoDecodeThread;\n')
            if (self.folder/'controller.commands').exists() and not sent:
                sent=True
                self.append('local command tap buttons=1000 hold_ms=250;\n'+scene())
        self.tick=poll
        after=Replay.send(self.replay,'A','main-menu')
        route,_=self.replay.wait_continue_route(after,timeout=1)
        self.assertEqual(route,'direct-resume')
        self.assertEqual((self.folder/'controller.commands').read_bytes(),b'A_HOLD\n')

    def test_direct_resume_never_sends_start_or_claims_movie_skip(self):
        self.after_continue=scene()
        self.replay.continue_game()
        self.assertEqual(self.sent,[('A','main-menu')]);self.assertTrue(self.verified)
        events=self.events();self.assertNotIn('movie-skipped',[e['event'] for e in events])
        self.assertEqual(events[-1]['route'],'direct-resume')
        self.assertFalse(events[-1]['character_control_verified'])
        self.assertFalse(events[-1]['gameplay_verified'])
        self.assertLess(self.now,1)

    def test_occluded_direct_resume_requires_opt_in(self):
        self.after_continue=scene(display='occluded')
        with self.assertRaisesRegex(TimeoutError,'neither a new movie'):
            self.replay.continue_game()
        self.assertFalse(self.verified)
        self.replay.allow_occluded=True
        self.replay.continue_game()
        self.assertTrue(self.verified)
        route=next(event for event in self.events() if event['event']=='route')
        self.assertEqual(route['route'],'direct-resume')
        self.assertIs(route['evidence']['display_accepted'],False)

    def test_movie_retains_ready_neutral_start_release_sequence(self):
        self.after_continue=('name=VideoDecodeThread;\n[NATIVE MOVIE INPUT] ready\n'
                             '[NATIVE MOVIE INPUT] armed slot=0; neutral input observed\n')
        self.replay.continue_game()
        self.assertEqual(self.sent,[('A','main-menu'),('START','opening-level-movie')])
        self.assertTrue(self.verified);self.assertEqual(self.events()[-1]['route'],'movie')
        self.assertIn('movie-skipped',[e['event'] for e in self.events()])

    def test_new_movie_takes_precedence_in_mixed_batch(self):
        self.after_continue=scene()+'[NATIVE MOVIE INPUT] ready\nslot=0; neutral input observed\n'
        self.replay.continue_game()
        self.assertEqual(self.events()[-1]['route'],'movie')

    def test_prequeue_neutral_cannot_arm_new_movie(self):
        def delayed_send(button,screen):
            self.append('slot=0; neutral input observed\n');self.replay.update()
            return self.send(button,screen)
        self.replay.send=delayed_send
        self.after_continue='name=VideoDecodeThread;\n[NATIVE MOVIE INPUT] ready\n'
        with self.assertRaisesRegex(TimeoutError,'neutral input observed'):self.replay.continue_game()
        self.assertEqual(self.sent,[('A','main-menu')])

    def test_movie_without_neutral_poll_does_not_send_start(self):
        self.after_continue='name=VideoDecodeThread;\n[NATIVE MOVIE INPUT] ready\n'
        with self.assertRaisesRegex(TimeoutError,'neutral input observed'):self.replay.continue_game()
        self.assertEqual(self.sent,[('A','main-menu')]);self.assertFalse(self.verified)

    def test_old_movie_and_scene_signals_cannot_choose_route(self):
        self.append('name=VideoDecodeThread;\n[NATIVE MOVIE INPUT] ready\n'+scene(12))
        with self.assertRaisesRegex(TimeoutError,'neither a new movie'):self.replay.continue_game()
        self.assertFalse(self.replay.input_sequence_completed);self.assertFalse(self.verified)

    def test_neither_route_times_out(self):
        self.after_continue=scene(10)+scene(display='occluded')+scene(frame=0)+scene(frame=41)+scene(completed=0)
        with self.assertRaisesRegex(TimeoutError,'neither a new movie'):self.replay.continue_game()
        self.assertFalse(self.verified)

    def test_failures_override_scene_route(self):
        for marker in ('[FAILURE]','[AOT FAILURE]','[THREAD FAILURE]','[TERMINATE]'):
            with self.subTest(marker=marker):
                self.after_continue=scene()+marker+' test failure\n'
                with self.assertRaisesRegex(RuntimeError,'test failure'):self.replay.continue_game()
                self.assertFalse(self.verified)

    def test_controller_conflicts_still_stop_replay(self):
        for message in ('game-window keyboard buttons=1000', 'Windows controller slot=0 status=0;'):
            with self.subTest(message=message):
                self.after_continue=scene()+message+'\n'
                with self.assertRaises(RuntimeError):self.replay.continue_game()
                self.assertFalse(self.verified)

    def complete_capture(self,number):
        directory=self.folder/'captures';path=directory/f'native-frame-{number}.rgb10a2'
        path.write_bytes(bytes([number]))
        path.with_suffix('.json').write_text(json.dumps(dict(width=1280,height=720,
            format='R10G10B10A2_UNORM_LE',front_copy_completed=True,
            capture_source='completed_front_renderer_readback',presentation=number)))
        (directory/'capture.request').unlink() # Renderer completion, never Replay.

    def test_route_publication_drains_observer_request_then_owns_fresh_capture(self):
        request=self.folder/'captures/capture.request';request.write_bytes(b'observer-owned')
        completions=[]
        def renderer():
            if request.exists():
                completions.append(request.read_bytes())
                self.complete_capture(len(completions))
        self.tick=renderer
        def verify():
            self.assertEqual(self.events()[-1]['event'],'sequence-complete')
            result=self.replay.capture(timeout=1)
            self.assertEqual(result[1]['presentation'],2)
        self.replay.wait_gameplay=verify;self.after_continue=scene()
        self.replay.continue_game()
        self.assertEqual(completions,[b'observer-owned',b''])

    def test_exclusive_create_handles_observer_racing_after_route_event(self):
        request=self.folder/'captures/capture.request';original=Path.open;attempts=[]
        def opening(path,mode='r',*args,**kwargs):
            if path==request and mode=='xb':
                attempts.append(mode)
                if len(attempts)==1:
                    with original(path,'wb') as stream:stream.write(b'racing-observer')
                    raise FileExistsError(str(path))
            return original(path,mode,*args,**kwargs)
        completed=[]
        def renderer():
            if request.exists():
                completed.append(request.read_bytes());self.complete_capture(len(completed))
        self.tick=renderer
        self.replay.event('route',route='direct-resume',gameplay_verified=False)
        with patch.object(Path,'open',opening):result=self.replay.capture(timeout=1)
        self.assertEqual(completed,[b'racing-observer',b''])
        self.assertEqual(result[1]['presentation'],2)

    def test_outstanding_capture_timeout_never_deletes_request(self):
        request=self.folder/'captures/capture.request';request.write_bytes(b'observer-owned')
        with self.assertRaisesRegex(TimeoutError,'capture request ownership'):self.replay.capture(timeout=.03)
        self.assertEqual(request.read_bytes(),b'observer-owned')

    def test_failure_while_draining_preserves_request(self):
        request=self.folder/'captures/capture.request';request.write_bytes(b'observer-owned')
        self.tick=lambda:self.append('[FAILURE] capture failure\n')
        with self.assertRaisesRegex(RuntimeError,'capture failure'):self.replay.capture(timeout=1)
        self.assertEqual(request.read_bytes(),b'observer-owned')


class FrameTimingTests(unittest.TestCase):
    def test_defaults_unchanged(self):
        args=create_parser().parse_args([])
        self.assertIsNone(args.frame_timing)
        self.assertFalse(args.frame_timing_frames_only)
        command=build_command('exe','profile','content',{'profile_id':'ID'},Path('run'))
        self.assertNotIn('--frame-timing',command)
        self.assertNotIn('--frame-timing-frames-only',command)
        self.assertNotIn('--frame-rate',command) # Respect in-game saved video preferences.

    def test_frames_only_without_timing_rejected_before_folders_or_launch(self):
        root=Path(__file__).resolve().parents[1]
        folder=root/'build/automatic-startup/test-frames-only-rejected'
        if folder.exists():
            self.skipTest('Reserved run directory already exists')
        with patch('auto_start_native.subprocess.Popen',side_effect=AssertionError('must not launch')):
            with self.assertRaisesRegex(SystemExit,'2'):
                auto_start_native.main(['--frame-timing-frames-only',
                    '--run-directory',str(folder),'--until','saved-games'])
        self.assertFalse(folder.exists())

    def test_timing_flags_pass_through_to_command(self):
        command=build_command('exe','profile','content',{'profile_id':'ID'},Path('run'),
            frame_timing=Path('timing.csv'),frame_timing_frames_only=True)
        self.assertIn('--frame-timing',command)
        self.assertIn('--frame-timing-frames-only',command)
        self.assertEqual(command[command.index('--frame-timing')+1],'timing.csv')

    def test_launch_json_records_real_command_including_timing(self):
        from unittest.mock import MagicMock
        root=Path(__file__).resolve().parents[1]
        folder=root/'build/automatic-startup/test-frame-timing-launch'
        if folder.exists():
            self.skipTest('Reserved run directory already exists')
        self.addCleanup(lambda:__import__('shutil').rmtree(folder,ignore_errors=True))
        fake=MagicMock();fake.pid=4321
        replay=MagicMock()
        replay.main_menu_verified=True;replay.input_sequence_completed=True
        replay.gameplay_verified=False;replay.gameplay_evidence=None
        with contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with patch.object(Path,'is_file',return_value=True):
                with patch('auto_start_native.subprocess.Popen',return_value=fake) as launched:
                    with patch('auto_start_native.Replay',return_value=replay):
                        status=auto_start_native.main(['--run-directory',str(folder),
                            '--until','saved-games','--frame-timing','timing.csv',
                            '--frame-timing-frames-only'])
        self.assertEqual(status,0)
        launched.assert_called_once()
        launched_command=launched.call_args[0][0]
        self.assertIn('--frame-timing',launched_command)
        self.assertIn('--frame-timing-frames-only',launched_command)
        recorded=json.loads((folder/'launch.json').read_text(encoding="utf-8"))['command']
        self.assertEqual(recorded,launched_command)


if __name__=='__main__':unittest.main()
