# Progress checkpoint — September13,2026 — LIVE027

Work stopped for the user's updated goal, **Make a progress checkpoint**. The
earlier reach-in-game objective remains unfinished. Both contributors have frozen
their files. No build, regeneration, replay or background implementation is running.

## Last verified executable

`build/native/SimpsonsNative.exe` is the LIVE027 executable. It predates the current
multiple-record changes. The last generation verified311 AOT files,303 chunks and
zero semantic diagnostics, with239 explicit unimplemented imports.

- `build/reach-game-rigid-union-regenerate.log`: successful generation.
- `build/reach-game-rigid-union-full-build-tests.log`: **160/160 passed,111.96s**.
- `build/automatic-startup/reach-game-027/result.json`: main menu verified,
input sequence completed, gameplay unverified. Actual final failure:
`Recording owner has live or unsupported record/state fields`.

LIVE027 loads the existing save, chooses Continue Game and skips the opening movie
with the original decoder cleanup. Both shadow cameras finish31 character/static
draws and two1024-square depth/stencil copies. The main static prepass completes133
draws. The first opaque rigid mesh then completes all of the following:

1. Original cache allocation and both recording-context aliases,82 scalar and320
   sampler resubmissions, FX context association and the three-row shadow loop.
2. Material commit,3530-index draw recorded into a real native deferred context.
3. Actual FinishCommandList: one draw,1320 owned immutable data bytes. Original CPU
   status2, accounting, LRU publication, attachment release and context restoration.
4. Original replay callbacks and both56-register banks. Both CPU copies of each
   stage are verified before native publication.
5. One actual ExecuteCommandList. **The private1280x720 scene target remains all
   zero before and after it**. No visible scene draw, presented gameplay frame or
   character control is established. The next object's begin hits the old single-
   record guard.

Raw evidence is under `build/automatic-startup/reach-game-027/captures`:
`recording-first.json`, before/after RGB10A2 files, and staged VS/PS binary banks.
These are private target readbacks, not completed front-buffer copies. The first
VS matrix is finite and the world matrix is identity; PS object ID is388. The
reason for zero changed pixels remains unconfirmed. Inspect the new draw-state
and geometry capture before changing shader, culling, depth or color-mask behavior.

## Saved, unbuilt changes

**Current source/configuration differs from generated AOT and the executable.**
Do not claim the current working tree passed160 tests. Regenerate before building.

- `runtime/engine_recording.cpp/.h`: multiple fresh successful bucket1 records;
  exact2000-slot free/borrowed partition; original object chains/backlinks and
  LRU checks; total-byte and independent per-event counter receipts; retained
  manager history; old-node replay; observation of original LRU touches, counter
  reset and quota return. Original quota exhaustion returns0 before allocation or
  eviction and reaches the game's own fallback. First-execution captures now cover
  up to16 distinct payloads, keeping `recording-first` for the first and using
  `recording-<payload-hex>` for later ones. Not compiled or exercised.
- `runtime/engine_effects.cpp/.h`: per-payload effect/geometry/replay-value ownership
  and older replay selection, independent of the reused global packet. Material
  accumulation survives, while per-record traversal state resets after finish.
  Original56+56 staging/copy checks remain. Added first-mesh raw input captures and
  `rigid-first-draw.json`, including actual color mask, cull/depth state and PS46,
  PS47,PS49 material bits. Not compiled or exercised.
- `config/simpsons.toml`: six **non-skipping** observation hooks already merged:
  `826F4C70/826F4CF4/826F4D00` LRU; `826F3988/826F39A0` counter;
  `826F4D50` quota return. Names match wrappers in recording.cpp and12-byte anchors
  came from the pinned original image. Do not rerun the one-shot add-hook scripts.
- `tests/test_rigid_mesh.cpp`: three independent cached objects on one deferred
  context, A/B/A/C/B replay, updating only A, immutable material IDs, unchanged
  immediate state/pixels during recording and per-object lease retirement.
  Added to existing WARP/hardware targets; not built or run.
- `tests/header/test_recording_graph_contract.h`: completed **unwired fixture** for
  A/B/A/C/A, seven graph corruptions, original event reset and subsequent replay.
  Requires three genuine prepared rigid packets from a runtime fixture. It is not
  included by a test target and has not been compiled or run. This is unfinished
  integration, not passing CPU graph coverage.
- `tools/analyze_rigid_capture.py`: reads first-mesh raw geometry/declaration/indices
  and original replay VS constants, reports approximate double-precision clip
  rejection/candidates and triangle winding signs. It does not simulate original
  shaders, rasterization, culling or depth tests. Inside/outside, degeneracy and
  strip-restart self-checks passed; no real captured mesh has been analyzed yet.
- `tools/checkpoint.py`: accepts additional exact evidence files through
  `--include-file`, offers `--current-only` to omit historical build logs, and
  streams archive data instead of retaining all inputs in memory. Default
  source/evidence selection remains available.

Read-only review found no confirmed bugs in the supported multiple-record model
before work stopped. Review of the latest fixture/source revisions was incomplete.
Retry, eviction, pool growth and populated original destruction remain guarded.

## Resume

1. Review frozen multiple-record changes and decide how to integrate the unwired
   CPU fixture. Retain the real original CPU helpers and owner/state checks.
2. Regenerate using the pinned generator/analyser, then build the application and
   affected tests. Run tests before replay; running driver tests and replay at the
   same time previously made startup and test timing much slower.

```powershell
python -B tools/recompile.py --generator build/generator-ninja/XenonRecomp/XenonRecomp.exe --analyser build/generator-ninja/XenonAnalyse/XenonAnalyse.exe *> build/reach-game-multiple-regenerate.log
& build/original-screen/build-focused.ps1 -Targets SimpsonsNative,EngineDriverTests,RigidMeshTests,RecordingDepthBindingTests,RigidMaterialConstantsTests *> build/reach-game-multiple-build.log
ctest --test-dir build/native --output-on-failure -R '^(NativeRigidMesh(WARP|Hardware)|NativeRecordingDepthBinding(WARP|Hardware)|OriginalRigidMaterialConstants|OriginalDriverLifecycle)$' *> build/reach-game-multiple-tests.log
python -B tools/auto_start_native.py --run-directory build/automatic-startup/reach-game-028 *> build/reach-game-028-live.log
```

3. Inspect first-draw actual state and per-payload image changes. Analyze the raw
   first mesh after the new capture exists:

```powershell
python -B tools/analyze_rigid_capture.py build/automatic-startup/reach-game-028/captures --output build/reach-game-028-projection.json
```

4. The expected next unported path after the original16-record quota is immediate
   rigid rendering through827400F8,8273FF80 and82701220. Its submesh callbacks use
   82700498 and826B54D0, distinct from the recorded material helper826B5618. Read
   `build/original-screen/rigid-immediate-plan.md` before implementation. Do not
   alter quotas or manufacture recording failures/success to select a route.
5. Broaden testing after further source changes justify it, then verify actual
   completed scene presentation and character control. The automatic replay's
   `gameplay_verified:false` is deliberate and cannot establish gameplay itself.

## Preserved boundaries and evidence

The port is x64 ahead-of-time PPC-to-C++ with owned native graphics services.
No runtime CPU interpreter/JIT, console GPU executor or runtime shader translator.
Original CPU allocation/cache/control flow remains authoritative. Native device,
FX and payload identities stay unmapped; do not fabricate SDK-shaped objects.
Owned CPU parameter/upload buffers are legitimate and retain explicit provenance.
Original game data, saves and reference trees remain unchanged. Regenerate from
source/configuration; never edit generated AOT. This workspace is not a Git repo.

Supporting plans: `build/original-screen/multiple-record-plan.md`,
`docs/native-recording-runtime.md`, `docs/native-rigid.md`,
`docs/automatic-startup.md`. The original effect-union mode stays unchanged. The
tested material bridge admits its nonzero mode only when all first64 filtered
shared dirty bits are zero, proving no work in all eight shared categories.

Snapshot: `checkpoints/native-rigid-live027-20260913.zip`. Its internal manifest
hashes each archived file. The adjacent receipt records the archive checksum and
verification. The archive preserves the current **unbuilt** authored sources and
selected last-tested evidence; it does not turn the old generation manifest into
proof for the new source. The last-tested main executable is included explicitly,
and its identity is recorded in the receipt. Large retail data is not included.

`STATUS.md` contains historical non-UTF8 bytes after the September11 marker.
Update its prefix with byte-preserving operations, not apply_patch.
