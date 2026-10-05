# Movie camera color continuity — actual152

**The proven mismatch is independent native backing for original surfaces that
share the same color-memory location. It is not an omitted full-size QUAD call.**
Original full-size camera rendering deliberately skips both the intermediate
color copy and the subsequent QUAD composition. The default color surface and
the full-size type-5 camera surface have identical original placement, pitch,
format and sampling. Their different resource headers do not imply independent
pixel storage. Native allocation currently makes their pixels independent.

The concrete fix boundary is the native camera surface backing/selection model
(`EngineRasters::create`, followed by the existing `EngineDriver` camera select
and reset operations). Preserve distinct logical raster/surface identities and
lifetimes while making the **qualified 1280x720, 182801B6, single-sample,
placement-zero color views** share pixel storage with the driver's default
working color surface. The front textures remain separate and the original
presentation resolve remains responsible for producing a front. This report
implements no copy, redirection, allocation change or runtime hook.

## Observations and authority

Immutable `analysis/simpsons.pe`: base `82000000`, 15,466,496 bytes, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The [checked original listing](../build/movie-composition/original.txt),
[machine-readable evidence](../build/movie-composition/evidence.json) and
[matched-frame log excerpt](../build/movie-composition/movie60-log.txt) are
produced by `python -B build/movie-composition/evidence.py`.
It checks 1,937 instruction rows in 23 spans, 54 literal instruction pins,
four data pins and six existing hook byte ranges. It scans all aligned `.text`
direct branches for the reported caller sets. Indirect calls require separate
owner/vtable evidence; an absent direct caller does not exclude them.

I inspected the existing, unmodified previews: private movie draw60 contains
the EA logo; completed front0332 is black. More precisely, **front0152 is the
matching completed front for movie draw60**, presentation96. Its raw RGB is
also entirely zero. Front0332 corresponds to movie draw150, presentation186.

| Existing readback | Movie count | Nonzero RGB pixels | Raw SHA-256 |
| --- | ---: | ---: | --- |
| `native-movie-target-0060` | 60 | 920,421 | `85aa3e6bacdd3a8cc5980f350cbf704f9ce853133984c700b76c8fbb53eb4b4c` |
| `native-frame-0152` | 60 | 0 | `0c660f2bd3eff3150dd0040789abe2291613b9af319df870203d4f77a4913a5f` |
| `native-frame-0332` | 150 | 0 | `0c660f2bd3eff3150dd0040789abe2291613b9af319df870203d4f77a4913a5f` |

Nonzero means any of the original packed RGB bits is set; there is no brightness
threshold or color correction. These are renderer readbacks, with
`display_accepted=false`, not observed display scanout or gameplay evidence.

## Original frame path and exact composition decision

Presenter `8282EC58` is vtable `8215D4B0` slot+3C:
`8215D4EC=8282EC58`. Original manager `82374FA0` permits states1/2, obtains its
presenter at owner+4, and calls virtual+3C at `82374FF4=4E800421` with the
incoming frame argument retained. Its full indirect provenance beyond the
established presenter owner is not inferred from a direct-call scan.
The presenter publishes/unlocks the frame then invokes movie draw
`8282ED5C=4BFFF68D ->8282E3E8`; the next instructions are its epilogue.
Neither presenter nor movie draw contains a color transfer or target selection.

Outer renderer `826B8068` calls the viewport camera pass at
`826B8238=4BFFFC21 ->826B7E58(camera,0)`. The important post-render sequence is:

| Original location | Operation / condition |
| --- | --- |
| `826B7FFC ->8269EAB8` | Execute camera render-list ranges0..18. |
| `826B8004 ->8269D2A0` | Ask whether this camera matches the selected display mode's width and height. |
| `826B8010=409A001C` | Nonzero skips the copy and branches to802C. |
| `826B8014/801C` | Otherwise obtain shared depth/color copies through823ED9C8/823ED9B8. |
| `826B8028=4BFF8889` | Call826B08B0(colorCopy,depthCopy,camera) only for the unequal-size case. |
| `826B8054=4E800421` | Invoke engine+20 with(1,0): the existing mode-zero binding reset restores default target roles. |
| `826B805C ->823F1A08` | End the CPU camera pass. |
| `826B82B8 ->823F1A18` | After the viewport loop, begin the restored outer/loading camera. |
| `826B82BC ->8269D2C8` | Query the first eligible active viewport's size against the display mode. |
| `826B82C8=409A0020` | Nonzero skips both subsequent QUAD calls. |
| `826B82E0=4BFF8A09` | **Exact next composition preparation caller:**826B0CE8(sharedColorCopy,sharedDepthCopy,outerCamera). |
| `826B82E4=4BFF8975` | **Exact next composition invocation caller:**826B0C58(). |
| `826B832C ->826B7E58` | Render the outer camera with flag1, including later overlays. |
| `826B83C8 ->823F1BD0` | Present the outer camera through the normal engine path. |

`8269D2A0` tail-calls `8269C688(manager,camera)` when the manager exists; absent
manager returns1. `8269C688` queries selected mode via823EC7C0/823EC768,
compares color raster+C/+10 against mode width/height, and returns1 only when
both agree (`8269C6C4=38600001`, height-equal branch8269C6D0).
`8269D2C8` scans manager+12C active entries, skipping null cameras and entries
whose manager+130+4*i word is FFFFFFFF. It returns1 if none is eligible, or the
same size comparison result for the first eligible camera. Thus the existing
1280x720 active profile takes the skip branches; a640x720 active profile would
request the separate copy/composition path. This is a size predicate, not a
test that the camera is the loading/default object.

For the unequal-size path, both composition helpers require nonnull global
effect manager82D08BFC and look up the literal **`QUAD` at820B0814** through
826B7088. Preparation826B0CE8 selects state3=(1,1), gets the original camera
rectangle through8269D388, passes the shared color/depth sources into
826B6148, installs the rectangle through826B3A38 and calls826B4B18.
Invocation826B0C58 supplies the original constant vectors to826B69A0.
That unused branch's full shader and effect semantics are outside this audit;
forcing it on for the full-size camera would change the original control flow.

The live depth-only copy is a different call: `826B1030` supplies r3=0 at
`826B105C=38600000`, r4=sharedDepthCopy, r5=camera, then
`826B1064=4BFFF84D ->826B08B0`. This occurs in the shadows callback. The native
copy hook correctly sees destination_color=0; turning it into a color copy
would invent an operation absent at that callsite.

Camera end823EE7F0 only clears82D0CB1C/82E3DD60 and returns1. Its original leaf
does not resolve, clear or unbind anything. The six hooks inside checked spans
do not replace the renderer's size predicates or QUAD callers. The camera-end
hook is preflight-only and retains the leaf. No skipped essential transfer is
found inside the checked camera-end, mode-zero reset or depth-copy boundaries.

## Why separate headers still share pixels

Default creation `823EE050..070` calls82440698 with
`(1280,720,182801B6,0,&{0,0,0})`; `823EE084=907ACB00` publishes its header.
Type-5 creation `823F7138..717C` calls the **same** constructor with the same
width/height/format, samples0 and explicit placement triple{0,0,0}.
The type-5 format table pin is `82062B88=182801B6,20010000`.

82440698 allocates a30-byte resource header and calls8243FC38. With a nonnull
placement pointer, `82440708=409A00AC` bypasses the automatic tile allocator.
The constructor reads the placement base at `8243FD0C=813A0000`, constructs
pitch/sampling in header+18, and puts the base in the low12 bits of header+1C
(`8243FE64=552A053E`, `8243FE70=917F001C`). The exact qualified color headers
have +18=`14000500`, +1C=`00020000`, +28=`182801B6`: identical1280-pixel pitch,
single sampling, format and base tile0. The 32-bit1280x720 surface spans
16*45=720 tiles. Both are views of the same tile interval[0,720).

Binding8243D230 loads header+1C at8243D268 and publishes that descriptor at
8243D28C. It does not manufacture a fresh independent pixel allocation for a
different header pointer. Mode-zero reset loads defaultCB00 and binds it at
823EFF68/6C. No full-size resolve is needed to keep the color visible there.
The read-only reference `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h`
lines224–270 corroborates the tile-addressed shared-memory terminology;
the original constructor and binding words establish this game's equal views.
No command-stream implementation is proposed or shipped.

The full-size depth views also request the same placement720 (2D0) through
823ED930 and format1A220197, so independent depth backing needs the same review.
Smaller surfaces use different pitches and may partially overlap/reinterpret
the same physical memory. **This audit does not qualify arbitrary overlapping
sizes, formats, expanded representations, MSAA or shadow/reflection lifetimes.**

## Native mismatch, repair scope and remaining verification

At the audited source snapshot, `EngineRasters::create` allocates a new
`createTarget(width,height,RGB10A2)` for every type5 and a fresh identity.
`EngineDriver::cameraColor` resolves that identity to independent backing;
`resetBindings` then selects the separate driver defaultColor. `present` copies
only defaultColor to the rotating front. Actual152 therefore preserves the
movie in00F0001B while default00F00001 still contains its own black/overlay
pixels. This is a loss of the original alias relationship at allocation,
observable at the correct original target-switch boundary.

A bounded repair should prove identical original surface profiles and share
their native working storage while retaining separate logical identities,
list nodes, destruction rights, camera ownership and changed-cache semantics.
Different IDs must still trigger the original viewport/reset behavior even if
their backing is shared. Do not attach movie draws to a front texture or change
the original size decisions. If independent backing is retained instead, it
requires a separately specified content-coherence model across all aliasing
reads/writes; a movie-only copy at presentation would not establish that model.

Existing `test_viewport_camera_pass.cpp` and `test_viewport_reset.cpp` seed
distinct full-size private/default colors and assert isolation, including
default pixel preservation. Those assertions encode the now-disproved storage
assumption. Future tests must preserve logical owner separation but verify
bidirectional content visibility, resets/reselection, original clear behavior,
and shared-backing lifetime after retiring one view. They must continue to keep
the true front and resolve textures separate. No tests were edited or run here.

The supplied `run152-sample1.json` main thread22896 contains
8269EAB8 <-826B7E58 <-826B8068, with inner movie update/wait functions.
That puts the later stall inside the render-list call826B7FFC, before the
post-list size/copy decision at826B8004. It does not imply the completed earlier
front copies were waiting on composition. Stall sampling/diagnosis stays with
the main task; this audit takes no new sample and does not modify synchronization.

After correcting storage semantics, the main task should run the actual game
and compare private and completed front readbacks at matching movie counts,
including intervening Im2D overlays and clears. The alias proof does not prove
their final pixels, physical console parity, working display scanout, loading
completion or gameplay. Shared GPU shader tests likewise do not test this
resource relationship. All authored output in this task is confined to this
document and `build/movie-composition/*`; no shared build, launch, UI action,
runtime/generated edit or original/reference write was performed.
