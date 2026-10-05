# Native writable RGBA8 textures

The fixture correction is frozen and passed main's build152 in both modes:
**306 checks on WARP and 306 checks on hardware**. Main's complete build152
suite passed **57/57 in 92.45 seconds**. These are main's native fixture runs;
this sidecar performed no builds or test executions.

The same correction previously passed main's build151 in both modes:
**306 checks on WARP in 0.08 seconds and 306 checks on hardware in 0.40 seconds**.
The complete build151 suite was **49/57**, not green; main identified a separate
quad declaration hook capturing unrelated startup calls and owns that fix.
Main's build150
compiled; both writable tests failed because the rejection helper reported
an unrelated cause (WARP 0.07 seconds, hardware 0.32 seconds). The preserved
source proves a mismatched expectation for a deliberately missing view,
described below. No compilation, syntax
compilation, runtime test, original-game execution or CMake edit was
performed by this sidecar.

The new `NativeBackend::createWritableTexture(width,height,format)` allocates
a real D3D11 texture and shader-resource view before returning. The only
supported format is `TextureFormat::RGBA8`; dimensions use the existing
positive 1..16384 texture bounds. Storage is `R8G8B8A8_UNORM`, one mip, one
array element, one sample, `D3D11_USAGE_DEFAULT`, shader-resource binding,
zero CPU access and zero miscellaneous flags.

Creation passes no initial data and performs no clear or upload. Initial
pixels have no specified value, and callers must write the full image before
reading or sampling it. This follows Microsoft's
[CreateTexture2D contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d).
There is no successful placeholder resource or fabricated default image.

## Full-image update contract

`NativeBackend::writeTexture(texture,bytes)` accepts exactly
`width * height * 4` bytes in row-major, tightly packed RGBA order. It validates
the original backend thread, device ownership, resource/view association,
metadata and writable storage before submitting any update. Empty, short,
oversized and padded inputs are rejected. Immutable textures reject writes.

The upload calls the existing immediate context's `UpdateSubresource` for
subresource zero, with no destination box, source row pitch `width * 4`, and
zero depth pitch. D3D11 captures the source bytes before that call returns;
the application may then overwrite or release its upload buffer. This is
source-memory lifetime completion, not a claim that the GPU has finished
executing the upload. See Microsoft's
[UpdateSubresource contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource).

There is no deferred upload pointer, CPU shadow image, format conversion,
channel swizzle, row padding, partial update, mip generation, resolve or draw
in this API. Device availability is checked after submission as well. Input
and ownership rejection precedes submission; device loss is not a rollback
guarantee for an already submitted update.

## Metadata and immutable compatibility

`Texture` has a private `writable` flag, default false. Only the new creation
path sets it true. `validateTexture` compares this flag with the actual
D3D11 resource usage: DEFAULT for writable storage and IMMUTABLE for existing
textures. It also checks:

- Actual dimensions, format, one mip/array element/sample, zero sample
  quality, exact shader-resource binding and zero CPU/miscellaneous flags.
- The existing private row-byte and row-count metadata against the layout
  calculated from the public dimensions/format.
- The texture's real device against the backend's device.
- The shader-resource view's actual resource, format, Texture2D dimension,
  most-detailed mip zero and one mip level.

The immutable `createTexture` implementation is unchanged. Existing RGBA8
and BC1/BC2/BC3 immutable upload/readback behavior remains supported, with
the same dimension/layout checks. Changing a host mutability flag alone does
not make immutable GPU storage writable. `readback` retains its existing
staging copy/map path and now accepts either validated storage usage.

The test-only `NativeWritableTextureProbe` friendship allows the fixture to
inspect real D3D11 descriptions before the first upload and inject temporary
metadata/view faults. No production caller gains a mutability setter or raw
resource getter through the public API.

## Original lock-pitch boundary

The original 32-by-32 border texture's 256-byte CPU lock pitch requires
`256 * 32 = 8192` staging bytes. Its native RGBA8 upload is
`32 * 4 * 32 = 4096` bytes with 128-byte rows. These are different storage
layouts, not interchangeable buffer sizes.

Main's runtime unlock adapter must gather the 128 image bytes from each
256-byte staging row into the full tight image, then call `writeTexture`.
The backend rejects the entire 8192-byte padded staging buffer. It does not
invent pixels, consume padding as texels, or reinterpret a partial upload as
initialization. The API is independent of the runtime lock protocol; this
sidecar does not implement the original SDK texture entry or unlock hook.

## Test handoff

[test_writable_texture.cpp](../tests/test_writable_texture.cpp) is designed to
link against `SimpsonsGraphics`, following the existing native resource tests.
Main owns CMake and the test registration added in build150. Run the resulting test
with no arguments for WARP, and with `--hardware` for an actual hardware
device. Hardware mode has no WARP fallback. Both modes exercise the same
test body:

- Real texture/view creation and actual DEFAULT description before any
  upload, without inspecting initial pixels.
- Three complete, asymmetric RGBA8 updates for 1x1, 19x7, 32x32 and 257x3
  textures, checking exact readback after the caller overwrites and releases
  its input buffer. Independent channels, alpha and non-square rows detect
  channel-order and row-pitch errors.
- Separate native resources and views for simultaneous textures; shared
  ownership and final wrapper release; COM ownership surviving destruction
  of the backend wrapper; rejection by a replacement backend/device.
- Wrong-device, wrong-thread, missing-resource, unsupported-format,
  invalid-dimension, bad-size and immutable-update rejection, with exact
  preservation of previously uploaded pixels where applicable.
- Temporary extent/layout/format/mutability corruption, missing or swapped
  views and a foreign resource. Validation and writes must reject before
  changing pixels; fields are restored before checking readback.
- Existing immutable RGBA8 and BC1/BC2/BC3 exact byte roundtrips and update
  rejection, including an attempted writable-flag forgery on immutable
  storage.
- A modeled 8192-byte padded lock image: reject it directly, gather its rows
  into 4096 bytes, upload, release client storage and verify exact pixels.

The fixture performs no draw, presentation, runtime startup, original guest
allocation or original shader execution. Weak-wrapper expiration checks
shared C++ ownership; it is not a complete D3D11 device-leak audit.

## Build150 fixture correction

The original build150 source and handoff were copied without modification to
[`build150-source`](../build/writable-texture/build150-source/manifest.json)
before editing. The manifest pins the backend, fixture, document and handoff.
The two failed test outputs are preserved in
[`build150-writable-tests.log`](../build/writable-texture/build150-writable-tests.log).
They contain no check number or actual exception text, so the diagnosis is
from the preserved source, not from an instrumented rerun.

`metadataRejections` previously reused the case-sensitive expected substring
`Native` for several faults. Clearing the shader-resource view makes
`validateTexture` throw `Missing native texture resource/view` at its first
resource check. That correct rejection does not contain capitalized `Native`.
Both validation and write still have to reject this fault, and the restored
texture must retain its exact previously uploaded pixels.

Each metadata fault now expects its actual first rejection cause: resource
metadata mismatch, missing resource/view, a view referencing another resource,
or a texture from another device. The BC1 format fault remains on the 19x7
profile and explicitly expects `Unverified compressed texture edge layout`:
`layout` rejects non-block-aligned dimensions before the writable-format and
backing checks. No format support or validation ordering changed.

On an unexpected rejection cause, the fixture prints the actual exception,
expected reason and next check number before asserting. Unexpected acceptance
has the same diagnostic fields. Wrong-thread checks now retain the actual
exception for this diagnostic, and the final failure line includes the total
check count. The successful check count and all pixel-preservation checks are
unchanged by this correction. Both production backend files are byte-identical
to their build150 snapshots; only the fixture, this document and evidence
changed. The correction has not been compiled or executed by this sidecar.

## Review and freeze

Changed production files are only
[native_backend.h](../renderer/native_backend.h) and
[native_backend.cpp](../renderer/native_backend.cpp). New owned paths are
the test, this document and `build/writable-texture/*`. Runtime, CMake,
configuration, shaders, original assets and prior frozen sidecars are unchanged
by this task. The build149 camera doc and handoff were already completed and
remain frozen; no allocator-proof work is pending.

[The source diff](../build/writable-texture/source-review.diff) and
[handoff](../build/writable-texture/handoff.json) record the bounded change and
file identities. Source review verifies that immutable creation and unrelated
backend methods retain their prior implementations. This is source review,
not a compiler or GPU test result. Corrected source remains frozen; main's
build151/build152 validation is recorded separately here. The fixture-only revision is recorded in
[`fixture-build151.diff`](../build/writable-texture/fixture-build151.diff) and
[`fixture-review-build151.json`](../build/writable-texture/fixture-review-build151.json).

Main's exact writable results are preserved in
[`build151-writable-tests.log`](../build/writable-texture/build151-writable-tests.log),
extracted from `build/shadow-texture-lifecycle/build151-tests.log`. They validate
the corrected source frozen above. The full build151 suite did not pass.
Main's subsequent 57/57 build152 run is frozen in
`build/shadow-texture-lifecycle/build152-tests.log`; the two writable test
sections are also preserved in
[`build152-writable-tests.log`](../build/writable-texture/build152-writable-tests.log).
