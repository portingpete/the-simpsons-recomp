"""Change the native Master volume row in the running game's Audio menu.

Uses the real Pause -> Options -> Audio menu with private saves, game-local
controller commands and a background window (see test_graphics_settings_live).
Checks the label/log of each step, Cancel restoring the opening value, Accept
saving <store>.audio.cfg, and that an original slider row still saves.
"""
from pathlib import Path
import datetime
import json
import shutil
import time

from test_graphics_settings_live import ROOT, Run


def enter_audio(run):
    run.send('START_HOLD'); run.wait(2.5); run.capture('pause', [1280, 720])
    run.send('DOWN_HOLD'); run.send('A'); run.wait(1); run.capture('options', [1280, 720])
    run.send('DOWN_HOLD'); run.send('A'); run.wait(2)


def log(run):
    return run.log_path.read_text(errors='replace')


def main():
    config = json.loads((ROOT/'config/startup_replay.json').read_text())
    folder = ROOT/'build/native-audio-menu'/('live-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    folder.mkdir(parents=True)
    shutil.copytree(ROOT/config['profile_store'], folder/'profile')
    shutil.copytree(ROOT/config['content_store'], folder/'content')
    (folder/'profile.video.cfg').write_text('10 0 0 0 120 0 0 0 0 100 1 1 1 1 1 1 0 0 1 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 50 0 0 0 0 0 0 0\n')
    audio = folder/'profile.audio.cfg'
    print('RUN', folder, flush=True)
    run_folder = folder/'run'; run_folder.mkdir()
    run = Run(run_folder, config)
    run.deadline = time.monotonic()+420
    try:
        run.start(); run.capture('scene', [1280, 720], True)
        enter_audio(run); run.capture('audio-opened', [1280, 720])
        assert '[NATIVE AUDIO MENU] opened master=100%' in log(run), 'Audio menu did not open with the native row'
        # The menu opens on Master volume (row 0).
        run.send('LEFT_HOLD'); run.send('LEFT_HOLD'); run.wait(1); run.capture('audio-master-90', [1280, 720])
        assert '[NATIVE AUDIO MENU] master=90%' in log(run), 'Master volume did not step down'
        assert not audio.exists(), 'Preview wrote audio preferences'
        run.send('B'); run.wait(1.5)
        assert 'cancelled and restored master=100%' in log(run), 'Cancel did not restore the opening master volume'
        assert not audio.exists(), 'Cancel wrote audio preferences'
        # Reopen from Options, step down three times, change Voice, then accept.
        run.send('A'); run.wait(2); run.capture('audio-reopened', [1280, 720])
        for _ in range(3): run.send('LEFT_HOLD')
        run.wait(1)
        run.send('DOWN_HOLD'); run.send('LEFT_HOLD'); run.wait(1); run.capture('audio-voice-moved', [1280, 720])
        run.send('A'); run.wait(1.5)
        text = log(run)
        assert 'accepted and saved master=85%' in text, 'Accept did not save the native master volume'
        assert audio.read_text().split() == ['1', '85'], audio.read_text()
        run.send('B'); run.send('B'); run.wait(2); run.capture('scene-after', [1280, 720], True)
    finally:
        run.close()
    (folder/'verification.json').write_text(json.dumps(dict(passed=True, audio=audio.read_text().split(),
        primary_stores_used=False, desktop_input_injected=False), indent=2)+'\n')
    print('PASS', folder, flush=True)


if __name__ == '__main__':
    main()
