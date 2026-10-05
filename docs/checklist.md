# Development validation checklist

Use these requirements when implementing and validating changes. They describe
ongoing development work, not a record of completed milestones.

- Record the supported executable identity and the source/configuration used to
  regenerate offline translation. Fix generators or source rather than generated
  game functions.
- Build the affected native targets and verify the AOT semantic/hash gate with
  explicit diagnostic accounting.
- Run relevant CPU and GPU regressions for ABI, resource ownership, rendering,
  platform services and failure handling.
- Validate affected startup screens, videos, menus and gameplay using actual
  executable logs, completed renderer captures and real interaction.
- Exercise keyboard, mouse and controller input, including focus changes,
  capture release and menu navigation when affected.
- Check affected audio, save/load, progression and level transitions; qualify
  extended stability separately from a short successful run.
- Measure newly rendered frames, frame-time distributions and guest simulation
  timing when assessing performance.
- Validate reproducible setup and packaging with the player's own game data;
  keep original game files, generated translation and private saves outside Git.

Record the tested route, duration and remaining limits. Compilation and isolated
tests alone do not prove gameplay or full-game compatibility.
