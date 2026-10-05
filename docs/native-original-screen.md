# Original loading-screen draw

The native bridge now implements the real `82756480` draw, retaining the original
AOT prefix, alpha early-out, coordinate calculation and register-restoring epilogue.
It consumes original texture objects, XY/UV endpoints, float4 color and selectors.
No replacement artwork, geometry, fade timing or loading-success flag is supplied.

The native arithmetic model is explicit and provisional: recovered float shader
calculations and blend equations, followed by nearest-even packing to RGB10A2.
Console source/factor/intermediate rounding and final ties remain unverified.
Earlier reports' recommendation to keep the entire draw blocked is superseded by
this implementation; those reports' numerical counterexamples and limits still
apply. Progress through native rendering does not certify console pixel parity.

## Resource ownership and state

The two screen declarations now have native immutable owners, created at the
original `827521C4/21D8` calls and released at `827522EC/22FC`. Original following
stores publish and clear the fields. Their exact source arrays are retained.

The four screen shaders use a different creation route from the earlier native
material service. Original `82750CD0` traverses linked lists, allocates shared
header/code storage, copies the original headers and payloads, and constructs CPU
shader objects. This code remains AOT. The bridge validates the selected source,
stage, shared CPU object, copied header and code, then owns a pinned immutable
record and its native compiled counterpart. CPU shader objects are not passed to
a console GPU service. Binding reset accepts these validated screen resources.

Only the six documented cache words change: VS, PS, declaration and stream-zero
buffer/offset/stride. Stream `+C` and other streams survive. Direct scalar/sampler
changes affect the effective state owner; application and RenderWare caches are
untouched. The flat branch retains an earlier texture/sampler. Both branches leave
depth requested on, alpha test and blending off, no culling, and the selected
shader/declaration/color. Original texture reference counts do not change.

The native pass temporarily uses a full1280x720 viewport with depth0..1, while
depth/stencil tests are disabled. The prior native viewport and original target
attachments are restored. Original logical reversed-depth camera state survives.
One-level Texture2D storage makes inherited W addressing, volume filtering and mip
selection inactive; their original requests remain retained. U/V repeat and linear
min/mag filtering are applied. Other unsupported profiles reject before submission.

## Native color conversion

An actual hardware test exposed a difference in ordinary UNORM output conversion:
the low-alpha example produced RGB code5 on the GPU and6 under WARP. Source alpha
was no longer reduced to two bits, but final native conversion still differed.

The final pass therefore writes explicit integer channel codes to a temporary
`R10G10B10A2_UINT` target. The original packed target supplies the destination and
is copied into the temporary before drawing, preserving all uncovered pixels.
After drawing, the packed result is copied back. D3D11 permits bit-preserving
copies between compatible members of one format family; this is an engine-level
native pass, without console command decoding. See Microsoft's
[CopyResource contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource).

Focused WARP and hardware fixtures now agree on the explicit arithmetic model.
This does not resolve console rounding, original1/16 subpixel raster quantization,
BC3 filtering precision, display gamma or presentation timing. Fractional original
rectangles use the native rasterizer's precision; no guessed UV correction is added.

## Inspection

`--capture-frames <directory>` saves the first32 distinct draw-bearing presented
frames as unmodified little-endian RGB10A2 readbacks plus metadata. It is disabled
by default and adds synchronous readback overhead when enabled. The raw file is
authoritative. `python -B tools/render_frame_capture.py <directory>` produces RGB16
PNG previews without brightness/gamma adjustment or overlays; alpha is omitted
because native display presentation ignores it. These are renderer readbacks,
not desktop screenshots or evidence of display scanout/color calibration.

Original evidence and actual build/run results live in `build/original-screen`.
The full game remains unfinished. The subsequent null UI-object read was resolved
by preserving the original frontend file-open mode; see `native-frontend-loading.md`.
Actual startup now reaches a separate guarded immediate draw. No fake object or
successful menu result is substituted.
