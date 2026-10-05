# The Simpsons Game launcher

Double-click **Play The Simpsons Game.lnk** in the workspace folder to start the
game directly. The native launch helper dispatches the game and exits without
opening a launcher window or asking you to click Play. This Windows shortcut
targets the GUI launch helper directly, so it creates no command window.
The `.cmd` version remains available and may briefly show a command window.

This is a **native development build**. Gameplay coverage is incomplete, and
unqualified engine paths can still stop the game.

Sound is enabled on normal launches and follows the default Windows audio
device. For silent diagnostics, add `--mute-audio` when running
`SimpsonsNative.exe` directly. The bounded `tools/run_native.py` diagnostic
helper supplies that option automatically.

For the first mission's completion transition, double-click **Play First Mission -
Completion.lnk**. This shortcut dispatches the game directly with
`--first-mission-completion`. Each launch copies the selected
profile, content/save store and saved video preferences into a new folder under
`build/mission-completion-runs`. The game's autosaves and settings changes use
those private copies. Your original profile and save stay unchanged.

The completion mode starts the mission-complete transition. It does not select
the next playable area. The ordinary **Play The Simpsons Game.lnk** shortcut uses
the normal game startup route.
See [the original completion route and verification](first-mission-completion.md)
for startup timing, guards and manual testing limits.

The dedicated **Play Bartman Begins.lnk** shortcut is intended to start Bartman
Begins through the original `brt/brt.str` route with `--bartman-begins`. It uses
fresh private profile, save and video-setting copies under
`build/bartman-begins-runs`. Startup movies are skipped only until the original
Bartman map-ready boundary; later cutscenes retain ordinary controls. Its build,
shortcut creation and live validation are pending. See
[Bartman Begins launch details](bartman-begins-launcher.md).

## Setup

Keep these files together in the workspace:

```text
Play The Simpsons Game.cmd
Play First Mission - Completion.cmd
Play The Simpsons Game.lnk
Play First Mission - Completion.lnk
Play Bartman Begins.lnk
build/native/SimpsonsLauncher.exe
build/native/SimpsonsNative.exe
analysis/simpsons.pe
build/mainmenu-profile-204/575cf79a-3815-45f7-a6f7-e8d709d16298.profile
build/mainmenu-content-204/save-index/575cf79a-3815-45f7-a6f7-e8d709d16298/45410809/SIMPSONS_SLOT1.save
```

The `.lnk` shortcuts target
`K:\SimpsonsNativeCopy\build\native\SimpsonsLauncher.exe`, use
`K:\SimpsonsNativeCopy` as their working directory, and request normal window
visibility for the game. The completion shortcut supplies
`--first-mission-completion`; the ordinary shortcut supplies no arguments.
The Bartman shortcut supplies `--bartman-begins` and is created after its build
passes. Their absolute targets match this workspace location. The `.cmd` alternatives
continue to locate the launch helper relative to their own folder.

The game also needs its existing runtime dependencies beside the native game
executable. If the shortcut says the launcher has not been built, build the
`SimpsonsLauncher` target from the configured workspace:

```bat
cmake --build build/native --target SimpsonsLauncher
```

Building this target also updates `SimpsonsNative` and `SimpsonsInputRecorder`,
so normal play, recording and resume launchers use the same current controls and
native icon artwork. The normal project build includes these targets too.
Build setup and image preparation are described in `STATUS.md`.

If the launch helper reports missing files, keep it under `build/native`, complete the
native build and image preparation, then try again. A stopped game can indicate
an incomplete part of the port; inspect its log for the actual cause.

## Controls and logs

Yellow native key and mouse icons appear in menus and tutorials for keyboard/mouse input.
Click the game window to activate mouse camera movement. **Left click** attacks,
**right click / K** uses special attack, and **Left Shift** activates Homer Ball.
**Escape** pauses and releases the mouse; **F6** toggles capture, and switching
away releases it.
See [all native bindings](native-input-prompts.md).

- With no controller connected in slot 0, the game window accepts **Escape** for
  Start/Pause, **Enter / Space** for A/Select, **right click / K** for B/Back, **Backspace** for the Back
  button, and **WASD / arrow keys** for menu navigation. WASD uses the game's
  directional-pad repeat delay in menus. Enter also skips movies. A physical controller
  in slot 0 takes priority. Keyboard input releases when the game loses focus.
- Launch shortcuts create the game process immediately after checking its files
  and preparing the launch. Completion and Bartman modes copy private stores first.
- The shortcuts and direct `SimpsonsNative.exe` launches use the saved
  [Options → Video preferences](native-video-settings.md). Without saved
  preferences the frame limit is 120 FPS. Actual performance depends on the scene.
- Ordinary launch logs are in `build/launcher-logs`. Completion launch logs are
  in each new private run's `logs` folder under `build/mission-completion-runs`.
  Bartman launch logs use the corresponding folder under `build/bartman-begins-runs`.
- File-copy or process-dispatch failures show a Windows error message. When a
  launch log exists, its path or containing private run appears in the message.

The helper exits after starting the game. The game keeps its inherited log handle
and runs until you close its window. If it crashes after dispatch, inspect its
log for the failure. The helper does not monitor or reattach to the running game.

Each launch creates a new `Simpsons-YYYYMMDD-HHMMSS-mmmZ-PID-NNNN.log` in
`build/launcher-logs`, or in the private completion/Bartman run's `logs` folder. Times are UTC. Existing files are never overwritten, even
for launches within the same millisecond. Each log includes the workspace and
command followed by the game's combined standard output and standard error.
Some output may remain buffered by the game until it flushes or exits. Logs can
be opened while the game is running; your text editor may need a refresh to show
new output.

Completion and Bartman runs retain their copied stores and logs after the game closes. Failed
copies also remain available for inspection, and the game starts only after all
copies succeed. Source folders and files with Windows reparse points (including
junctions and symbolic links) are rejected. The copy reads ordinary files without
following redirected entries. Missing optional video preferences use the game's
defaults; an existing preferences file is copied beside the private profile store
as `profile.video.cfg`.

## Build details

The CMake **`SimpsonsLauncher`** target builds `app/launcher.cpp` as a standalone
**Windows GUI subsystem** executable at `build/native/SimpsonsLauncher.exe`.
It uses **C++20**, **ClangCL `/W4 /WX /EHsc`**, and the Unicode definitions
`UNICODE`/`_UNICODE`. Entry point: **`wWinMain`**, through normal CRT startup.
It links **`user32.lib`, `gdi32.lib`, and `shell32.lib`**, plus the normal
Windows/CRT defaults (`kernel32.lib`). No game/runtime library is required.
The GUI subsystem avoids an extra launch-helper console. Successful dispatch
creates no helper window; launch errors use a standard Windows message box.

Root discovery walks upward from the launcher's absolute executable location,
choosing the nearest ancestor containing both `build/native/SimpsonsNative.exe`
and `analysis/simpsons.pe` as regular files. It never uses the calling directory
or executable search path to choose a workspace. Dispatch checks the required
files immediately before launching.

The child receives exactly:

```text
"<workspace>\build\native\SimpsonsNative.exe" --image "<workspace>\analysis\simpsons.pe" --profile-store "<workspace>\build\mainmenu-profile-204" --content-store "<workspace>\build\mainmenu-content-204" --local-profile "0:575cf79a-3815-45f7-a6f7-e8d709d16298"
```

The completion-mode command uses the same executable, image and selected local
profile ID, substitutes its private `profile` and `content` directories, and adds
`--first-mission-completion`. Bartman mode prepares its own private stores and
adds `--bartman-begins`. All three modes load saved video preferences without a
frame-rate or vsync override. The direct stage options are mutually exclusive.

This is the same recorded profile/content selection used by
`tools/auto_start_native.py` and the Steam entry in `docs/steam-input.md`.
Selecting local slot 0 up front prevents the game from falling into its
unimplemented sign-in UI path during profile load. Dispatch also verifies that the
recorded profile and existing save are present before creating the child.

Its working directory is the workspace. The launcher uses wide paths and
`CreateProcessW` with an explicit executable path and mutable, quoted command
line, following Microsoft's [process creation contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
and [argument quoting rules](https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments?view=msvc-170).
Spaces, non-ASCII characters, quotes, and trailing backslashes are preserved
by the launcher's command construction; it does not route the game through a
shell. **The current game entry point uses narrow `main` arguments**, so its
own handling of paths outside the active Windows code page remains a separate
port limitation; this launcher does not change `app/main.cpp`.

Only the log and a read-only `NUL` input handle are inherited, using a
[`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute).
`CREATE_NO_WINDOW` prevents an extra game console without hiding the game's GUI.
After a successful `CreateProcessW`, the helper releases its process/thread and
parent log handles and returns exit code 0. That code confirms process creation;
the game's later exit status appears in its own log. Inherited log and `NUL`
handles remain with the child. There is no job object linking the game's lifetime
to the helper.

## Self-test without starting the game

With testing enabled in the configured build, run the registered CTest test
from the workspace:

```bat
ctest --test-dir build/native -R "^NativeGameLauncher$" --output-on-failure
```

`NativeGameLauncher` runs `SimpsonsLauncher --self-test` with a **15-second
test timeout**. That timeout applies only to the self-test. To invoke the same
self-test directly in PowerShell:

```powershell
$launcherTest = Start-Process -FilePath '.\build\native\SimpsonsLauncher.exe' -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
$launcherTest.ExitCode
```

Exit **0** means success; **1** means failure. The mode opens no launcher window
and never creates a game process. Diagnostics go to an inherited standard output
handle when available and to the Windows debugger output. It can run without
the real game executable or image present.

Tests create an exclusively owned temporary directory beside the launcher,
then clean up only that directory after resolving its absolute path and checking
that it is still directly within the retained, resolved parent folder. Cleanup
is skipped if either location has been redirected. They check ancestor discovery with missing
files, a directory in place of the image, and a complete workspace; Unicode and
space-containing paths; command-line round trips including empty arguments,
embedded quotes, shell metacharacters, and trailing backslashes; colliding log
timestamps; preservation of prior log content; latest-log selection; and the
log handle's inheritance flag. Direct-dispatch checks cover default, completion and Bartman
action selection, required-file validation, prepared stores and logs without
calling process creation. Private-mode checks cover strict option parsing,
command propagation, unique private runs, copied profile/save/video bytes,
original-save isolation after a private write, missing optional preferences,
and rejection of source root and nested directory junctions. Bartman fixtures
also check its distinct run folder and isolation from completion runs. These fixtures use
only newly created test files; no real profile or game data is copied or changed.

Ordinary `SimpsonsLauncher.exe` invocation dispatches the game directly, as do
the completion and Bartman invocations. The self-test only prepares launches and checks files;
game execution, game Unicode decoding and rendering need separate integration checks.

## Steam Input

The launcher and preferred Steam entry now use the same recorded profile/content
selection. Steam still launches `build/native/SimpsonsNative.exe` directly
because Steam Input's virtual Xbox device arrives through the game's existing
XInput slots. See `docs/steam-input.md` for the exact Target / Start In /
Launch Options triple and the safe `tools/setup_steam_input.py` dry-run helper.
