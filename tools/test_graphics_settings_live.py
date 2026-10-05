"""Exercise the real Video menu using private saves and game-local controller input.

No desktop input is injected. Captures are completed renderer readbacks; this is
a correctness run, not a performance measurement.
"""
from pathlib import Path
import array
import argparse
import ctypes
from ctypes import wintypes
import datetime
import json
import os
import shutil
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
USER = ctypes.WinDLL('user32', use_last_error=True)
USER.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
USER.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER.EnableWindow.argtypes = [wintypes.HWND, wintypes.BOOL]
ENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
USER.EnumWindows.argtypes = [ENUM, wintypes.LPARAM]


def window_for(pid):
    found = []
    @ENUM
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        USER.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256)
        USER.GetClassNameW(hwnd, name, len(name))
        if owner.value == pid and name.value == 'SimpsonsNativeWindow':
            found.append(hwnd)
        return True
    USER.EnumWindows(visit, 0)
    return found[0] if found else None


def png(raw, width, height):
    codes = array.array('I'); codes.frombytes(raw)
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for pixel in codes[y*width:(y+1)*width]:
            rows.extend(((pixel & 1023)*255//1023, ((pixel >> 10) & 1023)*255//1023,
                         ((pixel >> 20) & 1023)*255//1023))
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width,height,8,2,0,0,0))+
            chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b''))


class Run:
    def __init__(self, folder, config):
        self.folder = folder; self.commands = folder/'controller.commands'
        self.commands.write_bytes(b''); self.captures = folder/'captures'; self.captures.mkdir()
        self.log_path = folder/'game.log'; self.log = self.log_path.open('wb')
        self.process = subprocess.Popen([
            str(ROOT/'build/native/SimpsonsNative.exe'), '--image', str(ROOT/'analysis/simpsons.pe'),
            '--render-test-first-mission', '--profile-store', str(folder.parent/'profile'),
            '--content-store', str(folder.parent/'content'), '--local-profile', '0:'+config['profile_id'],
            '--controller-input', str(self.commands), '--capture-frames', str(self.captures), '--capture-on-request',
        ], cwd=ROOT, stdout=self.log, stderr=subprocess.STDOUT,
            env=dict(os.environ, SIMPSONS_BACKGROUND_WINDOW='1', SIMPSONS_IGNORE_PHYSICAL_CONTROLLERS='1'))
        self.deadline = time.monotonic()+220; self.last_capture = None; self.observations = {}

    def wait(self, seconds):
        until = time.monotonic()+seconds
        while time.monotonic()<until:
            if self.process.poll() is not None:
                raise RuntimeError('Owned game exited early: '+str(self.process.returncode))
            if time.monotonic()>self.deadline:
                raise TimeoutError('Bounded graphics inspection deadline')
            if hwnd := window_for(self.process.pid):
                USER.EnableWindow(hwnd, False)  # No user keyboard/mouse can target this lab run.
            time.sleep(.1)

    def start(self):
        while '[NATIVE PRESENT]' not in self.log_path.read_text(errors='replace'):
            self.wait(.25)
        self.wait(12)

    def send(self, button):
        before = self.log_path.read_text(errors='replace').count('[NATIVE INPUT] local command tap')
        with self.commands.open('a', encoding='ascii') as stream:
            stream.write(button+'\n')
        self.wait(1.3)
        # Native channel has its own input acknowledgement; no foreground focus required.
        after = self.log_path.read_text(errors='replace')
        if after.count('[NATIVE INPUT] local command tap') <= before:
            raise RuntimeError('No native input acknowledgement for '+button)

    def enter_video(self):
        self.send('START_HOLD');self.wait(2.5)
        # Observe intermediate screens too, so a transition gate cannot silently
        # turn a menu-navigation command into a gameplay action.
        scale=int((self.folder.parent/'profile.video.cfg').read_text().split()[9])
        extent=[(1280*scale+50)//100,(720*scale+50)//100]
        self.capture('pause-navigation',extent)
        self.send('DOWN_HOLD');self.send('A');self.wait(1)
        self.capture('options-navigation',extent)
        self.send('A')
        self.wait(2)
        # Renderer captures and persisted preference assertions verify entry.
        # Native log text is buffered and can arrive after the screen is ready.

    def down(self, count):
        for _ in range(count):self.send('DOWN_HOLD')

    def edit_all(self):
        self.down(5);self.send('RIGHT_HOLD')  # 120 -> 144 FPS.
        self.down(3);self.send('LEFT_HOLD')   # Original -> 110-degree reference.
        self.down(1);self.send('LEFT_HOLD')   # 100 -> 75% render scale.
        for _ in range(3):
            self.down(1);self.send('RIGHT_HOLD')  # Bloom/DOF/blur Off.

    def resume(self):
        self.send('A');self.send('B');self.send('B');self.wait(2)

    def capture(self, name, expected_extent, scene=False):
        request = self.captures/'capture.request'; request.write_bytes(b'')
        while request.exists():self.wait(.1)
        raw = max(self.captures.glob('native-frame-*.rgb10a2'),key=lambda p:int(p.stem.split('-')[-1]))
        if raw == self.last_capture:raise RuntimeError('Capture reused an earlier presentation')
        self.last_capture = raw
        meta = json.loads(raw.with_suffix('.json').read_text())
        assert [meta['width'],meta['height']] == expected_extent, meta
        assert meta['front_copy_completed'] and meta['capture_source']=='completed_front_renderer_readback', meta
        if scene:
            assert meta['frame_scene_geometry_draws']>0, meta
            depth = meta['telemetry']['scene_depth_grid']
            assert depth['available'] and depth['presentation']==meta['presentation'], meta
        path = self.folder/(name+'.png');path.write_bytes(png(raw.read_bytes(),meta['width'],meta['height']))
        self.observations[name] = dict(png=str(path),raw=str(raw),presentation=meta['presentation'],
            internal=[meta['width'],meta['height']],scene=[meta['scene_width'],meta['scene_height']],
            draws=meta['frame_scene_geometry_draws'],completed=True)
        print('CAPTURE',name,path,flush=True)
        (self.folder/'captures.json').write_text(json.dumps(self.observations,indent=2)+'\n')

    def close(self):
        hwnd = window_for(self.process.pid)
        if hwnd:USER.PostMessageW(hwnd,0x10,0,0)
        forced = False
        try:self.process.wait(15)
        except subprocess.TimeoutExpired:
            forced=True;self.process.terminate();self.process.wait(10)
        self.log.close()
        lines = self.log_path.read_text(errors='replace').splitlines()
        failures = [line for line in lines if line.startswith('[FAILURE]')]
        good = not forced and not any(marker in line for line in lines for marker in
               ('[HOST EXCEPTION]','[TERMINATE]','[READ MEMO MISMATCH]')) and (
               self.process.returncode==0 and not failures or
               self.process.returncode==1 and failures==['[FAILURE] Native window closed'])
        (self.folder/'shutdown.json').write_text(json.dumps(dict(normal=good,forced=forced,
            exit_code=self.process.returncode,failures=failures),indent=2)+'\n')
        if not good:raise RuntimeError('Owned game failed normal shutdown')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe',action='store_true',help='Only capture a private 110-degree / 75% startup for camera diagnostics')
    parser.add_argument('--probe-menu',action='store_true',help='Also inspect the menu during --probe')
    parser.add_argument('--probe-scale',type=int,choices=[50,67,75,100,125,150,200],default=75)
    parser.add_argument('--probe-effects',choices=['off','on'],default='off')
    options=parser.parse_args()
    config=json.loads((ROOT/'config/startup_replay.json').read_text())
    folder=ROOT/'build/graphics-settings-20261004'/('live-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    folder.mkdir(parents=True)
    shutil.copytree(ROOT/config['profile_store'],folder/'profile')
    shutil.copytree(ROOT/config['content_store'],folder/'content')
    prefs=folder/'profile.video.cfg'
    initial=['5','0','0','0','120','0','3','0','0','100','1','1','1']
    accepted=['5','0','0','0','144','0','3','0','110','75','0','0','0']
    if options.probe:
        accepted[9]=str(options.probe_scale)
        accepted[10:]=['1' if options.probe_effects=='on' else '0']*3
        prefs.write_text(' '.join(accepted)+'\n')
        probe=folder/'probe';probe.mkdir();run=Run(probe,config)
        try:
            extent=[(1280*options.probe_scale+50)//100,(720*options.probe_scale+50)//100]
            run.start();run.capture('scene-fov-probe',extent,True)
            if options.probe_menu:
                run.enter_video();run.capture('video-layout-probe',extent)
        finally:run.close()
        print('PROBE',folder,flush=True)
        return
    prefs.write_text(' '.join(initial)+'\n')
    print('RUN',folder,flush=True)
    first=folder/'first';first.mkdir();run=Run(first,config)
    try:
        run.start();run.capture('scene-original',[1280,720],True)
        run.enter_video();run.capture('video-all-rows',[1280,720])
        run.edit_all();run.capture('video-edited-bottom',[1280,720])
        assert prefs.read_text().split()==initial,'Preview wrote preferences'
        run.send('B');run.send('A');run.capture('cancel-restored',[1280,720])
        assert prefs.read_text().split()==initial,'Cancel wrote preferences'
        run.edit_all();run.resume();run.capture('scene-wide-fov-live',[1280,720],True)
        assert prefs.read_text().split()==accepted,prefs.read_text()
        text=run.log_path.read_text(errors='replace')
        for marker in ('fov_reference_16_9=110','frame_rate=144','bloom=0 dof=0 motion_blur=0'):
            assert marker in text,marker
    finally:run.close()
    second=folder/'restart';second.mkdir();run=Run(second,config)
    try:
        run.start();run.capture('scene-scaled-after-restart',[960,540],True)
        run.enter_video();run.capture('video-persisted',[960,540])
        run.down(8);run.send('RIGHT_HOLD')  # 110 -> Original; live restore.
        run.resume();run.capture('scene-original-fov-restored',[960,540],True)
        restored=accepted.copy();restored[8]='0'
        assert prefs.read_text().split()==restored,prefs.read_text()
        text=run.log_path.read_text(errors='replace')
        assert 'scene=960x540' in text and 'render_scale=75' in text,text[-2000:]
        assert 'fov_reference_16_9=0' in text,'Original FOV restoration did not publish'
    finally:run.close()
    (folder/'verification.json').write_text(json.dumps(dict(passed=True,settings=prefs.read_text().split(),
        primary_stores_used=False,desktop_input_injected=False,timing_purpose='correctness-only'),indent=2)+'\n')
    print('PASS',folder,flush=True)


if __name__=='__main__':main()
