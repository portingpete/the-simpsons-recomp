# Original shadow constructor raster/resource contract

Bounded original-byte analysis for the build147 stop in the real `shadows`
registration callback `82707058`. This is allocation, CPU metadata and ownership
evidence. It does not qualify shadow shaders, resolve, parameter upload, camera
begin, binding, drawing, or manager finalization. Production sources are unchanged.

The type-5 color raster requests **10:10:10:2 UNORM**, using packed original
format `182801B6`. It needs its own native color target; it does not borrow the
driver's default color. The existing `TargetFormat::RGB10A2` matches this color
allocation. The associated private depth path uses `1A220197`, **20e4 floating
depth plus 8-bit stencil**. Existing native float32 depth/stencil backing can own
the values subject to its existing guarded rendering/quantization limitations;
`D24_UNORM_S8_UINT` would change the representation.

All numbers below are hexadecimal unless dimensions or byte counts are explicitly
marked decimal. The whole-image SHA256 is
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The flat image is 15,466,496 bytes, based at `82000000`.

## Actual caller and scope

Callback `82707058(name,M)` allocates `6C0` bytes through `8269BF70`, with
options `{2,10,0}`, then calls `827064C0(O,name,M)`. The actual constructor
calls `82704C68(400,400)` twice, at `82706590` and `827065A4`, publishing
the results at O+5B4 and O+5B8. Each dimension is 1024 decimal. This is not
the environment-camera helper `8273BC58`; that function remains a separate
caller with its own resource path.

The complete constructor and deletion evidence is in
[constructor review](../build/shadow-raster/constructor-review.md) and
[original constructor evidence](../build/shadow-raster/constructor-evidence.json).
The constructor also reaches distinct SDK texture allocations after its camera
allocations. Supporting raster type5 alone does not complete this callback.

Each `82704C68` creates a camera, color raster `(400,400,0,5)`, depth raster
`(400,400,20,1)`, frame, and camera extension. It attaches the frame, selects
camera mode2, view values `(25,25)` decimal, near1 and far101 decimal. The
extension owns an eight-byte owner, a `3F4`-byte table allocation and 21
element buffers; after five replacements their combined capacity is `EBD0`
bytes. Camera/frame pool and plugin extents are live registry values, not
fixed allocation sizes inferred from their base structures.

After both cameras, the exact r3..r10 SDK texture arguments are:

- `8270660C` and `82706638`: `(400,400,1,1,2,1A220197,0,3)`, stored at
  O+F0 and O+F4. These are two separate 1024x1024 depth textures, in addition
  to the two private camera depth surfaces.
- `8270666C`: `(20,20,1,1,0,18280086,0,3)`, stored at O+FC. This is the
  32x32 texture. `82440238` locks it, the original code zeros 32 pitch-sized
  rows and writes `FFFFFFFF` to its four one-pixel borders, then `8243E040`
  unlocks it.
- O+F8 is borrowed `BE32[82D0CF84]`, returned by `823ED9C8`. Its acquisition
  performs no retain and the shadows destructor performs no release.

The successful CPU construction also owns copied name storage, three base
arrays (`600`, `1E0`, `2A4` bytes), four `1000`-byte pointer buffers, an
embedded `58`-byte child, a `4000`-byte row buffer, and two container objects
with `1000`/`2000`-byte arrays and eight `C04`-byte node blocks. The complete
allocation requests and interior-pointer rules are in the linked constructor
review. Conditional global string-pool replenishment and dynamic engine/plugin
callbacks are explicitly outside a fixed per-object allocation total.

## Type5 normalization and exact native color format

Preserve the original raster wrapper `82408130(width,height,inputDepth,flags)`
and all plugin constructors/destructors. Platform create `823F7070` receives
`r3=0,r4=R,r5=flags`. Let `X=R+BE32[82E3DC94]`, using the validated live
plugin registry and extent described in
[the camera raster contract](native-camera-rasters.md).

For positive dimensions and flags exactly `5`, the initial writes are:

- R+04 and R+08 = zero; byte R+20 = 5 and R+21 = zero.
- X+00, X+04, X+0C, X+18 = zero; bytes X+08/+09/+0A = zero,
  X+0B = FF. X+10/+14/+1C are untouched.
- `823F70D4` calls `823F6E68(R,5)` with LR `823F70D8`.

The type5 normalization branch is CPU-only for this exact path:
`823F6EB8..ED4` reads presentation format `BE32[82E3DCE8]`, calls
`823F5048` at `823F6EC4` (LR `823F6EC8`), takes its halfword +2 and
sets R+14 to `20`. It does not enter the type0/4 SDK capability tests.
For live presentation format `182801B6`, the helper writes scratch bytes
`01 20 0B 00` at `82D0D000`; the common return writes byte R+23=`0B`.
Preserve this original scratch-memory side effect if retaining the helper.
Do not assume that every presentation format would select the same target.

At `823F7138..717C`, byte R+23 selects an eight-byte table row. The actual
row at `82062B88` is `{182801B6,20010000}`: format `182801B6`, depth byte
`20`, byte +5=`01`, trailing bytes zero. The branch writes X+18=`182801B6`
and X+08=`01`, then calls:

`82440698(width,height,182801B6,0,&{0,0,0})`, LR `823F7180`.

The return is stored at X+00. Null returns platform failure; nonnull continues
to the original list insertion `823F5DA8(R)` at `823F7268`, LR `823F726C`,
then returns Boolean1. The original branch does not initialize R+18,
R+24/+28/+2C/+30 or X+10/+14/+1C; preserve their prior bytes.
At successful return, X+08..0B are `01 00 00 FF`, not the uploaded type4
texture pattern `01 00 01 00`.

The format conclusion is stronger than the field name or 32-bit depth:

- `8243FC4C` extracts surface selector `format & 3F` = 54 decimal.
- `8243FC84..8C` explicitly changes lookup index54 to7.
- BE16 at `8206A036` is `3220`; `8243FDCC..D0` extracts nibble2.
- Original surface descriptor construction therefore selects integer
  `2_10_10_10`, not float10 and not four 16-bit components. The packed format
  itself is also retained in SDK header+28 at `8243FD88`.

This independently corroborates [the driver format contract](native-driver-state.md).
The stored color bits, normalized unsigned interpretation and ZYXW sampled
swizzle are the same original format as the driver's default color surface.
Native sampling/channel adaptation still belongs to the eventual shader/resolve
contract; resource creation alone does not license that use.

## Private depth creation

Type1 normalization sets R+14=`20`, byte R+23=`09`. At `823F71D4`, live
`82CD1D88` chooses whether the raster borrows the existing default depth or
allocates private depth. Normal startup's first camera has consumed the flag;
the shadow path must validate the live zero value rather than reset it or alias
the default depth to avoid allocation.

Do not select depth from the type5 color table using R+23: row9 there contains
`1A2201A1`. The private-depth branch instead supplies its hardcoded `1A220197`.

On the private branch `823F7208 ->823F61F8(R,X)`:

- X+18 becomes `1A220197`.
- It calls CPU arithmetic leaf
  `823ED930(width,height,18280186,0)` at `823F6230`.
- It passes the result as the first word of a local `{placement,0,0}` and calls
  `82440698(width,height,1A220197,0,&placementWords)` at `823F6258`.
- X+00 receives the result. Success returns1 to the outer branch, which inserts
  the original CPU list node. Failure follows the original error path and
  returns0. It does not re-arm `82CD1D88`.

For 1024x1024, the CPU placement calculation, with all operands decimal, is
`ceil(1024/80)*80 * align16(1024) * 4 / 5120 = 832`, or hex `340`.
This number describes original eDRAM placement, not a guest allocation pointer
or native D3D11 address. The original input format to that calculation is
`18280186`; the depth surface still receives `1A220197`.

`8243FC6C..80` recognizes depth selectors22/23. Selector23 uses BE16
`8206A056=1120`, selecting floating depth format1. The existing depth target
uses `R32G8X24_TYPELESS` storage with `D32_FLOAT_S8X24_UINT` DSV and separate
depth/stencil SRVs. That preserves ownership and representable values; general
20e4 write rounding and later shadow rendering remain unqualified.

## Surface helper boundary

`82440698` allocates a `30`-byte SDK resource header, calls `8243FC38`, and
returns that header. At `824406FC..40708` it tests whether the placement-word
pointer is null. Both raster calls pass a **nonnull** pointer, so they skip its
automatic SDK placement allocator branch. This is not proof that the raster
has no real resource or can be represented by a successful dummy pointer.

The native engine bridge should own actual D3D11 color/depth allocations and
publish only checked native resource identities. Do not allocate synthetic SDK
headers, execute `82440698`, or let original SDK readers consume native IDs.
The original CPU raster/list/plugin objects and allocation order remain real.

The constructor's texture calls use separate helper `82440578`, which forwards
the original format to `8243F928` and allocates texture backing. Resource
selector3 maps to dimension1 at `8243F95C..F964/F9D8`, corroborating 2D,
not cube, resources. The exact `18280086` format used for its 32x32 texture
decodes as surface6 (8:8:8:8), endian2, **linear** (tiled bit0), unsigned
normalized components and ZYXW swizzle. It differs from the tiled color target.
Its zero/FFFFFFFF border pixels permit a native RGBA8 upload without channel
ambiguity for this specific payload; this does not license arbitrary texture
conversion or permit the remaining SDK lock/release calls to receive native IDs.

## Exact raster deletion and ownership

The outer `82407DC0` wrapper runs all plugin destructors, then platform callback
`823F62A0`, then frees R through the original allocator. It ignores the
platform callback's Boolean. Preflight unsupported or stale ownership before
allowing that sequence; returning false is insufficient.

Platform destruction first checks eight original texture stages. A matching
raster leads to mixed CPU/SDK unbind `82401940`; retain the current explicit
bound-stage rejection until native unbind is qualified.

For an owned **root type5**, flags bit80 clear:

1. Branch `823F6348..6354` goes to `823F6404`.
2. Call `823F5E10(R)` at `823F6408`, LR `823F640C`, removing its original
   CPU list node.
3. Read X+00; nonzero calls SDK resource release `82441708` at `823F6418`,
   LR `823F641C`.
4. Return1; no clearing of X+00 is performed before the wrapper frees R.

For a **root private type1**, flags bit80 clear:

1. Remove the original list node at `823F6368`, LR `823F636C`.
2. Compare X+00 with current `82D0CAFC`; equality skips resource release.
3. A distinct nonzero private surface reaches the same release at `823F6418`.

The native bridge substitutes release of its own real resource for the SDK
release. `82441708` decrements a header refcount and eventually invokes
`82441050`; it is not a safe helper for a native ID. Keep private ownership
separate from default-role borrowing, and use the exact type5 list-removal LR
`823F640C` rather than the type2 LR `823F6430`.

All non-type2 child rasters skip these release/list operations. This root-raster
evidence does not implement subraster aliasing or lifetime. Zero dimensions and
type5 bit80 no-storage branches also remain separate unsupported profiles.

## Shadows object deletion and the remaining ownership gap

The typed object's vtable `8214E518` slot0 is deleting destructor `82706A18`.
It calls `82705700`, then frees O only when the caller's deleteFlags bit0 is
set. Its ordered body is:

1. For O+5B4, then O+5B8: detach and destroy the frame, then call plain camera
   destroy `823F1D48`. Zero both camera fields after the two sequences.
2. Destroy the O+B8 container, then O+B4 and its node blocks/bucket array.
3. Release and zero O+F0, O+F4, O+FC through `82441708`, in that order.
4. Destroy the embedded child O+5F0; its fresh resource words are zero.
5. Free buffers at O+59C, O+580, O+564, O+1C4, O+A8, in that order.
6. Run base destructor `826B3E50`: free O+30 minus4 (the true array base),
   O+28, O+38, then return the owned name slot to its pool.

There is **no established attached-raster or camera-extension release** in
this selected destructor path. `823F1D48` runs registered camera destructors,
detaches its remaining frame-list link and returns the camera to its pool. It
does not read C+60/+64. The known extension plugin `EA44` registers destructor
`8269E2D0`, which only clears its camera pointer, without freeing the extension
owner/table/buffers. The other two known camera-plugin destructors do not
release these raster attachments or this extension either.

The fuller helper `82714220` explicitly destroys both rasters and frees the
extension, but the shadows destructor does not call it. Do not substitute that
other owner's behavior and label it original shadows teardown. Four live native
raster records and two extension allocations therefore need a separately proven
later owner or an explicit native cleanup policy before claiming complete
shadows teardown or driver restart. The current guard at `82440578` limits
constructor progress; it does not prove unwind or cleanup of preceding cameras.

Read-only review of the bounded flags5/private-depth implementation found no
additional blocker in allocation format, metadata writes, CPU normalizer and
placement calls, per-type list removal, or checked native identity lookup.
Camera begin/binding remains outside this allocation contract.

## Reproduction and limits

Run `python -B build/shadow-raster/verify.py --check` to reproduce the checked
[raster evidence](../build/shadow-raster/evidence.json) and
[original disassembly](../build/shadow-raster/raster-original.txt). Omit `--check`
to regenerate only those two artifacts. The script pins the original image and
the existing PE/disassembly-validation helper, uses whole original `.pdata`
extents or explicitly labelled bounded leaves, and verifies every decoded word
against the original image. It also cross-checks the prior camera report.

Current raster/format verification: 15 spans, 1,248 instruction words, 697 prior
camera instruction cross-checks, 13 call-target checks, 29 instruction-shape
checks and eight rejected mutation cases. No SDK call was executed and no
original binary asset was copied into an artifact.

The independently collected constructor evidence separately checks 65 function
spans and 3,191 instruction words, plus three separately hashed reviewed leaves.
It distinguishes the exact selected deletion path from unproved dynamic callback
and later-owner closure.

Retain the `826B7218` guard until the separate typed-finalizer closure is proved.
The shadow constructor's later depth textures, procedural texture, CPU containers
and complete ordered deletion are part of this resource dependency, not grounds
to skip its callback or declare all25 registration complete prematurely.
