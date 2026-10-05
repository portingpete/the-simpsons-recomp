"""Press Start once when a fresh renderer capture matches the visible title prompt."""
import argparse
import json
import os
import struct
import time
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--captures',type=Path,required=True)
p.add_argument('--commands',type=Path,required=True)
p.add_argument('--template',type=Path,required=True)
p.add_argument('--wait-only',action='store_true',help='Report the fresh visible prompt without sending input')
a=p.parse_args()
reference=a.template.read_bytes()
if len(reference)!=1280*720*4:raise ValueError('The verified title template must be 1280x720 RGB10A2')
white=[];black=[]
def rgb(data,at):
    word=struct.unpack_from('<I',data,at)[0]
    return [(word>>shift)&1023 for shift in (0,10,20)]
for y in range(592,626):
    for x in range(545,738):
        at=4*(y*1280+x);pixel=rgb(reference,at)
        if min(pixel)>940:white.append(at)
        if max(pixel)<30:black.append(at)
if len(white)<500 or len(black)<500:raise ValueError('Reference lacks the verified prompt lettering')
deadline=time.monotonic()+90
while time.monotonic()<deadline:
    request=a.captures/'capture.request'
    try:
        with request.open('xb'):pass
    except FileExistsError:
        time.sleep(0.05)
        continue
    until=time.monotonic()+10
    while request.exists():
        if time.monotonic()>until:raise TimeoutError('Renderer capture did not complete')
        time.sleep(0.02)
    candidates=[f for f in a.captures.glob('native-frame-*.rgb10a2')]
    if not candidates:
        time.sleep(0.25)
        continue
    def _press_key(f):
        try:
            return int(f.stem.removeprefix('native-frame-'))
        except ValueError:
            return -1
    candidates=[f for f in candidates if _press_key(f)>=0]
    if not candidates:
        time.sleep(0.25)
        continue
    latest=max(candidates,key=_press_key)
    try:
        meta=json.loads(latest.with_suffix('.json').read_text(encoding="utf-8"));data=latest.read_bytes()
    except (OSError, ValueError) as exc:
        raise ValueError(f'Unreadable capture {latest}: {exc}')
    if meta['width']!=1280 or meta['height']!=720 or not meta['display_accepted']:raise ValueError('Capture is not an accepted full-size presentation')
    w=sum(min(rgb(data,at))>900 for at in white)/len(white)
    b=sum(max(rgb(data,at))<70 for at in black)/len(black)
    if w>=0.98 and b>=0.98:
        if a.wait_only:
            print(f'Visible Press START matched in {latest}; no input sent.',flush=True)
            break
        if a.commands.is_symlink():
            raise ValueError("Controller command path must not be a symlink")
        descriptor=os.open(a.commands,os.O_WRONLY|os.O_APPEND|os.O_BINARY|getattr(os,"O_NOFOLLOW",0))
        try:
            if os.write(descriptor,b'START_HOLD\n')!=11:raise OSError('Incomplete Start command write')
        finally:os.close(descriptor)
        print(f'Visible Press START matched in {latest}; held Start queued once.',flush=True)
        break
    time.sleep(0.25)
else:raise TimeoutError('Visible Press START was not found within 90 seconds')
