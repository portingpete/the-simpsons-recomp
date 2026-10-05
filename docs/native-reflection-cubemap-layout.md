# Original reflection-cubemap CPU layout

Actual muted boot099 completes effect setup and reaches constructor8273C2B8
at caller826FF198. This is the zero-flag N=16 path. The constructor remains
guarded before changing its object or reading a console SDK device. Original
N=256 construction is the other branch, not the observed startup choice.

The isolated layout regression executes original CPU header builder8243F928
and layout helper8243F268. It passes198,412 checks across88 AOT calls: six
cube/full2D/half2D profiles, all cube faces, and four initial header byte patterns.
It validates allocation extents, pitch, face spacing, level geometry, caller
stack/LR and the52-byte header write boundary. The original header still contains
unspecified padding bits; the fixture does not construct a production SDK object.
No SDK device, synchronization call or GPU resource is used by this test.

| Cube profile | Resource | Size | Faces | Original row pitch | Bytes per face | Primary bytes |
|---|---|---:|---:|---:|---:|---:|
|16|Cube|16×16|6|128|4,096|24,576|
|16|Full2D|16×16|1|128|4,096|4,096|
|16|Half2D|8×8|1|128|4,096|4,096|
|256|Cube|256×256|6|1,024|262,144|1,572,864|
|256|Full2D|256×256|1|1,024|262,144|262,144|
|256|Half2D|128×128|1|512|65,536|65,536|

All profiles use one level and no secondary allocation. Packed format282801B6
has surface54, endian2, tiled layout and ZYX1 sampling. The established format
family stores32 bits as10:10:10:2 UNORM. Native raw RGB10A2 storage does not by
itself implement expanded filtering precision or the forced sampled alpha.

The constructor visits face selectors **0,1,4,5,2,3**, from821503A0 stride40.
For each face it clears exactlyN*N*4 contiguous bytes at the returned lock
pointer. It does not clear the full padded allocation or iterate by pitch.
With the checked layout and the two retained reference address formulations:

- N=16 clears1,024 bytes and initializes only128 of256 logical texels: rows0..7.
  Rows8..15 retain unspecified backing contents.
- N=256 clears262,144 bytes and initializes all65,536 logical texels.
- The two companion2D textures are allocated but not initialized by this body.

The address formulations come from the pinned local RexGlue texture utility and
conversion sources. They agree at every tested4-byte coordinate and produce
unique, aligned addresses within each original face allocation. Original CPU
execution establishes the header and resource-relative geometry; these
reference equations establish the bounded logical projection. This is not an
original-hardware pixel capture. Preserve the distinction when adding native
partial uploads; do not report the uninitialized part as original black pixels.

The original paired destructor8273C000 releases its retained device first, then
O+8/+C/+10, cameraO+60, and CPU container allocationO+68. No direct call to this
destructor was found in the complete text scan. Normal top-level826FF248 is a
single return. An indirect/global cleanup route has not been established, and
camera teardown may retain attached raster ownership as in the shadow case.

Native six-face RGB10A2 storage and partial writes now pass618 checks each on
WARP and hardware, including untouched rows/faces. See
`native-cube-texture-backend.md`. The subsequent native context/constructor
lock/clear/unlock and paired cleanup work is described below. The camera's size16/256
type5/type1 allocation profile passes514 checks through the original helper;
see `native-reflection-camera-profile.md`. The constructor/paired resource
integration now passes284 checks and actual boot103 reaches the following image
loader guard. See `native-reflection-texture-lifecycle.md`; the direct camera
attachment and global cleanup gaps remain explicitly observed.

Evidence: `build/reflection-cubemap/evidence.json` pins20 spans/1,472 words and
23 instruction mutations. `layout-evidence.json` records the88 calls and exact
reference hashes. Reproduce with `python -B build/reflection-cubemap/verify.py`,
`python -B build/reflection-cubemap/layout-evidence.py`, and CTest
`OriginalCubeCpuLayout`. The earlier exploratory output is retained in
`build/reflection-cubemap/probe158.log`; its target was replaced by the regression.
