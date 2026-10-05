# Jev level playtesting

`tools/jev_level_test.py` starts the native game through the existing verified
menu replay, copies the configured profile and save into a private run folder,
captures fresh rendered gameplay frames, asks TypeSafe Jev to choose one of a
bounded set of controller actions, and records the delivered input and result of
each step. It currently targets the saved Land of Chocolate entry in
`config/startup_replay.json`.

In an unattended window, DXGI may report the display as occluded. The Jev
runner explicitly accepts completed front-renderer readbacks in that case and
records `display_accepted: false`; ordinary startup verification retains its
accepted-display requirement. Scene geometry, fresh presentation numbers,
nonzero RGB pixels, and complete readback metadata are still required.

Jev's API accepts text/JSON state, not game images. Each qualified RGB10A2
capture has metadata from that same renderer presentation. The native port
reads the original 43-bone character atomic's frame world transform when one
unambiguous, valid character is rendered, and sends its position to Jev. It
also samples the original viewport's camera eye and a 16×9 scene depth grid
when those sources are available. The runner rejects telemetry with a
different presentation, unsupported schema, invalid coordinates, or malformed
depth values. It measures player and camera movement after each delivered
action and keeps them distinct. The 32×18 color and brightness grids remain
visual context and the fallback when structured facts are unavailable. Jev
selects an action; game code applies its physics and rules.

The depth grid runs from screen top to bottom and left to right. It uses
logarithmically scaled reversed depth: 0 means far and 255 means near. The
scaling preserves useful variation in this level's distant surfaces; numeric
steps are not equal world distances. It orders visible surfaces but does not
provide world distance, collision geometry, or a walkability map. Camera
position is not a goal location.

The encoder maps a valid raw D32 reversed-depth sample `d` in `[0, 1]` to
`round(255 × log(1 + 65535d) / log(65536))`. The number is useful for ordering
surfaces on the screen; it cannot be converted to meters without the camera's
projection parameters.
Objective text, death state, nearby entity identities, and a general
level-completion event are not yet qualified. Player velocity and grounded
state are also unavailable; the runner measures displacement across captures
instead. A run without a configured goal
therefore reports
`decision_budget_exhausted` or `wall_time_exhausted`, never a passed level.

## Setup

From the workspace root in PowerShell:

```powershell
python -m venv build/jev-venv
.\build\jev-venv\Scripts\python.exe -m pip install typesafe-sdk==0.7.1
$env:TYPESAFE_API_KEY = '<your TypeSafe API key>'
.\build\jev-venv\Scripts\python.exe -B tools/jev_level_test.py doctor
```

Keep the key in an environment or secret store. The runner never writes it to
the repository or run output. It pins `jev-1.13.0` by default; use `--model`
when deliberately changing models. The SDK and current model contract are in
the [TypeSafe Python SDK](https://docs.typesafe.ai/sdk/python) and
[model reference](https://docs.typesafe.ai/models).

## Run

First test the native startup, capture, and controller path without API calls:

```powershell
.\build\jev-venv\Scripts\python.exe -B tools/jev_level_test.py smoke --max-decisions 3 --wall-seconds 300
```

To exercise the new controls without an API call, choose a short known-action
sequence. Each named action is one smoke decision, including the three-PAD
double-jump macro:

```powershell
.\build\jev-venv\Scripts\python.exe -B tools/jev_level_test.py smoke --max-decisions 4 --wall-seconds 300 --smoke-actions attack,interact,ball,double_jump
```

`--smoke-actions` accepts 1–32 comma-separated names from the runner's
action list. The sequence repeats if `--max-decisions` is longer than the list.

Then run a bounded Jev exploration:

```powershell
.\build\jev-venv\Scripts\python.exe -B tools/jev_level_test.py play --max-decisions 120 --wall-seconds 1800 --action-ms 800
```

The default goal is to finish Land of Chocolate. Jev receives a route hint from
the [Xbox 360 walkthrough](https://gamefaqs.gamespot.com/xbox360/939416-the-simpsons-game/faqs/50683)
and visual review of an earlier run. First it should chase chocolate bunnies
through the village, find the pretzel fence opening, double jump from
marshmallow pads to a higher platform, and cross the bridge. The earlier run
reached a solid embankment near `[7.48, 0.01, -32.62]`; that is an obstacle,
so it should backtrack to open cobblestone and move laterally toward the
fence opening. Later the guide calls for X against a gate and spawners, Y at
the red button in the cake clearing, platform jumps, a punch against the white
rabbit, and RT/X for Homer Ball dashes. This is a guide to exploration, not
live telemetry or proof that an objective was completed. The `--objective`
option can override the goal text.

The optional B ability is withheld from Jev by default because a prior run
using B stopped at an unimplemented native particle shader variant. Use
`--allow-b` only when specifically testing that ability or after the renderer
supports its effect.

To score completion, provide a game-specific, verified success signal:

```powershell
.\build\jev-venv\Scripts\python.exe -B tools/jev_level_test.py play --goal-cue config/level-complete-cue.json
```

`--goal-cue` accepts the screen sample format used in
`config/startup_replay.json` (`samples`, `tolerance`, and `minimum_match`).
Alternatively use `--goal-log-regex` when the native log exposes an
authoritative completion marker. Do not treat a general rendering or input
receipt as a completion signal. Choose the goal cue from a known success screen
and check it against ordinary gameplay frames before relying on it.

Runs go under `build/jev-level-tests/<timestamp>`. Each folder contains a
private save/profile copy, controller commands, game log, captured frames,
`decisions.jsonl` (visual observation, qualified telemetry or rejection status,
Jev probabilities, request ID, selected action, and a separate `action-result`
event on the exact successor frame with measured movement),
and `summary.json`. The adjacent `<timestamp>.startup.log` contains the menu
replay events. Unreferenced startup captures are removed after successful
startup to keep repeated runs manageable; frames cited by startup events and
the first/last gameplay evidence are retained. `summary.json` distinguishes `goal_observed`, bounded
exploration, startup failure, API failure, input failure, and native game
failure. Exit code 0 means the configured goal was observed, or that a
scripted smoke run passed; 2 means the Jev budget ended without a goal; 1
means an error. The runner stops only the game process it launched unless
`--keep-game-running` is set.

The `telemetry_frames`, `player_telemetry_frames`,
`world_telemetry_frames`, and `scene_depth_grid_frames` counts in
`summary.json` show how many captured frames had qualified facts.
`last_telemetry_status` explains a missing or rejected snapshot. In
`recent_actions`, `player_distance` measures player displacement in game
units; `camera_distance` only measures camera motion. A stationary camera
alone does not prove the player is stuck.

The supported gameplay actions are wait, camera-relative stick movement,
jump, X attack, Y interaction, RT Homer Ball activation, and combinations of
these with forward movement. A double jump sends two separate A presses with
an A-released in-air gap as one Jev decision. Each PAD hold lasts 50–2000 ms;
the double-jump sequence uses 100 ms, 180 ms, and 100 ms holds. The native
controller stream preserves the older menu command format and four-field PAD
format while accepting `PAD <buttons_hex> <lx> <ly> <duration_ms> [rt]` for
gameplay. The button mask permits face buttons (`F000`) and the four DPad
directions (`000F`); Start, Back, shoulders and thumb buttons retain their
existing separate input routes. A 123 ms DPad hold can therefore reproduce
the observed house-exit input without extending it to the named 250 ms hold.
Every submitted PAD segment must receive a matching native input
receipt before the next decision. If player motion is under 0.25 game units
at the current position, the runner temporarily excludes that ineffective
movement action from Jev's next choice list and tells Jev when it has
revisited a recent position.

## Current scope

The port has verified entry into Land of Chocolate and two visible jumps.
Full traversal and mission completion are not yet verified. Jev runs can now
exercise the level and flag crashes or missing input/capture progress, while
their decision trace, telemetry, and screenshots show where an exploration
stopped. For
reliable automated pass/fail level tests, add a qualified completion cue or
native objective event for each level, then validate it with both passing and
nonpassing captures.
