# Original full-size camera copy

The engine helper826B08B0 now copies qualified full1280x720 camera attachments
to the separate original shared color/depth copy roles. Original startup reaches
the depth-only call from826B1030 with cameraE4D41AB0, private depth00F0001A and
destination00F00003. It then stops at the existing binding-reset restriction
that still assumes default targets. No original game screen or draw is verified.

## Original contract

The helper takes optional color destination in r3, optional depth destination in
r4 and camera in r5. It calls original8269D388 for the camera rectangle. That
leaf finds the camera in the original viewport manager and copies the four words
at manager+24+16*slot, or falls back to the camera raster's full dimensions.
The helper supplies source rectangle(0,0,right-left,bottom-top) and destination
point(left,top) to82455570. The color selector is0; depth is4. The clear flags,
mip/slice arguments and auxiliary arguments are zero. The zero clear values
are unused because neither clear-enable flag is set.

The SDK's checked instructions select depth from its current attachment for
selector4. For the established single-sample profile they choose raw depth
copy without a clear. Destination-point offsets are applied relative to the
source rectangle. These instructions are evidence only: the native runtime
never executes this SDK body or constructs its command stream.

The actual callback826B1030 gets its destination from823ED9C8, which returns
82D0CF84, then calls the helper with no color destination. The following effect
name is `shadows`. The destination is the previously established separate
1A220197 depth-copy role, with floating20e4 depth and eight stencil bits.
Both decoded20e4 values and stencil are retained by native storage.

`build/viewport-copy/evidence.py` checks complete original spans, direct calls,
all hook byte pins, the explicit leaf extents, and reference-header hashes.
`original-chain.txt` is the final checked disassembly; `initial-disassembly.txt`
is an earlier exploratory listing with incomplete leaf extents and is not the
contract authority. The existing ReAgent/Ghidra trial remains documented in
`docs/reagent-ghidra-probe.md`; no generated replacement candidate is used here.

## Native behavior and limits

The native helper verifies driver/context ownership, the selected original
camera, its active CPU pass, native attachment cache, and fixed destination
roles before GPU work. The original rectangle lookup stays AOT. Only a full
1280x720 rectangle at(0,0) is currently qualified. Both destinations are
preflighted before either copy; a genuine device/submission failure after work
has been queued cannot roll it back.

Color uses exact RGB10A2 storage copies. Depth uses matching
R32G8X24_TYPELESS resources, preserving float depth and all stencil bits.
Neither operation changes native bindings, clears, shader state or presentation.
A real GPU event retains both resource owners until it completes and is retired.
Discarding a receipt cannot release pending backing; driver shutdown waits for
work before releasing ownership. A completed copy is not a shown game frame.

D3D11 permits matching whole-resource depth copies; partial depth copies must
copy the whole subresource with zero destination offsets. Split-screen depth
placement needs a separately verified implementation and currently rejects.
[Microsoft CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource),
[CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion).

No general depth rasterization,20e4 write rounding, reversed-depth shader
transform, shadow sampling or other copy destination is qualified by this work.
Actual private depth contents before this first copy are unspecified. The copy
does not invent initialized depth or establish correct scene depth.

## Validation

`OriginalViewportCopies` passes6,242 checks. WARP and hardware tests use an
independent depth fixture containing eight exactly representable depth values
and all256 stencil values. Every pixel's depth bits and stencil are checked,
alongside color preservation, actual pipeline snapshots, event ownership,
thread/device/source/extent/predication rejection and abandoned receipts.

The original-startup fixture constructs all three original viewport cameras,
then exercises loading/full private sources, color-only/depth-only/both/null
requests, preserved defaults, integer/floating-point ABI, invalid destinations,
malformed rectangles, inactive/mismatched cameras, both partial-camera
rejections and original camera reset/deletion. It checks that presentation
counters remain at their pre-existing startup values; an initial test wrongly
assumed those counters started at zero and was corrected without changing the
copy implementation.

All automated runs are muted. Full-build and actual-run evidence are recorded
in the build169 summary and project STATUS. Screens, world, gameplay,
progression and save/load remain unverified.
