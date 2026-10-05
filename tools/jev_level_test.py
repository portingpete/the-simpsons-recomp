"""Bounded Jev playtesting of the native Land of Chocolate build.

Startup uses the verified menu replay. Jev sees text derived from completed
renderer captures and chooses only controller actions that this script defines.
The run records actual controller receipts and fresh gameplay captures. A level
pass requires an explicit goal cue or log pattern; exploration alone is not a
level-completion claim.
"""
from __future__ import annotations

import argparse
from collections import deque
from collections.abc import Collection
from contextlib import nullcontext
from dataclasses import dataclass
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

from auto_start_native import ROOT, matches
from jev_observation import observe_frame
from jev_telemetry import capture_telemetry, movement_result
from observe_native_scene import Process, Tail


FAILURES = ('[FAILURE]', '[AOT FAILURE]', '[THREAD FAILURE]', '[TERMINATE]')
RECEIPT = re.compile(r'local command tap buttons=([0-9A-Fa-f]{4}) hold_ms=(\d+); '
                     r'left=\((-?\d+),(-?\d+)\)(?: rt=(\d+))?; delivered')

# The left stick is relative to the game camera. A is jump; B is the secondary
# gameplay button. X attacks (or dashes in Homer Ball), Y interacts, and RT
# activates or exits Homer Ball. The native command source bounds each hold to
# 50..2000 ms.
@dataclass(frozen=True)
class Action:
    buttons: int
    lx: int
    ly: int
    description: str
    rt: int = 0


ACTIONS = {
    'wait': Action(0, 0, 0, 'Pause briefly to observe animation or let the scene settle.'),
    'forward': Action(0, 0, 32767, 'Move forward in the camera-relative direction.'),
    'back': Action(0, 0, -32768, 'Move backward in the camera-relative direction.'),
    'left': Action(0, -32768, 0, 'Move left in the camera-relative direction.'),
    'right': Action(0, 32767, 0, 'Move right in the camera-relative direction.'),
    'forward_left': Action(0, -32768, 32767, 'Move diagonally forward and left.'),
    'forward_right': Action(0, 32767, 32767, 'Move diagonally forward and right.'),
    'jump': Action(0x1000, 0, 0, 'Jump in place.'),
    'forward_jump': Action(0x1000, 0, 32767, 'Jump while moving forward.'),
    'left_jump': Action(0x1000, -32768, 0, 'Jump while moving left.'),
    'right_jump': Action(0x1000, 32767, 0, 'Jump while moving right.'),
    'double_jump': Action(0x1000, 0, 0, 'Tap A, release it briefly in midair, then tap A again.'),
    'forward_double_jump': Action(0x1000, 0, 32767,
                                  'Move forward and tap A twice with an in-air release between taps.'),
    'attack': Action(0x4000, 0, 0, 'Use X to attack; in Homer Ball this may dash.'),
    'forward_attack': Action(0x4000, 0, 32767,
                             'Move forward with X attack, or dash forward when in Homer Ball.'),
    'interact': Action(0x8000, 0, 0, 'Use Y to interact with a nearby object or character.'),
    'ball': Action(0, 0, 0, 'Press RT to activate or exit Homer Ball; observe the result.', 255),
    'ball_forward': Action(0, 0, 32767,
                           'Press RT while moving forward; RT may change Homer Ball form.', 255),
    'ability': Action(0x2000, 0, 0, 'Use the B gameplay action.'),
}
DEFAULT_PLAY_ACTIONS = tuple(name for name in ACTIONS if name != 'ability')
MOVEMENT_ACTIONS = frozenset(('forward', 'back', 'left', 'right',
                              'forward_left', 'forward_right', 'ball_forward'))
FIRST_LEVEL_ROUTE = (
    'Land of Chocolate route hint from a walkthrough and prior visual review, '
    'not measured current game state: first chase chocolate bunnies through the '
    'village and find the opening in the pretzel fence. A prior run reached '
    'player position about [7.48, 0.01, -32.62] and was blocked by a solid brown '
    'embankment with a bridge overhead. If there, backtrack to open cobblestone, '
    'move laterally toward the fence opening, then double jump from marshmallow '
    'pads to a higher platform and cross the bridge. Later use X against a gate '
    'and spawners, Y at the red button in the cake clearing, jump across cookie '
    'platforms, punch the white rabbit, then RT for Homer Ball and X to dash '
    'into bunnies. Observe each result before moving on.'
)


def action_plan(action: str, duration_ms: int) -> tuple[tuple[int, int, int, int, int], ...]:
    """Produce native PAD holds; two A edges are one logical double-jump action."""
    if action not in ACTIONS:
        raise ValueError(f'Unknown gameplay action: {action}')
    if type(duration_ms) is not int or not 50 <= duration_ms <= 2000:
        raise ValueError('PAD duration must be 50..2000 ms')
    spec = ACTIONS[action]
    if action in ('double_jump', 'forward_double_jump'):
        # An A hold cannot generate a second button-down edge. Preserve stick
        # direction in the gap so a forward jump continues moving in the air.
        return ((spec.buttons, spec.lx, spec.ly, 100, 0),
                (0, spec.lx, spec.ly, 180, 0),
                (spec.buttons, spec.lx, spec.ly, 100, 0))
    return ((spec.buttons, spec.lx, spec.ly, duration_ms, spec.rt),)


def pad_command(action: str, duration_ms: int) -> bytes:
    lines = []
    for buttons, lx, ly, hold_ms, rt in action_plan(action, duration_ms):
        suffix = f' {rt}' if rt else ''
        lines.append(f'PAD {buttons:04X} {lx} {ly} {hold_ms}{suffix}\n')
    return ''.join(lines).encode('ascii')


def parse_receipt(line: str) -> tuple[int, int, int, int, int] | None:
    """Parse both current RT receipts and older four-field PAD receipts."""
    match = RECEIPT.search(line)
    if not match:
        return None
    buttons = int(match[1], 16)
    hold_ms, lx, ly = map(int, match.groups()[1:4])
    rt = int(match[5]) if match[5] is not None else 0
    return buttons, hold_ms, lx, ly, rt


def change_label(value: float | None) -> str:
    if value is None:
        return 'first observation'
    if value < 0.005:
        return 'very little visual change'
    if value < 0.03:
        return 'some visual change'
    return 'large visual change'


def decision_state(observation: dict, history: deque, objective: str,
                   telemetry: dict | None = None) -> dict:
    state = {
        'game': 'The Simpsons Game, Land of Chocolate, third-person platform adventure',
        'objective': objective,
        'route_hint': FIRST_LEVEL_ROUTE,
    }
    if telemetry:
        state['verified_game_state'] = telemetry
        state['game_state_note'] = ('These facts come from the native game at this exact captured presentation. '
                                    'World positions are in game coordinates; stick actions are camera-relative. '
                                    'world.camera_eye is the camera position, not a player or goal position. '
                                    'scene_depth_grid rows run screen top to bottom and columns left to right. '
                                    'It is logarithmically scaled reversed depth: 0 is far and 255 is near. '
                                    'Equal number steps do not mean equal world distances; '
                                    'it is not world distance, a collision map, or proof a route is walkable. '
                                    'A missing field has not been verified and should not be assumed.')
        player = telemetry.get('player')
        if player and isinstance(player.get('position'), list):
            current = player['position']
            blocked = []
            for result in history:
                motion = result.get('observed_motion') or {}
                endpoint = result.get('end_position')
                if (result.get('action') in MOVEMENT_ACTIONS and
                        isinstance(endpoint, list) and len(endpoint) == 3 and
                        isinstance(motion.get('player_distance'), (float, int)) and
                        motion['player_distance'] < 0.25 and
                        math.dist((current[0], current[2]),
                                  (endpoint[0], endpoint[2])) < 0.6):
                    blocked.append(result['action'])
            if blocked:
                state['temporarily_blocked_actions'] = sorted(set(blocked))
                state['navigation_warning'] = (
                    'At this position these movement actions produced under '
                    '0.25 game units of displacement. Try turning, jumping, '
                    'attacking, or interacting; do not repeat a blocked move.')
            earlier = [item.get('end_position') for item in list(history)[:-1]]
            if any(isinstance(position, list) and len(position) == 3 and
                   math.dist((current[0], current[2]),
                             (position[0], position[2])) < 0.6
                   for position in earlier):
                state['revisited_recent_position'] = True
                state['navigation_warning'] = (
                    state.get('navigation_warning', '') +
                    ' This position was recently visited; explore a different route '
                    'rather than moving back and forth.').strip()
    state.update({
        'recent_actions': list(history),
        'last_visual_change': change_label(observation['frame_change_fraction']),
        'view': ('These 32x18 grids summarize the current 1280x720 game frame from top to bottom. '
                 'R/G/B/Y/C/M/W are bright red/green/blue/yellow/cyan/magenta/white; lowercase is dim; '
                 'a dot is black. The luminance grid uses spaces for dark and @ for bright. '
                 'This is a coarse visual summary, not exact geometry.'),
        'color_grid': observation['color_grid'],
        'luminance_grid': observation['luminance_grid'],
    })
    return state


def record_action_result(history: deque, observation: dict, prior_telemetry: dict | None,
                         telemetry: dict | None, log, frame: str) -> None:
    """Close the pending action against this exact successor capture."""
    if not history or history[-1]['visual_change'] != 'pending':
        return
    result = history[-1]
    result['visual_change'] = change_label(observation['frame_change_fraction'])
    motion = movement_result(prior_telemetry, telemetry)
    if motion:
        result['observed_motion'] = motion
    player = telemetry and telemetry.get('player')
    if player and isinstance(player.get('position'), list):
        result['end_position'] = player['position']
    log.write(json.dumps({'event': 'action-result', 'step': result['step'],
                          'action': result['action'], 'frame': frame,
                          'presentation': observation['presentation'],
                          'visual_change': result['visual_change'],
                          'observed_motion': motion}, separators=(',', ':')) + '\n')


def parse_choice(response, allowed_actions: Collection[str] = ACTIONS) -> dict:
    answer = response.choices['action']
    action = answer.choice
    confidence = answer.confidence
    probabilities = answer.probabilities
    if action not in allowed_actions:
        raise ValueError(f'Jev returned an action outside the allowed set: {action!r}')
    if (not isinstance(confidence, (float, int)) or not math.isfinite(confidence)
            or not 0 <= confidence <= 1):
        raise ValueError('Jev returned invalid confidence')
    if not isinstance(probabilities, dict) or set(probabilities) != set(allowed_actions):
        raise ValueError('Jev returned an incomplete action distribution')
    if any(not isinstance(p, (float, int)) or not math.isfinite(p) or not 0 <= p <= 1
           for p in probabilities.values()):
        raise ValueError('Jev returned invalid action probabilities')
    return {
        'action': action,
        'confidence': float(confidence),
        'probabilities': {key: float(probabilities[key]) for key in allowed_actions},
        'model': response.model,
        'request_id': response.request_id,
        'input_tokens': response.usage.input_tokens,
        'output_tokens': response.usage.output_tokens,
    }


def redact_secret(message: str) -> str:
    key = os.environ.get('TYPESAFE_API_KEY')
    return message.replace(key, '[redacted API key]') if key else message


class JevPolicy:
    def __init__(self, model: str, *, allow_b: bool = False):
        os.environ.setdefault('TYPESAFE_LOG_LEVEL', 'off')
        try:
            from typesafe_sdk import Choice, RetryPolicy, TypeSafeClient
        except ImportError as error:
            raise RuntimeError('Install the official SDK: python -m pip install typesafe-sdk') from error
        self._choice_type = Choice
        self._allowed_actions = tuple(ACTIONS) if allow_b else DEFAULT_PLAY_ACTIONS
        self._instructions = ('Choose the next controller action to complete the first level. '
                              'Follow the route hint as a guide, but use verified game state and '
                              'measured player movement for the current situation. '
                              'Recent player_distance is observed player world-space movement; '
                              'camera_distance is only camera motion. Scene depth gives near/far '
                              'order on the screen, not walkability. If an action is blocked at '
                              'the current location or a position is revisited, try a materially '
                              'different action or route. X attacks and can dash in Homer Ball; '
                              'Y interacts; RT toggles Homer Ball. Double jump is two A taps. '
                              'Choose only one listed action. The game applies physics and rules.')
        self._client = TypeSafeClient(model=model, retry=RetryPolicy(max_retries=1, timeout=20.0))

    def __enter__(self):
        self._client.__enter__()
        return self

    def __exit__(self, *args):
        return self._client.__exit__(*args)

    def choose(self, state: dict, step: int) -> dict:
        blocked = set(state.get('temporarily_blocked_actions', ()))
        allowed = tuple(name for name in self._allowed_actions if name not in blocked)
        choice = self._choice_type(
            instructions=self._instructions,
            criteria={name: ACTIONS[name].description for name in allowed},
        )
        response = self._client.system_one(state=state, questions={'action': choice})
        return parse_choice(response, allowed_actions=allowed)


class SmokePolicy:
    SCRIPT = ('jump', 'forward', 'forward_jump')

    def __init__(self, script: tuple[str, ...] | None = None):
        self.script = script if script is not None else self.SCRIPT

    def choose(self, state: dict, step: int) -> dict:
        return {'action': self.script[step % len(self.script)], 'model': 'scripted-smoke',
                'confidence': None, 'probabilities': None, 'request_id': None,
                'input_tokens': 0, 'output_tokens': 0}


def parse_smoke_actions(value: str | None) -> tuple[str, ...]:
    """Accept a bounded, explicit list of controller actions for a smoke run."""
    if value is None:
        return SmokePolicy.SCRIPT
    names = tuple(name.strip() for name in value.split(','))
    if not 1 <= len(names) <= 32 or any(name not in ACTIONS for name in names):
        raise ValueError('Smoke actions must be 1..32 comma-separated known action names')
    return names


class GameSession:
    def __init__(self, run: Path, pid: int, main_menu_presentation: int, goal_log: re.Pattern | None):
        self.run = run
        self.process = Process(pid, (run / 'launch.json').stat().st_mtime)
        self.tail = Tail(run / 'game.log')
        # Check the startup/play boundary for failures, then arm goal matching
        # only for new log lines. An old completion marker cannot pass this run.
        size = self.tail.path.stat().st_size
        with self.tail.path.open('rb') as stream:
            stream.seek(max(0, size - 1024 * 1024))
            recent = stream.read(size - stream.tell()).decode('utf-8', errors='replace')
        for line in recent.splitlines():
            if any(marker in line for marker in FAILURES):
                self.process.close()
                raise RuntimeError('Native game failure at startup/play boundary: ' + line.strip()[:500])
        self.tail.offset = size
        self.receipts: list[tuple[int, int, int, int]] = []
        self.goal_log = goal_log
        self.goal_log_seen = False
        self.main_menu_presentation = main_menu_presentation
        self.presentation = main_menu_presentation
        self.last_capture_number = max((self.frame_number(path) for path in (run / 'captures').glob('native-frame-*.rgb10a2')), default=-1)

    @staticmethod
    def frame_number(path: Path) -> int:
        try:
            return int(path.stem.rsplit('-', 1)[1])
        except (IndexError, ValueError):
            return -1

    def check(self):
        for _ in range(16):
            for line in self.tail.read():
                if any(marker in line for marker in FAILURES):
                    raise RuntimeError('Native game failure: ' + line.strip()[:500])
                if 'game-window keyboard buttons=' in line and 'buttons=0000' not in line:
                    raise RuntimeError('Manual keyboard input took control during the automated run')
                if 'Windows controller slot=0 status=0;' in line:
                    raise RuntimeError('A physical controller took slot 0 during the automated run')
                receipt = parse_receipt(line)
                if receipt:
                    self.receipts.append(receipt)
                if self.goal_log and self.goal_log.search(line):
                    self.goal_log_seen = True
            if self.tail.caught_up():
                break
        code = self.process.exit_code()
        if code is not None:
            raise RuntimeError(f'Native game exited during automated play (code {code})')

    def wait(self, seconds: float):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.check()
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))

    def capture(self, timeout: float = 45.0) -> tuple[Path, dict, bytes]:
        directory = self.run / 'captures'
        request = directory / 'capture.request'
        deadline = time.monotonic() + timeout
        while True:
            self.check()
            if time.monotonic() >= deadline:
                raise TimeoutError('Waiting for renderer capture request ownership')
            try:
                with request.open('xb'):
                    pass
                break
            except FileExistsError:
                time.sleep(0.02)
        while request.exists():
            self.check()
            if time.monotonic() >= deadline:
                raise TimeoutError('Renderer did not complete the requested capture')
            time.sleep(0.02)
        frames = (path for path in directory.glob('native-frame-*.rgb10a2')
                  if self.frame_number(path) > self.last_capture_number)
        latest = max(frames, key=self.frame_number, default=None)
        if latest is None:
            raise RuntimeError('Renderer acknowledged capture without a new frame')
        meta = json.loads(latest.with_suffix('.json').read_text(encoding='utf-8'))
        data = latest.read_bytes()
        self.last_capture_number = self.frame_number(latest)
        return latest, meta, data

    def act(self, action: str, duration_ms: int, on_delivery=None):
        self.check()
        before = len(self.receipts)
        plan = action_plan(action, duration_ms)
        command = pad_command(action, duration_ms)
        started = time.monotonic()
        with (self.run / 'controller.commands').open('ab', buffering=0) as stream:
            if stream.write(command) != len(command):
                raise OSError('Incomplete gameplay command write')
        deadline = time.monotonic() + 8 + sum(hold_ms for _, _, _, hold_ms, _ in plan) / 1000
        while len(self.receipts) < before + len(plan):
            self.check()
            if time.monotonic() >= deadline:
                raise TimeoutError(f'Game did not consume PAD action {action}')
            time.sleep(0.02)
        delivered = self.receipts[before:before + len(plan)]
        expected = [(buttons, hold_ms, lx, ly, rt)
                    for buttons, lx, ly, hold_ms, rt in plan]
        if delivered != expected:
            raise RuntimeError(f'Unexpected controller receipt: {delivered}, expected {expected}')
        if on_delivery is not None:
            on_delivery(delivered)
        # The last receipt occurs at the beginning of its hold. Wait for the
        # final release and the requested observation interval before capture.
        target = started + max(duration_ms, sum(hold_ms for _, _, _, hold_ms, _ in plan)) / 1000 + 0.08
        self.wait(max(plan[-1][3] / 1000 + 0.08,
                      target - time.monotonic()))

    def close(self):
        self.process.close()


def prepare_run(args) -> Path:
    run = (args.run_directory or ROOT / 'build' / 'jev-level-tests' /
           dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')).resolve()
    if not run.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('Run directory must be inside the workspace build folder')
    if run.exists():
        raise FileExistsError(f'Run directory already exists: {run}')
    run.parent.mkdir(parents=True, exist_ok=True)
    return run


def compact_startup_captures(run: Path, result: dict) -> int:
    """Retain cited startup frames and the last frame; discard unused readbacks."""
    captures = run / 'captures'
    keep = set()

    def collect(value):
        if isinstance(value, dict):
            for child in value.values():
                collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)
        elif isinstance(value, str):
            name = Path(value).name
            if re.fullmatch(r'native-frame-\d+\.rgb10a2', name):
                keep.add(name)

    collect(result)
    events = run / 'inputs.jsonl'
    if events.is_file():
        with events.open(encoding='utf-8') as stream:
            for line in stream:
                collect(json.loads(line))
    frames = list(captures.glob('native-frame-*.rgb10a2'))
    if frames:
        keep.add(max(frames, key=GameSession.frame_number).name)
    removed = 0
    for frame in frames:
        if frame.name not in keep:
            frame.unlink()
            frame.with_suffix('.json').unlink(missing_ok=True)
            removed += 1
    return removed


def startup(run: Path, args) -> tuple[int, dict]:
    command = [sys.executable, '-B', str(ROOT / 'tools' / 'auto_start_native.py'),
               '--executable', str(args.executable), '--run-directory', str(run),
               '--isolate-data', '--allow-occluded']
    # The startup helper and game need no Jev credentials. Keep the key in this
    # runner process, where only the Jev policy can use it.
    child_env = {name: value for name, value in os.environ.items()
                 if not name.upper().startswith('TYPESAFE_')}
    with (run.parent / (run.name + '.startup.log')).open('wb') as output:
        completed = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                   timeout=args.startup_timeout, check=False, env=child_env)
    result_path = run / 'result.json'
    if not result_path.is_file():
        raise RuntimeError(f'Startup exited {completed.returncode} without result.json')
    result = json.loads(result_path.read_text(encoding='utf-8'))
    if completed.returncode or not result.get('success') or not result.get('gameplay_verified'):
        raise RuntimeError('Verified startup failed: ' + str(result.get('reason', completed.returncode)))
    launch = json.loads((run / 'launch.json').read_text(encoding='utf-8'))
    return launch['pid'], result


def prerequisite_report(play: bool, executable: Path) -> dict:
    config = json.loads((ROOT / 'config' / 'startup_replay.json').read_text(encoding='utf-8'))
    profile = ROOT / config['profile_store'] / (config['profile_id'] + '.profile')
    save = ROOT / config['content_store'] / config['save_index']
    try:
        import typesafe_sdk  # noqa: F401
        sdk = True
    except ImportError:
        sdk = False
    checks = {
        'windows': os.name == 'nt',
        'executable': executable.is_file(),
        'original_image': (ROOT / 'analysis' / 'simpsons.pe').is_file(),
        'profile': profile.is_file(),
        'save': save.is_file(),
        'typesafe_sdk': sdk,
        'typesafe_api_key_present': bool(os.environ.get('TYPESAFE_API_KEY')),
    }
    checks['ready'] = all(checks[key] for key in ('windows', 'executable', 'original_image', 'profile', 'save')) and (
        not play or (checks['typesafe_sdk'] and checks['typesafe_api_key_present']))
    return checks


def validate_goal_cue(cue: object) -> dict:
    if not isinstance(cue, dict):
        raise ValueError('Goal cue must be a JSON object')
    samples = cue.get('samples')
    tolerance = cue.get('tolerance')
    minimum_match = cue.get('minimum_match')
    if not isinstance(samples, list) or not 1 <= len(samples) <= 256:
        raise ValueError('Goal cue requires 1..256 pixel samples')
    if type(tolerance) is not int or not 0 <= tolerance <= 1023:
        raise ValueError('Goal cue tolerance must be an integer 0..1023')
    if (type(minimum_match) not in (float, int) or not math.isfinite(minimum_match)
            or not 0 < minimum_match <= 1):
        raise ValueError('Goal cue minimum_match must be above zero and at most one')
    for sample in samples:
        if (not isinstance(sample, list) or len(sample) != 4
                or any(type(value) is not int for value in sample)
                or not 0 <= sample[0] <= 1280 * 720 * 4 - 4 or sample[0] % 4
                or any(not 0 <= value <= 1023 for value in sample[1:])):
            raise ValueError('Goal cue samples must contain a valid pixel byte offset and RGB10 codes')
    if 'exclude' in cue:
        validate_goal_cue(cue['exclude'])
    return cue


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('doctor', 'smoke', 'play'))
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/native/SimpsonsNative.exe')
    parser.add_argument('--run-directory', type=Path)
    parser.add_argument('--model', default='jev-1.13.0', help='Versioned Jev model for reproducible runs')
    parser.add_argument('--objective', default='Complete Land of Chocolate, the first story level.')
    parser.add_argument('--goal-cue', type=Path, help='JSON screen cue using the startup replay sample format')
    parser.add_argument('--goal-log-regex', help='A known, authoritative level-completion log marker')
    parser.add_argument('--max-decisions', type=int, default=40)
    parser.add_argument('--wall-seconds', type=float, default=600)
    parser.add_argument('--action-ms', type=int, default=600)
    parser.add_argument('--smoke-actions',
                        help='Comma-separated action names for smoke mode; repeats if decisions exceed list length')
    parser.add_argument('--allow-b', action='store_true',
                        help='Offer the optional B ability to Jev; currently risks a known native particle-shader failure')
    parser.add_argument('--startup-timeout', type=float, default=300)
    parser.add_argument('--keep-game-running', action='store_true')
    return parser


def main(argv=None) -> int:
    args = create_parser().parse_args(argv)
    args.executable = args.executable.resolve()
    if args.smoke_actions is not None and args.mode != 'smoke':
        raise SystemExit('--smoke-actions is only valid in smoke mode')
    try:
        smoke_script = parse_smoke_actions(args.smoke_actions)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    if args.mode == 'doctor':
        report = prerequisite_report(True, args.executable)
        print(json.dumps(report, indent=2))
        return 0 if report['ready'] else 1
    if not 1 <= args.max_decisions <= 10000 or not 10 <= args.wall_seconds <= 86400:
        raise SystemExit('Invalid decision or wall-time bound')
    if not 50 <= args.action_ms <= 2000 or not 10 <= args.startup_timeout <= 3600:
        raise SystemExit('Action duration or startup timeout is outside the supported range')
    if args.executable.name != 'SimpsonsNative.exe' or not args.executable.is_relative_to((ROOT / 'build').resolve()):
        raise SystemExit('Expected SimpsonsNative.exe in the workspace build folder')
    report = prerequisite_report(args.mode == 'play', args.executable)
    if not report['ready']:
        print(json.dumps(report, indent=2))
        return 1
    cue = None
    if args.goal_cue:
        cue = validate_goal_cue(json.loads(args.goal_cue.read_text(encoding='utf-8')))
    goal_log = re.compile(args.goal_log_regex) if args.goal_log_regex else None
    run = prepare_run(args)
    summary = {'mode': args.mode, 'model_requested': args.model if args.mode == 'play' else 'scripted-smoke',
               'run_directory': str(run), 'outcome': 'startup_failed', 'decisions': 0,
               'goal_configured': bool(cue or goal_log), 'key_recorded': False,
               'b_ability_enabled': bool(args.allow_b and args.mode == 'play'),
               'smoke_actions': list(smoke_script) if args.mode == 'smoke' else None,
               'telemetry_frames': 0, 'player_telemetry_frames': 0,
               'world_telemetry_frames': 0, 'scene_depth_grid_frames': 0}
    session = None
    pid = None
    stage = 'startup'
    started = time.monotonic()
    try:
        pid, startup_result = startup(run, args)
        summary['startup_gameplay_verified'] = startup_result['gameplay_verified']
        summary['unused_startup_captures_removed'] = compact_startup_captures(run, startup_result)
        menu = startup_result['gameplay_evidence']['first_scene_frame']['presentation'] - 1
        session = GameSession(run, pid, menu, goal_log)
        deadline = started + args.wall_seconds
        history = deque(maxlen=6)
        previous_data = None
        previous_telemetry = None
        unique_frames = set()
        stage = 'provider' if args.mode == 'play' else 'smoke_policy'
        policy = JevPolicy(args.model, allow_b=args.allow_b) if args.mode == 'play' else SmokePolicy(smoke_script)
        with policy if args.mode == 'play' else nullcontext(policy):
            with (run / 'decisions.jsonl').open('x', encoding='utf-8', buffering=1) as log:
                for step in range(args.max_decisions):
                    if time.monotonic() >= deadline:
                        summary['outcome'] = 'wall_time_exhausted'
                        break
                    stage = 'capture'
                    path, meta, data = session.capture()
                    observation = observe_frame(data, meta, minimum_presentation=session.presentation,
                                                previous_data=previous_data, allow_occluded=True)
                    telemetry, telemetry_status = capture_telemetry(meta, observation['presentation'])
                    record_action_result(history, observation, previous_telemetry, telemetry,
                                         log, str(path.relative_to(run)))
                    session.presentation = observation['presentation']
                    previous_data = data
                    previous_telemetry = telemetry
                    unique_frames.add(observation['pixel_sha256'])
                    if telemetry:
                        summary['telemetry_frames'] += 1
                        summary['player_telemetry_frames'] += int(bool(telemetry['player']))
                        summary['world_telemetry_frames'] += int(bool(telemetry['world']))
                        summary['scene_depth_grid_frames'] += int(bool(telemetry['scene_depth_grid']))
                    summary['last_telemetry_status'] = telemetry_status
                    if session.goal_log_seen or (cue and matches(data, cue)):
                        summary['outcome'] = 'goal_observed'
                        summary['goal_evidence'] = {'frame': str(path.relative_to(run)),
                                                    'presentation': session.presentation,
                                                    'source': 'log' if session.goal_log_seen else 'screen_cue'}
                        break
                    state = decision_state(observation, history, args.objective, telemetry)
                    stage = 'provider' if args.mode == 'play' else 'smoke_policy'
                    decision = policy.choose(state, step)
                    event = {'step': step, 'observation': observation,
                             'telemetry': telemetry, 'telemetry_status': telemetry_status,
                             'decision': decision,
                             'frame': str(path.relative_to(run)), 'command':
                             pad_command(decision['action'], args.action_ms).decode('ascii').strip()}
                    log.write(json.dumps(event, separators=(',', ':')) + '\n')
                    summary['last_action_attempted'] = decision['action']
                    def record_delivery(receipt):
                        summary['last_action_delivered'] = decision['action']
                        event = {'event': 'controller-delivered', 'step': step,
                                 'action': decision['action']}
                        if len(receipt) == 1:
                            event['receipt'] = receipt[0]
                        else:
                            event['receipts'] = receipt
                        log.write(json.dumps(event,
                                             separators=(',', ':')) + '\n')
                    stage = 'input'
                    session.act(decision['action'], args.action_ms, on_delivery=record_delivery)
                    summary['decisions'] += 1
                    history.append({'step': step, 'action': decision['action'],
                                    'visual_change': 'pending'})
                    print(f"step {step + 1}: {decision['action']} at presentation {session.presentation}", flush=True)
                else:
                    summary['outcome'] = 'smoke_passed' if args.mode == 'smoke' else 'decision_budget_exhausted'
                # The final action needs an observed successor frame. This also
                # lets a goal reached on the last allotted action be credited.
                if summary['decisions'] and summary['outcome'] != 'goal_observed':
                    stage = 'capture'
                    path, meta, data = session.capture()
                    observation = observe_frame(data, meta, minimum_presentation=session.presentation,
                                                previous_data=previous_data, allow_occluded=True)
                    telemetry, telemetry_status = capture_telemetry(meta, observation['presentation'])
                    record_action_result(history, observation, previous_telemetry, telemetry,
                                         log, str(path.relative_to(run)))
                    session.presentation = observation['presentation']
                    unique_frames.add(observation['pixel_sha256'])
                    if telemetry:
                        summary['telemetry_frames'] += 1
                        summary['player_telemetry_frames'] += int(bool(telemetry['player']))
                        summary['world_telemetry_frames'] += int(bool(telemetry['world']))
                        summary['scene_depth_grid_frames'] += int(bool(telemetry['scene_depth_grid']))
                    summary['last_telemetry_status'] = telemetry_status
                    log.write(json.dumps({'event': 'final-observation', 'observation': observation,
                                          'telemetry': telemetry, 'telemetry_status': telemetry_status,
                                          'last_action_result': history[-1],
                                          'frame': str(path.relative_to(run))}, separators=(',', ':')) + '\n')
                    if session.goal_log_seen or (cue and matches(data, cue)):
                        summary['outcome'] = 'goal_observed'
                        summary['goal_evidence'] = {'frame': str(path.relative_to(run)),
                                                    'presentation': session.presentation,
                                                    'source': 'log' if session.goal_log_seen else 'screen_cue'}
        summary['unique_frame_hashes'] = len(unique_frames)
        summary['last_presentation'] = session.presentation
        return 0 if summary['outcome'] in ('goal_observed', 'smoke_passed') else 2
    except Exception as error:
        reason = str(error)
        if reason.startswith('Native game failure') or reason.startswith('Native game exited'):
            summary['outcome'] = 'native_failure'
        elif 'Manual keyboard input' in reason or 'physical controller' in reason:
            summary['outcome'] = 'input_error'
        else:
            summary['outcome'] = {'startup': 'startup_error', 'capture': 'capture_error',
                                  'provider': 'provider_error', 'input': 'input_error',
                                  'smoke_policy': 'smoke_error'}.get(stage, 'error')
        summary['error'] = redact_secret(f'{type(error).__name__}: {error}')[:1000]
        print(summary['error'], file=sys.stderr, flush=True)
        return 1
    finally:
        summary['elapsed_seconds'] = round(time.monotonic() - started, 3)
        if session:
            session.close()
        if pid is None and (run / 'launch.json').is_file():
            try:
                pid = json.loads((run / 'launch.json').read_text(encoding='utf-8'))['pid']
            except (OSError, ValueError, KeyError):
                pass
        if pid is not None and not args.keep_game_running:
            try:
                owned = Process(pid, (run / 'launch.json').stat().st_mtime)
                try:
                    if owned.exit_code() is None:
                        os.kill(pid, signal.SIGTERM)
                finally:
                    owned.close()
                summary['game_stopped'] = True
            except (OSError, ValueError) as error:
                summary['game_stop_error'] = redact_secret(str(error))[:300]
        run.mkdir(parents=True, exist_ok=True)
        (run / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'summary': str(run / 'summary.json'), 'outcome': summary['outcome'],
                          'decisions': summary['decisions']}), flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
