# Original reflection owner with native resources

Actual muted boots103/104 complete original reflection constructor8273C2B8 on the
zero-flag16 path and advances to the explicit image-memory loader82B84838 stop,
caller826FF1D8. The constructor's CPU container, three resource publications,
quad query, six original memset calls, camera construction and parent enable
selection all execute. No original draw or new game image is established.

The runtime owns one record per original90-byte object. An entry preflight
admits only the two original parent callers and their sizes16/256. The context
retain call at8273C2F8 becomes a native driver dependency lease. Driver stop
rejects any live reflection record, including partial construction/destruction;
terminal teardown releases those children before the backend. This models the
needed lifetime without a console device header or SDK+3C reference counter.
The original caller does not consume the AddRef return value.

The shared texture factory dispatches the three verified reflection callsites
to six-face RGB10A2 cube storage, full-size RGB10A2 2D storage and half-size2D
storage. It retains the existing shadow allocation path for its callers. Native
identities use the driver's monotonic, unmapped target namespace. Original
instructions publish O+8/+C/+10. Allocation leaves GPU pixels unspecified.

The original lock/clear/unlock loop visits0,1,4,5,2,3. Each lock publishes its
qualified pitch and a real mapped CPU staging allocation, filled with a checked
canary. The original memset writes exactlyN*N*4 contiguous bytes. Unlock checks
the whole staging allocation: that prefix must be zero and the rest unchanged.
The prior original-layout proof maps this uniform zero to top8 logical rows for
N16 and all256 rows forN256. Only those rows are uploaded; staging is freed after
the immediate native API has captured the input. No undefined lower rows or
companion2D pixels are invented. This adapter admits only the constructor's
bounded initialization, not arbitrary console texture locks or command streams.

Commit validates the borrowed quad's original vtable820B7170, manager/wrapper
publication and live native effect, plus the original camera and its native
color/depth raster ownership. It then records readiness. Cube sampling,
orientation, expanded filtering precision, forced sampled alpha and later
rendering/application remain separately unqualified.

The original paired destructor8273C000 now executes. Entry requires a complete,
unlocked owner with no texture attachment. The SDK context release at8273C028
releases its native lease; original code clears O+14. Original resource-release
calls then retire cube/full2D/half2D in order and clear O+8/+C/+10. The direct
camera destructor823F1D48 and CPU-container free8269BF10 remain original. A final
hook checks completion and retires the host record. The outer object and its
global publication are not freed/cleared by this body.

The lifecycle regression passes284 checks over four complete constructors and
paired destructors: simultaneous16/256 owners,24 original face clears, both
destruction orders and fresh native identities. It runs the original parent
826FF0F8, so allocation descriptors, callsites and constructor arguments are
provided by original code. It catches the subsequent image-loader guard before
that loader changes constant-texture outputs. Whole owner bytes, known GPU rows,
unrelated bindings, context/resource counts, shared ownership, stale IDs,
wrong-thread access and rejected lock/context states are checked. The original
destructor's nonvolatile ABI is checked. No initial unknown pixel values are
assumed. The separate WARP/hardware test verifies untouched regions with seeds.

The test also confirms a remaining ownership gap: direct camera destruction
detaches the camera from its frame but leaves the frame and two attached rasters
alive. Their native backing remains owned by the raster service. Only after
checking that exact result does the fixture explicitly call the original raster
and frame destruction helpers and free the outer object. Those extra operations
are not substituted into the game's destructor. The normal global826FF248
helper remains a no-op, and complete global cleanup is unqualified. The existing
four retained shadow rasters are unchanged.

Source: `runtime/engine_reflection_textures.cpp`, `engine_driver.cpp`, the shared
resource-release dispatch in `engine_quad_declarations.cpp`, and handwritten
hook/native-source configuration. Generated AOT files are not edited. Regression:
`tests/test_reflection_texture_lifecycle.cpp`, CTest
`OriginalReflectionTextureLifecycle`. Focused evidence is retained in
`build/reflection-cubemap/lifecycle-focused161-tests.log`; actual execution is
`build/boot-104.log` after all67 build161 suites pass in101.70 seconds. Original
geometry/owner evidence remains in this directory's
earlier frozen manifests, with new supporting CPU spans in `integration-evidence.json`.

Next qualify the three original white/black/gray memory-image requests and their
decoder, format, mip and ownership behavior before replacing82B84838. The new
loader guard rejects before its first mutation or SDK access.
