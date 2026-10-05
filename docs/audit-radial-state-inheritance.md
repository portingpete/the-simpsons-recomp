# Original radial draw state and regression scope

This audit traces the Tree Hugger rejection `Immediate stencil/cull/fill/clip/viewport state is unqualified`, captured in `build/restrictive-audit/stage-repairs-expanded/tree_hugger-92e450f6`. That older run did not record the complete radial snapshot, so it does not establish which member of the combined guard failed. The later `tree-radial-lifetimes/tree_hugger-655c8ec3` route completed 20 actions in 51.33 seconds and retained 45 radial observations: creation inherited cull 2, while drawing inherited cull 0. This later success does not reconstruct the older rejected tuple.

The mapped retail image is `analysis/simpsons.pe`, SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. A recorded last-function value `82774938` is the read-only fade helper called by radial code at `8276BA2C`; it is not the radial state producer or virtual caller.

## Whole original caller

The original owner loop `827803E0` performs these operations:

1. Calls `8276B270` at `82780414` to construct the descriptor on its own stack from the current camera and emitter. This can take an optional direct-sprite path.
2. Requires an active descriptor and a nonempty original object list (`owner+0xD0` through sentinel `owner+0xD8`).
3. Calls original batch setup `8276B750` at `82780438`.
4. Invokes each object's vtable `+0x38` at `82780464`, using the actual camera and stack descriptor. Radial object vtable `82158D0C` stores `8276B938` in that slot. The loop follows each object's `+0xA0` link.
5. Calls original batch cleanup `8276B898` at `8278047C`.

The radial constructor is `82780608`: it calls base constructor `82774AD0`, publishes vtable `82158D0C` at `8278063C`, stores the original definition at object `+0xB0`, and optionally acquires a texture through `82752910` plus original callback registration. Cleanup `82780738` removes that callback when a texture exists and unlinks the object from its original list. A fixture should use these constructors/destructors if it exercises the parent owner loop; copying a vtable pointer alone does not prove object lifecycle support.

## Explicit state versus inherited state

Batch setup `8276B750..8276B890` explicitly disables alpha test, enables blending, disables depth testing and depth writes, installs packed blend `0x10106`, and establishes the original stage-0 linear/mirror and stage-1 point/mirror sampling state. Batch cleanup `8276B898..8276B930` disables blending, enables depth testing and depth writes, disables alpha test, and removes declaration/texture bindings.

Neither function sets culling (`82439F00`), stencil enable, fill, clip-plane enable, viewport enable, half-pixel offset or alpha-to-mask. The radial body `8276B938..8276BEA4` also leaves those fields inherited. Its graphics calls are VS/declaration/PS selection (`8276BC68`, `8276BC80`, `8276BCB0`), expanded-blend toggle (`8276BD54`, `8276BE90`), constants (`8276BD70`), vertex reservation (`8276BDA8`) and completion (`8276BE84`). The optional direct-sprite path may establish a different inherited state; absence of that path must not silently acquire its state changes.

The bridge's `EngineDriver::directSpriteBatch` preserves these original state changes. `radialOperation` snapshots the inherited state before retaining the original CPU vertex stores. The backend previously required culling off and hardcoded `D3D11_CULL_NONE`. Its scoped radial implementation now preserves the existing native scene and Im2D meanings of `0`, `2` and `6`: no culling, back-face rejection with CCW front, and back-face rejection with CW front respectively. Admission and rasterizer behavior change together. Other immediate primitives retain their previous cull contract.

The SDK setter `82439F00` stores three bits and getter `82439F20` returns them; that alone does not prove all eight encodings are legitimate authored cull modes. The radial implementation accepts only the established `0/2/6` meanings. Stencil, fill, clipping, viewport, half-pixel and alpha-to-mask admission remain unchanged. No recorded receipt establishes any other member of the old combined failure as the cause.

## Original size and geometry contract

The definition's segment byte (`object+0xB0`, then `+0x11`) is clamped to `3..32` at `8276BBCC..8276BC38`. Mode byte `+0x10 == 2` rounds the count up to an even value at `8276BC3C..8276BC4C`, preserving the maximum of 32. Thus the original vertex count is `2*(segments+1)`, from 8 through 66, or from 10 through 66 for mode 2. Reservation is primitive 6 and 12 bytes per vertex. The CPU produces every packed color/radius/angle pair at `8276BDD0..8276BE7C`; it repeats the first angle at the closing pair. Preserve that loop in a regression.

The backend regression retains its manual radial mesh/query case and adds opposite winding with cull 2/6, requiring one orientation to draw and the other to leave the cleared target unchanged. `tests/test_radial_original.cpp` additionally uses actual parent and radial constructors, the whole `827803E0` loop, original CPU stores and paired cleanup/global release. `OriginalRadialOwnerLoop`, `NativeImmediateWARP` and `NativeImmediateHardware` passed in `build/restrictive-audit/producer-ranges-frontier-tests.xml`.

## Focused regression design

Use the existing original-startup checkpoint in `tests/immediate_original_helpers.h`. Original startup calls `8276BEA8` at `82752218`, creating radial declaration `[82DFF284]`, the companion declaration `[82DFF288]`, shader-list links and query owner `[82DFF28C]`. Do not invoke the creator a second time against live owners.

For each independently selected case, construct valid original owner/definition/descriptor inputs and run actual `8276B750`, `8276B938`, `8276B898` through `EngineCpuCalls`; use `827803E0` as the stronger parent-loop case once its original descriptor preparation and linked ownership are present. Assert original nonvolatile registers, retained camera/texture references, unchanged source inputs, exact reservation/count and CPU-generated ring bytes, one completed native submission, and expected setup/cleanup scalar changes. Exercise cull `0`, `2`, `6` with asymmetric center/scales and opposite ring winding so a hardcoded no-cull rasterizer cannot pass. Compare changed pixels and unchanged depth/stencil, and verify retained host bindings are restored.

Include raw segment bytes below 3 and above 32, both extrema, mode-2 odd rounding, flat and ready query paths, and the original not-ready query early return. Keep malformed declaration/owner, query-cell, consumed nonfinite values and vertex-reservation checks before submission; rejection must leave color/depth and draw counts unchanged. Complete original global release `8276C010` (normal startup-owner cleanup calls it at `827522A4`) and assert `[82DFF284]`, `[82DFF288]`, `[82DFF28C]` clear, with no active radial phase or retained declaration ownership. A terminal native destructor after a rejected draw does not qualify as successful original release.

The source pins and direct-branch census are retained in `build/restrictive-audit/radial-source-evidence.json`. The scoped production cull repair and whole-owner fixture passed the coordinated native tests. The passing route recorded cull 0 draws; it remains insufficient to assign a particular cull value to the older failure.
