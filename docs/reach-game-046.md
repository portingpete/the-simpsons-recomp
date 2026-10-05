# Replay 046: gameplay still blocked by rigid gloss

No gameplay frame or character control was verified. This session did not advance
live rendering beyond the pre-existing replay044 executable behavior.

## Live evidence

Fresh replays045 and046 completed the main menu, Continue Game and movie skip.
Replay046 reached the main menu at24.8928s, completed movie skip at26.1424s and
failed at26.498s. Its result explicitly records gameplay_verified=false.

Evidence: `K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-046\result.json`
and `K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-046\game.log`.
The selected effect is simpsons_rigid_gloss82019988, rejected at scene dispatch
82740680, caller8273B4E0. No runtime shader translator runs here. The earlier
LIVE034 dualtextured blocker is historical; existing sources already handle it.

## Limited changes this session

- Offline emitter `K:\SimpsonsNativeCopy\tools\analyze_rigid_shader.py` now emits
  DOT2ADD, scalar multiply, IEEE exp2 and log2. DOT2ADD uses swizzled XY products
  and src2.X with legacy zero-product handling. Coissued writes remain ordered
  after both RHS evaluations.
- Four regression cases in `K:\SimpsonsNativeCopy\tests\test_rigid_alu_emission.py`
  check the five originally rejected gloss slots, writes and unknown-op rejection.
  CMake registers OriginalRigidAluEmission. These are source-emission tests, not
  GPU arithmetic qualification for gloss.
- `K:\SimpsonsNativeCopy\build\check_gloss_translation.py` now counts failures
  and returns nonzero rather than printing a misleading PASS. It currently
  accepts97/97 ALU slots. Fetch/interface/control-flow integration is NOT proven.
- `K:\SimpsonsNativeCopy\build\inspect_gloss_layout.py` records fetch/CF layout.
- No production runtime, shader HLSL, guard or original asset was modified.

## Remaining actual rendering implementation

Opaque gloss uses VS8201A07C (940 bytes, code offset556,384 bytes,4 CF pairs)
and PS8201A6EC (2504 bytes, code offset1220,1284 bytes,12 CF pairs).
Selected context offset is0x2B00. VS fetch7 writes r3.xy and fetch8 r3.zw;
this differs from dualtextured. VS exports packed UV in output0, world position
in output1, character/world projections in outputs2/3, normal4 and color5.
PS samples base stage2 including alpha, plus two nine-tap shadow groups.
PS slot37 reads c50, outside the existing HLSL pc[50] bank. Parameter maps and
per-material/replay ownership must be established before extending that bank.
There is no gloss HLSL or compiled gloss artifact in the current renderer.

Next work: exact static HLSL/interface, independent GPU output probes, selected
private parameter maps and material profile, constant-bank handling, direct and
recorded shader binding, then source admission and a fresh live replay. Do not
alias gloss to textured or skip its draws to report gameplay.

## Validation

Application build succeeded; AOT verification reports311 files,0 diagnostics.
Existing rigid/textured shader evidence tests pass unchanged. All four new Python
cases pass. Initial full suite passed163/164 with OriginalViewportCameraPasses
failing; its isolated retry passed (cause not established). Final complete suite
passes165/165 in101.28s, including the registered new test:
`K:\SimpsonsNativeCopy\build\reach-game-046-final-ctest.log`.
Neither those passes nor successful menu inputs establish playable gameplay.
