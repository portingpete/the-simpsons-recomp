# Dualtextured integration / replay 040

Gameplay is NOT verified. This checkpoint advances beyond LIVE034's dual effect
source guards to a separate unqualified ITXD texture format.

## Implementation

- Effect 8202AD78 is admitted in both scene dispatch and recording-build guards.
  All non-source packet checks remain intact.
- Rigid profile selects VS8202B4CC / PS8202BB44, context2920, 18 sampler rows,
  156 private words, shadow leaf20/word140 and rim leaf21/word144.
- RigidVertex is 56 bytes. UV1 decode is opt-in only for this effect; existing
  materials retain zero UV1 and ignore nonfinite dead UV1 payloads.
- Native shader artifacts and the depth-exporting dual draw adapter are built
  and selected for immediate/recorded draws. The input layout contains all five
  attributes (including UV0 at40 and UV1 at48), with an array-derived count.
- Base texture stage2 validation, sampler creation and ownership apply to both
  textured variants. No second base texture or replacement textured shader.
- The material commit accepts the selected profile's private-bank extent.
- The dual shader auditor's --verify now compares the HLSL file against its
  generated transcription. It passes, with 107 rejected semantic mutations.
- Regression tests cover consumed/missing/nonfinite UV1, bit preservation,
  dual dirty constant mapping, bank-size rejection and shader artifact support.

## Evidence

Clean build (450 steps): K:\SimpsonsNativeCopy\build\dual-clean-build.log.
Replay039 retained the original guards and reproduced82740680/caller8273B4E0.
After runtime integration, regeneration and build succeeded; five focused rigid
vertex/mesh/shader tests passed, including WARP and hardware.

Replay: K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-dual-040\game.log.
Line15904 binds the actual dual pair. Lines15912-15931 complete five immediate
submesh draws across three geometries while that pair remains selected.
No presented gameplay frame or character control was verified. Recorded dual
rendering is wired but not separately established by this live replay.

## Next blocker (guard intentionally retained)

Line15937 rejects original texture loc_candy_wall:

- raster E1BFD488, generation402; payload EA575000
- extent512x512; format18280186; auxiliary00000000
- payload bytes1400832
- raw descriptor84000002,00000086,003FE1FF,00000C14,00000140,00100A00

The existing RGBA8 decoder only qualifies simpsons_palette's exact64x64,
16384-byte, single-level descriptor. This texture describes maxLevel5 and a
base allocation of100000 hex. It requires separate original-byte, tiling,
channel-order and mip-chain qualification; no guard bypass or substitute
texture was added. A captured-byte texture-only regression remains next work.

## Final validation

Full build passed. The first full rerun after shader expectation updates passed
161/162; NativeHostFloatingPoint failed during toolchain setup. Capturing stderr
identified vcvars64.bat's "The input line is too long" error with an inherited
6451-character PATH. Running in a child process with a short PATH and matching
__VSCMD_PREINIT_PATH resolved it without changing machine settings or test code.
All nine FP fixture checks then passed, followed by the complete162/162 suite.
No test was disabled, removed or weakened.
Final log: K:\SimpsonsNativeCopy\build\dual-clean-env-ctest.log.
Earlier environment-failure log: K:\SimpsonsNativeCopy\build\dual-final-ctest.log.

The workspace has no accessible .git metadata (git status reported not a git
repository); no claim of a pristine tree or commit is made.
