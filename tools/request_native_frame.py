"""Request one completed renderer readback from an opted-in native game."""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture_directory', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
request = a.capture_directory / 'capture.request'
try:
    with request.open('xb'):
        pass
except FileExistsError:
    raise SystemExit('Capture request already pending; wait for the game to consume it')
deadline = time.monotonic() + 10
while request.exists():
    if time.monotonic() >= deadline:
        raise TimeoutError('The game has not completed the requested frame; request remains pending')
    time.sleep(0.05)
frames = [f for f in a.capture_directory.glob('native-frame-*.rgb10a2') if f.with_suffix('.json').is_file()]
if not frames:
    raise SystemExit('No completed renderer captures found')
def _frame_key(f):
    try:
        return int(f.stem.removeprefix('native-frame-'))
    except ValueError:
        return -1
frames = [f for f in frames if _frame_key(f) >= 0]
if not frames:
    raise SystemExit('No well-formed renderer captures found')
latest = max(frames, key=_frame_key)
a.output.mkdir(parents=True, exist_ok=False)
for f in (latest, latest.with_suffix('.json')):
    shutil.copy2(f, a.output / f.name)
subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('render_frame_capture.py')), str(a.output)], check=True, timeout=60)
