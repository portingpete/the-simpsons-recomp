# The Simpsons Game launcher

Double-click **Play The Simpsons Game.cmd** in the project folder to start the
normal game. This is the regular launcher supplied by the repository. It starts
`build/native/SimpsonsLauncher.exe`, which dispatches the game and exits without
a launcher window or an extra Play click. Windows may briefly show the command
window while the `.cmd` file runs.

This is a native development build. Gameplay coverage is incomplete, and
unqualified engine paths can still stop the game. Sound is enabled and follows
the default Windows audio device.

For runtime stall diagnostics, use **Play The Simpsons Game - Stall Profiler.cmd**.
It uses the normal save and settings, enables profiling only in the game
process, and writes separate logs under `build/stall-profiler-logs`. See
[runtime stall profiling](runtime-stall-profiler.md) for the output and
compile-time control. The helper also accepts `--stall-profile` directly.

## Setup

Follow [repository setup](repository-setup.md) for prerequisites, local game
inputs and build limitations. Keep the launcher at the project root with the
configured native build and local game files:

```text
Play The Simpsons Game.cmd
Play The Simpsons Game - Stall Profiler.cmd
build/native/SimpsonsLauncher.exe
build/native/SimpsonsNative.exe
analysis/simpsons.pe
build/mainmenu-profile-204/575cf79a-3815-45f7-a6f7-e8d709d16298.profile
saves/save-index/575cf79a-3815-45f7-a6f7-e8d709d16298/45410809/SIMPSONS_SLOT1.save
```

The native executable also needs its runtime dependencies beside it. Game data,
prepared images, binaries, profiles and saves are local inputs and are excluded
from Git. If the launcher has not been built, run:

```powershell
cmake --build build/native --target SimpsonsLauncher
```

Building this target also updates `SimpsonsNative` and `SimpsonsInputRecorder`.
The launcher finds the project from its executable location and validates the
native executable, prepared image, selected profile and save before starting.
The `.cmd` file locates the helper relative to its own folder.

## Controls and settings

See [native input prompts](native-input-prompts.md) and
[controller setup](native-controllers.md) for input bindings and device behavior.
Use **Options → Controls** for [keyboard/mouse bindings and mouse options](native-control-settings.md).
The game loads saved [Video preferences](native-video-settings.md).
Choose **Exit Game** from the main menu, then **Yes** to close the game.
**No** or **Back** returns to the menu.
[Steam Input](steam-input.md) describes the supported Steam configuration.

## Logs and errors

Each launch writes a unique combined game output log under
`build/launcher-logs`, or `build/stall-profiler-logs` for a profiling launch.
Existing logs are preserved. File validation or process
creation failures show a Windows error message with the log path when available.

The helper exits after creating the game process. If the game stops after
startup, inspect its log for the failure. Closing the game window ends the game.
For silent diagnostics, run `SimpsonsNative.exe` with `--mute-audio`; the bounded
`tools/run_native.py` helper supplies that option automatically.

## Launcher self-test

With testing enabled in a configured build, run:

```powershell
ctest --test-dir build/native -R "^NativeGameLauncher$" --output-on-failure
```

This runs `SimpsonsLauncher --self-test` without starting the game. It checks
file discovery, command quoting, required-file validation, unique log creation
and developer diagnostic modes, including the profiling command and child-only
Unicode environment override. Exit zero means the self-test passed; this
does not establish gameplay coverage.

The helper is a C++20 Windows GUI subsystem executable built from
`app/launcher.cpp`. It starts the native game with `CreateProcessW`, an explicit
executable path and quoted arguments, using the project as the working directory.
Only the log and read-only `NUL` input handles are inherited. The game's current
narrow `main` arguments can still limit paths outside the active Windows code page.
