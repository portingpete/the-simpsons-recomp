# Native movie-plane lifetime — build183

The original three-frame allocation now runs through all nine movie rasters in
an actual muted launch. The original video worker starts, but movie presentation
is still incomplete. Boot149 stops in `8243B718`, called by movie draw
`8282E3E8`, while reading absent console-device state at `000029C0`.

## Implementation

Original provider `8282ED68`/vtable `8215D4F0` and presenter
`8282EB00`/vtable `8215D4B0` have separate identities. The original provider's
`8282E940` method creates each descriptor's three root rasters. The existing
shared raster factory now admits that specific caller with format `28000002`,
flags `400`, selector zero, and dimensions 1280×720 or 640×480. The original
descriptor/context stores, raster wrappers, allocation loops, and decoder
remain ahead-of-time compiled.

Each plane owns one D3D11 R8 texture and stable CPU storage. The native factory
calls the original pooled allocator `8238E880(size, BC800000)`; paired raster
destruction calls `8238EB00(pointer, B1800000)`. This preserves the original
pool, page alignment, cache/protection request, and allocation TLS stores.
Using additional platform physical allocations exhausted the already committed
game budget in boot148; the corrected path uses the original existing pool.

Pitch rounds width to 256 bytes. For 1280×720 output, pitches are 1280/768/768
and heights are 720/360/360. Logical byte counts remain width times height;
row padding is separate. Public lock/unlock functions retain their original
bodies; verified callback cuts publish and clear their defined CPU fields.
The initial pixels are unspecified. Unlock copies logical rows into native
R8 storage, preserving the stable CPU address across later relocks.

The original frame destructor retains ownership of contexts and raster
allocations. It supports locked, unlocked, and mixed planes through its normal
loop. Native backing is released when that loop destroys each raster.
Crossfade textures can coexist with the nine movie planes.

R8 is currently a storage format. Existing screen and Im2D pipelines reject it.
No YUV conversion, movie draw, console SDK object, or fabricated decoder result
is supplied by these changes. Native metadata deliberately has no SDK surface
pointer; the original numeric SDK surface-header representation is not shipped.

## Verification and limits

`MoviePlaneTests` executes original descriptor, provider, presenter and allocator
constructors, frame allocation, public access wrappers, and retirement. Its two
profiles cover 18 plane lifetimes and 54 uploads/readbacks with distinct CPU
patterns, row-padding sentinels, exact defined metadata, nonvolatile/thread ABI,
invalid access rejection, and crossfade coexistence. It passes 3,019 checks.
Unchanged physical-pool counts do not independently establish absence of
original heap leaks. Full codec output and video-thread races remain unverified.

Writable-texture tests pass 576 checks on both WARP and hardware, including
R8 readback and rejection by existing draw pipelines. The original crossfade
lifecycle still passes 244 checks. These are resource/ABI checks, not proof of
movie appearance or gameplay.

Boot149 was allowed 180 seconds and exited on the explicit memory failure
before that allowance elapsed. The original video worker was created after all
three descriptors committed. Unadjusted renderer capture0030 contains the dim
Itchy/Scratchy loading artwork, and capture0031 is black. Neither establishes
movie playback or desktop scanout; the capture policy retains only the first
32 changed renderer frames.

Original evidence: [native-movie-planes.md](native-movie-planes.md),
`build/im2d-upload/movie-plane-original183.json` and its image-matched listing.
The next implementation boundary is the original movie draw's direct state
changes and three-plane rendering path, with shader behavior still to be
qualified from the original code and assets.
