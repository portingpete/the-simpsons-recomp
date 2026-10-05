"""Temporary direct Land of Chocolate launch for rendering work."""
from pathlib import Path
import argparse
import datetime
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-frames', action='store_true',
                        help='Enable on-request captures in the new run folder')
    parser.add_argument('--auto-defeat-enemies', action='store_true',
                        help='Automatically defeat NPC enemies in Land of Chocolate')
    parser.add_argument('--vsync', action='store_true',
                        help='Use the previous display-synchronized presentation mode')
    parser.add_argument('--capped-frame-rate', action='store_true',
                        help='Use one-refresh original pacing (60 FPS) instead of uncapped 120+ FPS testing')
    args = parser.parse_args()
    config = json.loads((ROOT / 'config/startup_replay.json').read_text(encoding='utf-8'))
    executable = ROOT / 'build/native/SimpsonsNative.exe'
    image = ROOT / 'analysis/simpsons.pe'
    profile = ROOT / config['profile_store']
    for path in (executable, image, profile / (config['profile_id'] + '.profile')):
        if not path.is_file():
            parser.error(f'Missing required file: {path}')
    folder = ROOT / 'build/render-tests' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    folder.mkdir(parents=True, exist_ok=False)
    command = [str(executable), '--image', str(image),
               '--render-test-first-mission', '--profile-store', str(profile),
               '--content-store', str(ROOT / config['content_store']),
               '--local-profile', '0:' + config['profile_id']]
    if args.capped_frame_rate:
        command += ['--frame-rate', '60']
    else:
        command += ['--uncapped-frame-rate']
    if args.capture_frames:
        (folder / 'captures').mkdir()
        command += ['--capture-frames', str(folder / 'captures'), '--capture-on-request']
    if args.auto_defeat_enemies:
        command += ['--auto-defeat-loc-enemies']
    if args.vsync:
        command += ['--vsync']
    with (folder / 'game.log').open('xb') as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    (folder / 'launch.json').write_text(
        json.dumps({'pid': process.pid, 'command': command}, indent=2) + '\n', encoding='utf-8')
    print(f'Land of Chocolate rendering test launched (PID {process.pid}). Log: {folder / "game.log"}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
