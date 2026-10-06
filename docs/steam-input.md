# Steam Input launch (gamepad emulation)

This is a **native Windows development build**. Audio is muted, gameplay is
incomplete, and the game may stop during startup. There is no Steamworks SDK,
no Steam app ID, and no borrowed (for example Spacewar) ID in this setup.

## Why a direct Steam shortcut works

Steam Input gamepad emulation hooks XInput and injects a virtual Xbox-style
device. The existing native runtime (`runtime/controllers.cpp`) already polls
all four XInput 1.4 slots through the normal OS query and preserves the
returned buttons, trigger magnitudes, and signed stick positions for the
original input manager. No game-code change or reverse engineering was needed:
a controller configured in Steam Input simply appears as ordinary XInput
state, which is the intended input source for this page.

References: `docs/native-controllers.md`, `runtime/controllers.cpp`, and Valve's
[Steam Input documentation](https://partner.steamgames.com/doc/features/steam_controller),
which describes how Steam Input emulates a standard Xbox-style gamepad for
non-Steam shortcuts.

## Reproducible Steam entry (preferred)

Create **one** non-Steam shortcut with these exact values. Paths are absolute
so the entry does not depend on the caller working directory.

```text
App name:    The Simpsons Game (Native)
Target:      "K:\SimpsonsNativeCopy\build\native\SimpsonsNative.exe"
Start in:    K:\SimpsonsNativeCopy
Launch opts: --image K:\SimpsonsNativeCopy\analysis\simpsons.pe --frame-rate 60 --profile-store K:\SimpsonsNativeCopy\build\mainmenu-profile-204 --content-store K:\SimpsonsNativeCopy\saves --local-profile 0:575cf79a-3815-45f7-a6f7-e8d709d16298
```

Recorded selection behind the launch options
(from `config/startup_replay.json`, small fields only; the file's large
` screens` pixel-cue table is intentionally not reproduced here):

```text
profile_store=build/mainmenu-profile-204
content_store=saves
profile_id=575cf79a-3815-45f7-a6f7-e8d709d16298
save_index=save-index/575cf79a-3815-45f7-a6f7-e8d709d16298/45410809/SIMPSONS_SLOT1.save
```

This reuses the existing recorded Player profile and existing save folder. It
does not create a new game and does not use the per-run automatic-replay
flags (`--controller-input`, `--capture-frames`), which belong to
`tools/auto_start_native.py` diagnostics, not to interactive Steam Input play.
Manual keyboard input and the automatic command channel are separate paths;
see `docs/native-controllers.md` and `docs/automatic-startup.md`.

The generated non-Steam appid for the entry above is `-1000453302`
(unsigned `0xC45E4B4A`), computed as Steam computes it
(`signed32(CRC32(Exe + AppName) | 0x80000000)`). It is Steam's local shortcut
identifier, not a Steamworks title ID.

## Setup helper (dry run by default)

Run from the workspace folder:

```powershell
python -B tools/setup_steam_input.py
python -B tools/setup_steam_input.py --write-record build/steam-input-shortcut.json
python -B -m unittest tests.test_steam_input_shortcut -v
```

The default mode is read-only. It prints the exact Target / Start In /
Launch Options triple, the recorded selection fields above, the shortcuts
file it inspected, whether Steam is running, how many entries exist, the next
free index, and whether the exact owned entry is already present. It never
writes to Steam files, never stops or restarts Steam, and never launches the
game. `--write-record` additionally writes the reproducible record at
`build/steam-input-shortcut.json`.

The explicit install path is intentionally hard to run by accident:

- It requires `--install` **and** Steam fully closed (normal Exit, not kill).
  While Steam is running the helper refuses with exit code 3 and changes
  nothing, because Steam rewrites `shortcuts.vdf` on exit.
- It requires exactly one unambiguous `userdata/*/config/shortcuts.vdf`
  (or an explicit `--user-id`), refuses unknown value types and non-numeric
  indices, refuses an appid collision with a different entry, and refuses
  when the file changed between inspection and install.
- It preserves every pre-existing entry byte-for-byte by appending the new
  bytes before the final terminators, writes an exclusive timestamped backup
  (`shortcuts.vdf.bak.YYYYMMDD-HHMMSS-microseconds-pid<PID>`, never
  overwriting an earlier backup), replaces atomically, then re-parses and
  verifies idempotent detection of the exact owned entry.

## Manual Steam steps (no script required)

1. Close the game. Leave Steam running.
2. In Steam: **Games > Add a Non-Steam Game to My Library > Browse**, select
   `K:\SimpsonsNativeCopy\build\native\SimpsonsNative.exe`, add it, then open
   its **Properties** and set the name, Start In, and Launch Options exactly
   as in the box above.
3. Keep the **Steam overlay enabled** for the entry (the helper records
   `AllowOverlay=1`, the Steam default). Open the entry's **Controller**
   settings, enable **Steam Input**, and select the **Gamepad** template
   (Xbox-style).
4. Launch from Steam. The virtual gamepad arrives through the normal XInput
   slots; slot 0 behaves like a physical controller in slot 0.

## What this does not claim

- User evidence only (2026-09-19): Steam shortcut "The Simpsons Game (Native)"
  with the Gamepad template selected; user confirmed "buttons work"
  (evidence: `build/performance-316/steam-input-verification.json`). No full
  button matrix, stick, rumble, or long-term stability claim is made here.
- The normal launcher (`SimpsonsLauncher.exe`, `docs/launcher.md`) is unchanged
  and is not required for Steam Input. Its `--self-test` path is unaffected.
- Never commit or upload proprietary game assets or binaries, and never inspect
  Steam credentials. The helper reads only the shortcut structure and the four
  small selection fields above.
