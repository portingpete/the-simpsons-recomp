# Original reflection camera allocation

The native raster owner now admits the original square16/256 reflection profiles
in addition to1024 shadows. Other private sizes and rectangular requests still
fail explicitly. The change retains the original allocation, plugin callbacks,
format helper, list insertion/removal and private-depth placement calculation.
It replaces only the existing SDK surface boundary with owned native backing.

The whole original8273BC58(N) helper, already pinned in the cubemap and earlier
shadow-constructor evidence, requests type5 `(N,N,0,5)` color and type1
`(N,N,32,1)` depth rasters. Its camera/frame constructors and attachment remain
original AOT code. The camera selects projection1, keeps the base1.0 view window
and reciprocal with zero offset, and sets near1/far400. The retained constants
are82000BB0=3F800000,821DD0D8=00000000 and82150490=43C80000. In particular, the
base zero offset uses a signed displacement from821E0000; it is not821ED0D8.

The registered EA44 plugin constructor8269E2B8 stores zero at the camera's
82CED790 extension. Reflection leaves it zero. Shadows alone call826B84D8 and
replace it with the21-entry state allocation. Sharing the raster services does
not imply identical camera construction or CPU extension ownership.

Private-depth CPU leaf823ED930 receives `(N,N,18280186,0)`. Its original scalar
is `ceil(N/80)*80 * align16(N) * 4 / 5120`:1 for16,64 for256 and832 for1024.
The new fixture executes all three cases and checks SP/LR/nonvolatile registers.
The bridge retains this call and checks the profile's exact result; the scalar
is original eDRAM placement metadata, never a native address. The actual depth
format remains1A220197. Native color uses RGB10A2 and depth uses the established
D32_FLOAT_S8X24 representation, with the previous depth-precision limits.

CTest `OriginalReflectionCameraLifecycle` runs the actual muted startup to the
established pre-FX observer, then executes two concurrent original reflection
cameras,16 and256, twice. All514 checks pass. They cover original projection,
clip/view fields, plugin registration, frame links/matrices, raster metadata and
padding, original list order, native allocation/readback extents, distinct
identities, resource lifetime and stale IDs. The612-check shadow case remains
green. No initial GPU pixel values are assumed and no draw or clear is submitted.

The second reflection cycle reuses camera allocations and reuses an old frame's
heap storage for a raster; same-type raster reuse is not observed in that cycle.
The shadow case still demonstrates same-type raster reuse. Neither fixture
forces the allocator or changes its metadata. Both reject retired native IDs.

Cleanup here explicitly invokes original isolated-owner helper82714220, which
removes the attached rasters and releases their backing. The actual reflection
owner destructor8273C000 instead calls823F1D48 directly after releasing its three
textures. This test does not establish that owner's normal camera/frame/raster
cleanup, nor the global teardown route. The subsequent284-check owner regression
now executes that destructor and observes the retained raster/frame attachments;
see `native-reflection-texture-lifecycle.md`. The constructor guard has been
replaced by the qualified native ownership bridge; the next loader stays guarded.

Source: `runtime/engine_rasters.cpp` and the `--cube` mode of
`tests/test_shadow_camera_lifecycle.cpp`. Focused evidence is retained in
`build/reflection-cubemap/camera-focused160-tests.log`. The earlier exploratory
failures retain the incorrectly addressed zero expectation, shadow-only state
expectation and same-type heap-reuse expectation; those were fixture assumptions,
not reasons to change the original camera code.
