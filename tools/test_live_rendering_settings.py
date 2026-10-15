"""Change render resolution, scale, AA and filtering in the running game.

Uses the real Pause -> Options -> Video menu with private saves, game-local
controller commands and a background window (see test_graphics_settings_live).
Every change is verified by a completed renderer readback at the new internal
extent in the same process; no restart is involved. Correctness only.
"""
from pathlib import Path
import datetime
import json
import re
import shutil
import time

from test_graphics_settings_live import ROOT, Run

LIVE = re.compile(r"\[NATIVE VIDEO\] live rendering scene=(\d+)x(\d+)->(\d+)x(\d+) aa=(\d+)->(\d+) "
                  r"anisotropy=(\d+)->(\d+).*?re-backed=(\d+) color/(\d+) depth re-recorded=(\d+) ms=([0-9.]+)")
# v9: version window fullscreen vsync rate render filter aa fov scale
#     bloom dof blur fog grading bars sharpening background latency renderer
INITIAL = "9 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 0 0 1 0".split()
AA_ORDER = (0, 4, 1, 5, 2, 7, 16)  # Full menu order, Original through SMAA and TAA.
# Version-10 pages (each opens on its page row):
#   Display: page, brightness, display mode, window, refresh, vsync, frame cap, render resolution, render scale
#   Quality: page, antialiasing, filtering, anisotropy, shadows, ambient occlusion, upscaling, sharpening, detail


def scaled(width, height, percent):
    return [(width*percent+50)//100, (height*percent+50)//100]


def enter_video(run, extent):
    """Run.enter_video, with the internal extent given instead of assumed 720p-based."""
    run.send('START_HOLD'); run.wait(2.5); run.capture('pause-navigation', extent)
    run.send('DOWN_HOLD'); run.send('A'); run.wait(1); run.capture('options-navigation', extent)
    run.send('A'); run.wait(2)


def main():
    config = json.loads((ROOT/'config/startup_replay.json').read_text())
    folder = ROOT/'build/live-rendering-settings-20261014'/('live-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    folder.mkdir(parents=True)
    shutil.copytree(ROOT/config['profile_store'], folder/'profile')
    shutil.copytree(ROOT/config['content_store'], folder/'content')
    prefs = folder/'profile.video.cfg'
    prefs.write_text(' '.join(INITIAL)+'\n')
    print('RUN', folder, flush=True)
    run_folder = folder/'run'; run_folder.mkdir()
    run = Run(run_folder, config)
    run.deadline = time.monotonic()+480
    try:
        run.start(); run.capture('scene-1280x720', [1280, 720], True)
        enter_video(run, [1280, 720]); run.capture('video-opened', [1280, 720])
        # Display page: render resolution 1280x720 -> 1600x900.
        run.down(7); run.send('RIGHT_HOLD'); run.wait(1)
        run.capture('video-render-1600x900', [1600, 900])
        # Quality page: anisotropy Off -> 2x, then every AA choice.
        for _ in range(7): run.send('UP_HOLD')
        run.send('RIGHT_HOLD'); run.down(3); run.send('RIGHT_HOLD'); run.wait(1)
        run.capture('video-anisotropy-2x', [1600, 900])
        run.send('UP_HOLD'); run.send('UP_HOLD')
        for aa in AA_ORDER[1:]:
            run.send('RIGHT_HOLD'); run.wait(1)
            run.capture(f'video-aa-{aa}', [1600, 900])
        # Back to Display: render scale 100% -> 75%.
        run.send('UP_HOLD'); run.send('LEFT_HOLD'); run.down(8); run.send('LEFT_HOLD'); run.wait(1)
        run.capture('video-scale-75', scaled(1600, 900, 75))
        assert prefs.read_text().split() == INITIAL, 'Preview wrote preferences'
        run.resume(); run.capture('scene-live-accepted', scaled(1600, 900, 75), True)
        accepted = prefs.read_text().split()
        # v10: render, filtering, AA, FOV, scale ... anisotropy (field 24).
        assert accepted[0] == '10' and accepted[5:10] == ['1', '0', str(AA_ORDER[-1]), '0', '75'] and accepted[24] == '2', accepted
        # Reopen, preview 1280x720, then cancel: the accepted storage returns.
        enter_video(run, scaled(1600, 900, 75)); run.capture('video-reopened', scaled(1600, 900, 75))
        run.down(7); run.send('LEFT_HOLD'); run.wait(1)
        run.capture('video-preview-1280x720', scaled(1280, 720, 75))
        run.send('B'); run.wait(1)
        run.capture('cancel-restored', scaled(1600, 900, 75))
        assert prefs.read_text().split() == accepted, 'Cancel wrote preferences'
        run.send('B'); run.send('B'); run.wait(2)
        run.capture('scene-after-cancel', scaled(1600, 900, 75), True)
    finally:
        run.close()
    text = run.log_path.read_text(errors='replace')
    changes = [dict(scene_before=[int(m[1]), int(m[2])], scene_after=[int(m[3]), int(m[4])],
                    aa=[int(m[5]), int(m[6])], anisotropy=[int(m[7]), int(m[8])],
                    rebacked_color=int(m[9]), rebacked_depth=int(m[10]), rerecorded=int(m[11]), ms=float(m[12]))
               for m in LIVE.finditer(text)]
    rejected = [line for line in text.splitlines() if 'live rendering change rejected' in line]
    assert not rejected, rejected
    # Render, anisotropy, six AA steps, scale, preview and cancel restore.
    assert len(changes) >= 11, changes
    (folder/'verification.json').write_text(json.dumps(dict(passed=True, settings=accepted, live_changes=changes,
        restarts=0, primary_stores_used=False, desktop_input_injected=False,
        timing_purpose='correctness-only'), indent=2)+'\n')
    for change in changes:
        print('LIVE', change, flush=True)
    print('PASS', folder, flush=True)


if __name__ == '__main__':
    main()
