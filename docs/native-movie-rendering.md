# Native movie draw integration

The native engine boundary replaces original `8282E3E8`, reached at the sole
checked caller `8282ED5C` (continuation `8282ED60`). The original presenter,
frame queue, decoder, CPU allocator, locks, uploads and retirement remain AOT.
The launcher starts the current native executable without a time limit.

## Input and publication

The bridge checks the original caller registers and committed descriptor, then
obtains three independently owned, unlocked and uploaded R8 planes from the
raster owner. A presenter cannot borrow another presenter's frame. The active
original full-size camera must retain its owned color/depth attachments.

VS `82152880`, movie PS `82152B68` and declaration `82151724` must have their
original creation owners. Source records and original CPU shader copies are
checked against the pinned catalog. The movie record is appended after the
235 existing identities, preserving their indices. Seven records now have
offline native shader artifacts; other materials still fail before binding.

`prepareMovieState` snapshots effective requests without flushing the pending
RenderWare queue or editing application caches. It changes exactly four scalar
requests (half-pixel, cull, depth enable and alpha test), target0's packed blend
word and min/mag filters at stages0..2. Other requests survive. Unqualified
fill, stencil, mask, tessellation, bias, guardband, expanded-target, wrapping,
viewport and sampler profiles fail before submission.

The native submission owns four vertices and binds original planes **0/2/1**
as Y/Cr/Cb with independent linear samplers. It requires the already selected
full native viewport and leaves it unchanged. Logical reversed depth remains
retained and inactive because this draw disables depth testing/writing.

Only after native submission succeeds does the bridge publish the prospective
effective state and original declaration/VS/PS cache words at `82CD1A68/6C/70`.
It does not clear the original stream cache at `82D0CAB0/CAB4/CAB8`. The backend
restores prior native stream0 and unbinds PS texture slots0..2, retaining the
movie VS, float PS, declaration and sampler selection. The checked caller does
not consume volatile return registers; it resumes its own original epilogue.

## Pixel policy and limits

The shader uses the original binary32 coefficients and offsets. Alpha is
explicitly zero. The first observed input (Y16, Cr128, Cb128) converts to native
RGB10A2 codes **3,0,4,0**, so treating it as exactly black would change the
original arithmetic. Integer target storage uses explicit saturation and
nearest-even rounding, separately documented from the original shader.

The four original width/flag geometry branches produce three UV profiles and
a full rectangle. Original Y/V orientation and literal float bits are retained.
These contracts and native GPU tests do not establish physical Xenos precision,
filtering, multisample, gamma or display equivalence. They do not prove complete
codec behavior, normal original teardown, completed startup or gameplay.

Presentation metadata counts movie draws separately. Captures retain the first
32 changed startup frames, the first five movie draws, each30 through draw300,
and each300 through draw18000. A separate `native-movie-target-*` capture records
the private camera output immediately after the draw, before composition.
Captures are completed renderer readbacks with actual
display acceptance recorded; occluded presentation is not desktop scanout.

Original evidence: [draw and metadata](native-movie-draw.md),
[shader](movie-shader.md), [geometry](native-movie-geometry.md), and
[plane lifetime](native-movie-plane-lifecycle.md). Exact build, actual-run and
checkpoint results are recorded in `STATUS.md`.
