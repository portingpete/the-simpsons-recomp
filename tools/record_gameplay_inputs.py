"""Launch the native game with an F8 input recorder and a private copy of its save."""
from __future__ import annotations
import argparse
import ctypes
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare_run(root=ROOT, first_mission=False):
    config = json.loads((root / 'config/startup_replay.json').read_text(encoding="utf-8"))
    executable = root / 'build/native/SimpsonsInputRecorder.exe'
    image = root / 'analysis/simpsons.pe'
    profile = root / config['profile_store']
    content = root / config['content_store']
    save_relative = Path(config['save_index'])
    profile_name = config['profile_id'] + '.profile'
    for path in (executable, image, profile / profile_name, content / save_relative):
        if not path.is_file():
            raise FileNotFoundError(f'Required recorder file is missing: {path}')
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%SZ')
    folder = root / 'build/input-recordings' / (timestamp + '-' + uuid.uuid4().hex[:8])
    folder.mkdir(parents=True, exist_ok=False)
    # Keep the initial snapshot separate from the writable run's autosaves.
    try:
        shutil.copytree(profile, folder / 'profile')
        shutil.copytree(content, folder / 'content')
        video_preferences = profile.with_name(profile.name + '.video.cfg')
        if video_preferences.is_file():
            shutil.copy2(video_preferences, folder / 'profile.video.cfg')
    except Exception:
        shutil.rmtree(folder, ignore_errors=True)
        raise
    (folder / 'initial-state').mkdir()
    shutil.copy2(folder / 'content' / save_relative, folder / 'initial-state' / 'SIMPSONS_SLOT1.save')
    shutil.copy2(folder / 'profile' / profile_name, folder / 'initial-state' / profile_name)
    if (folder / 'profile.video.cfg').is_file():
        shutil.copy2(folder / 'profile.video.cfg', folder / 'initial-state/profile.video.cfg')
    command = [str(executable), '--image', str(image), '--frame-rate', '60',
               '--profile-store', str(folder / 'profile'), '--content-store', str(folder / 'content'),
               '--local-profile', '0:' + config['profile_id']]
    if first_mission:
        command += ['--render-test-first-mission', '--input-recording-auto-start']
    command += ['--input-recording-directory', str(folder)]
    manifest = {'started_utc': timestamp, 'command': command, 'working_directory': str(root),
                'executable_sha256': digest(executable), 'image_sha256': digest(image),
                'initial_save_sha256': digest(folder / 'initial-state/SIMPSONS_SLOT1.save'),
                'initial_video_settings_sha256': digest(folder / 'initial-state/profile.video.cfg') if (folder / 'initial-state/profile.video.cfg').is_file() else None,
                'profile_id': config['profile_id'], 'recording_hotkey': 'F8',
                'recording_starts': 'automatic_first_game_poll' if first_mission else 'manual_in_level',
                'recording_auto_start': first_mission, 'first_mission': first_mission,
                'in_memory_save_state_captured': False}
    (folder / 'launch.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return folder, command, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--first-mission', action='store_true',
                        help='Open Land of Chocolate directly with F8 recording available')
    args = parser.parse_args()
    folder, command, manifest = prepare_run(first_mission=args.first_mission)
    started = time.monotonic()
    with (folder / 'game.log').open('wb', buffering=0) as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            manifest['pid'] = process.pid
            (folder / 'launch.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            exit_code = process.wait()
        except Exception:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
            raise
    result = {'pid': process.pid, 'exit_code': exit_code, 'duration_seconds': time.monotonic() - started,
              'recordings': [path.name for path in sorted(folder.glob('inputs-*.jsonl'))]}
    (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        ctypes.windll.user32.MessageBoxW(None, str(error), 'Gameplay input recorder', 0x10)
        raise SystemExit(1)
