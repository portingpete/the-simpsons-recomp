# Full-size depth backing qualification

**Qualified:** the default depth surface and a full 1280x720 type-1 private
camera depth surface created through the audited path describe the same depth
storage. Both may retain the same native `DepthTarget`, while keeping distinct
logical surface IDs and original raster ownership. This conclusion comes from
the depth constructors and binding descriptors, independently of color matching.
The separate depth copy, color copy, and two fronts must retain independent
backing. No runtime or test change is made by this sidecar.

Reproduce from the workspace root:

```powershell
python -B build/fullsize-surface-alias/verify.py
```

[Verifier](../build/fullsize-surface-alias/verify.py),
[original listing](../build/fullsize-surface-alias/original.txt),
[full evidence](../build/fullsize-surface-alias/evidence.json), and
[result](../build/fullsize-surface-alias/verification.json).
The verifier checks 1,163 original instruction rows in 13 spans, 82 independent
literal instruction pins, two table pins, and the qualified placement/header
arithmetic. It rechecks the immutable image SHA before returning.
Image: `analysis/simpsons.pe`, 15,466,496 bytes, base `82000000`, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.

## Constructor inputs and cached presentation context

Default initialization `823EDF20` clears the presentation structure at
`P=82E3DCE0`. `823EDF50=83CB0204` and `823EDF54=83AB0208` read selected width
and height from `82E3DF84/88`. They are cached in `P+0/+4` at
`823EDF90=93DF0000` / `823EDF94=93BF0004` and retained in r30/r29 across
the creation calls. The current qualified display profile is1280x720;
this audit does not assume every selected mode has those dimensions.

The constructor stores color format `182801B6` at `P+8=82E3DCE8`
(`823EDF64..6C`) and depth format `1A220197` at `P+28=82E3DD08`
(`823EDF7C..84`). Default color is created with samples0 and placement
`{0,0,0}` at `823EE070=48052629 ->82440698`, then published in `82D0CB00`.
Depth placement is computed from this color format and the retained dimensions
at `823EE08C=4BFFF8A5 ->823ED930`; result is stored at stack+50 by
`823EE090=90610050`. The next call reads **the depth format** at
`823EE098=80BF0028`, uses samples0 (`823EE09C=38C00000`), and stores zero
in the other two placement words at `823EE0A0/0A8`.
`823EE0B0=480525E9 ->82440698` returns the depth header, published at
`82D0CAFC` by `823EE0BC=907BCAFC`.

Private type-1 raster creation follows `823F7070`'s type1 branch. With
`82CD1D88==0`, `823F71DC=419A0024` reaches the private helper call
`823F7208=4BFFEFF1 ->823F61F8`. The helper obtains width/height from
raster+C/+10, writes literal depth format `1A220197` into extension+18
(`823F620C=617C0197`, `823F621C=939F0018`), and computes placement with
**color format `18280186`**, samples0 (`823F6210..18`,
`823F6230=4BFF7701 ->823ED930`). This different placement-input color format
is explicitly checked: both `18280186` and default `182801B6` use four bytes
per sample in `823ED930`, giving exactly720 tiles at1280x720.

`823F6238=90610058` saves that result; `823F6250/6254` write the other two
placement words zero. `823F6240=38C00000` fixes samples0, and
`823F6258=4804A441` calls the same surface constructor with
`(1280,720,1A220197,0,&{720,0,0})`. Its new header is published in the
private extension at `823F6260=907F0000`.

The `82CD1D88!=0` branch instead consumes that flag and directly publishes
the existing default depth header (`823F71F4=816BCAFC`,
`823F71F8=917E0000`). It already borrows the driver identity and must not
be conflated with the independently created private header case.

## Equal descriptors, physical interval and binding behavior

`82440698` allocates a distinct 0x30-byte resource header. Its nonnull placement
branch `82440708=409A00AC` bypasses automatic surface-memory placement.
Header builder `8243FC38` reads all three supplied words at `8243FD0C/10/14`.
For **each** depth view the qualified output is:

| Field | Value / meaning |
| --- | --- |
| extent / format / samples |1280x720 / `1A220197` /0 (one sample) |
| header+18 |`14000500`:1280-pixel surface pitch with the same aligned descriptor pitch |
| header+1C |`000102D0`:depth format selector1, base tile720 |
| header+20 |`00000011`:same depth/HiZ descriptor from explicit second placement word0 |
| header+28 |`1A220197` |
| header+2C |`00384000`:3,686,400 bytes,720 tiles |

Depth's low format index is23. Table pin `8206A056=1120` and
`8243FDD8=556B83DE` select the depth-format bit; base is inserted at
`8243FDE8=917F001C`. The depth auxiliary descriptor is written at
`8243FE0C=92DF0020`. Size uses the original tile result times5120 at
`8243FE74/78`. Thus equality includes the depth-specific branch and auxiliary
descriptor, not just the color constructor's result.

The depth interval is **tiles[720,1440)**, bytes[3,686,400,7,372,800).
It is within2048 tiles /10,485,760 bytes with no wrap. Default/full-size
color uses tiles[0,720), so these color and depth intervals do not overlap.
The read-only reference `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h`
lines224–270 corroborates physical capacity and tile terminology. The original
game's size helper and descriptors establish the equal depth views. Depth tile
layout differs from color; this is not permission to share a color resource
with a depth resource. Auxiliary HiZ storage is not included in the main depth
byte interval, and native HiZ emulation is not proposed.

Camera selection loads private extension+0 (`823EE760=809F0000`), compares
the **header identity** with cached `82D0CF58` at `823EE768..770`, updates
that cache (`823EE778=908BCF58`), and calls `8243D598` at823EE77C.
The SDK setter retains the header at device+30A0 (`8243D5B8=93DF30A0`) and
copies header+1C/+20 into device+2888/+2940 (`8243D5D4..5E0`). It does not
allocate new pixels for a different header.

Mode-zero reset reads default `CAFC` at `823EFFA4=809ECAFC`, compares and
updates the same cache (`823EFFB4=90950000`), and invokes the setter at
823EFFB8;823EFFC4 is a further unconditional setter call. Therefore native
ID changes must continue to trigger the established target/cache behavior
even when the two IDs resolve to one retained `DepthTarget`. Keep private
raster/list lifetimes, destruction rights and driver lifetime separate.

## Copy and front resources are separate allocations

All four use **texture constructor82440578**, with
`(r3..r10)=(W,H,1,1,0,format,0,3)`; none uses the explicit surface-placement
pointer above. Initialization publishes them as follows:

| Call / publication | Role | Format |
| --- | --- | --- |
|823EE104 /823EE108=907C002C |`82D0CF84` depthCopy |`1A220197` |
|823EE12C /823EE130=907C0038 |`82D0CF90` first front |`28280136` |
|823EE154 /823EE158=907C0034 |`82D0CF8C` second front |`28280136` |
|823EE17C /823EE180=907C0030 |`82D0CF88` colorCopy |`182801B6` |

The texture constructor allocates a0x34-byte header through8238E880 at
`824405B0=4BF4E2D1`, computes its texture layout/size via8243F928 at
`82440604=4BFFF325`, then separately allocates backing at
`82440624=4BF4E25D`. Any additional mip allocation occurs at82440650.
It writes the returned backing address into texture header+20 at
`82440680=53AB0026`, `82440688=917F0020`. These are four separate successful
live allocations, not views assigned an EDRAM tile base. Allocation failure
has explicit cleanup and null return. There is no common backing pointer
passed among these four creation calls.

The copy getters load `CF88` at `823ED9BC=806BCF88` and `CF84` at
`823ED9CC=806BCF84`. Camera copy826B08B0 passes its nonnull color destination
in r6 (`826B094C=7FC6F378`) to resolve82455570 at826B0958. Depth uses its
own r6 (`826B0984=7F86E378`), selector4 at826B098C, and resolve at826B0990.
Front presentation loads oldCF90 as resolve destination at823EE85C, swaps
CF90/CF8C at823EE874/878, resolves at823EE87C, and passes the resolved old
front onward at823EE8A0/8A8. These explicit transfers would lose their
snapshot/presentation meaning if copy/front backing were merged with working
surfaces. Matching a color format alone cannot justify aliasing them.

## Integration boundary and limits

Qualify sharing only when the driver and private type1 both match this full
profile:1280x720, format1A220197, samples0, pitch1280, explicit placement
{720,0,0}, original owner and path. Retain a new private logical surface ID,
but point its native depth ownership at the driver's retained default depth.
The corresponding main color repair may share only the independently
qualified full-size type5 and default working color. **Do not include CF84,
CF88, CF90 orCF8C in either alias group.**

Smaller dimensions have different pitches and placement; their possible
partial physical overlaps, reinterpretations, multisampling, and other
formats/lifetimes are unqualified here. This report does not establish native
depth precision, clears/comparisons, resolve format parity, rendered pixels,
stall cause, loading completion or gameplay. Required integration checks
should preserve distinct identities while checking shared content and
lifetime across clear/select/reset/retire operations; implementation and tests
remain with main except for the subsequently assigned shadow-camera test below.
The proof performed no shared build, game run, UI action, runtime/test edit or
original/reference write.

## Assigned shadow-camera test follow-up

Only `tests/test_shadow_camera_lifecycle.cpp` was edited after the proof, under
the follow-up assignment. Full1280x720 now requires private color/depth backing
to equal the driver's defaults, while all private IDs remain unique and
non-addressable. The other tested dimensions retain their independent backing
assertions. All profiles additionally exclude depthCopy and colorCopy/front
backing. After private retirement or viewport-manager reset, full-size weak
owners must still resolve to the driver's same backing; other sizes must
expire. Existing stale-ID/readback rejection, ABI, guest metadata, allocation
reuse, original list and manager checks remain intact. No shared build or test
execution was performed by this sidecar; main must compile and run the revised
test with its runtime alias change.
