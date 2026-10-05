# Original movie draw entrance and native boundary

This is bounded implementation evidence for boot149, using immutable
`analysis/simpsons.pe` at base `82000000`, size 15,466,496 bytes, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The checked original listings and word pins are
[movie-draw-original184.txt](../build/im2d-upload/movie-draw-original184.txt)
and [movie-draw-original184.json](../build/im2d-upload/movie-draw-original184.json).
The [183 plane evidence](native-movie-planes.md) remains unchanged.
Addresses, offsets, masks and state IDs are hexadecimal; dimensions, vertex
counts and strides are decimal. Bit positions count from the least significant
bit. SDK field layouts below are provenance, not native object layouts.

## Actual failure and narrow cut

`build/boot-149.log` records three completed frame descriptors
`FF614918`, `FF614788`, `FF614A08`, all using provider `E1ADC040`, nine R8
rasters, and creation of `VideoDecodeThread`. It then reports:

```text
address=000029C0 width=4 write=0 function=8243B718
lr=8282E408 sp=0203F3B0
```

This is the first operation in draw `8282E3E8`: `8282E400=807FCAF8` loads
`D=BE32[82D0CAF8]`, and `8282E404=4BC0D315` calls `8243B718(D,1)`.
`8243B718=816329C0` immediately reads D+29C0. The fault address therefore
establishes D=0 for this invocation. No scalar, shader, texture, declaration
or vertex mutation inside the draw has completed. The draw has already saved
registers and subtracted 70 from SP; its entry SP was `0203F420`.

The proposed boundary is the whole draw call `8282ED5C=4BFFF68D` to
`8282E3E8`, with continuation `8282ED60`. At that callsite r3 is the presenter,
r4 is copied from r27 (frame byte+51 for selector<2); this draw overwrites r4
with 1 immediately and never consumes the incoming value. A full aligned
`.text` scan finds only this one encoded direct branch to the draw. This is
not a proof excluding indirect calls or data aliases.

Retain presenter `8282EC58` and its queue/lifetime work before the call. For
selector0/count3, it copies context rasters into presenter+8/+C/+10, re-locks
and returns the prior frame, stores the new descriptor at presenter+14
(`8282ED1C=93DF0014`), then unlocks the current planes at `8282ED3C`.
Thus draw preflight runs after the original frame commit and native uploads;
it cannot promise rollback of that preceding work. Its own rejection can
leave draw-side state and targets untouched.

Presenter vtable is `8215D4B0`; the provider's separate vtable is `8215D4F0`.
At draw entry r3=presenter; after the draw prologue r30=presenter and
r31=`82D10000`. Do not use the allocation provider as the presenter identity.
Replacing only `8243B718` would next reach `82439F00` at `8282E410`, then
the remaining SDK object/command paths. Supplying a fake D pointer does not
qualify those paths.

## Original draw operations, in order

Every SDK call reloads D from `82D0CAF8`. None goes through the application
state dispatcher or RenderWare sampler cache/dirty queue.

| Callsite | Original target | Exact request / effect |
| --- | --- | --- |
| 8282E404 | 8243B718 | HALFPIXELOFFSET ID144 = 1 |
| 8282E410 | 82439F00 | CULLMODE ID38 = 0 |
| 8282E41C | 8243A6C8 | ZENABLE ID28 = 0 |
| 8282E428 | 82439F60 | ALPHATESTENABLE ID60 = 0 |
| 8282E43C | 8243CB80 | target0 packed blend = 00010001 |
| 8282E454 | 82445578 | VS = BE32[82CF2340] |
| 8282E46C | 82445278 | PS = BE32[82CF2328] |
| 8282E490 | 824408E0 | stage0 = extension resource of presenter+8; mask80000000 |
| 8282E4AC | 824408E0 | stage1 = extension resource of presenter+10; mask40000000 |
| 8282E4C8 | 824408E0 | stage2 = extension resource of presenter+C; mask20000000 |
| 8282E4D8 / E4E8 | 8243BA40 / 8243BBD0 | stage0 MINFILTER ID14 / MAGFILTER ID10 = 1 / 1 |
| 8282E4F8 / E508 | same | stage1 min/mag = 1 / 1 |
| 8282E518 / E528 | same | stage2 min/mag = 1 / 1 |
| 8282E544 | 82445798 | declaration = BE32[82DFEB34] |
| E57C / E624 / E664 | 8244C450 | D, primitive8, vertex count3, stride16; branch selects one call |
| 8282E6C4 | 8244C8F0 | finalize the SDK immediate draw allocation |
| E6DC / E6F0 / E704 | 824408E0 | unbind stages0/1/2 with their same masks |

Here X=R+BE32[82E3DC94], and each resource is BE32[X+0]. Presenter+8/+C/+10
contain frame planes0/1/2, so the sampler binding order is **0,2,1**.
These are resource identities, distinct from CPU pixel pointers X+14.
All three resources must remain independently owned and valid during sampling.

The packed target0 word `00010001` selects replacement blending in the
existing verified blend model. It does not change scalar blend-enable,
separate-alpha or expanded-target requests. The function does not select a
target, change viewport/scissor, clear a target, present, set depth-write,
set stencil state, set color masks, or reset sampler addressing/mips/LOD.
It does not write a per-draw PS color constant as `82756480` does.
The shader-binding metadata path is a separate qualification below.

## Exact scalar and sampler provenance

| Setter | Original CPU writes for the reached request |
| --- | --- |
| 8243B718 | D+29C0 bit0=1; BE64[D+20] OR 0000000800000000. Stores B728/B734. |
| 82439F00 | D+2948 low3=0, preserving other bits; BE64[D+10] OR40. Stores 9F08/9F14. |
| 8243A6C8 | D+2E5C=0, D+2934 bit1=0; BE64[D+10] OR20800. Stores A6CC/A6E4/A6F0/A6F8. The depth attachment check cannot re-enable a zero request. |
| 82439F60 | D+293C bit3=0; BE64[D+10] OR40200. Stores 9F68/9F74/9F7C. |
| 8243CB80, target0 | D+2938=00010001 verbatim; BE64[D+10] OR20400. Stores CBA8/CBC4/CBD0. |

Min/mag setters use per-stage descriptor D+480+18*stage. They retain inherited
anisotropy data at D+2E8C+stage and separate-Z controls at D+2EDA+stage.
MIN writes descriptor+10/+C/+10 at BA9C/BABC/BAE8; MAG writes them at
BC2C/BC4C/BC78. Each ORs `1ULL<<(31-stage)` into BE64[D+18]. Request1 is
the qualified linear request, but all other effective sampler inputs must
come from the current state owner; these calls do not install fresh sampler
defaults. Detailed arithmetic is in
[original sampler evidence](native-application-samplers.md).

Texture binder `824408E0` imports SDK resource words+1C..30 into descriptor
fields, preserves selected sampler fields, clips mip limits against retained
request bytes, and stores the bound pointer at D+30F8+4*stage (`824409E4`).
It also tracks the old resource and can enter SDK submission-list allocation.
It cannot consume a native R8 identity as an SDK texture object. Null unbind
retains inactive descriptor contents; it does not reset sampler state.

## Shader and declaration identities; unresolved program

Immutable data pairs are `82CF2340={0,82152880}` and
`82CF2328={0,82152B68}`: the first words are runtime-populated handles,
not zero-valued live-handle assertions. The VS record is the previously
qualified `Screen_Xenon_VSTextured`; the PS record is the separate movie
program whose embedded debug path contains `vp6_y_cr_cb_Xenon_PS`.
This identifies the program without proving its arithmetic.

The draw unconditionally publishes three engine binding caches before the
corresponding SDK calls:

| Engine cache | Source | Original store |
| --- | --- | --- |
| 82CD1A6C, VS | BE32[82CF2340] | 8282E450=908B1A6C |
| 82CD1A70, PS | BE32[82CF2328] | 8282E468=908B1A70 |
| 82CD1A68, declaration | BE32[82DFEB34] | 8282E540=908B1A68 |

SDK binders store VS at D+3190 (`82445618`) and PS at D+318C (`824452FC`),
and read old shader objects for submission tracking. Optional metadata loops
read copied header+14 at VS object+37C (`82445630`) and PS object+3C
(`82445320`). An opaque native handle cannot enter these routines.
Declaration binder `82445798..57A8` stores D+2E24 and ORs80000 into
BE64[D+10]; it does not itself parse the declaration or retain/release it.

The metadata distinction is concrete: VS record+14 (`82152894`) is zero,
but PS record+14 (`82152B7C`) is **118**. PS metadata starts at `82152C80`:

```text
00000000 00000001 00000000 00000000 00000014
01FC0010 00000000 00000000 00000000 00000000
```

For this exact copied header, `82445330..53C` clears bit0 of BE64[D+8]
using the first metadata BE64 value1. The next BE64 is zero, so the optional
D+20 bit56 write is skipped. The group reader starts at `82152C94`, ends
at `82152CA8`, skips the `01FC0010,00000000` pair, and passes zero group
terminators at `82152C9C`, `82152CA0`, `82152CA4`. The descriptor-copy and
masked-patch loops therefore perform no descriptor writes for this record.
This is a bounded walk of original metadata, not shader arithmetic or a
general justification to suppress metadata for other shaders.

The textured declaration source `82151724` contains stream0 float2 elements
at byte offsets0 and8, with POSITION and TEXCOORD0 usages. The original
creation store `827521E0=907F0114` publishes it to the runtime handle slot
82DFEB34. Its pinned three 12-byte records are:

```text
00000000 002C23A5 00000000
00000008 002C23A5 00050000
00FF0000 FFFFFFFF 00000000
```

The current native screen path explicitly selects PS `82152708` or `821524C8`
and owns a single sampled texture. That path does not implement this separate
movie PS with three inputs. R8 upload success and use of a known screen VS
do not qualify movie pixel output. No YUV coefficients, range, Cb/Cr labels,
alpha result, gamma behavior, chroma siting or host shader is guessed here.

## Exact geometry and SDK immediate-draw contract

`8282E550=2F0B0280` compares the signed luma raster width against640.
`8282E568/E570/E574` pass stride16, count3, primitive8. The read-only
reference [xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:53)
names primitive8 `kRectangleList`. Treating the three records as an ordinary
triangle would draw only half the intended rectangle. Native rectangle
expansion/raster coverage remains a separate rendering qualification.

The records are `(X,Y,U,V)`, in increasing buffer order:

| Signed luma width | Presenter byte+41 | Vertex0 | Vertex1 | Vertex2 |
| --- | --- | --- | --- | --- |
| <=640 | zero | (-1,-1,0,0) | (1,-1,1,0) | (-1,1,0,1) |
| <=640 | nonzero | (-1,-1,0,A) | (1,-1,1,A) | (-1,1,0,B) |
| >640 | zero | (-1,-1,0.125,0) | (1,-1,0.875,0) | (-1,1,0.125,1) |
| >640 | nonzero | (-1,-1,0,0) | (1,-1,1,0) | (-1,1,0,1) |

A is float32 bits3DCCCCCD (0.10000000149011612), B is3F666666
(0.8999999761581421). Original constants are -1 at821DD110, zero at821DD0D8,
one at82000BB0, A at82001894, B at820036E8, 0.125 at8206A014, and0.875
at8215D498. Each branch issues twelve `stfs` stores, covering all48 bytes
once. No arithmetic scales these UVs using width or height beyond choosing
the width-threshold branch. Boot149's presenter byte+41 is not in the log,
so neither >640 UV branch is asserted to be the live one.

`8244C450` is an SDK immediate allocation/submission routine. It temporarily
sets byte D+30E8=4 (stride in words), may dirty D+10 bit19, clears D+18 bit0,
processes pending SDK dirty banks +0/+8/+10/+18/+20, and restores the previous
D+30E8 byte at `8244C6DC=9B3F30E8`. At `8244C700=4800A661`, helper
`82456D60` receives D,12 words,16-byte alignment. A null result returns0;
the original movie does not check it before its vertex stores.

The success path builds command/upload words. With D+2ABC bit0 clear,
draw header/control are C0012201/00030088. The bit0-set branch adds command
and tracking work and advances D+33B8 by10. It then sets D+18 bit0 and
publishes prepared cursor D+3474 (`8244C8DC=917F3474`), vertex pointer
D+3478 (`8244C8E0=939F3478`), and word count12 at D+3480
(`8244C8E4=92DF3480`). These are console submission fields, not a native
vertex-buffer API or permission to allocate a fake device layout.

End `8244C8F0` copies D+3474 to D+30 (`8244C8F8=91630030`). If byte
D+2ABC has bit80 set it returns; otherwise `8244C904=48013CBC` tail-calls
`824605C0`. The draw block publishes no vertex pointer to the presenter or
frame. A whole-draw native replacement can own transient vertex data itself;
keeping the original `stfs` block instead requires a real writable48-byte
return span and correct native submission lifetime. No generic begin/end
SDK emulation or downstream submission helper support is established here.

## Snapshot required at the boundary

Capture this as one consistent draw input before any draw-side publication:

1. Caller PC/LR/SP and presenter identity/vtable; current descriptor at +14;
   selector/count/dimensions, three contexts and raster associations; presenter
   byte+41; original plane-index mapping0/2/1. Require live ownership, stable
   CPU backing, completed unlock/upload and the correct frame association.
2. A value copy of the native effective state, including all three sampler
   stages and inherited scalar/target policies. Stage0-only screen snapshots
   are insufficient. Record pending state/queue status rather than flushing
   it: this original draw calls no RenderWare commit helper.
3. Actual current native camera, color/depth attachments, formats, dimensions,
   viewport including depth range, and scissor. The last boot149 log selection
   is camera E4D41AB0, color00F0001B, depth00F0001C, viewport1280x720.
   The log does not supply a complete draw-state snapshot or prove that the
   default/window target is the destination.
4. Three owned R8 views with logical dimensions and row pitches, generation
   and upload readiness; live VS/PS/declaration identities and original source
   records; previous effective binding identities and the three cache words.
   Preserve owning references if native work outlives the immediate call.
5. Derive a prospective state copy using only this draw's four scalar writes,
   packed target0 blend write, and six filter calls. Retain all other fields.
   Validate the whole pipeline before committing this state or cache words.

There is no complete live presenter/descriptor/shader/sampler snapshot in the
existing failure log. The snapshot above is the concrete input contract for
main's next boundary, not invented values for boot149. The draw-local r30
holds the presenter at the fault, but its numeric value was not logged.

## Completion and unsupported work

On an actually successful native draw, publish the exact three engine cache
words and the draw's resulting effective state. The original epilogue unbinds
all three texture slots and leaves shaders, declaration, half-pixel mode,
cull/depth/alpha settings, packed blend and filters selected. It performs no
restore of a prior state snapshot and no depth-on cleanup. In particular,
`applyScreenQuadState`/`finishScreenQuadState` have different original behavior
and must not be substituted for this movie sequence.
The original movie body also makes no stores to the screen helper's three
stream-cache words82D0CAB0/CAB4/CAB8; do not copy those screen-specific
post-state writes into the movie boundary without separate evidence.

Until the exact movie PS, primitive8 geometry/rasterization, inherited sampling
and target policy are qualified, the useful narrow implementation is a
validated draw-entrance snapshot followed by a specific unsupported-operation
failure before state/target mutation. It must not return to `8282ED60` as though
pixels were submitted. Continuing the existing AOT body would require native
replacements for every SDK-facing operation above, including native draw
storage with a real returned pointer for the original vertex stores.

No build, launch, runtime/config/generated edit, shader implementation or
reference modification was performed. Only this document and the two named
184 evidence files are writable in this audit. Original source bytes and the
three finalized 183 artifacts are checked unchanged when finalizing.

Verification: 17 original disassembly spans, 1,206 image-matched rows,
67 literal instruction pins and19 data spans. The four geometry fixtures
each cover exactly twelve output words; a separate bounded metadata walk
checks the movie PS groups. These are static evidence checks, not native
rendering or decoder tests.
