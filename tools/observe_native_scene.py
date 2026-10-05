"""Observe native scene receipts; optionally request raw renderer captures, never input."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import datetime
import json
import math
import os
import re
import time

from gameplay_evidence import EvidenceError, verify_gameplay_frame

ROOT = Path(__file__).resolve().parents[1]
FAILURES = ('[FAILURE]', '[AOT FAILURE]', '[THREAD FAILURE]', '[TERMINATE]')
PRESENT = re.compile(
    r'\[NATIVE PRESENT\] copy=(\d+).*?completed=1 display=(accepted|occluded).*?'
    r'scene_geometry_draws=(\d+) frame_scene_geometry_draws=(\d+)')


def parse_receipt(line):
    """Return only authoritative completed presentation counters."""
    match = PRESENT.search(line)
    if not match:
        return None
    presentation, display, cumulative, frame = match.groups()
    cumulative, frame = int(cumulative), int(frame)
    if frame > cumulative:
        raise ValueError('Frame scene count exceeds cumulative scene count')
    return dict(presentation=int(presentation), display_accepted=display == 'accepted',
                scene_geometry_draws=cumulative, frame_scene_geometry_draws=frame)


class Tail:
    def __init__(self, path):
        self.path, self.offset, self.partial = path, 0, b''

    def read(self):
        if not self.path.exists():
            return []
        with self.path.open('rb') as stream:
            if os.fstat(stream.fileno()).st_size < self.offset:
                raise ValueError('Observed log was truncated: ' + str(self.path))
            stream.seek(self.offset)
            data = stream.read(4 * 1024 * 1024)
            self.offset = stream.tell()
        rows = (self.partial + data).split(b'\n')
        self.partial = rows.pop()
        return [row.decode('utf-8', errors='replace') for row in rows]

    def caught_up(self):
        return self.path.exists() and self.offset == self.path.stat().st_size


class Process:
    """Hold a query/synchronize-only handle; never signal or terminate the process."""
    def __init__(self, pid, launch_time):
        if os.name != 'nt':
            raise OSError('Live observation requires Windows')
        if type(pid) is not int or not 0 < pid <= 0xFFFFFFFF:
            raise ValueError('Invalid launch PID')
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.api.CloseHandle.restype = wintypes.BOOL
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetExitCodeProcess.restype = wintypes.BOOL
        self.api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.api.GetProcessTimes.restype = wintypes.BOOL
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            times = [wintypes.FILETIME() for _ in range(4)]
            if not self.api.GetProcessTimes(self.handle, *(ctypes.byref(t) for t in times)):
                raise ctypes.WinError(ctypes.get_last_error())
            created = ((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime) / 1e7 - 11644473600
            if created > launch_time + 1:
                raise ValueError('Launch PID was reused after launch.json was written')
        except (OSError, ValueError, ctypes.WinError):
            self.close()
            raise

    def exit_code(self):
        status = self.api.WaitForSingleObject(self.handle, 0)
        if status == 258:
            return None
        if status != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        code = wintypes.DWORD()
        if not self.api.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def waiting_movie(event, sequence_complete):
    return (not sequence_complete and event.get('event') == 'delivered'
            and event.get('button') == 'A' and event.get('screen') == 'main-menu')


def observe(args):
    run = args.run_directory.resolve()
    if not run.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('Run directory must be inside this workspace build folder')
    started = time.monotonic()
    deadline = started + args.duration
    log, inputs = Tail(run / 'game.log'), Tail(run / 'inputs.jsonl')
    process = None
    event = {}
    sequence_complete = False
    seen = set()
    live_scene_count = 0
    live_tail = False
    first = last = None
    last_presentation = last_draws = 0
    last_scene_presentation = 0
    requests = 0
    next_capture = started
    last_requested_presentation = 0
    failure = None
    exit_code = None
    reason = 'duration_expired'
    try:
        while time.monotonic() < deadline:
            launch = run / 'launch.json'
            if process is None and launch.exists():
                try:
                    config = json.loads(launch.read_text(encoding='utf-8'))
                except json.JSONDecodeError:
                    time.sleep(.05)
                    continue  # Launcher may still be writing this small file.
                process = Process(config['pid'], launch.stat().st_mtime)
            for row in inputs.read():
                event = json.loads(row)
                sequence_complete |= event.get('event') == 'sequence-complete'
            for line in log.read():
                if any(marker in line for marker in FAILURES):
                    failure = line.strip()
                    break
                receipt = parse_receipt(line)
                if receipt is None:
                    continue
                presentation = receipt['presentation']
                if presentation <= last_presentation:
                    continue
                if receipt['scene_geometry_draws'] < last_draws:
                    raise ValueError('Cumulative scene draw receipt regressed')
                last_presentation, last_draws = presentation, receipt['scene_geometry_draws']
                if receipt['display_accepted'] and receipt['frame_scene_geometry_draws']:
                    seen.add(presentation)
                    last_scene_presentation = presentation
                    if live_tail:
                        live_scene_count += 1
                        last = time.monotonic() - started
                        if first is None:
                            first = last
            if failure:
                reason = 'failure'
                break
            if log.caught_up():
                live_tail = True  # Initial historical backlog cannot establish a time span.
            if process is not None:
                exit_code = process.exit_code()
                if exit_code is not None:
                    # Drain buffered receipts before ending, without issuing requests.
                    if not log.caught_up():
                        continue
                    reason = 'process_exit'
                    break
            now = time.monotonic()
            if (args.capture and process is not None and seen and log.caught_up()
                    and inputs.caught_up() and waiting_movie(event, sequence_complete)
                    and last_scene_presentation == last_presentation
                    and last_scene_presentation > last_requested_presentation and now >= next_capture):
                # Exclusive creation never overwrites Replay's pending request.
                # Only the Continue-delivered/movie-wait phase permits us to write.
                try:
                    with (run / 'captures' / 'capture.request').open('xb'):
                        pass
                except FileExistsError:
                    pass
                else:
                    requests += 1
                    last_requested_presentation = last_presentation
                    next_capture = now + args.capture_interval
            time.sleep(.05)
    except (OSError, ValueError, KeyError) as error:
        failure, reason = str(error), 'observer_error'
    finally:
        if process is not None:
            process.close()
    span = 0 if first is None else last - first
    captures = []
    # Metadata and raw renderer bytes remain the authority; no screenshots or rewrites.
    validation_deadline = time.monotonic() + 5
    for path in sorted((run / 'captures').glob('native-frame-*.json'),
                       key=lambda p: int(p.stem.removeprefix('native-frame-')), reverse=True):
        if time.monotonic() >= validation_deadline:
            break
        try:
            meta = json.loads(path.read_text(encoding="utf-8"))
            if not meta.get('frame_scene_geometry_draws', 0):
                continue
            raw = path.with_suffix('.rgb10a2')
            evidence = verify_gameplay_frame(raw.read_bytes(), meta, minimum_presentation=0)
            captures.append(dict(frame=str(raw.relative_to(run)), **evidence))
        except (OSError, ValueError, EvidenceError):
            continue  # Incomplete/unqualified captures cannot provide evidence.
    report = dict(run_directory=str(run), elapsed_seconds=round(time.monotonic() - started, 3),
                  stop_reason=reason, failure=failure, process_exit_code=exit_code,
                  scene_presentations=len(seen), first_scene_seconds=first, last_scene_seconds=last,
                  live_scene_presentations=live_scene_count,
                  scene_span_seconds=round(span, 3), last_presentation=last_presentation,
                  last_cumulative_scene_draws=last_draws, capture_requests=requests,
                  replay_sequence_complete=sequence_complete,
                  sustained_scene_rendering=live_scene_count >= 30 and span >= 10,
                  timing_basis='observer_monotonic_receipt_time_not_game_timestamps',
                  gameplay_verified=False, character_control_verified=False, captures=captures,
                  capture_validation='available raw receipts checked within a 5-second final budget')
    # Historical logs read together do not manufacture elapsed scene time.
    output = run / ('scene-observation-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
    if run.is_dir():
        with output.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
    else:
        report['failure'] = failure = 'Run directory never appeared'
        report['stop_reason'] = 'observer_error'
        output = None
    print(json.dumps(dict(report=str(output), **{k: v for k, v in report.items() if k != 'captures'})))
    return 1 if failure or exit_code not in (None, 0) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_directory', type=Path)
    parser.add_argument('--duration', type=float, default=120, help='Observation seconds, 0 < value <= 3600')
    parser.add_argument('--capture', action='store_true', help='Opt in to capture.request writes during Replay movie wait')
    parser.add_argument('--capture-interval', type=float, default=2)
    args = parser.parse_args()
    if not math.isfinite(args.duration) or not 0 < args.duration <= 3600:
        parser.error('--duration must be finite and between 0 and 3600 seconds')
    if not math.isfinite(args.capture_interval) or not 0.1 <= args.capture_interval <= 3600:
        parser.error('--capture-interval must be between 0.1 and 3600 seconds')
    if os.name != 'nt':
        parser.error('Live observation requires Windows')
    return observe(args)


if __name__ == '__main__':
    raise SystemExit(main())
