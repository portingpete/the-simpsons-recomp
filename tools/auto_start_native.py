"""Launch the native game and replay recorded menu inputs against visible screen cues."""
from pathlib import Path
from collections import deque
import argparse
import csv
import datetime
import json
import math
import os
import re
import shutil
import statistics
import struct
import subprocess
import time
from gameplay_evidence import EvidenceError, verify_gameplay_frame
from replay_recorded_route import NativeWindow

ROOT=Path(__file__).resolve().parents[1]
BUTTONS={'START':'0010','A':'1000'}
STATIONARY_PRESENTATIONS=(3300,5300)
SCENE_PRESENT=re.compile(r'\[NATIVE PRESENT\] copy=(\d+).*?completed=1 display=(accepted|occluded).*?scene_geometry_draws=(\d+) frame_scene_geometry_draws=(\d+)')

def matches(data,cue):
    if len(data)!=1280*720*4:return False
    if cue.get('exclude') and matches(data,cue['exclude']):return False
    good=0
    for offset,r,g,b in cue['samples']:
        word=struct.unpack_from('<I',data,offset)[0]
        if max(abs((word&1023)-r),abs(((word>>10)&1023)-g),abs(((word>>20)&1023)-b))<=cue['tolerance']:
            good+=1
    return good>=len(cue['samples'])*cue['minimum_match']

class Replay:
    def __init__(self,process,folder,cues,allow_occluded=False,require_visible_title=False):
        self.process=process;self.folder=folder;self.cues=cues
        self.allow_occluded=allow_occluded
        self.require_visible_title=require_visible_title
        self.started=time.monotonic();self.next_input=0
        self.offset=0;self.partial='';self.receipts=[];self.lines=deque(maxlen=4096)
        self.log_serial=0;self.route_signals=deque(maxlen=4096)
        self.main_menu_verified=False;self.input_sequence_completed=False
        self.gameplay_verified=False;self.gameplay_evidence=None;self.menu_presentation=0
        self.first_scene_frame=None
        self.latest_presentation=0
        self.last_capture_presentation=0
        self.movie_serial=0;self.armed_movie_serial=0;self.skipped_intro_serial=0;self.skip_intro=False
        descriptor=os.open(folder/'inputs.jsonl',os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_BINARY',0))
        self.events=os.fdopen(descriptor,'w',encoding='utf-8',buffering=1)
        self.event('launched',pid=process.pid)

    def event(self,kind,**details):
        item={'event':kind,'seconds':round(time.monotonic()-self.started,4),**details}
        self.events.write(json.dumps(item)+'\n')
        print(json.dumps(item),flush=True)

    def update(self):
        with (self.folder/'game.log').open('rb') as stream:
            stream.seek(self.offset);new=stream.read();self.offset=stream.tell()
        text=self.partial+new.decode('utf-8',errors='replace')
        complete=text.split('\n');self.partial=complete.pop()
        for line in complete:
            self.log_serial+=1
            self.lines.append(line)
            if 'name=VideoDecodeThread;' in line or '[NATIVE MOVIE INPUT] ready' in line:
                self.route_signals.append((self.log_serial,'movie',line.strip()))
            scene=SCENE_PRESENT.search(line)
            if scene:
                presentation,display,cumulative,frame=scene.groups()
                presentation,cumulative,frame=map(int,(presentation,cumulative,frame))
                self.latest_presentation=max(self.latest_presentation,presentation)
                if 0 < frame <= cumulative and (display=='accepted' or self.allow_occluded):
                    self.route_signals.append((self.log_serial,'direct-resume',
                        {'presentation':presentation,'display_accepted':display=='accepted',
                         'scene_geometry_draws':cumulative,'frame_scene_geometry_draws':frame}))
            if '[NATIVE MOVIE INPUT] ready' in line:
                self.movie_serial+=1
            if '[NATIVE MOVIE INPUT] armed' in line and 'slot=0;' in line:
                self.armed_movie_serial=self.movie_serial
            if any(marker in line for marker in ('[FAILURE]','[AOT FAILURE]','[THREAD FAILURE]','[TERMINATE]')):
                raise RuntimeError(line.strip())
            if 'game-window keyboard buttons=' in line and 'buttons=0000' not in line:
                raise RuntimeError('Manual keyboard input detected; automatic input stopped.')
            if 'Windows controller slot=0 status=0;' in line:
                raise RuntimeError('A physical controller owns slot 0; automatic command input stopped.')
            if 'local command tap buttons=' in line:self.receipts.append(line.strip())
        if self.process.poll() is not None:
            if self.process.returncode==3:
                raise RuntimeError('Game exited with status 3: native SEH hardware exception, not a clean quit.')
            raise RuntimeError(f'Game exited with status {self.process.returncode}.')

    def skip_launch_movie(self):
        if self.skip_intro and self.movie_serial>self.skipped_intro_serial and self.armed_movie_serial==self.movie_serial:
            self.skipped_intro_serial=self.movie_serial
            self.send('START',f'launch-movie-{self.movie_serial}')

    def wait_log(self,needle,timeout=30,after=None):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            self.update()
            found=next((line for index,line in enumerate(reversed(self.lines)) if needle in line and (after is None or self.log_serial-index>after)),None)
            if found:return found
            time.sleep(.02)
        raise TimeoutError('Waiting for '+needle)

    def capture(self,timeout=30):
        directory=self.folder/'captures';request=directory/'capture.request'
        deadline=time.monotonic()+timeout
        while True:
            self.update()
            if time.monotonic()>=deadline:
                raise TimeoutError('Waiting for capture request ownership')
            try:
                with request.open('xb'):pass
                break
            except FileExistsError:
                time.sleep(.01)
                continue
        # The first rendered frame can take more than ten seconds during native
        # startup. This is a deadline, not an input delay: return as soon as the
        # renderer acknowledges its completed readback.
        while request.exists():
            self.update()
            self.skip_launch_movie()
            if time.monotonic()>deadline:raise TimeoutError('Renderer capture did not complete')
            time.sleep(.01)
        def frame_key(p):
            try:
                return int(p.stem.split('-')[-1])
            except (ValueError, IndexError):
                return -1
        candidates=[p for p in directory.glob('native-frame-*.rgb10a2') if frame_key(p)>=0]
        if not candidates:
            raise TimeoutError('No completed renderer captures found')
        latest=max(candidates,key=frame_key)
        meta=json.loads(latest.with_suffix('.json').read_text(encoding="utf-8"))
        if isinstance(meta.get('presentation'),int):
            self.last_capture_presentation=max(self.last_capture_presentation,meta['presentation'])
        if (meta['width']!=1280 or meta['height']!=720 or
            meta['format']!='R10G10B10A2_UNORM_LE' or not meta['front_copy_completed'] or
            meta['capture_source']!='completed_front_renderer_readback'):
            return None
        return latest,meta,latest.read_bytes()

    def wait_screen(self,name,timeout=90):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            self.update()
            self.skip_launch_movie()
            result=self.capture()
            if result and name!='damaged-save' and matches(result[2],self.cues['damaged-save']):
                raise RuntimeError('The game reports a damaged save; automatic input stopped.')
            if result and matches(result[2],self.cues[name]):
                path,meta,_=result
                self.event('screen',screen=name,frame=path.name,presentation=meta['presentation'])
                return result
            time.sleep(.10)
        raise TimeoutError('The expected screen did not appear: '+name)

    def send(self,button,screen):
        while time.monotonic()<self.next_input:
            self.update();time.sleep(.01)
        self.update();before=len(self.receipts);queued_after=self.log_serial
        with (self.folder/'controller.commands').open('ab',buffering=0) as channel:
            command=(button+'_HOLD\n').encode('ascii')
            if channel.write(command)!=len(command):raise OSError('Incomplete controller command')
        self.event('queued',button=button,screen=screen)
        deadline=time.monotonic()+5
        while len(self.receipts)==before:
            self.update()
            if time.monotonic()>deadline:raise TimeoutError('Game did not consume '+button)
            time.sleep(.01)
        receipt=self.receipts[-1]
        if f'buttons={BUTTONS[button]} hold_ms=250;' not in receipt:
            raise RuntimeError('Unexpected controller receipt: '+receipt)
        self.event('delivered',button=button,screen=screen,receipt=receipt)
        # A release poll must separate repeated edges; after modal close the game
        # also needs a neutral poll before it accepts another button.
        self.next_input=time.monotonic()+.30
        return queued_after

    def play(self,until):
        self.wait_log('local command channel ready')
        self.skip_intro=True
        try:title=self.wait_screen('title')
        finally:self.skip_intro=False
        if self.require_visible_title and title[1].get('display_accepted') is not True:
            raise RuntimeError('Native display is occluded; visible gameplay FPS cannot be measured')
        self.send('START','title')
        self.wait_screen('storage-warning');self.send('A','storage-warning')
        selector=self.wait_log('[NATIVE STORAGE UI] visible')
        if 'accept=1;' not in selector:raise RuntimeError('The configured save folder cannot be selected.')
        self.event('screen',screen='storage-selector',evidence=selector.strip())
        self.send('A','storage-selector')
        self.wait_log('[NATIVE STORAGE UI] completed selected=1')
        self.wait_screen('autosave');self.send('A','autosave')
        self.wait_screen('saved-games')
        if until=='saved-games':
            self.event('complete',result='saved-games');return
        self.send('A','saved-games')
        self.wait_screen('load-confirmation');self.send('A','load-confirmation')
        main_menu=self.wait_screen('main-menu')
        self.menu_presentation=main_menu[1]['presentation']
        self.main_menu_verified=True
        if until=='main-menu':
            self.event('complete',result='main-menu',gameplay_verified=False);return
        self.continue_game()

    def wait_continue_route(self,after,timeout=60):
        """Select a fresh route; log receipts alone never verify gameplay."""
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            self.update()
            fresh=[item for item in self.route_signals if item[0]>after]
            movie=next((item for item in fresh if item[1]=='movie'),None)
            if movie:
                return (movie[1],movie[2])
            scene=next((item for item in fresh if item[1]=='direct-resume' and item[2]['presentation']>self.menu_presentation),None)
            if scene:
                return (scene[1],scene[2])
            time.sleep(.02)
        raise TimeoutError('Continue Game produced neither a new movie nor a qualified scene presentation')

    def continue_game(self):
        self.update();self.lines.clear()
        after=self.send('A','main-menu')
        route,evidence=self.wait_continue_route(after)
        self.event('route',route=route,evidence=evidence,gameplay_verified=False,character_control_verified=False)
        if route=='movie':
            self.event('screen',screen='opening-level-movie',evidence=evidence)
            self.wait_log('[NATIVE MOVIE INPUT] ready',timeout=30,after=after)
            self.wait_log('slot=0; neutral input observed',timeout=30,after=after)
            skip_after=self.send('START','opening-level-movie')
            skipped=self.wait_log('[NATIVE MOVIE SKIP] original decoder released',timeout=30,after=skip_after)
            self.event('movie-skipped',screen='opening-level-movie',evidence=skipped.strip())
            result='Continue Game and movie Start delivered'
        else:
            result='Continue Game delivered; direct resume route observed'
        self.input_sequence_completed=True
        self.event('sequence-complete',route=route,result=result,gameplay_verified=False,character_control_verified=False)
        self.wait_gameplay()

    def wait_gameplay(self,timeout=90):
        if not self.main_menu_verified or not self.input_sequence_completed:
            raise RuntimeError('Gameplay verification requires the recorded startup sequence.')
        deadline=time.monotonic()+timeout
        reason='No completed capture received'
        last_reported=None
        first_at=None
        previous=None
        frames=[]
        while time.monotonic()<deadline:
            self.update()
            final=self.capture(timeout=min(30,max(.01,deadline-time.monotonic())))
            self.update()
            if final:
                path,meta,data=final
                if matches(data,self.cues['damaged-save']):
                    self.event('screen',screen='damaged-save',frame=path.name,presentation=meta['presentation'])
                    raise RuntimeError('The game reports a damaged save; automatic input stopped.')
                known=next((name for name,cue in self.cues.items() if matches(data,cue)),None)
                if known:
                    reason='Capture still matches startup screen: '+known
                else:
                    try:
                        evidence=verify_gameplay_frame(data,meta,
                            minimum_presentation=(previous['presentation'] if previous else self.menu_presentation),
                            allow_occluded=self.allow_occluded)
                        if previous and evidence['scene_geometry_draws']<previous['scene_geometry_draws']+evidence['frame_scene_geometry_draws']:
                            raise EvidenceError('Scene geometry counters did not advance with the presentation')
                        previous=evidence
                        receipt={**evidence,'frame':str(path.relative_to(self.folder))}
                        frames.append(receipt)
                        if first_at is None:
                            first_at=time.monotonic()
                            self.first_scene_frame=receipt
                            self.event('first-scene-frame',evidence=receipt,gameplay_verified=False)
                        elapsed=time.monotonic()-first_at
                        if len(frames)>=30 and elapsed>=10.0:
                            self.update()
                            self.gameplay_evidence={**receipt,'verification':'sustained_scene_rendering','observed_scene_frames':len(frames),'observation_seconds':round(elapsed,4),'first_scene_frame':frames[0],'character_control_verified':False}
                            self.gameplay_verified=True
                            result=('sustained presented scene frames verified' if evidence['display_accepted']
                                    else 'sustained completed scene readbacks verified while display was occluded')
                            self.event('complete',result=result,gameplay_verified=True,evidence=self.gameplay_evidence)
                            return
                        reason='First scene rendered; waiting for sustained scene presentations'
                    except EvidenceError as error:
                        reason=str(error)
            else:
                reason='No qualified completed front-renderer readback'
            if reason!=last_reported:
                self.event('gameplay-pending',reason=reason,gameplay_verified=False)
                last_reported=reason
            time.sleep(.05)
        raise TimeoutError('No verified gameplay frame: '+reason)

def create_parser():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,default=ROOT/'build/native/SimpsonsNative.exe')
    parser.add_argument('--run-directory',type=Path)
    parser.add_argument('--until',choices=('game','saved-games','main-menu'),default='game')
    parser.add_argument('--frame-timing',type=Path)
    parser.add_argument('--frame-timing-frames-only',action='store_true',help='Measure presentation intervals without per-packet diagnostic clocks')
    parser.add_argument('--isolate-data',action='store_true',help='Copy the configured profile and save content into this run directory')
    parser.add_argument('--allow-occluded',action='store_true',help='Accept completed scene readbacks when the display is occluded')
    parser.add_argument('--benchmark-stationary',action='store_true',
                        help='Measure 2001 accepted stationary gameplay frames after the startup replay')
    parser.add_argument('--benchmark-detailed',action='store_true',
                        help='Include diagnostic timing buckets in a stationary benchmark run')
    parser.add_argument('--vsync',action='store_true',
                        help='Use the previous display-synchronized presentation mode')
    return parser

def build_command(executable,profile,content,config,folder,frame_timing=None,frame_timing_frames_only=False,
                  vsync=False):
    command=[str(executable),'--image',str(ROOT/'analysis/simpsons.pe')]
    if vsync:
        command.append('--vsync')
    if frame_timing:
        command+=['--frame-timing',str(frame_timing)]
    if frame_timing_frames_only:
        command.append('--frame-timing-frames-only')
    command+=[ '--profile-store',str(profile),'--content-store',str(content),'--local-profile','0:'+config['profile_id'],
             '--controller-input',str(folder/'controller.commands'),'--capture-frames',str(folder/'captures'),'--capture-on-request']
    return command


def stationary_benchmark(replay,folder):
    first,last=STATIONARY_PRESENTATIONS
    report={'first_presentation':first,'last_presentation':last,'frames':last-first+1,
            'valid':False,'scope':'stationary initial gameplay'}
    if replay.latest_presentation>=first:
        report['reason']='Startup observer crossed the fixed benchmark window'
        return report
    if replay.last_capture_presentation>=first-20:
        report['reason']='Startup capture overlaps the benchmark exclusion margin'
        return report
    replay.event('benchmark-wait',first_presentation=first,last_presentation=last)
    deadline=time.monotonic()+180
    # FrameTiming flushes every 120 presentations. By 5420, its 5400 flush has
    # completed, including the fixed window's final presentation 5300.
    while replay.latest_presentation<5420:
        replay.update()
        if time.monotonic()>=deadline:
            report['reason']='Timed out before the fixed gameplay window completed'
            return report
        time.sleep(.05)
    try:
        with (folder/'frames.csv').open(newline='',encoding='utf-8') as stream:
            rows=[row for row in csv.DictReader(stream)
                  if row.get('presentation','').isdigit() and first<=int(row['presentation'])<=last]
    except (OSError,csv.Error) as error:
        report['reason']=f'Frame timing CSV is unavailable: {error}'
        return report
    if [int(row['presentation']) for row in rows]!=list(range(first,last+1)):
        report['reason']='Gameplay presentation window is incomplete or duplicated'
        return report
    if any(row.get('display_accepted')!='1' for row in rows):
        report['reason']='Gameplay presentation window contains occluded frames'
        return report
    try:
        durations=[float(row['frame_ms']) for row in rows]
    except (KeyError,TypeError,ValueError) as error:
        report['reason']=f'Gameplay frame duration is missing or invalid: {error}'
        return report
    if any(not math.isfinite(value) or value<=0 for value in durations):
        report['reason']='Gameplay frame duration is nonpositive or nonfinite'
        return report
    mean=statistics.fmean(durations)
    report.update(valid=True,fps=1000/mean,mean_ms=mean,median_ms=statistics.median(durations),
                  max_ms=max(durations),over_25_ms=sum(value>25 for value in durations))
    return report

def main(argv=None):
    parser=create_parser()
    args=parser.parse_args(argv)
    if args.frame_timing_frames_only and not args.frame_timing:
        parser.error('--frame-timing-frames-only requires --frame-timing')
    if args.benchmark_detailed and not args.benchmark_stationary:
        parser.error('--benchmark-detailed requires --benchmark-stationary')
    if args.benchmark_stationary:
        if args.until!='game' or args.allow_occluded or args.frame_timing:
            parser.error('Stationary benchmark requires --until game, visible frames and its private timing CSV')
        args.isolate_data=True
    executable=args.executable.resolve()
    if executable.name!='SimpsonsNative.exe' or not executable.is_relative_to(ROOT/'build') or not executable.is_file():
        parser.error('Expected a native game build in this workspace.')
    config=json.loads((ROOT/'config/startup_replay.json').read_text(encoding="utf-8"))
    profile=ROOT/config['profile_store'];content=ROOT/config['content_store']
    if not (profile/(config['profile_id']+'.profile')).is_file():
        parser.error('The recorded Player profile is missing.')
    if not (content/config['save_index']).is_file():
        parser.error('The recorded existing save is missing; refusing to create a new game.')
    folder=(args.run_directory or ROOT/'build/automatic-startup'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
    if not folder.is_relative_to(ROOT/'build'):parser.error('Run directory must be inside this workspace build folder.')
    folder.mkdir(parents=True,exist_ok=False)
    if args.isolate_data:
        isolated_profile=folder/'profile';isolated_content=folder/'content'
        shutil.copytree(profile,isolated_profile)
        shutil.copytree(content,isolated_content)
        video_preferences=profile.with_name(profile.name+'.video.cfg')
        if video_preferences.is_file():shutil.copy2(video_preferences,folder/'profile.video.cfg')
        if not (isolated_profile/(config['profile_id']+'.profile')).is_file():
            parser.error('The copied Player profile is missing; game was not launched.')
        if not (isolated_content/config['save_index']).is_file():
            parser.error('The copied existing save is missing; game was not launched.')
        profile=isolated_profile;content=isolated_content
    (folder/'captures').mkdir()
    with (folder/'controller.commands').open('xb'):
        pass
    if args.benchmark_stationary:
        args.frame_timing=folder/'frames.csv'
        args.frame_timing_frames_only=not args.benchmark_detailed
    command=build_command(executable,profile,content,config,folder,frame_timing=args.frame_timing,
                          frame_timing_frames_only=args.frame_timing_frames_only,
                          vsync=args.vsync)
    with (folder/'game.log').open('wb') as log:
        process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    replay=Replay(process,folder,config['screens'],allow_occluded=args.allow_occluded,
                  require_visible_title=args.benchmark_stationary)
    (folder/'launch.json').write_text(json.dumps({'pid':process.pid,'command':command},indent=2)+'\n', encoding="utf-8")
    window=NativeWindow(process.pid) if args.benchmark_stationary else None
    benchmark=None
    try:
        if window:
            deadline=time.monotonic()+30
            while not window.find() and process.poll() is None and time.monotonic()<deadline:
                time.sleep(.03)
            if not window.hwnd:
                raise TimeoutError('Native game window did not appear for the visible benchmark')
            window.show_and_focus()
        replay.play(args.until)
        if args.until=='game' and not replay.gameplay_verified:
            raise RuntimeError('The requested gameplay frame was not verified.')
        if args.benchmark_stationary:
            benchmark=stationary_benchmark(replay,folder)
            if not benchmark['valid']:
                raise RuntimeError(benchmark['reason'])
    except Exception as error:
        replay.event('stopped',reason=str(error),gameplay_verified=False)
        result={'success':False,'reason':str(error),'pid':process.pid,
            'main_menu_verified':replay.main_menu_verified,'input_sequence_completed':replay.input_sequence_completed,
            'first_scene_frame':replay.first_scene_frame,'gameplay_verified':False}
        if args.benchmark_stationary:
            result['gameplay_verified']=replay.gameplay_verified
            result['benchmark']=benchmark
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        return 1
    finally:
        replay.events.close()
        if window and process.poll() is None:
            try:
                if window.hwnd:window.post(0x0010) # WM_CLOSE
                else:process.terminate()
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.terminate();process.wait(timeout=10)
            except OSError:
                process.terminate();process.wait(timeout=10)
    result={'success':True,'input_sequence_completed':replay.input_sequence_completed,
        'main_menu_verified':replay.main_menu_verified,'gameplay_verified':replay.gameplay_verified,
        'gameplay_evidence':replay.gameplay_evidence,'pid':process.pid}
    if args.benchmark_stationary:result['benchmark']=benchmark
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
