"""Run the native executable with a bounded muted diagnostic capture."""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--timeout',type=float,default=20)
p.add_argument('--executable',type=Path,default=ROOT/'build/native/SimpsonsNative.exe',help='Native SimpsonsNative.exe build beneath this workspace build directory')
p.add_argument('--log',type=Path,default=ROOT/'build/last-run.log')
p.add_argument('--hold-on-failure',action='store_true')
p.add_argument('--profile-store',type=Path)
p.add_argument('--content-store',type=Path)
p.add_argument('--capture-frames',type=Path)
p.add_argument('--capture-on-request',action='store_true')
p.add_argument('--frame-rate',type=int,choices=[30,60,90,120,144,165,240])
p.add_argument('--uncapped-frame-rate',action='store_true',help='Disable the original frame wait for 120+ FPS testing')
p.add_argument('--frame-timing',type=Path)
p.add_argument('--frame-timing-frames-only',action='store_true',help='Measure presentation intervals without per-packet diagnostic clocks')
p.add_argument('--controller-input',type=Path,help='Existing append-only file for explicit native controller commands')
p.add_argument('--local-profile',action='append',default=None,metavar='SLOT:ID')
a=p.parse_args()
if not 0<a.timeout<=3600: p.error('--timeout must be between 0 and 3600 seconds')
if a.frame_timing_frames_only and not a.frame_timing:p.error('--frame-timing-frames-only requires --frame-timing')
if a.uncapped_frame_rate and a.frame_rate:p.error('Choose either --frame-rate or --uncapped-frame-rate')
a.executable=a.executable.resolve()
if a.executable.name!='SimpsonsNative.exe' or not a.executable.is_relative_to((ROOT/'build').resolve()) or not a.executable.is_file():
    p.error('--executable must identify an existing SimpsonsNative.exe beneath this workspace build directory')
a.log=a.log.resolve()
if not a.log.is_relative_to((ROOT/'build').resolve()) or a.log.suffix.lower()!='.log':
    p.error('--log must be a .log file beneath this workspace build directory')
a.log.parent.mkdir(parents=True,exist_ok=True)
command=[str(a.executable),'--image',str(ROOT/'analysis/simpsons.pe')]
if a.hold_on_failure: command.append('--hold-on-failure')
if a.profile_store: command += ['--profile-store',str(a.profile_store)]
if a.content_store: command += ['--content-store',str(a.content_store)]
if a.capture_frames: command += ['--capture-frames',str(a.capture_frames)]
if a.capture_on_request: command.append('--capture-on-request')
if a.frame_rate: command += ['--frame-rate',str(a.frame_rate)]
if a.uncapped_frame_rate: command.append('--uncapped-frame-rate')
if a.frame_timing: command += ['--frame-timing',str(a.frame_timing)]
if a.frame_timing_frames_only:command.append('--frame-timing-frames-only')
if a.controller_input: command += ['--controller-input',str(a.controller_input)]
for profile in (a.local_profile or []): command += ['--local-profile',profile]
with a.log.open('w',encoding='utf-8') as log:
    try:
        run=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout)
        status=run.returncode
    except subprocess.TimeoutExpired:
        log.write('\n[HARNESS] Time limit reached; child process terminated. Gameplay not verified.\n')
        status=124
print(f'Native run status={status}; full log: {a.log.resolve()}')
# Long diagnostic runs can produce hundreds of megabytes. Keep the authoritative
# log intact without copying it all into the terminal or a second console log.
with a.log.open('rb') as captured:
    captured.seek(0,2)
    end=captured.tell()
    captured.seek(max(0,end-32768))
    tail=captured.read().decode('utf-8',errors='replace')
    if end>32768:
        tail=tail.partition('\n')[2]
        print('[HARNESS] Showing final 32 KiB only.')
    print(tail)
sys.exit(status)
