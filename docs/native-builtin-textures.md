# Native built-in images and explicit mip levels

Build163 passes73/73 suites in111.26 seconds. Actual muted boot106 completes
the original white, black and gray texture requests and the original parent's
three borrowed aliases. It then reaches an unsupported raster request with
flags00000580. No original screen, menu, world, gameplay, progression or save/load
has been verified.

## Original image work retained

The bounded engine replacement at82B84838 accepts only the three verified calls
from826FF0F8, with exact source identity, length, output global and native context.
It calls the original CPU image constructor82BBD720, decoder82BC2FF8, requirements
helper82B7F950 and destructor82BBE1F8. Build162's original nonlocal save/restore
correction remains in use for the decoder's JPEG rejection before TGA detection.

Original CPU filter82BC8EB8 consumes two54-byte descriptors: pixel pointer,
format, row/slice pitch, full/active boxes, dither/error-diffusion/gamma flags,
color key and palette. Its original constructor82BC52E0 and destructor82BC61D0
retain CPU helper ownership. The core overwrites destination+40/+44/+48 and
source+48; source+40/+44 are incidental in the original uploader. The fixture
checks zero, CD, FF and5A source flag poisons, and tight or padded row strides.
Those variations produce the same qualified built-in values.

The first level uses the original00080004 filter. Smaller levels use original
box filtering5, consuming the preceding level as82B80D78 does for this mode.
Every level is produced by original AOT CPU instructions. Native graphics
receives their captured bytes without automatic mip generation.

| Original source | Primary global | Extent / levels | Original format | Original BE32 mip values | Native storage |
|---|---|---|---|---|---|
|82CED9A8,102C bytes|82D63004|32x32 /6|18280086|FFFFFFFF at every level|RGBA8|
|82CEECF0,102C bytes|82D6300C|32x32 /6|18280086|FF000000 at every level|RGBA8|
|82CEE9D8,312 bytes|82D63008|16x16 /5|28280086|FF808080 at level0;00808080 at levels1..4|BGRX8|

The original CPU header builder8243F928 independently establishes surface
format6, endian mode2, linear storage and swizzlesZYXW/ZYX1. Read-only RexGlue
xenos.h definitions identify swizzle selectors; its hash is pinned in
build/builtin-textures/evidence163.json. No reference GPU implementation ships.
Gray's fourth byte is unused. Preserving its zero lower-level X bytes while
sampling alpha as one is required; treating them as ordinary RGBA alpha would
make the smaller gray levels transparent.

## Native storage and ownership

NativeBackend creates immutable D3D11 resources with explicit level data and a
view covering exactly those levels. The new BGRX8 format uses
DXGI_FORMAT_B8G8R8X8_UNORM. Microsoft documents its unused fourth byte in the
[DXGI format definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format).
The hardware and WARP sampling tests independently verify alpha-one behavior.
Explicit initial mip data follows the
[D3D11 texture creation contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d).

The new API accepts RGBA8/BGRX8 full or partial chains, validates every tight
level size, and captures caller bytes before returning. Readback selects one
subresource with matching staging dimensions. Texture submission now validates
the resource, declared level count and associated view before binding. Existing
compressed texture support remains its separately qualified single-level path.

EngineBuiltinTextures owns three native texture records with unique unmapped
identities. Original CPU allocations, decoder pixels and filter helpers retire
after native creation captures all bytes. Primary globals publish only after
successful decoding, filtering, native creation and CPU cleanup. Wrong callers,
sources, output locations, duplicate bundles, changed globals and foreign-thread
access fail explicitly. Unknown consumer release remains rejected.

The original parent's stores publish82D63010/18 as aliases of black and82D63014
as an alias of white. There are no extra retains. A verification hook before
the parent's original epilogue826FF23C checks the completed bundle.
Original global cleanup826FF248 is a no-op and leaves all globals/resources
alive. Native backing remains owned until terminal runtime cleanup. A normal
paired release and future consumer bindings have not been qualified; driver
retirement rejects outstanding image ownership.

The reflection lifecycle fixture now uses a synchronous diagnostic observer
before image loading to isolate its existing four reflection lifetimes. The
observer can throw a marker; returning executes the production operation.
The new image lifecycle fixture separately runs the complete original parent,
with all three real image requests and final alias stores.

## Verification and next request

- Original filtering:188,540 checks,504 direct AOT calls,136 mip-level cases;
  all texels, four source flag poisons, tight/padded pitches, source immutability,
  metadata/pixel canaries, original helper cleanup and header swizzles.
- Native explicit levels:18,357 checks and47 sampled levels each on WARP and
  hardware; asymmetric per-channel data, full/partial and non-power-of-two
  chains, exact raw readback, alpha-one sampling, caller-storage lifetime,
  malformed metadata and cross-device/thread rejection.
- Original image lifecycle:3,161 checks; full parent, three owned textures,
  all17 native mip readbacks, borrowed aliases, invalid request/publication
  rejection, original no-op cleanup and terminal backing expiration.
- Existing original decoder10,365 checks, nonlocal699 checks and reflection
  lifecycle288 checks continue passing. All178 hook ranges match original bytes;
  AOT has311 outputs and zero semantic diagnostics.

Actual boot106 logs the completed parent, then original4MB pool allocation
and an explicit flags00000580 raster failure. Static tracing identifies a
subsequent crossfade constructor82702028: it passes dimensions from82E3DCE0/4,
format18280186 and flags500 to823F7278 at827020A8. That helper adds80 before
the original raster create82408130. Strings identify
iMsgCrossfadeReadyToCommit and iMsgCrossfadeCommitComplete. The exact live
caller/dimensions still need qualification before extending this raster profile.

The intervening declaration allocator824458E0 constructs CPU metadata through
824457F0; it does not submit a GPU command. Its consumer/lifetime remains
unqualified. No additional guard was added merely to prevent CPU metadata work.
Next qualify the crossfade raster and original texture-factory/lifetime chain,
including18280186 storage and its later consumers. Do not admit580 as an existing
camera profile without that evidence.

Frozen artifacts: build/builtin-textures/build163-summary.json,
build163-tests.log, aot-manifest163.json, evidence163.json, original-chain163.txt,
and build/boot-106.log. Earlier checkpoints retain historical162 evidence.
