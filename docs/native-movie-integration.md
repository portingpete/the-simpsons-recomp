# Original movie draw bridge integration tests

`tests/test_movie_planes.cpp` now has an optional `--integration` mode. Its
existing two-profile plane-lifetime mode remains the default. The new mode
drives the original presenter, uploads its original allocations, calls the
native movie bridge, checks output and retained state, and exercises rejection
and recovery. No runtime, CMake, generated code or renderer file was changed.

Main integration validation passes25,597 checks, including five real native
movie draws. The default plane-lifetime mode remains separately tested.
The fixture first submits a real original Im2D draw to establish the full native
viewport, matching actual startup; it does not inject backend readiness.

## Build184 handoff

Reuse the existing `MoviePlaneTests` target, its `SimpsonsRuntime` dependency,
C++20 configuration, `/fp:strict /W4 /WX`, and large stack link option. No new
library, runtime hook or public accessor is required. Main can add a separate
CTest registration using the same executable and these arguments:

```text
MoviePlaneTests K:\SimpsonsNativeCopy\analysis\simpsons.pe --integration
```

The existing command with only the image argument continues to run the
original plane-lifetime suite. The integration mode expects the generated
`8282E3E8` entry to call `SimpsonsNativeMovieDraw`; it does not intercept that
draw or supply a replacement success result. A successful run prints
`PASS original movie integration` and has submitted exactly five test movie
draws relative to its starting count. This task ran no executable or shared
build and wrote no original/reference input.

## Real startup and ownership

The mode uses the existing original pre-FX checkpoint at `828166FC`, shared by
`effect_catalog_lifecycle_helpers.h` and the working setup pattern in
`tests/test_original_screen_bridge.cpp`. It checks for real submission,
textured declaration, shared shader-header/code storage and VS/movie-PS owners.
Missing owners fail the fixture; no readiness field or fake shader is supplied.
This checkpoint occurs after original screen initialization. The earlier
first-presentation checkpoint in `test_engine_driver.cpp` is insufficient for
these shader/declaration prerequisites and was not adopted.

Original camera begin `823F1A18` activates the loading camera from `82E07248`.
Original mode-zero pipeline scope `826B09A0` uses storage obtained through the original
general allocator, then pairs with `826B09F0` and camera end `823F1A08`.
The fixture requests the verified guard-one state through the existing public
state owner. Original `82409308` supplies the preceding Im2D viewport setup.
The earlier screen-quad setup attempt correctly failed: that helper restores
the prior native viewport and cannot establish an absent one.

The existing harness provides real original constructors and allocation paths:

- Movie allocator adapter `8274B1C8`, provider `8282ED68`, allocator setter
  `8282E910`, presenter `8282EB00`, descriptor initializer `823738C0`.
- Original three-plane allocation `8282E940`, original public lock/unlock
  wrappers, cached RasterContexts and distinct R8 native owners.
- Four simultaneous frame descriptors: two1280x720 and two640x480, with twelve
  native planes. CPU fixtures fill only the owned pitched allocations and retain
  padding sentinels. Descriptor guards and publication words are independently
  checked against the original allocator's field writes.

The public `movieDrawCount()` plus existing state, camera and readback APIs are
sufficient for these end-to-end checks. A separate forwarding `movieFrame`
accessor is not required. The aggregate snapshot is tested through the real
draw; direct inspection of the returned `NativeMovieFrame` value is not claimed.

## Positive draw and original return ABI

Each fresh incoming frame stays locked until the fixture invokes original
`8282EC58(presenter, descriptor)`. That parent publishes presenter+8/+C/+10,
relocks the previous frame, commits the incoming descriptor at +14, uploads
through three public unlocks, calls `8282E3E8`, and executes its own epilogue.
The original constructor leaves the prior-frame return callback null; the test
preserves that valid path and does not fake a queue callback.

The four calls cover width1280/flag1, width1280/flag0, width640/flag1 and
width640/flag0. Descriptor byte+51 is independently set to A0..A3, exposing
confusion between caller r4/r27 transport and presenter byte+41 geometry.
SP, LR, r2/r13, all nonvolatile GPR/FPR values, CR2..4 and v20..31 are compared
across the complete parent call. The original movie function is void; no
invented boolean or volatile-register return value is asserted.

Readback checks every pixel of the1280x720 target, including both triangles,
all edges and the alpha bits:

| Original frame plane0 / plane1 / plane2 | Shader samples Y / Cr / Cb | Expected native RGB10A2 word |
| --- | --- | --- |
| 16 / 128 / 128 | 16 / 128 / 128 | 00400003 |
| 93 / 37 / 211 | 93 / 211 / 37 | 00039B7F |

These literal expectations come from the independently decoded shader fixtures
in `build/movie-shader/movie-shader.json` and the established explicit native
packing model. Unequal chroma makes the original stage order **0,2,1** observable.
Uniform planes remove filtering/edge ambiguity from the bridge test. They do
not test vertical orientation or distinguish the cropped UV branches by pixels;
the geometry module's exact-bit tests and backend's spatial fixtures cover those
separately. The first fixture preserves the reported initial decoded plane
values rather than imposing black/opaque output.

## State, caches and rejected transactions

Before each positive draw and the rejection suite, the test requests distinct
half-pixel, cull, depth and alpha states and point min/mag filtering through the
public initialized state owner. Original application sampler selectors are
resolved from the original SDK-ID table. It also seeds the three CPU binding
cache words with the **real original flat** declaration/VS/PS owners. These
distinct inputs expose premature state/cache publication after an earlier movie
draw; the test does not merely compare already-equal movie values.

On success, the expected changes are independently enumerated from the original
draw: four scalar requests, min/mag for stages0..2, packed replacement blend for
target0, and the declaration/VS/PS caches. Every other exposed scalar, all other
sampler fields/stages, packed blend targets1..3, selected camera and logical
viewport remain unchanged. Depth-write alternates between0/1 to verify retention
while actual depth/stencil storage remains byte-identical.

Snapshots compare application state/stack bytes, RenderWare pending/applied
state and queues, stream-zero/other stream cache words, attachment caches,
camera ownership globals, presenter/provider/allocator bytes, descriptor guards,
RasterContexts and raster metadata. Draw/clear/copy/present/reset counters and
native raster/physical-pool counts are checked. Positive parent writes and
incoming unlock fields are accounted for explicitly, not ignored wholesale.
Prior frames must have been relocked without changing cached samples or pitch.

Rejected leaf calls use the correct post-parent ABI as their baseline and mutate
one condition at a time. Tests cover:

- Wrong LR, stack alignment, presenter/r3/r31, r4/r27, loop count and context
  pointer registers; consistent wrong descriptor and frame-byte transport.
- Presenter class, descriptor magic/selector/count/dimensions, absent or swapped
  plane pointers, another live descriptor's plane/context, pitch, CPU pointer
  and raster native-identity publication.
- A real relock of each individual plane through the original public wrapper,
  followed by valid original unlock/upload recovery.
- Inactive current-camera publication, missing original declaration, corrupted
  original CPU-created VS/PS header/code words, unsupported inherited stencil,
  color mask, guard and expanded-target state, and foreign-thread access.

Each rejection verifies the intended error gate rather than accepting an
unrelated earlier setup failure. State/cache/ownership snapshots, caller ABI,
draw counters and color/depth readbacks must remain unchanged. Temporary input
mutations themselves must also survive until the test restores them. A final
valid leaf call verifies recovery and the unequal-chroma output again. Original
`8282EA50` retirement then releases all twelve plane owners, including locked
older frames and the unlocked current frame; weak native references must expire.

## Evidence and remaining limits

The new 22 presenter/draw instruction/data pins were checked directly against
the immutable15,466,496-byte image, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Static inspection confirmed the generated whole-draw boundary and the existing
original screen setup precedent. A second read-only review checked C++ source,
gate ordering, original parent writes and retirement expectations. Compilation
and execution remain for main. See
[source-verification.json](../build/movie-integration/source-verification.json).

The user separately reports boot151 continuing beyond20 seconds with native
movie draws, five initial captured frames, and182 presentations/323 total draws.
That report supports continued original presenter/decode progress; it is not a
run of this new suite and is not relabeled as its validation.

Remaining: actual build184 test results; direct `NativeMovieFrame` copy/lifetime
inspection; nonnull original prior-frame return callbacks and asynchronous
decoder/queue interaction; direct D3D context assertions for texture unbinding
and retained native bindings; device/allocation failure injection; spatial
sampling/orientation and original-console precision parity. Existing standalone
geometry/backend tests remain complementary. No new playback, scanout, audio,
or visual movie-picture claim follows from this unexecuted integration test.

Changed files in this task: `tests/test_movie_planes.cpp`, this document, and
`build/movie-integration/source-verification.json`.
