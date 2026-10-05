# Implementation checklist

- [x] Identify actual game/platform from workspace and record original XEX hash.
- [x] Inspect DarkRecomp read-only and check primary tool documentation.
- [x] Reproducible executable metadata, import, function and asset inventories.
- [x] Reproducible offline translation with explicit diagnostic accounting.
- [x] Native x64 build executes the original entry point with actionable failures.
- [ ] Resolve all reached initialization, ABI and platform-service failures.
- [ ] Original asset access, startup screens, video and menus in the actual app.
- [ ] Verify engine renderer boundary from original code/data.
- [ ] Original world geometry, materials, lights, skinning, effects and targets.
- [ ] Keyboard/mouse/controller gameplay, focus/capture and cursor release.
- [ ] Audio, progression, save/load, transitions and extended stability.
- [ ] Measure new game frames, frame-time distributions and simulation timing.
- [ ] Package reproducibly around the player's own data.

Each milestone requires actual executable logs and, where visual, unedited
captures and real interaction. Tests or compilation alone do not prove gameplay.
