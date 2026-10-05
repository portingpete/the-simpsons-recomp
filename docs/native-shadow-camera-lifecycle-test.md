# Shadow camera allocation/lifetime fixture

**Frozen for checkpoint after build149.** `OriginalShadowCameraLifecycle`
passed **612 checks in 4.77 seconds**, completing two concurrent-camera
creation/cleanup cycles with `cameraReuse=1`, `rasterReuse=1`, `frameReuse=0`.
The full build149 suite completed **54/55 passing tests in 88.76 seconds**.
The remaining catalog failure is the explicit texture boundary `82440578`,
caller `82706610`, also reached by actual muted boot092 after four private
native surfaces were created.

The current source is 20,872 bytes, SHA256
`0b16c957d21a36b73da167e871e2c835e4b37163af50c8a161a42946c4ecd1f6`.
Main's frozen [build148 source](../build/shadow-camera-lifecycle/fixture-build148.cpp)
is 20,474 bytes, SHA256
`7f50b34f486cfdc55649b806254fe32588a9cdbc62de4b6c1f3b5596f17bd466`;
its [manifest](../build/shadow-camera-lifecycle/manifest-build148.json) and
logs remain unchanged. This sidecar has **not compiled or run the fixture**.
Main owns build integration, execution and the two source corrections.

**Normal shadows teardown remains unqualified.** The fixture calls the real
creation helper `82704C68` and uses another original helper, `82714220`, as
**explicit isolated-owner cleanup**. This is deliberately different from
the selected shadows destructor `82705700`. The passing allocation fixture
does not prove that production shadows deletion retires its attachments.

The callback assertion follow-up is resolved in main's source correction.
Main independently checked original `823D29E0` instructions and changed the
expected callbacks to plugin `0509`'s `823D2940`, `823D1100`, and `823D1160`
at camera offsets `10`, `18`, and `1C`. This sidecar verified those corrected
assertions by reading the frozen source; it made no further source changes.

## Build149 verified runtime result

Main's frozen [full test log](../build/shadow-camera-lifecycle/build149-tests.log)
records `PASS shadow camera lifecycle: 612 checks` and `Test Passed` for
`OriginalShadowCameraLifecycle`, with duration 4.77 seconds. Both cleanup
cycles completed through the explicit original helper `82714220`. All eight
new raster associations were released, weak native owners expired, retired
lookup/readback IDs were rejected, and the second cycle's old IDs remained
stale despite camera/raster address reuse. Baseline list/count, the other
concurrent camera, default targets and native context checks all passed.

The runtime prints `REUSE camera=1 raster=1 frame=0`. Main retained strict
camera/raster reuse and every stale-ID check while making frame reuse an
observation. No guest address was forced and no allocation order was changed.

[The build149 summary](../build/native-shadow-camera-149.log) records 54/55
passing tests in 88.76 seconds. The frozen
[failure list](../build/shadow-camera-lifecycle/build149-failed.log) contains
only `OriginalEffectCatalogLifecycle`, stopped at the separately guarded
original texture request. This is not an all-tests-passing checkpoint.

[Actual muted boot092](../build/shadow-camera-lifecycle/boot-092-output.log)
creates two private color/depth pairs, with native IDs `00F00007..0A`, then
rejects texture entry `82440578`, caller `82706610`. The request is
1024 by 1024, one level, usage `2`, format `1A220197`, dimension `3`.
The boot therefore corroborates the actual constructor's resource boundary;
it does not claim shadow drawing, completed construction or normal teardown.

The test's later terminal Runtime messages still report incomplete cleanup
of the broader startup graph. The 612-check pass qualifies only this fixture's
explicit camera allocation/lifetime scope, not whole-application shutdown.

## Build148 result and address-reuse correction

Build148 compiled and completed 53/55 passing tests in 93.65 seconds. This
fixture failed check547 because it required immediate reuse of a frame start
address. Main corrected that assertion before the passing build149 run.

[The archived test log](../build/shadow-camera-lifecycle/build148-tests.log)
shows successful creation of the first concurrent pair, explicit cleanup of
both, and creation of the second pair. The failing assertion occurs before
the second cycle's old-ID rechecks and cleanup, so those later checks are not
claimed as passed in build148.

| Original allocation | Cycle1 pair | Cycle2 pair |
|---|---|---|
| Camera | `E2CA71D0`, `E2CA7390` | `E2CA71D0`, `E2CA7390` |
| Frame | `E1A66850`, `E1A66E40` | `E1A66930`, `E1A66E80` |
| Color raster | `E1A66750`, `E1A66D40` | `E1A66850`, `E1A667D0` |
| Depth raster | `E1A667D0`, `E1A66DC0` | `E1A66750`, `E1A66E00` |

Both camera addresses were reused. Raster addresses `E1A66750` and
`E1A667D0` were reused across color/depth roles, while surface IDs advanced
from `00F00007..0A` to `00F0000B..0E`. Neither new frame began at an old
frame address. In fact, old frame `E1A66850` became the new first color
raster. This is observed general storage reuse, not a missing frame free.

The original wrappers explain why frame-to-frame address equality is not an
ownership requirement. Frame creation `823F2200` loads engine allocation
slot `+120` at `823F221C`, requests live size `BE32[82CD1B50]` with `r4=10`,
and calls it at `823F222C`. Frame destruction `823F2B60` runs plugin and
list cleanup, then calls engine slot `+124` at `823F2BFC` with
`(frame, live registry size, 10)`. Raster allocation/free use those same
size-based engine slots at `8240816C` and `82407E1C`. These wrappers do not
reserve freed storage for the same object type or promise the next address.
The allocator's full fit/split/coalescing policy is not inferred here.

Main's build149 correction therefore retains `cameraReuse && rasterReuse`
and reports `frameReuse` without requiring it. This preserves the native
surface identity test across actual guest raster-address reuse. It neither
changes allocation order nor forces the allocator to return chosen addresses.

The separate [actual muted boot091 log](../build/shadow-camera-lifecycle/boot-091-output.log)
also reaches two private cameras and four native surface IDs `00F00007..0A`,
then rejects the next original texture allocation at `82440578`, caller
`82706610`. This is the expected next constructor boundary, not completed
shadow construction, manager finalization, rendering or normal teardown.

## What the fixture exercises

[The test](../tests/test_shadow_camera_lifecycle.cpp) starts the actual Runtime,
uses its established audio observer, and stops at `828166FC`, after the real
muted Dac source and OS worker exist and before the FX manager is published.
It uses a fresh saved entry context and `EngineCpuCalls`. It installs no
replacement guest callback, modifies no dispatch/registration table, sets no
synthetic LR or return flag, and substitutes no guest allocator.

It invokes `82704C68(1024,1024)` twice concurrently, cleans both cameras, then
repeats that cycle. Every camera, frame, raster, list node, camera plugin and
CPU state allocation comes from the actual original AOT call path. No typed
FX object, native FX metadata record or shader is created by this probe.

Checks cover:

- Original live plugin registry links, raster plugin `040C`, camera plugin
  `EA44`, dynamic allocation extents, camera projection/view/near/far fields,
  frame root and matrix fields, and the single camera attachment in its frame.
- The camera's 21-row CPU state allocation and owned row-storage pointers.
  These are the objects produced by `826B84D8`, not a world reference.
- Real type5 and private type1 metadata, unique opaque surface IDs, original
  list-node insertion order, and unchanged consumed shared-depth flag.
- Independent native backing for concurrent cameras. `color(id,alphaOne)`
  must expose RGB10A2, 1024 by 1024, with `alphaOne=false`; `depth(id)` must
  expose a separate 1024-square allocation. IDs must differ from all six
  default/copy/front driver roles and all previous private IDs.
- Actual driver readback sizes: 4 MiB for color and 8 MiB for the native
  depth/stencil allocation. No value is asserted for uninitialized pixels,
  stencil, or unused depth readback bytes. The public depth API exposes
  dimensions rather than a DXGI-format getter; the existing backend source
  establishes `R32G8X24_TYPELESS` with `D32_FLOAT_S8X24_UINT` DSV. A readback
  size alone is not an independent format query.
- Cross-kind ID rejection; expiration of weak native owners after cleanup;
  rejection of retired lookup/readback IDs; survival of the other live camera;
  and fresh identities when the second cycle reuses real camera and raster
  storage. Camera/raster reuse remains a strict assertion, not a host-forced
  allocation; frame address reuse is observational only.
- Unchanged original FX registration rows, raster stages, attachment caches,
  startup camera, default target identities, native context and clear count.

The test preserves real allocation contents. It does not poison unknown
padding, forge an allocation observer, or expect whole-object zeroing.
After creation it snapshots the full camera/raster extents and CPU state
header/table to check interference from the other camera's lifecycle.
This verifies preservation **after creation**; it does not prove the write
history of uninitialized bytes before the original allocator returned them.
Original write-set evidence supplies that separate static constraint.

## Exact raster metadata and scratch effect

Let R be a raster and X = R + `BE32[82E3DC94]`. Both raster wrappers retain
the original parent/self pointer, 1024-square dimensions, zero offsets and
plugin lifecycle. Both have R+14=`20`, R+21=0, R+22=0. Their distinguishing
fields are:

| Field | Color, flags5 | Private depth, flags1 |
|---|---|---|
| R+20 | `05` | `01` |
| R+23 | `0B` | `09` |
| X+00 | unique native color ID | unique native depth ID |
| X+04 / X+0C | zero | zero |
| X+08 | `010000FF` | `000000FF` |
| X+18 | `182801B6` | `1A220197` |

R+18/+24/+28/+2C/+30 and X+10/+14/+1C are not assigned invented initial
values. The original type5 normalizer calls `823F5048` and leaves scratch
`82D0D000..003 = 01 20 0B 00`. The fixture checks this effect and the
neighboring scratch bytes. Type1 does not undo it. The live shared flag at
`82CD1D88` must already be zero; the fixture never resets it.

Main's bounded private-depth bridge also retains the original CPU placement
calculation `823ED930(1024,1024,18280186,0) = 340`. This is an original
placement scalar, not a native GPU address. The fixture does not fabricate
SDK surface headers or call the original SDK allocation/release helpers.
See [the raster contract](native-shadow-raster-contract.md) for the independent
format, placement and per-type list-removal evidence.

## Cleanup distinction and preconditions

`82704DC0` is not a function entry: it is a register restore inside the
`82704D70` epilogue. `82704D70` detaches a camera's frame, destroys the frame,
then invokes `823F1D48(camera)`. It contains no raster destruction.

The selected shadows destructor `82705700` performs equivalent frame/camera
sequences for O+5B4 and O+5B8 at `8270573C/44/4C` and `82705770/78/80`.
The plain camera destructor invokes the live camera plugin destructors,
detaches its object-list link and returns camera storage to the pool. It
does not read C+60 or C+64.

The reviewed known camera plugins do not close that ownership gap:

- `0010`: `823CC538` immediately returns.
- `0509`: `823D11A0` frees its own optional plugin allocation, clears its
  fields and restores the camera callbacks saved by `823D29E0`. It does not
  release either raster or the separate EA44 state block.
- `EA44`: `8269E2D0` only stores zero to C+`BE32[82CED790]`; it does not
  invoke `8269EDC0` or free the state's owner, array or row buffers.

For the isolated fixture, `82714220` explicitly handles those allocations:

1. Visit frame children through `823F2628`; detach a parent if present;
   detach this camera; free the now-empty frame through `8273E838`.
2. Destroy C+60 through `82407DC0` at `82714288`, then zero that field.
3. Destroy C+64 through `82407DC0` at `8271429C`, then zero that field.
4. Destroy the CPU state block through `8269EDC0` at `827142B8`, then zero
   its dynamic camera field.
5. Invoke `823F1D48` at `827142C8` for the remaining camera/plugin lifecycle.

The fixture requires a root frame with no children and exactly its own camera
in the object list, so the child callback and parent-detach paths are excluded.
It preflights both native raster owners before entering the destructive
sequence. It removes the older of the concurrent cameras first, exercising
interior raster-list unlink, and checks that the other camera remains intact.
It never dereferences guest objects after freeing them.

The build149 pass qualifies only the explicit isolated-owner cleanup.
It is neither an implementation proposal to replace `82705700` nor proof of
normal game shutdown, normal shadows teardown, total CPU leak freedom or
driver restart. A separately established later owner or explicit native
cleanup policy is still required for the original shadow destructor path.

## Static evidence and reproduction

Run from the workspace root:

```powershell
python -B build/shadow-camera-lifecycle/verify_original.py --test
```

The script reads the pinned 15,466,496-byte flat original image, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`, based at
`82000000`. It validates the PE and `.pdata`, checks the existing disassembler's
every address/word, and compares deterministic evidence with the saved files.
Explicitly reviewed leaves are distinguished from `.pdata` functions.

Current static result: **34 spans, 1,165 instruction words, 35 call-target pins,
24 field/branch pins, and 12 rejected mutations**. Mutation checks remove
individual raster/state/frame destructor calls, change flags/publication
offsets, alter the EA44 destructor, substitute the shadows cleanup call,
change the frame child field, and corrupt/truncate decoded instructions.
Semantic mutation checks deliberately bypass the image hash, demonstrating
rejection by the asserted contract itself.

[Original evidence](../build/shadow-camera-lifecycle/original-evidence.json)
and [disassembly](../build/shadow-camera-lifecycle/original-disassembly.txt)
are bounded lifecycle evidence, not an exhaustive indirect allocator/plugin
call graph. Main's [build149 static check log](../build/shadow-camera-lifecycle/main-checks-build149.log)
confirms the same 34 spans, 1,165 words and 12 rejected mutations. Runtime
and static checks establish separate parts of this bounded contract.
`--write --test` regenerates the two evidence files; do not regenerate or edit
these frozen checkpoint artifacts without reopening the sidecar scope.

Owned new paths are this document, the C++ fixture, and
`build/shadow-camera-lifecycle/*`. Prior caller/pool/finalizer evidence,
production, renderer, CMake, config, shader files and originals are untouched.
Final runtime counts and file identities are recorded in the frozen
[handoff](../build/shadow-camera-lifecycle/handoff.json). No required work
remains in this sidecar; the production texture boundary and normal shadows
cleanup gap remain separate scopes.
