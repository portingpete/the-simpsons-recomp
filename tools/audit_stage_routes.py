"""Independent original stage launches, private stores, bounded command routes.

Action receipts prove input delivery. A screenshot and resource receipt are
separate evidence; delivery alone never proves enemies, pickups or lifetimes.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
STAGES = tuple(re.findall(r'"([a-z_]+)"', (ROOT / 'runtime/audit_stage.h').read_text()))
RECEIPT = re.compile(r'local command tap buttons=([0-9A-F]{4}) hold_ms=(\d+); left=\((-?\d+),(-?\d+)\) rt=(\d+); delivered')
FAILURE_MARKERS = ('[FAILURE]', '[AOT FAILURE]', '[THREAD FAILURE]', '[TERMINATE]', '[SEH]')
SMOKE = [
    dict(name='jump', buttons=0x1000, lx=0, ly=0, ms=350, rt=0),
    dict(name='attack', buttons=0x4000, lx=0, ly=0, ms=450, rt=0),
    dict(name='ability', buttons=0x2000, lx=0, ly=0, ms=600, rt=0),
    dict(name='interact', buttons=0x8000, lx=0, ly=0, ms=350, rt=0),
    dict(name='special-trigger', buttons=0, lx=0, ly=0, ms=350, rt=255),
    dict(name='forward-attack', buttons=0x4000, lx=0, ly=32767, ms=900, rt=0),
    dict(name='return', buttons=0, lx=0, ly=-32768, ms=900, rt=0),
    dict(name='change-character', buttons=0x0200, lx=0, ly=0, ms=350, rt=0),
]
# Explicit confirmation edges exercise tutorial/dialogue UI and give opening
# gameplay actions a chance to run after a modal prompt. Delivery is recorded.
SMOKE = ([dict(name='opening-confirm-' + str(index), command='A') for index in range(4)] +
         [entry for action in SMOKE for entry in (action, dict(name='confirm-after-' + action['name'], command='A'))])


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def check_plain(path):
    info = path.lstat()
    if path.is_symlink() or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError(f'Reparse source is not copied: {path}')


def scan_plain(root):
    root = root.absolute()
    for ancestor in (root, *root.parents):
        check_plain(ancestor)
    todo, files = [root], []
    while todo:
        directory = todo.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                check_plain(path)
                if entry.is_dir(follow_symlinks=False):
                    todo.append(path)
                elif entry.is_file(follow_symlinks=False):
                    files.append(path)
                else:
                    raise ValueError(f'Non-regular source: {path}')
    return files


def tree_hashes(root):
    return {file.relative_to(root).as_posix(): digest(file) for file in sorted(scan_plain(root))}


def validate_route(actions):
    if not isinstance(actions, list):
        raise ValueError('Route requires an action list')
    for action in actions:
        if not isinstance(action, dict) or not isinstance(action.get('name'), str):
            raise ValueError('Each route action needs a name')
        if type(action.get('wait_after_ms', 1000)) is not int or not 0 <= action.get('wait_after_ms', 1000) <= 30000:
            raise ValueError('Route wait must be 0..30000 milliseconds')
        if type(action.get('capture_after', False)) is not bool:
            raise ValueError('Route capture flag must be Boolean')
        marker = action.get('wait_for_log')
        if marker is not None and (not isinstance(marker, str) or not 1 <= len(marker) <= 256
                                   or any(ord(ch) < 32 or ord(ch) > 126 for ch in marker)):
            raise ValueError('Route log marker must be a bounded printable literal')
        if type(action.get('wait_for_log_ms', 60000)) is not int or not 1 <= action.get('wait_for_log_ms', 60000) <= 120000:
            raise ValueError('Route log wait must be 1..120000 milliseconds')
        occurrence = action.get('wait_for_log_occurrence', 1)
        if type(occurrence) is not int or not 1 <= occurrence <= 256 or (marker is None and occurrence != 1):
            raise ValueError('Route log occurrence must be 1..256 and have a marker')
        if 'command' in action:
            if action['command'] not in ('START', 'BACK', 'A', 'B', 'UP', 'DOWN', 'LEFT', 'RIGHT'):
                raise ValueError('Unknown named controller command')
            continue
        for field, low, high in (('buttons', 0, 0xFFFF), ('lx', -32768, 32767), ('ly', -32768, 32767),
                                 ('ms', 50, 2000), ('rt', 0, 255)):
            value = action.get(field)
            if type(value) is not int or not low <= value <= high:
                raise ValueError('Invalid bounded route field: ' + field)
        if action['buttons'] & ~0xF3FF:
            raise ValueError('PAD route has unsupported button bits')
    return actions


def command_text(action):
    if 'command' in action:
        return action['command'] + '\n'
    return 'PAD {buttons:04X} {lx} {ly} {ms} {rt}\n'.format(**action)


def log_boundary(log, marker, occurrence=1):
    matches = [line for line in log.splitlines() if marker in line]
    return matches[occurrence - 1] if len(matches) >= occurrence else None


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def close_owned(process):
    if process.poll() is not None:
        return 'already-exited'
    from replay_recorded_route import NativeWindow
    window = NativeWindow(process.pid)
    if window.find():
        window.post(0x0010)
        try:
            process.wait(timeout=10)
            return 'owned-window-close'
        except subprocess.TimeoutExpired:
            pass
    # Popen owns this exact process handle. No image-name or wildcard kill.
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    return 'owned-process-termination'


def prepare_private_video(source, run, expected_hash, baseline_video=False):
    target = run / 'profile.video.cfg'
    if expected_hash is not None:
        shutil.copy2(source, target)
        if digest(target) != expected_hash:
            raise ValueError('Private video copy differs from original snapshot')
    if not baseline_video:
        return None
    # The ordinary version4 native preference format, confined to this run.
    # 720p window/render, windowed, 60 FPS, original filtering and original AA.
    raw = b'4 0 0 0 60 0 0 0\n'
    target.write_bytes(raw)
    return dict(path=target.name, sha256=digest(target), bytes=len(raw),
                parameters=[4, 0, 0, 0, 60, 0, 0, 0], scope='private-run-only')


def launch_command(executable, run, config, stage, completion, play_intro):
    """Owned-process arguments: private stores, the owned command channel, the audit file and on-request captures."""
    command = [str(executable), '--image', str(ROOT / 'analysis/simpsons.pe'),
               '--profile-store', str(run / 'profile'), '--content-store', str(run / 'content'),
               '--local-profile', '0:' + config['profile_id'], '--controller-input', str(run / 'controller.commands'),
               '--resource-audit', str(run / 'resources.jsonl'), '--capture-frames', str(run / 'captures'), '--capture-on-request']
    if completion:
        command.append('--first-mission-completion')
    elif stage != 'frontend':
        command.extend(['--stage', stage])
    if play_intro:
        command.append('--play-stage-intro')
    return command


def log_pump_due(now, last_pump, active, interval=0.5):
    """The game's stderr is a 1 MiB full buffer, so the log can trail the game by many seconds; every delivered command receipt flushes
    it. While an outro skip is armed a neutral 50 ms PAD every `interval` seconds keeps the log current (neutral input also arms the
    original movie's skip)."""
    return active and (last_pump is None or now - last_pump >= interval)


def outro_skip_due(now, outro_ready_at, requested, after):
    """True once, `after` seconds past the outro's original movie readiness (never before the completion helper ran)."""
    return after is not None and outro_ready_at is not None and not requested and now - outro_ready_at >= after


COMPLETION_READY = '[FIRST MISSION COMPLETION] original EpisodeComplete 8296FFC8'


def run_stage(stage, parent, executable, config, route, timeout, play_intro=False, intro_skip_after=None, queue=None, baseline_video=False, completion=False, receipt_timeout=10, outro_skip_after=None):
    route = list(route)
    run = parent / (stage + '-' + uuid.uuid4().hex[:8])
    run.mkdir()
    source_profile, source_content = ROOT / config['profile_store'], ROOT / config['content_store']
    before = dict(profile=tree_hashes(source_profile), content=tree_hashes(source_content))
    video = source_profile.with_name(source_profile.name + '.video.cfg')
    if video.exists():
        check_plain(video); before['video'] = digest(video)
    write_json(run / 'primary-before.json', before)
    shutil.copytree(source_profile, run / 'profile', symlinks=True)
    shutil.copytree(source_content, run / 'content', symlinks=True)
    video_override = prepare_private_video(video, run, before.get('video'), baseline_video)
    if tree_hashes(run / 'profile') != before['profile'] or tree_hashes(run / 'content') != before['content']:
        raise ValueError('Private store verification differs from original snapshots')
    captures = run / 'captures'; captures.mkdir()
    channel = run / 'controller.commands'; channel.write_bytes(b'')
    audit = run / 'resources.jsonl'
    command = launch_command(executable, run, config, stage, completion, play_intro)
    summary = dict(stage=stage, run=str(run), completion=completion, stage_ready=False, frontend_presented=False, route=route, actions=[], failures=[], captures=[],
                   play_intro=play_intro, intro_skip_after=intro_skip_after, interactive_queue=str(queue) if queue else None,
                   video_override=video_override,
                   limitations=['Command delivery does not prove an ability hit, pickup, enemy attack or destruction.',
                                'This opening route does not prove checkpoint reload, death, later cutscene or mission exit.']
                   + (['Completion mode: the native shortcut invokes the original EpisodeComplete helper once after map-ready; this is not a naturally played completion.'] if completion else []))
    process = None
    try:
        with (run / 'game.log').open('xb', buffering=0) as output:
            process = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        write_json(run / 'launch.json', dict(pid=process.pid, command=command, executable_sha256=digest(executable),
                                           image_sha256=digest(ROOT / 'analysis/simpsons.pe')))
        started = time.monotonic(); deadline = started + timeout
        log = ''; cursor = 0; partial = ''; receipts = []; movie_ready_at = None; completed_at = None; outro_ready_at = None; last_pump = None

        def poll():
            nonlocal cursor, log, partial, movie_ready_at, completed_at, outro_ready_at, last_pump
            with (run / 'game.log').open('r', encoding='utf-8', errors='replace') as source:
                source.seek(cursor); fresh = source.read(); cursor = source.tell()
            log += fresh; partial += fresh
            complete = partial.split('\n'); partial = complete.pop()
            for line in complete:
                if any(marker in line for marker in FAILURE_MARKERS):
                    if line.strip() == '[FAILURE] Native window closed' and summary.get('close') == 'owned-window-close':
                        summary.setdefault('expected_shutdown_messages', []).append(line)
                    else:
                        summary['failures'].append(line)
                if '[STAGE AUDIT] original map initialized stage=' + stage + ' ' in line and not completion:
                    summary['stage_ready'] = True
                if completion and COMPLETION_READY in line:
                    summary['stage_ready'] = True
                    if completed_at is None:
                        completed_at = time.monotonic()
                if stage == 'frontend' and '[NATIVE PRESENT]' in line and 'display=accepted' in line:
                    summary['frontend_presented'] = True
                if '[NATIVE MOVIE INPUT] ready owner=' in line:
                    summary.setdefault('movie_starts', []).append(line)
                    if movie_ready_at is None:
                        movie_ready_at = time.monotonic()
                    if completed_at is not None and outro_ready_at is None:
                        outro_ready_at = time.monotonic()  # The first movie after the completion helper ran: the outro.
                if '[NATIVE MOVIE SKIP] original decoder released and completion dispatched' in line:
                    summary.setdefault('movie_retirements', []).append(line)
                match = RECEIPT.search(line)
                if match:
                    receipts.append([int(match[1], 16), *map(int, match.groups()[1:])])
            if log_pump_due(time.monotonic(), last_pump, outro_skip_after is not None and not summary.get('outro_skip_requested', False)):
                last_pump = time.monotonic(); summary['log_pumps'] = summary.get('log_pumps', 0) + 1
                with channel.open('ab', buffering=0) as out:
                    out.write(b'PAD 0000 0 0 50 0\n')
            if outro_skip_due(time.monotonic(), outro_ready_at, summary.get('outro_skip_requested', False), outro_skip_after):
                summary['outro_skip_requested'] = True
                with channel.open('ab', buffering=0) as out:
                    out.write(b'START\n')
            return process.poll() is None and time.monotonic() < deadline and not summary['failures']

        def wait(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if not poll():
                    return False
                time.sleep(.1)
            return poll()

        def capture(label):
            def frames():
                return [path for path in captures.glob('native-frame-*.json')
                        if re.fullmatch(r'native-frame-\d+\.json', path.name)]
            known = {path.name for path in frames()}
            request = captures / 'capture.request'
            if request.exists():
                return
            request.write_bytes(b'')
            until = min(deadline, time.monotonic() + 15)
            while time.monotonic() < until and poll():
                available = sorted((path for path in frames() if path.name not in known),
                                   key=lambda path: int(path.stem.rsplit('-', 1)[1]))
                for candidate in reversed(available):
                    try:
                        metadata = json.loads(candidate.read_text(encoding='utf-8'))
                        raw = candidate.with_suffix('.rgb10a2')
                        complete = (metadata.get('capture_source') == 'completed_front_renderer_readback'
                                    and metadata.get('front_copy_completed') is True
                                    and metadata.get('display_accepted') is True
                                    and raw.is_file() and raw.stat().st_size == metadata['width'] * metadata['height'] * 4)
                    except (OSError, ValueError, KeyError, TypeError):
                        complete = False
                    if complete:
                        summary['captures'].append(dict(label=label, metadata=str(candidate)))
                        return
                time.sleep(.1)

        def ready():
            return summary['stage_ready'] or (stage == 'frontend' and summary['frontend_presented'])

        def progress():
            summary['elapsed_seconds'] = time.monotonic() - started
            write_json(run / 'progress.json', summary)

        def actions():
            yield from route
            if queue is None:
                return
            offset, pending = 0, ''
            progress()
            while poll():
                with queue.open('r', encoding='utf-8') as source:
                    source.seek(offset); pending += source.read(); offset = source.tell()
                lines = pending.split('\n'); pending = lines.pop()
                for line in lines:
                    if not line.strip():
                        continue
                    item = json.loads(line)
                    if isinstance(item, dict) and set(item) == {'close'} and item['close'] is True:
                        summary['interactive_close_requested'] = True; progress(); return
                    if isinstance(item, dict) and set(item) == {'capture'} and isinstance(item['capture'], str):
                        capture(item['capture']); progress(); continue
                    action = validate_route([item])[0]
                    route.append(action)
                    yield action
                time.sleep(.1)

        while poll() and not ready():
            if intro_skip_after is not None and movie_ready_at is not None and not summary.get('intro_skip_requested'):
                if time.monotonic() - movie_ready_at >= intro_skip_after:
                    capture('intro-before-skip')
                    summary['intro_skip_requested'] = True
                    with channel.open('ab', buffering=0) as out:
                        out.write(b'START\n')
            time.sleep(.1)
        if ready() and wait(5):
            capture('opening')
            for action in actions():
                if not poll():
                    break
                marker = action.get('wait_for_log')
                if marker is not None:
                    occurrence = action.get('wait_for_log_occurrence', 1)
                    until = min(deadline, time.monotonic() + action.get('wait_for_log_ms', 60000) / 1000)
                    while log_boundary(log, marker, occurrence) is None and time.monotonic() < until and poll():
                        time.sleep(.1)
                    matched = log_boundary(log, marker, occurrence)
                    if matched is None:
                        summary['route_error'] = 'Original log boundary not reached: ' + marker + ' occurrence ' + str(occurrence)
                        break
                    summary.setdefault('log_boundaries', []).append(dict(action=action['name'], marker=marker,
                        occurrence=occurrence, matched=matched))
                before_receipts = len(receipts)
                attempt = dict(action=action, elapsed=time.monotonic() - started, delivered=False)
                summary['actions'].append(attempt)
                with channel.open('ab', buffering=0) as out:
                    out.write(command_text(action).encode('ascii'))
                until = min(deadline, time.monotonic() + receipt_timeout)
                while len(receipts) == before_receipts and time.monotonic() < until and poll():
                    time.sleep(.05)
                if len(receipts) > before_receipts:
                    attempt['receipt'] = receipts[before_receipts]
                    named = dict(START=0x10, BACK=0x20, A=0x1000, B=0x2000, UP=1, DOWN=2, LEFT=4, RIGHT=8)
                    expected = ([named[action['command']], 0, 0, 0, 0] if 'command' in action else
                                [action['buttons'], action['ms'], action['lx'], action['ly'], action['rt']])
                    attempt['delivered'] = attempt['receipt'] == expected
                    if not attempt['delivered']:
                        summary['route_error'] = 'Mismatched command receipt: ' + action['name']; break
                else:
                    summary['route_error'] = 'Unacknowledged command: ' + action['name']; break
                if not wait(action.get('ms', 0) / 1000 + action.get('wait_after_ms', 1000) / 1000):
                    break
                if action.get('capture_after', False):
                    capture(action['name'])
                if queue is not None:
                    progress()
            if wait(3):
                capture('after-route')
        summary['elapsed_seconds'] = time.monotonic() - started
        summary['deadline_reached'] = time.monotonic() >= deadline
        summary['close'] = close_owned(process)
        poll()
        summary['returncode'] = process.returncode
        summary['encounter_lines'] = sum(1 for _ in audit.open(encoding='utf-8')) if audit.exists() else 0
    except Exception as error:
        summary['runner_error'] = str(error)
    finally:
        if process is not None and process.poll() is None:
            summary['close'] = close_owned(process)
        after = dict(profile=tree_hashes(source_profile), content=tree_hashes(source_content))
        if video.exists():
            after['video'] = digest(video)
        summary['primary_unchanged'] = before == after
        write_json(run / 'primary-after.json', after)
        summary['success'] = ((summary['stage_ready'] or (stage == 'frontend' and summary['frontend_presented'])) and summary.get('close') == 'owned-window-close'
                              and not summary['failures'] and not summary.get('runner_error')
                              and not summary.get('route_error') and not summary.get('deadline_reached')
                              and len(summary['actions']) == len(route)
                              and all(action['delivered'] for action in summary['actions']) and before == after)
        write_json(run / 'result.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stages', nargs='+', choices=STAGES)
    parser.add_argument('--frontend', action='store_true', help='Use ordinary frontend startup; presentation is not map readiness')
    parser.add_argument('--interactive', action='store_true', help='Accept bounded actions/capture/close requests from the owned evidence queue')
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/native/SimpsonsNative.exe')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--route', type=Path, help='JSON action list using bounded PAD values or named commands')
    parser.add_argument('--baseline-video', action='store_true', help='Use private 720p windowed 60 FPS preferences for repeatable capture; original preferences are unchanged')
    parser.add_argument('--timeout', type=float, default=150, help='Finite owned-run deadline of 20..1800 seconds')
    parser.add_argument('--receipt-timeout', type=float, default=10, help='Seconds to wait for each delivery receipt (5..60); a stalled game stops the route as an unacknowledged command')
    parser.add_argument('--outro-skip-after', type=float, help='With --completion: deliver Start this many seconds (0.2..4, before the first route action) after the original movie readiness of the outro that follows the completion helper')
    parser.add_argument('--completion', action='store_true', help='Launch the first-mission completion transition (the native shortcut calls the original EpisodeComplete helper once); exclusive with stage options')
    parser.add_argument('--play-intro', action='store_true', help='Play original stage opening movies')
    parser.add_argument('--intro-skip-after', type=float, help='Deliver Start this many seconds after original movie readiness')
    args = parser.parse_args()
    if args.frontend and (args.stages or args.play_intro or args.intro_skip_after is not None):
        parser.error('Frontend startup is exclusive with stage or stage-intro options')
    if not math.isfinite(args.receipt_timeout) or not 5 <= args.receipt_timeout <= 60:
        parser.error('Receipt timeout must be 5..60 seconds')
    if args.outro_skip_after is not None and (not args.completion or not math.isfinite(args.outro_skip_after) or not 0.2 <= args.outro_skip_after <= 4):
        parser.error('Outro skip requires --completion and a delay of 0.2..4 seconds')
    if args.completion and (args.frontend or args.stages or args.play_intro or args.intro_skip_after is not None):
        parser.error('Completion mode is exclusive with stage, frontend and intro options')
    stages = ['first_mission_completion'] if args.completion else ['frontend'] if args.frontend else (args.stages or list(STAGES))
    if args.interactive and (len(stages) != 1 or not args.output):
        parser.error('Interactive observation needs one stage/frontend and an explicit output directory')
    if os.name != 'nt' or not math.isfinite(args.timeout) or not 20 <= args.timeout <= 1800:
        parser.error('Windows and a finite bounded timeout of 20..1800 seconds are required')
    if args.intro_skip_after is not None and (not args.play_intro or not 1 <= args.intro_skip_after <= 120):
        parser.error('Intro skip requires playback and a delay of 1..120 seconds')
    executable = args.executable.resolve()
    if not executable.is_file() or not executable.is_relative_to(ROOT / 'build'):
        parser.error('Executable must be a current workspace build')
    parent = (args.output or ROOT / 'build/restrictive-audit/stage-runs' /
              dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d-%H%M%SZ')).absolute()
    if not parent.resolve().is_relative_to((ROOT / 'build').resolve()):
        parser.error('Evidence directory must be inside workspace build')
    parent.mkdir(parents=True, exist_ok=False)
    runner_snapshot = parent / 'runner-source.py';runner_snapshot.write_bytes(Path(__file__).read_bytes())
    config = json.loads((ROOT / 'config/startup_replay.json').read_text())
    route = validate_route(json.loads(args.route.read_text()) if args.route else ([] if args.interactive else SMOKE))
    queue = parent / 'interactive-actions.jsonl' if args.interactive else None
    if queue:
        queue.write_bytes(b'')
    write_json(parent / 'run-manifest.json', dict(schema=1, stages=stages, frontend=args.frontend, route=route,
        runner_source=dict(path=runner_snapshot.name, sha256=digest(runner_snapshot)),
        startup_config=dict(path='config/startup_replay.json', sha256=digest(ROOT / 'config/startup_replay.json')),
        executable=dict(path=str(executable), sha256=digest(executable)), image_sha256=digest(ROOT / 'analysis/simpsons.pe'),
        play_intro=args.play_intro, intro_skip_after=args.intro_skip_after, timeout=args.timeout,
        baseline_video=args.baseline_video, completion=args.completion, receipt_timeout=args.receipt_timeout, outro_skip_after=args.outro_skip_after,
        interactive_queue=queue.name if queue else None))
    results = []
    for stage in stages:
        result = run_stage(stage, parent, executable, config, route, args.timeout, args.play_intro, args.intro_skip_after, queue, args.baseline_video, args.completion, args.receipt_timeout,
                           outro_skip_after=args.outro_skip_after)
        results.append(result)
        write_json(parent / 'summary.json', dict(schema=1, results=results))
        print(json.dumps({key: result.get(key) for key in ('stage', 'success', 'stage_ready', 'elapsed_seconds', 'failures', 'runner_error', 'run')}), flush=True)
    if queue:
        write_json(parent / 'interactive-queue-receipt.json', dict(path=queue.name, sha256=digest(queue), bytes=queue.stat().st_size))
    return 0 if all(case['success'] for case in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
