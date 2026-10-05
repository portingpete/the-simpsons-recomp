# Immediate effect crash audit — September 27, 2026

The second reported crash is recorded in
`build/render-tests/20260927-134626-488668/game.log`. It ends at scene 1805 with:

```text
Unimplemented general immediate geometry draw: caller=8277B124 primitive=6 vertices=4
```

The earlier animated mono-mask repair is active in this run. The log records
1,439 mono draws, including a 17-bone mesh. This failure is the next effect
submission, after those masks rendered successfully.

## Cause and repair

The original `8277B230` routine transforms a polyline into view space and calls
`8277B040` to construct each beam segment. Its four-vertex reservation had no
native consumer. The repair keeps the original transforms, width calculations,
vertex writes and segment loop, and submits the completed vertices through the
existing qualified mode-zero shaders, VS `821511D8` and PS `821509D8`.

The audit also found missing submissions in the endpoint beam, six-face queue,
eight-vertex strip, seven-vertex ribbon and both decal producers. Their original
CPU geometry, loops, callbacks, early returns and resource lifetimes are retained.
Hooks pin the exact original instructions at each native rendering boundary.

Decals need independent triangles as well as strips. Their projected variant,
and projected billboard/trail variants, use three additional original shader
records: VS `821513B8`, PS `82150C98` and dual-texture PS `82150F18`. These have
separate native artifacts, verified instruction by instruction against the
original records. They retain the original homogeneous transform, conditional
projection, four half-texel shadow taps, depth comparison, interpolation and
color/alpha multiplication. The shadow input is the live original owner's
1024-square depth surface.

Shared defects found alongside the missing draws:

- These mode-zero producers write only six floats of each eight-float vertex.
  The renderer rejected nonfinite values in the unused second UV pair. It now
  validates the fields the selected shader consumes and clears the unused pair
  in the native upload only. Original ring bytes stay intact. Dual-texture and
  radial shaders still validate all fields they consume.
- The crash's `Arc1` texture had a published copied-ITXD owner (`T=E1B22840`,
  raster `E1B228B8`, header `E1B2290C`), but the shared texture setter did not
  look up that owner. It skipped the bind and retained the previous texture.
  The shared setter now validates the published record and its metadata before
  resolving that header to a native texture.
- Immediate rendering required a viewport left behind by an earlier draw even
  though it validates and installs its own viewport. This could fail on the
  first effect after startup. The renderer now accepts the saved host viewport
  state and restores it after using the effect's own viewport.
- The six-face queue permits a zero texture identity, including its first row.
  The native path now explicitly binds a null resource and preserves the
  reference's RGBA-zero sample. It does not reuse the previous texture. Original
  alpha-test, color-copy and depth-write state still determine the result.
  The same source-qualified null binding is used by beam, endpoint, eight-vertex
  and ribbon producers; their alpha tests discard transparent samples.
- The shared renderer had constant depth offset and slope offset reversed.
  Setter `8243AAA0` (SDK `CC`) writes slope multiplied by 16 into `2A50/2A58`;
  setter `8243AB68` (SDK `D0`) writes constant offset into `2A54/2A5C`.
  Eleven original upload sites map those fields to GPU `2380..2383`, whose order
  is front scale/offset, back scale/offset. The corrected enum and shared depth
  adapter preserve that mapping. The reference's subpixel conversion cancels
  the slope's factor of 16; constant offset is added directly. This avoids
  incorrectly clamping ordinary decal depth to 1. `analyze_polygon_offset.py`
  pins the setters, uploads and register definitions.
- A decal mesh can abandon a deferred rebuild at `82766E3C`. Its completion
  hook now closes the native child scope before the original parent continues.
- Beam preflight now follows the original opacity early return. Faded-out beams
  can skip absent or released point storage without native validation touching
  data the original routine does not read.

## Scope of the caller audit

All nine direct callers of the original immediate reservation were inspected.
The return addresses distinguish the original producers:

| Return address | Original producer | Coverage |
| --- | --- | --- |
| `8275F6E4` | Billboard `8275F168` | Modes zero/two plus projected four/six |
| `8277E640` | Trail `8277E2E8` | Modes zero/two plus projected four/six |
| `8277B124` | Beam segment `8277B040` | Current crash repair |
| `8286D868` | Endpoint beam `8286D638` | Same-shader repair |
| `823CB238` | Quad helper `823CB210`, parent `823CB2A0` | Original six-face queue, retained rows and texture changes |
| `82776680` | `82775F68` | Original eight-vertex strip, fades and edge flags |
| `82778F7C` | `82778F58`, parent `827793A0` | Original selected ribbon segments and object transform |
| `82764738` | `82764608` | Decal quad modes one/five, original parent lists |
| `82766FDC` | `82766D98` | Decal triangle lists, cached geometry and quad fallback |

The audit covers all nine direct callers of this reservation helper. It does
not establish that every other rendering subsystem in the port is complete.
Unknown owners, stale resources and unsupported state still fail explicitly.

## Replay evidence

The reconstructed latest input route contains 637 controller changes, including
mouse camera movement and held special attack. The pre-repair executable
completed that reconstructed route through scene 3425 without entering the
failing beam path (`build/burp-charge-fix/beam-baseline-20260927-135325`). The
log is not a full recording of the original timing and starting state, so it
cannot establish reproduction of the exact manual crash. Tests must execute
the original effect functions directly to verify the repaired boundary.

The first beam repair checkpoint completed the same reconstructed route through
scene 3447 in 44 seconds without a gameplay failure. The harness then closed its
private game instance. Artifacts are under
`build/burp-charge-fix/beam-fixed-20260927-140556`; its executable hash matches
that checkpoint's `build/native/SimpsonsNative.exe`. This remains a gameplay smoke test,
not evidence that the manual beam trigger was replayed.

The final executable completed all 637 input changes through scene 3481 in
44 seconds, with no gameplay or thread failures. The harness then closed its
private instance. Artifacts are under
`build/burp-charge-fix/immediate-final-20260927-145538`; the replay executable's
SHA-256 matches the final development build. The same manual-reproduction
limitation applies.

## Verification

Both final builds passed all 35 affected tests: development in 26.25 seconds
and release in 27.57 seconds. Results are recorded in
`build/immediate-effects-fix/tests-final.log` and `tests-final-release.log`.
Final AOT verification checked 311 generated files with zero semantic
diagnostics (`aot-final.log`).

The affected regression set includes:

- Hardware and WARP immediate rendering, including unused UV data and restoring
  zero or multiple prior viewports, triangle ordering, null textures and bias.
- Original immediate, radial and projected shader evidence; the projected
  analyzer rejects 546 mutations of original records and shader semantics.
- Hardware and WARP projected rendering against an independent CPU oracle:
  1,200 combinations of shadow patterns, UVs, depth and ambient factor, plus
  dual textures, negative projection predicate and resource/state rejection.
- Whole original beam owner, point and segment loops, including transforms,
  diagonal widths, alpha multiplication/testing, repeated draws, degenerate
  points, the original short-segment threshold and the 25-point stack bound.
- Whole original endpoint beam, including its optional color callback, endpoint
  alpha, hidden/invalid owners, repeated draws and original vertex bytes.
- Five copied-ITXD lifecycle cases with the shared texture caller, mutated
  metadata, preserved bindings on rejection and stale resource rejection.
- Original mono-mask and screen-bridge regressions.
- Whole original six-face, eight-vertex, ribbon and decal producers, with
  actual pixels, source bytes, owner lifetimes and nonvolatile-register checks.
- Shared depth adaptation across shadow, z-prepass, mono, rigid, skin and sky
  meshes on hardware and WARP; material inventory and engine-state bridges.

`build/immediate-effects-fix/verification.json` records executable hashes, test
logs and replay results. Intermediate checkpoint logs remain in that directory
to distinguish earlier validation from the final binaries.
