# Native root camera bridge

The original loading camera now selects native attachments, clears the real
1280x720 RGB10A2/depth/stencil resources, and runs its original CPU begin/end
bookkeeping. This is a resource operation, not a rendered or presented frame.
The latest executable observation is recorded in local development logs.
The later viewport extension is documented in `native-viewport-camera-passes.md`;
the loading-camera-only discussion below records the original bounded milestone.

## Original boundary and retained behavior

The byte-pinned replacements are target selection `823EE6C8` and clear
`823EE940`. Entry checks at begin `823F00C0` and end `823EE7F0` fall through to
their original AOT bodies. Public wrappers, frame synchronization, projection
arithmetic, matrix-pool allocation/copies, pending state updates, and the
`CB1C`/`DD60`/engine-current-camera stores remain original code. The underlying
instruction evidence is in `native-camera-pass.md` and its deterministic
analyzer/report.

The native owner accepts the live loading camera at `82E07248`, a registered
type-2 root color raster and optional registered type-1 root depth raster.
Plugin registry, dynamic extension offset, allocation extent, list node,
metadata, dimensions and zero raster offsets are checked. The six original
target role words must still contain the owner's opaque native identities.
Additional color attachments and foreign cached bindings reject before work.
Active camera raster destruction rejects before original plugin cleanup.

Native target selection uses actual D3D11 attachments. It publishes only the
original color/depth binding-cache changes. The logical camera viewport
retains `{0,0,1280,720,1,0}`; it is not passed to D3D11 as an invalid reversed
depth range. Full-resource clears do not depend on a host viewport. A draw
still needs a proved depth/raster mapping.

## Clear contract

The original eight-entry table maps selectors 0 through 7 to masks
`00,0F,10,1F,20,2F,30,3F`. Missing camera depth removes both depth and stencil
flags while the original type-2 default-depth attachment remains selected.
The RGBA pointer is accessed only when color is selected. Each component is
currently restricted to exact byte endpoints 0 or 255; general RGB8-to-RGB10A2
rounding remains unsupported. Selected depth clears to exactly zero. Stencil
comes from `82D0CB14` and must fit a byte when selected. All four expanded-blend
requests must be zero.

Color uses `ClearRenderTargetView`; depth/stencil use independently selected
`ClearDepthStencilView` flags. Selector zero retains binding semantics without
clearing pixels. Argument and ownership checks precede binding or GPU mutation;
a device submission failure remains terminal. Original current-camera and
begin/end fields are unaffected by clear.

Real GPU readback checks all pixels, all eight selector combinations, absent
depth, unchanged attachment components and rejected calls. Separate backend
tests seed nonzero depth and stencil and verify each independent flag on WARP
and hardware. The driver fixture also exercises original camera reuse and
resource destruction; its latest result is reported separately in local development logs.

## Pipeline state before the screen helper

The original 67-row reset at `823F4618` remains an AOT loop. Its indirect SDK
setter block at `823F4648` updates the native effective-state owner and resumes
at `823F4658`. Preflight validates every original row and setter before the
loop. Neither application caches nor RenderWare pending/applied caches are
rewritten by this direct reset.

The original pipeline mode-stack push/pop wrappers remain AOT. Their two
direct calls at `826B09D4` and `826B0A0C` set native half-pixel mode to 1.
Thus the observed sequence is application mode 1, reset mode 0, wrapper mode
1. Guard factors remain 1.0. The separate screen raster policy report documents
why the actual mode needs no position or UV shift and why arbitrary fractional
quads still require a precision check.

The screen helper `82756480` and presentation `823EE820` remain guarded.
These camera and state changes do not permit skipping original drawing or
claiming startup-screen fidelity from a cleared resource.
