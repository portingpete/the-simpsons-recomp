# Native driver formats and startup state

Bounded incremental findings, 2026-09-09. Original-byte facts below are ready for
native implementation review. No runtime/backend changes or new call-graph
expansion. The existing
[lifecycle contract](K:/SimpsonsNativeCopy/docs/native-driver-contract.md) remains
frozen; this document supplies the narrower format/state contract. The parent's
four native screen shaders are separate from resource and draw-state fidelity.

## Immediate implementation decisions

1. Allocate the startup color roles with real **10:10:10:2 UNORM** backing.
   `182801B6` and `28280136` are not float10. Do not select RGBA16F or 8-bit
   color merely because the engine exposes a 32-bit mode.
2. The depth role `1A220197` is **20e4 floating depth plus 8-bit stencil**.
   `D24_UNORM_S8_UINT` changes the number representation. A native float32
   depth/stencil resource can hold its decoded values, but needs explicit
   conversion/quantization at meaningful write/read boundaries before claiming
   equivalent depth rendering.
3. Initialize a native state owner with the original defaults below, backed by
   real D3D11 state/resource objects. Keep guest pending/applied caches coherent;
   never run the original device mutations through a fabricated `CAF8` pointer.
4. A depth-disabled screen draw can use the real color target while the depth
   conversion path remains explicitly unsupported. Its stencil must also be
   disabled. This does not authorize dropping the depth allocation, losing its
   lifetime, or silently allowing later depth-enabled draws.

## Exact format decode

The original texture descriptor builder `8243F928`, especially
`8243FB48..8243FC1C`, moves these bits from the supplied format word:

```text
surface format  bits 0..5       endian       bits 6..7
tiled           bit 8          sign X/Y/Z/W bits 9..16, two each
number format   bit 17         swizzle X/Y/Z/W bits 18..29, three each
```

This is a decode of an SDK format word, not a DXGI enum. Read-only Rex definitions
identify endian 2 as byte reversal within each 32-bit unit; sign 0 as unsigned;
selectors 0/1/2/3 as X/Y/Z/W and 5 as constant one. The three startup words decode
as follows:

- **`182801B6`:** surface 54 (`2_10_10_10_AS_16_16_16_16`), endian 2, tiled,
  unsigned components, normalized-number selection 0, swizzle **ZYXW**.
  It is 32 stored bits, with 10-bit color and 2-bit alpha. The suffix does not
  allocate four 16-bit components. The surface builder `8243FC84..8C` explicitly
  changes lookup index 54 to 7; BE16 at `8206A036` is `3220`, whose extracted
  render-target nibble is **2**, integer `2_10_10_10`.
- **`28280136`:** same surface 54, tiled and unsigned normalized; endian **0**,
  swizzle **ZYX1**. Same render-target lookup/result 2. Sampled alpha is forced
  to one; the underlying storage still has two high bits. This is the startup
  front/auxiliary color interpretation, not an sRGB view.
- **`1A220197`:** surface **23** (`24_8_FLOAT`), endian 2, tiled, sign fields 0,
  number-format bit 1, swizzle **XYZW**. The surface builder recognizes 22/23 as
  depth at `8243FC6C..80`; BE16 at `8206A056` is `1120`, giving depth format
  **1**, floating 24-bit depth with 8-bit stencil. The number-format bit must
  not cause this floating format to be mapped to a UINT texture.

The contrast is decisive: **`1A2201BF`** has surface 63 and maps through
`8206A0A6 = 1320` to color format **3**, `2_10_10_10_FLOAT`. It is a comparison
fixture here, **not** one of the allocations made by `823EDF20`.

Source definitions and field layouts:
[xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:443),
[fetch descriptor](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:1174).
The independent read-only texture mapping also maps surface 54 to
`R10G10B10A2_UNORM`:
[texture_cache.cpp](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/texture_cache.cpp:426).
These files are evidence only; no GPU command processor is required or imported.

### Native color representation

Use an engine-owned `DXGI_FORMAT_R10G10B10A2_UNORM` texture/RTV/SRV for logical
RGBA color, or its typeless storage family when genuinely needed for views.
Preserve the original format, alpha policy and owner role in host metadata.
The DXGI format supplies the needed component widths; `R11G11B10_FLOAT` lacks
alpha and has a different exponent/mantissa layout.
[Microsoft format definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format).

Byte order and channel selection are distinct. If importing/exporting raw
original storage, apply the documented byte swap and ZYXW/ZYX1 interpretation.
If native engine producers already write logical RGBA, do not apply another
red/blue swap at every sample. Define that convention once. For `28280136`, the
native shader/view service must deliver alpha one; an ordinary D3D11 UNORM SRV
alone does not force it. Keeping alpha one in every writer is valid only when
all writers are proven to preserve that invariant.

These words have no gamma sign fields. Do not attach an sRGB transfer curve on
their behalf. Final display transfer and the engine's gamma service remain
separate contracts. Likewise, native blending precision is not proven identical
merely by matching storage widths. The screen helper changes expanded blend
precision for format 2 to format 10; see Gödel's
[screen material contract](K:/SimpsonsNativeCopy/docs/screen-material.md:23).
That switch never turns this integer target into float10.

Actual startup roles, all width/height from `82E3DF84/88`:

- `82D0CB00`: default color surface, `182801B6`, original multisample selector 0
  (one sample).
- `82D0CAFC`: default depth surface, `1A220197`, also one sample.
- `82D0CF84`: separate depth-format texture, `1A220197`.
- `82D0CF90` and `82D0CF8C`: two distinct textures, `28280136`.
- `82D0CF88`: separate color texture, `182801B6`.

Texture calls request one level; preserve separate ownership and backing for
each role. No clear of their contents is established by allocation itself.
The guest fields are not host COM pointers. Preserve the lifecycle contract's
host-side resource ownership and validated native service boundaries.

### Float10 and floating depth: precise limits of native substitutes

Actual float10 **7e3** uses seven fraction bits, three exponent bits, bias 3,
positive finite values, range 0..31.875; its smallest positive value is 2^-9.
Alpha remains two-bit UNORM. All 1,024 RGB codes decode exactly into binary16,
but an RGBA16F render target alone allows extra values and has different alpha
precision. Faithful use would require original-format clamping, quantization
and blend behavior; quantizing only at final presentation is not generally
equivalent. This is useful for future format-63 work, not a requirement to
replace the present integer startup color with float16.
[Microsoft's 7e3 conversion implementation](https://raw.githubusercontent.com/microsoft/DirectXTex/main/DirectXTex/DirectXTexConvert.cpp)
and the local
[reference conversion](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/spirv_translator_rb.cpp:26)
agree on this representation. An Xbox-specific format constant in a content
tool is not evidence of desktop D3D11 format support.

For depth **20e4**, let e be the high four bits of its 24-bit number and m the
low twenty. The reference decode is `m * 2^-34` when e=0; otherwise
`(1 + m/2^20) * 2^(e-15)`. Thus `000001` is 2^-34, `100000` is 2^-14,
`F00000` is 1, and `FFFFFF` is 2 - 2^-20. Every decoded value fits float32
exactly. This is not uniformly spaced 24-bit normalized depth.
[Local conversion evidence](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/spirv_translator_rb.cpp:220).

Viable native allocation: `R32G8X24_TYPELESS` storage with
`D32_FLOAT_S8X24_UINT` DSV and appropriate depth/stencil SRVs where required.
For the separate resolved depth texture, real R32_FLOAT depth backing plus
retained stencil backing, or an explicitly packed integer resource with native
sampling conversion, can preserve the data roles. A depth-only R32_FLOAT copy
must not silently discard stencil if a reader/copy contract needs it. Rex's
depth texture path intentionally does not expose stencil; that limitation is
not a fidelity argument for this port.

**Remaining depth uncertainty:** the format and exact decoded values are
established; the original raster depth rounding choice is not. The local
reference has both truncating and nearest-even 20e4 paths, and a half-range
mapping to preserve values above one using normalized host depth interfaces.
Plain D32 writes preserve too much precision; D24_UNORM changes the spacing.
Before enabling native depth writes, specify and test quantization, viewport
depth transform, clears, comparison, stencil and resolve/readback together.
Depth-disabled loading quads do not require choosing an unverified rounding
mode. Explicit `SV_Depth` conversion is a possible native shader implementation,
with MSAA/early-depth consequences; it is not a console command-stream renderer.
[Reference depth modes](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/dxbc_translator_om.cpp:1619),
[Microsoft depth-output semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics).

## Defaults from `824008E0` inherited by the quad

The following is the **startup baseline**, not proof that no intervening engine
call changes state before a quad. The quad must consume the current native state
owner. Gödel's shader tests establish shader arithmetic, not inherited state.

Original scalar cache IDs below are hexadecimal and are not desktop D3DRS enum
values. Their meanings follow the original setter table `82CD28B8` and the
corresponding original setter field writes, cross-checked against
[registers.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/registers.h:779).

- Depth: `28=1` enable; `30=1` writes; **`2C=6` GREATER_EQUAL**. Native comparison
  must not default to LESS. SDK depth enable is effective only with a bound
  depth surface, while retaining the requested enable separately.
- Cull: **`38=2`** sets cull-back=1, cull-front=0, face=0 (CCW front).
  Native `CullMode=BACK`, `FrontCounterClockwise=TRUE` matches this orientation
  when the native coordinate/viewport mapping preserves winding.
- Blend: `3C=0` disabled; `48=6` source alpha; `4C=7` one minus source alpha.
  Keep those factors even while disabled. Earlier SDK defaults specify ADD and
  separate-alpha disabled (`50=0`, `40=0`). Do not enable blending merely because
  nontrivial factors exist.
- Alpha test: `60=0` disabled, `64=0` reference, `68=4` GREATER. D3D11 requires
  shader-side alpha-test behavior when enabled, not an invented blend-state bit.
- Stencil: `6C=0` disabled; `74=78=7C=0` KEEP; `80=7` ALWAYS; `84=0` reference;
  `88=8C=FFFFFFFF`, whose effective masks are the low byte, FF.
- Earlier SDK defaults also provide color-write masks F for all four target
  slots (`D4/D8/DC/E0`), zero depth/slope bias (`CC/D0`), filled polygon mode
  (`34=0`), and expanded blend precision disabled (`134/138/13C/140=0`).
  These are not all rewritten by `824008E0`.

The SDK initializer `82466800` walks **101 records of 12 bytes** at
`82CD28B8`: `{getter,setter,default}`, installing each setter at
`device+40+id`, with ids 0,4,...,190, then applying its default. It also walks
20 sampler records at `82CD2D78`. The new JSON records every raw row, plus the
30 engine scalar override calls and their exact PCs. Not every raw SDK row has
been assigned a native semantic name here; do not treat an unnamed live state
as successful native implementation.

### Eight startup samplers

The loop is `82400ABC..82400C40`, stages 0..7. Original capability word
`8206AA70 = 07030700` has bit 200 set, choosing the linear branch.

- Clear the texture binding for each stage (`82400AE0 -> 824408E0`, texture=0).
- Minification and magnification both request **1 = LINEAR** through
  `8243BA40/8243BBD0`. The original SDK anisotropy default is 1 (maximum ratio
  one), so this startup request does not establish anisotropic filtering.
- Mip filtering is **2 = base-map only**. This is the inline write at
  `82400B94..BB4`; it writes sampler shadow `+60` to 2 and device fetch-word
  bits 23..24 to 2. Replacing only called SDK functions misses this write.
- Sampler IDs `0=0`, `4=0`: U/V repeat. W repeat comes from SDK default `8=0`.
- `C=0` selects the SDK's zero border-color selector; repeat addressing makes
  the border inactive. Exact border alpha is not established by this source.
- `24=1` retains maximum anisotropy one. SDK defaults supply LOD bias `1C=0`,
  min mip level `20=0`, max mip level `34=13`. Base-map-only filtering dominates
  the startup mip choice; mip-enabled behavior needs its own contract.

For these one-level 2D startup resources, a real native linear min/mag sampler,
repeat U/V/W, anisotropy one, and level zero selection represents the effective
baseline. Preserve the raw base-map-only policy rather than turning it into
trilinear sampling when a later texture has multiple levels. Unknown later
sampler overrides must be diagnosed or implemented explicitly.

The engine's eight 24-byte logical sampler records at `82D0E3F8 + 24*s`
receive six BE words `{0,1,1,2,0,1}`. The separate SDK-facing sampler cache at
`82D0D170` uses **320 bytes per stage**, because its offset-style IDs are used
as indices in `base + 4*(80*stage+id)`. Do not confuse these two strides.

### What the screen quad changes

Per [screen-material.md](K:/SimpsonsNativeCopy/docs/screen-material.md:35), the
quad temporarily requests depth off and culling off, alpha test GREATER with
reference float32(1/255), its selected RGB/alpha blend equations, and linear
min/mag plus U/V repeat. It subsequently sets replacement blending, alpha test
off and depth on. It does **not** restore a complete state snapshot.

Therefore its startup-inherited stencil is disabled, color mask is RGBA, mip
policy is base-only, and the retained depth comparison/write policy remains
GREATER_EQUAL/write-enabled even while the quad disables depth testing. Preserve
these as state, not per-quad guesses. Target, viewport, texture/sign/alpha policy
and any intervening mutations still come from the engine resource/state owner.

## Preserve the CPU state caches when replacing device work

- `823FFE78` fills the applied scalar array `82E3D580` (425 words) with FFFFFFFF,
  except IDs 88,8C,198,1A0 which start at zero. It copies these values into
  `82D0F3B0 + 8*id` and clears each adjacent dirty word. Scalar dirty queue is
  `82D0ED08`, count `82D10114`.
- `82400170(id,value)` updates the pending value only on change and appends the
  ID only if its dirty word was clear. Preserve overwrite-without-duplicate and
  comparison semantics. `824001C8` reads the pending value.
- `824001E0(stage,id,value)` does the same for texture-stage state at
  `82D0DB70 + 8*(33*stage+id)`. Applied stage words are at `82E3D160`, queue
  pairs at `82D0E4C8`, count `82D10118`. Getter is `82400258`.
- `82400040` commits scalar entries, clears dirty flags, compares pending with
  applied, then dispatches only scalar IDs **below 194** through the SDK. Higher
  IDs are real engine shader/pipeline shadow state, still copied to applied;
  they are not permission to discard those values.
- Its stage commit calls `823F47F0 -> 8240EBB0 -> 8240E960`. These are **CPU
  pipeline-stage record updates**, not GPU submission: preserve them. Stage 0
  receives `(id1=3,id3=0,id4=3,id6=0)`; stages 1..7 receive `(id1=1,id4=1)`.
  `8240E960` validates operations/arguments and stores the 40-byte records at
  `82D501E0 + 40*stage`. Other defaults come from `8240EBD0`/`8240EC28`.
- `82400278(stage,id,value)` compares/updates the sampler cache, then dispatches
  through `device+1D4+id`. Port this service. Port all of `824008E0`'s mixed
  initialization, including its inline sampler writes; do not call it with a
  dummy device and expect a commit hook to make it safe.

The JSON also lists the direct CPU words written by startup. Initialize them,
the pending/applied arrays, and native effective state coherently. A host
submission failure must not leave a cache claiming the state was applied; use
transactional native initialization and a clear failure path.

## Boot023 cross-check and bounded readiness

Read-only `build/boot-023.log:27..63` observes E=`E2C99D40`, allocated size `5B4`,
lifecycle 2 and **35 registered plugins**. All 34 registration records from the
frozen static report match live ID, size, ctor and dtor. The additional live
record is **050F**, zero-sized, ctor `823E0170`, dtor `823E0188`; live 0122 was
already present in the static attachment records. The core 14 appear in the
same order; `14C + sum(plugin sizes) = 5B4`. Zero-sized entries remain meaningful.
This snapshot precedes request-2 completion and does not prove constructors ran.

Selected mode is 0, 1280x720, format **28280186**, flags 401. Original start
nevertheless supplies **182801B6** explicitly for its color surface and
**28280136** for its front/auxiliary roles. Native allocation must not substitute
the selected-mode format for those explicit arguments. The log still terminates
at `VdInitializeEngines`; no native driver or original pixels are proved by it.

Next parent work can allocate the six real resource roles, build the verified
native state baseline, preserve CPU caches, and wire native engine services.
Keep unsupported depth writes/format precision transitions guarded until their
semantics are implemented. Plugin construction and shader-resource ownership
requirements remain in the frozen lifecycle contract; this document does not
relax them or authorize skipping constructors.

Check native format capabilities and every creation result on the actual
device; do not infer support from the enum's existence.
[Microsoft CheckFormatSupport](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkformatsupport).

## Reproduction and limitations

```powershell
python -B tools/analyze_native_driver_state.py --self-test
python -B tools/analyze_native_driver_state.py --report analysis/native-driver-state.json
```

The new tool imports the frozen driver analyzer read-only, pins the original
15,466,496-byte flat image and its SHA256, and compares disassembly words against
the original bytes. Current result: **25 functions plus two explicitly labeled
code windows, 5,181 checked instruction words; 19 format/numeric fixture checks**.
It records all SDK default rows and reviewed engine initialization call sites.
The manually reviewed state ledger is not an automatic proof of every setter's
semantics. Numerical fixtures check representation, not hardware rounding.

Only the new tool/report and this document are written. No backend test, parent
build, game execution, original asset mutation, reference-project edit or
existing lifecycle/Gödel report edit was performed for this investigation.
