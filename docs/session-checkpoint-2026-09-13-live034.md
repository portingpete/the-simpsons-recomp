# LIVE034 built progress

The goal is paused, and **Reach in-game is incomplete**. The current executable
reaches a later material in the first scene but has not presented a verified
gameplay frame or accepted verified character control. No runtime CPU decoder,
shader translator, console command processor, or substitute scene was added.
Original assets and reference trees remain unchanged.

## Built changes since LIVE027

- Multiple original cache records own independent native payloads and material
  state. The original immediate fallback also completes with state restoration.
- Original zero-normal arithmetic follows the verified legacy zero-product rule;
  vertex data is preserved.
- `simpsons_rigid_textured`820168F8 activates the exact opaque pair
  VS8201700C / PS82017658. Its VS instructions match the existing rigid VS,
  and its distinct PS is statically transcribed and compiled offline.
- Rigid profiles retain the different private storage sizes and dirty maps.
  Original base texture headers resolve only to their published ITXD owner.
  Direct and recorded draws retain the texture and original sampler state.
- Original BC1/BC2/BC3 storage supports base-zero, unsigned XYZW, tiled8-in-16,
  power-of-two dimensions4..2048, and contiguous packed mip chains. The
  descriptor's mip maximum is preserved. Every subresource and addressed block
  is bounded; no mip is generated or discarded. Packed-base tails remain
  unqualified. Existing one-level UI textures remain supported.
- Native immutable compressed mip storage includes partial chains and sub4x4
  levels. Readback keeps the original top-level allocation and maps the requested
  subresource. Im2D still rejects mip chains; storage support does not broaden
  that separate consumer.

Sources include `runtime/rigid_profile.h`, the existing rigid runtime/renderer,
`renderer/rigid_textured_shader.hlsl`, `renderer/rigid_textured_draw.hlsl`,
`renderer/itxd_blocks.cpp`, and `renderer/native_backend.cpp`. The fixed offline
auditors are `tools/analyze_rigid_textured_shader.py` and
`tools/prepare_itxd_mip_fixture.py`. Do not edit generated AOT files.

## Live evidence

LIVE032 activated the textured effect and stopped at `loc_coloredcandychunk01`:

```
256x256, format1A200152, 65536 bytes
82000002 00000052 001FE0FF 00000D10 00000100 00008A00
```

These are five authored levels256,128,64,32,16. Their stored regions start at
0,32768,40960,49152,57344; the final level begins at block(4,0) in its packed
tile. Native consumption now succeeds without dropping mip data.

LIVE033 caught a new rejection of small one-level menu textures. That regression
was corrected and covered by tests. LIVE034 restores the full menu flow:

- Main menu at22.8994s; opening movie skip completes24.3067s.
- Both shadow cameras complete31 draws; the static depth prepass completes133.
- Sixteen original native cache payloads execute18 recorded rigid draws.
- Eighteen original immediate packets complete27 rigid draws.
- Fifteen original mipmapped textures upload, including BC1/BC3, rectangular
  and square extents, and3..6 authored levels.
- Failure at24.6621s:

```
[NATIVE SCENE EFFECT] typed=E1AACC20 vtable=820616C0
identity=00500024 source=8202AD78
[FAILURE] Unimplemented native engine graphics boundary 0x82740680,
caller 0x8273B4E0; original SDK access rejected
```

Evidence is under `build/automatic-startup/reach-game-034`: `game.log`,
`inputs.jsonl`, `result.json`, and `captures/scene-*.bin`. The result explicitly
says `main_menu_verified:true`, `input_sequence_completed:true`, and
`gameplay_verified:false`. Draw counts establish submission, not visible world
rendering or physical-console pixel parity. The game exited on failure.

## Validation

- Regeneration:311 AOT files,303 chunks,0 semantic diagnostics,239 explicit
  unsupported imports (`build/reach-game-mips-regenerate.log`).
- Exact textured shader audit and WARP/hardware fixtures pass, including channel
  packing, UV selection, normal arithmetic and all18 shadow taps.
- Rigid material accumulation and direct/recorded ownership fixtures pass on
  WARP/hardware, including A/B/A replay and RGB10A2/depth/stencil readback.
- Six mip fixtures independently address34 levels: four pinned original
  textures and two synthetic full packed tails through1x1. Native readbacks
  match every byte. Compressed hardware/WARP sampling and invalid uploads pass.
- Seven mip/lifecycle suites pass; small-UI regression checks also pass.
- Full build succeeds (`build/reach-game-mips-full-build.log`). The initial
  full run passes160/162; two old rejection expectations needed updating for
  the textured shader and valid BC mip storage. Both corrected suites pass
  (`build/reach-game-mips-expectations-tests.log`).
- Final complete suite: **162/162 passed in109.15s**, zero failures
  (`build/reach-game-034-final-tests.log`).

The existing format warning in ITXD load diagnostics is unrelated. These native
tests do not establish exact physical-console filtering, floating-point
rounding, display transfer, or gameplay correctness.

## Next implementation

The next effect is **simpsons_rigid_dualtextured**, blob8202AD78,12832 bytes,
SHA256 `1c71a2f2f31878a775f79c47d3b161a8a799711b86594bc1a4515c15d2702649`.
The opaque technique uses VS8202B4CC / PS8202BB44 and context offset0x2920.
Its pass sets18 sampler rows at stages0,1,2; the effect cache's24 total rows
also include its other technique. The opaque PS reads base sampler2 plus the
two shadow samplers. Its additional UV input affects packed surface data;
the effect's name alone does not imply another sampled texture.

- VS:944 bytes, instructions at560,384 bytes,4 CF pairs, five vertex fetches.
  Fetch7 writes UV data to r3.zw; fetch8 writes a second pair to r3.xy.
  Position/normal/color and shadow projection follow the earlier rigid pattern,
  with shifted output semantics. Inspect original semantic associations and the
  captured vertex declaration before mapping UV0/UV1.
- PS:2160 bytes, instructions at1092,1068 bytes, **11 CF pairs**. The initial
  inspection log used10 and omits the final execution CF pair; it is not a
  complete transcription. Literals include c251..255. Additional scalar
  opcodes0/44 and DOT2ADD need explicit audit.
- Private storage has156 words; shadow/rim storage starts at words140/144.
  Base texture header remains word92 but occupies a longer stored descriptor
  interval. Inspect all original selected context maps before extending profiles.
- `decodeRigidVertices` recognizes UV1 declarations but discards the unconsumed
  field. `RigidVertex` exposes only UV0. A qualified input path, native layout,
  and independent probes are needed before enabling this shader.
- Preserve actual recorded/immediate ownership and add independent output
  tests. Do not alias this material to the textured shader or skip its draws.

The catalog is `analysis/native-post-effect-catalog.json`. Initial observations
are in `build/original-screen/rigid-dual-inspection.log`; these are investigative
output, not a completed shader qualification.

## Commands when the goal resumes

```powershell
python -B tools/recompile.py --verify
& build/original-screen/build-focused.ps1 -Targets all
ctest --test-dir build/native --output-on-failure
python -B tools/auto_start_native.py --run-directory build/automatic-startup/reach-game-035
```

After runtime edits, regenerate first with the pinned generator/analyser;
verification alone cannot update the manifest. Use a new replay directory each
time. Preserve existing evidence and the original data/reference trees.
