# Original movie plane allocation, CPU access, and lifetime

This is implementation evidence for a bounded native storage replacement. The
original movie descriptor allocator, decoder/AOT, public raster allocation,
public lock/unlock wrappers, frame queues, and paired destruction can remain.
The SDK-facing callbacks require native CPU storage and ownership equivalents.
This report does not establish successful movie playback, shader equivalence,
movie audio, or the correctness of a native implementation.

The immutable mapped image is `analysis/simpsons.pe`, base `82000000`, size
15,466,496 bytes, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The checked listings and machine-readable word pins are
[movie-plane-original183.txt](../build/im2d-upload/movie-plane-original183.txt)
and [movie-plane-original183.json](../build/im2d-upload/movie-plane-original183.json).
Addresses and offsets below are hexadecimal unless explicitly stated otherwise.
Guest metadata words are big-endian; sample bytes are individual 8-bit values.

## Reached request and scope

Actual muted boot146, reported as full build182, stopped at shared helper
`823F7278` with LR `8282E9B8`, width 1280, height 720, format `28000002`, flags
`400`, provider `r23=E1ADC040`, output descriptor `r20=FF614918`, and plane index
`r30=0`. The archived `build/boot-146.log` contains this request and the stack
through `82375868`, `82373000`, and `8282DD94`. Boot145 had already established
that the earlier crossfade owner/raster remained `E1AB0988/E1AB3F70`: its
duplicate-crossfade error was a shared-helper classification error.

The path is movie video: `82372D24..82372D68` checks `MVhd`, reads and byte-swaps
its LE16 width/height at +C/+E, and stores session +AC/+B0. These movie dimensions
feed the plane allocator; they do not come from screen dimensions. The decoder
and embedded `vp6_y_cr_cb_Xenon_PS` path at `82152B93` strongly support VP6.
The existing [asset report](assets.md) identifies EA-chunked `vp60` movies,
including 1280x720 and 640x480 dimensions. Exact Cb/Cr naming and conversion
matrix/range remain unqualified here.

## Allocation and CPU descriptor

`8282E940` is a virtual buffer-allocation method, not an object constructor
returning its input. Provider vtable `8215D4F0` has +4=`8282E940`,
+8=`8282E910` (store allocator at provider+4), and +10=`8282EA50`.
Input registers are r3=provider, r4=output descriptor O, r5=W, r6=H,
r7=format selector F, r8=plane count N. The method keeps these in
r23/r20/r25/r24/r19 and stores N at O+38.

The provider and presenter are **different objects**. Provider constructor
`8282ED68` loads `8215D4F0` and stores it at provider+0 with
`8282ED70=91630000`; startup calls it at `8282DCF8=48001071` after requesting
an 8-byte object. Startup then calls setter `82372058` at `8282DD18`.
That setter stores the provider at session+58 (`82372068=90830058`) and,
when the manager's +48 state is zero, at `[session+10C]+10
(`82372078=908B0010`). It makes no change when session+150 is unsigned >=2.
Manager setup calls provider vtable+8 at `82375724`; target `8282E910`
stores the allocator at provider+4 (`90830004`). Allocation then loads this
provider from manager+10 at `82375840`, loads its vtable+4 at `8237585C`,
and calls `8282E940` at `82375864=4E800421`.

Presenter constructor `8282EB00` instead installs vtable `8215D4B0`
(`8282EB24=396BD4B0`, `8282EB40=917F0000`). In presenter method `8282EC58`,
`8282EC68=7C7F1B78` saves that presenter in r31; current descriptor is its
+14 field. Thus provider r23 during allocation must not be equated with
presenter r31 during frame commit. Main's boot147 correction confirms this
distinction live; the constructor/setter/call-chain evidence here is original
image evidence and does not depend on boot147 completing.

`823738C0` initializes the 54-byte descriptor, including magic `37047734` at
+0 and invalid selector 4 at +30. `823756D0` contains equivalent inlined
initialization. Neither initialization zeroes the pitch slots +1C/+20/+24;
the successful plane method supplies them. Do not impose a whole-object-zero
precondition or assert a meaningful O return value from `8282E940`.

For each i=0,1,2, `8282E940` unconditionally creates a distinct raster:

| Output field | Value after successful creation |
| --- | --- |
| O+04+4*i | CPU pointer D[i] returned by public raster lock |
| O+10+4*i | Logical byte count B[i] from `82373950` |
| O+1C+4*i | Byte pitch P[i], loaded from raster extension +10 |
| O+28 / O+2C | Full W / H |
| O+30 | F |
| O+38 | N |
| O+44+4*i | Pointer to a separately allocated C-byte RasterContext |

Each RasterContext is `{raster, D[i], P[i]}` at +0/+4/+8. Allocation dispatches
through `[provider+4]` vtable +4 with size C, label `RasterContext` at
`8215D49C`, r6=0, alignment argument r7=4, r8=0. These contexts remain original
allocator-owned objects, separate from native pixel backing.

At `8282E994..8282E9B0`, the shift is zero for plane0 and one for planes1/2.
`8282E9B4=4BBC88C5` calls `823F7278(W>>shift,H>>shift,28000002,400)`;
`8282E9C4=4BBD9845` calls `82408208(raster,0,5)`.
`8282E9E8=834A0010` loads the pitch from X+10, where
`X=raster+BE32[82E3DC94]`. Publication words are
`8282EA1C=937FFFF4` (data), `8282EA24=907F0034` (context),
`8282EA28=935F000C` (pitch), and `8282EA2C=917F0000` (logical count).

### Selector, plane count, and number of frame buffers

Startup `8282DBD8` supplies 4 to codec constructor `82379CE0`. That constructor
clears codec+1C and accepts only input 0 or 1; input4 leaves selector0.
Getter `82379DE0=8063001C` returns it. Thus `82375794=409A000C` skips the
selector1-only +60 additions to W/H at `82375798/9C`. The reached selector0
does not allocate a 96-pixel border.

Renderer getter `8282E808` returns N=3 for selectors0/1: table words
`8282E82C/30=8282E83C`, and `8282E83C=38600003`. It returns 1 for selectors2/3.
The hard-coded three-allocation loop in `8282E940` is qualified here only for
F=0,N=3, even positive movie dimensions. Its behavior is not generalized to
the other selector/count combinations.

`8282DD7C=38800003` and `82372C64=90830108` set session+108 to three frame
buffers. `82372FF8=91610054` passes that count on the stack;
`823757A0=83810124` retrieves it in the D0-byte callee frame. The allocation
loop ends with `82375890=4082FF1C`. Therefore this startup requests three
descriptors times three planes: nine simultaneously live video rasters, in
addition to the unrelated crossfade raster.

### Logical sizes and initial raster fields

For F<2,N!=1, `82373950` computes B={W*H,(W*H)>>2,(W*H)>>2}.
`823739C8=556AF0BE` performs the quarter-size shift; `823739CC/D0/D4`
store all three counts. These are logical sample counts, not pitched spans.
For odd dimensions the independent half-dimension shifts and area/4 formula
need not agree; that profile is outside this qualification.

| Movie dimensions (decimal) | Plane dimensions (decimal) | B[0], B[1], B[2] (hex bytes) |
| --- | --- | --- |
| 1280x720, reached boot146 | 1280x720, 640x360, 640x360 | E1000, 38400, 38400 |
| 640x480, asset-derived fixture | 640x480, 320x240, 320x240 | 4B000, 12C00, 12C00 |

Shared helper `823F7278` passes flags480 to public allocator `82408130`.
The latter initializes R+22=0 and root/offset fields. Original `823F7070`
initializes X+0/+4/+C/+18 to zero, X+8/+9/+A to zero, X+B to FF, and
R+4/+8 to zero. It retains normalizer `823F6E68`: table row
`82062B50=28000102`, `82062B54=08000000` establishes depth8 and public format4.
The no-allocation flag80 suppresses the internal storage/list path.
The shared helper's later factory produces the requested `28000002` resource,
publishes X+0, clears flag80 and the public low format nibble, and stores the
requested scalar at X+18. For this root the final bytes R+20/+21/+22/+23 are
0/0/0/0. Depth remains8. X+8 is BE32 `000000FF` before the first lock.

For this nonzero-size/no-allocation branch, the original constructor does not
initialize R+18, R+28/+2C, or X+10/+14. First lock overwrites these; they must
not be required to start at zero. No reset-list insertion occurs in the
qualified helper branch (r28=1), and plane construction initializes no pixels.

## Public wrappers and actual callback pointers

Let E=BE32[82D0CA68]. Request11's original standard-callback registration
`823F03A0` builds index/target pairs, then installs them into the table at E+48.

| Slot | E offset | Callback | Registration word evidence |
| --- | --- | --- | --- |
| 15 decimal | 84 | `823F53D8` lock | index store823F04D8, target store823F04E0 |
| 16 decimal | 88 | `823F5588` unlock | index store823F04E8, target store823F0500 |

`82408208(R,level,mode)` encodes `((level&FF)<<8)+mode`, passes
r3=&stack+50 (output pointer slot), r4=R, r5=encoded flags to E+84, and calls
at `82408234=4E800421`. Return LR is `82408238`. On callback success it returns
the output slot; on callback zero it returns null. A replacement at this
callsite must write the output slot and return nonzero in r3, not just put D
in r3.

`82407BA8(R)` passes r3=0,r4=R,r5=0 to E+88 at
`82407BD8=4E800421`, return LR `82407BDC`. It ignores the callback result and
returns R. Retaining this wrapper does not turn callback failure into an
observable null public return.

## Exact root lock contract: mode5, level0

`823F53D8` first rejects nonzero R+4 (already locked). It follows the parent
chain to the root and permits only raster types0 or4. This evidence qualifies
a root R with zero subraster offsets, one level, X+9=0, mode5, level0.

Encoded bit0 controls write access: it selects SDK flags0 rather than10 and
later ORs 4 into raster byte R+22. Encoded bit1 would OR 2 into R+22. Input
bit2, present in mode5, causes no additional action in this callback. Therefore
mode5 sets only raster bit4 and passes SDK flags0. The original callback does
not translate that input bit2 into a discard request.

With X+9=0, `823F54C4=4804ACBD` calls `82440180(X+0,level0)` to acquire a 2D
SDK surface. `823F5498=907E000C` writes it to X+C. Root locking uses a null
rectangle. `823F54FC=4804B35D` calls `82440858(surface,&X+10,null,0)`, producing
the SDK lock pair `{pitch,data}` in X+10/+14. That SDK call's return is not
tested before the subsequent CPU publications.

Successful CPU field writes, in original order:

| Destination | Mode5/level0 value | Original instruction(s) |
| --- | --- | --- |
| X+C | acquired SDK surface S | 823F5498 `907E000C` |
| X+10 / X+14 | pitch P / mapped pointer D | SDK helper called at823F54FC |
| R+22, byte | old value OR 04 | 823F551C..5524, store `997F0022` |
| R+4 | D | 823F5530 `917F0004` |
| R+2C / R+28 | pre-lock H / W | 823F5538 `915F002C`, 553C `917F0028` |
| R+C / R+10 | max(1,W>>level) / max(1,H>>level), hence W/H | 823F5544/5550 and conditional clamps |
| R+18 | P | 823F5570 `917F0018` |
| X+B, byte | level0 | 823F5574 `9B5E000B` |
| callback output slot | D | 823F557C `91770000` |
| callback r3 | 1 | 823F556C `38600001` |

X+B is part of X+8: BE32 X+8 changes from `000000FF` to `00000000` on first
lock for this profile. No other raster/extension fields are directly written
by the qualified callback path. SDK resource-object writes are separate from
these raster fields.

## Exact unlock contract and retained fields

`823F5588` rejects null R+4 (not locked), and permits raster types0/4.
`823F55F8=48048AE9` calls `8243E0E0(S)` for SDK unlock;
`823F5600=4804C109` calls `82441708(S)` to release the acquired surface.
The original callback then performs:

| Destination | Value | Original instruction(s) |
| --- | --- | --- |
| R+18 | 0 | 823F5610 `917F0018` |
| R+4 | 0 | 823F5614 `917F0004` |
| R+C / R+10 | saved R+28 / R+2C | 823F561C `915F000C`, 5620 `913F0010` |
| R+22, byte | previous flags AND F9 | 823F5680 `716B00F9`, 5684 `997F0022` |
| callback r3 | 1 | 823F5688 `38600001` |

The automatic-mipmap branch requires R+23 bit10, among other conditions. The
qualified movie profile has R+23=0, so that branch is skipped. X+B remains0,
not FF. R+28/+2C remain W/H. X+10/+14 remain the last P/D; they are not cleared.
X+C remains the numeric value of the released SDK surface, although its
ownership has ended. The surface value must not be interpreted as a still-live
resource merely because the original field remains nonzero.

A native adapter should preserve the meaningful raster fields and live cached
P/D contract using its own backing association. Keeping X+C unavailable/zero,
instead of fabricating a released SDK pointer, is an explicit native
representation difference. Retained SDK routines must never dereference a
native texture identity as a console object. A native unlock uploads/commits
the real CPU-written logical rows before allowing sampling; pointer clearing
is not an upload substitute.

## Numeric pitch and console footprint

For scalar28000002, factory resource kind3 (2D), depth1, one mip, and level0,
the original CPU pitches are **1280 bytes for luma and768 bytes for each
640-wide chroma plane**. These are static derivations from the original SDK
header/lock path, not measurements from a completed live console lock.

| Logical dimensions (decimal) | CPU pitch (decimal / hex bytes) | SDK primary allocation request | Final allocation height (decimal) | Page-rounded SDK lock extent |
| --- | --- | --- | --- | --- |
| 1280x720 | 1280 / 500 | E1000 | 720 | E1000 |
| 640x360 | 768 / 300 | 43800 | 360 | 44000 |

Secondary allocation is zero. For the three-plane frame, the pitched CPU
payload totals `168000` bytes: `E1000 + 43800 + 43800`. The logical samples
total `151800`; the difference is chroma row padding, not extra pixels.

### Format, width alignment, and the height exception

`8243FA08=57FA06BE` extracts hardware format2 from28000002;
`8243FA14=57E9C7FE` extracts tiled=0. Format table bytes
`8206A02C..2D = 20 08` provide bits/sample8, read at
`8243E6D4=7EA950AE`. Block helper `8243E2A8` takes the format2 branch
`8243E2B8=41980070` to `8243E328`, returning block dimensions1x1.

For this linear profile, `8243E588..8243E5AC` computes bytes/block=1 and
alignment `256/bytesPerBlock=256` blocks. `8243E5B0..E5D8` rounds width to that
alignment. Thus W=1280 stays1280 and W=640 becomes768. A 32-texel-only rule
would incorrectly give chroma pitch640.

Helper `8243E520` initially rounds heights to736/384, giving intermediate
extents `E6000/48000`. Those are not the final primary allocation requests.
`8243E7AC..E7E4` admits this linear, non-array, internal dimension1, one-mip,
unpacked, border0 case. It obtains block-height1 at `8243E7F4`, rounds the
original height only to that block height at `E7F8..E808`, then computes
height*pitch at `8243E80C=7F2BD9D6`. `8243E810=4800017C` jumps to the final
publication, bypassing the generic padded-height size. `8243E99C=932B0000`
publishes E1000/43800. Factory `8244060C` loads that value and `82440624`
passes it to allocator `8238E880`.

### Header-to-lock chain

Header builder `8243F928` encodes the padded width through
`8243FB70/FB7C/FBAC` and final store `8243FC1C=90CB001C` into texture T+1C.
For these two widths, `(BE32[T+1C]>>22)&1FF` equals40/24 decimal; the masked
pitch portion `BE32[T+1C]&FFC00000` is `0A000000/06000000`. These are field
masks, not assertions that the complete header word has either value.

`82440180` acquires the level0 surface and publishes its parent texture at
surface+18 (`824401F4=93DF0018`). Thin wrapper `82440858` unwraps that texture,
extracts face/level zero, and tail-calls `8243F7F0` via
`82440874=4BFFEF7C`. The chain is:

```text
823F53D8 -> 82440180 -> 82440858 -> 8243F7F0 -> 8243F728 -> 8243F268
```

`8243F468=574B55FE`, `F46C=7D6B79D6`, `F470=557E10FA` decode pitch as
headerField*8*4, giving1280/768. `8243F6B0=93C90000` publishes it;
`8243F7A0/F7AC` multiplies by block-height1, leaving it unchanged.
`8243F86C=913D0000` and `F870=917D0004` finally write P/D into the caller's
X+10/+14 pair. `8243F68C..F6E8` separately computes a page-rounded lock extent,
giving E1000/44000. This value is not the SDK factory's primary byte request.
The allocator follow-up below independently establishes a page-rounded pool
request; any additional underlying pool bookkeeping remains unqualified.

### Native stride choice

Preserving the original CPU-visible stride uses1280/768 and backing covering
at least P*H logical rows (E1000/43800), with suitable alignment for the
retained decoder's vector/word stores. Native memory reservation may round
further for its own allocator; that is a host choice. Neither the intermediate
32-row height padding nor the page-rounded SDK lock extent establishes a need
to expose additional image rows to the native renderer.

A tight640-byte chroma stride is a possible native representation change,
not the original numeric contract. It would require all published raster,
extension, context, descriptor and decoder strides to agree, equal pitches
for both chroma planes, stable pointers/pitches on reuse, and separate
validation of decoder alignment/bounds. Retaining768 is the directly evidenced
choice. For GPU upload, gather only640 logical sample bytes from each768-byte
chroma row, or supply the backend's explicitly supported row-pitch interface;
do not interpret padding as image data or assume a host GPU-map pitch equals
the original CPU stride.

### Original pooled allocation and paired free (boot148 follow-up)

Main reports that boot148 completed the first three-plane frame, then a
separate native physical allocation exhausted its budget. That runtime result
is reported by main; this sidecar performed no launch. The bounded native
ownership choice is to retain the original CPU allocation/free calls at the
factory/destruction boundary and associate their real bytes with native R8
storage, while omitting the SDK header object.

For usage0, `82440608..82440624` derives r4=`BC800000` and calls
`8238E880(pitch*H,BC800000)`. The original wrapper loads allocator
`BE32[82D57244]` at `8238E89C`, decodes alignment1000, protection/cache
flags404 and dispatch flags0, then calls virtual+20 at `8238EAB8`.
Pinned vtable words `820B60D8=8268E138`, `820B60DC=8268E6E8` identify
the allocation/free methods for allocator vtable `820B60B8`.

`8268E138` takes the pool branch for dispatch flags0, rounds the requested
size to alignment1000 (`8268E1A8=7D7F4878`), allocates through virtual+0 at
`8268E1C8`, and invokes protection/cache helper `824338F8` at `8268E1D8`.
Thus primary requests E1000/43800 become pool requests E1000/44000.
`8238EAEC=7FEB492E` and `8238EAF0=7F6B512E` also publish the result and
original requested size at +4/+8 of the thread storage reached through
`BE32[r13]`. Retaining the wrapper preserves these CPU-side effects.

The original texture-release branch sets r4=`B1800000` at `82441110/18`
and calls `8238EB00` at `82441124=4BF4D9DD`. `8238EB74` selects virtual+24;
`8268E6E8` checks pool membership, frees a pool pointer through virtual+4 at
`8268E758`, and invokes `824338F8` at `8268E768`. Its out-of-pool branch is
separate. Native raster destruction should free its associated original CPU
allocation through this paired wrapper, without constructing or traversing a
console SDK header. Full native pool-budget recovery remains main's runtime
qualification, not a result of this static audit.

Inline words `8238E8F8..E904`, `8238E964..E974`, and `8238E9E8..EA24` are
absolute jump-table entries. The evidence labels them as data, even where the
disassembler prints a syntactically valid instruction for their bit patterns.

## Decoder writes, reuse, and final destruction

The worker `82375580` dispatches decoder vtable+14 at `82375604`, passing O in
r5. Decoder `82379DF0` loads O+1C and O+20, then writes decoder+4360/+4364 at
`82379E70=912B4360`, `82379E74=914B4364`. It publishes O+4/+8/+C to
decoder+4354/+4358/+435C and calls `8237A1F0` at `82379EA0=48000351`.
Both chroma planes use O+20: O+24 is not consumed on this path. Equal chroma
pitches are therefore required even though the descriptor contains three
pitch slots.

The decoder's `82380D40` path computes external byte offsets using the supplied
pitches. Its macroblock coordinates subtract 48 for luma and24 for chroma;
these are decoder-coordinate adjustments, not evidence that selector0 output
allocations need a 96-pixel border. A concrete pixel output leaf `823851D8`
writes packed bytes using `stvewx`, and advances external rows using r7 at
`8238523C=7D463A14`, `8238525C=7D4A3A14`. Internal reconstruction rows use a
separate pitch. This proves pitched external output, not full VP6 bitstream
or VMX correctness. The original worker writes mapped guest pixel memory;
native graphics calls and CPU decode-thread access are distinct operations.

Presenter `8282EC58` re-locks the previous descriptor's planes using level0,
mode5 at `8282ECE4`, ignores the returned pointers, and does not refresh cached
O/context data or pitches. It invokes the previous-frame return callback at
`8282ED18`, assigns the new current descriptor, unlocks its planes at
`8282ED3C`, and calls draw `8282E3E8`. Pointers and pitches must remain stable
through the entire frame-buffer lifetime, including every re-lock. Pixel
storage must remain owned while the public raster is unlocked and while the
frame may be displayed or queued.

Original texture stages0/1/2 bind descriptor planes0/2/1 at
`8282E490/E4AC/E4C8`. Preserve that index order; shader-name text alone does
not prove the in-memory chroma names or color-conversion constants.

`8282EA50(provider,O)` iterates N=O+38 contexts. For each, it tests the whole
raster byte+22; if nonzero it calls public unlock at `8282EA8C=4BBD911D`.
It then calls public raster destroy at `8282EA94=4BBD932D`, frees the C-byte
context through the same original allocator's vtable+C, and clears the context,
data, and logical-count slots. At exit it clears O+28/+2C and sets O+30=4.
It does not free O or clear O+38, O+1C/+20/+24, or unrelated descriptor fields.
It is not an idempotent repeated-destructor API. Keep the public raster's
plugin traversal/free and original context/descriptor ownership rather than
freeing those guest allocations in a native pixel-release callback.

## Bounded implementation and qualification handoff

1. Classify helper callers before applying resource-specific ownership guards.
   The movie profile is LR8282E9B8, format28000002, flags400, valid provider/O,
   selector0, count3, and plane index0..2. A crossfade can coexist with all nine
   video rasters. Single pending construction is distinct from one live plane.
2. Retain original public lock/unlock AOT, but replace their SDK-facing callback
   dispatch only for a verified native plane association. Match the callback
   ABI and exact success publications above, including the output pointer slot.
3. Use distinct, stable, adequately aligned CPU backing for every plane/frame;
   validate width, height, pitch, byte span, and equal chroma pitches. Treat
   descriptor logical counts separately from padded backing size.
4. Exercise original descriptor initialization, three-descriptor allocation,
   CPU writes, unlock/upload, frame reuse with unchanged D/P, and paired
   `8282EA50` destruction. Include distinct per-plane patterns, padded rows,
   sentinel padding, locked and already-unlocked destruction, and coexistence
   with the crossfade. Compare only defined pixels after a real write.
5. Verify exact fields retained after unlock/destruction, complete backing
   release after original raster destruction, allocator-owned contexts, and
   unchanged unrelated raster/singleton state. Unknown, stale, mismatched,
   subraster, nonzero-level, and other-format calls remain outside this profile.

No build, game launch, codec execution, or native storage test was performed
for this sidecar. Evidence includes 49 checked disassembly spans, 4,490
image-matched rows, 122 literal instruction pins, and 11 pinned data spans.
Original `.pdata` extents were decoded from the image and compared with
analysis metadata; manually reviewed leaf extents are labelled separately.
The two callback entries were recovered from original registration literals.
Its permitted writes are only this document and its two
named evidence files. Generated AOT, source implementation, original images,
and references remain read-only.
