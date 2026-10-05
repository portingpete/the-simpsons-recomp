# Auxiliary viewport surface ownership

Boot188 reached Saved Games. Choosing the first empty slot invoked original
82750FA8 and stopped at its SDK release return address82751020. The object
E1AB4230 was not a shadow texture: the viewport constructor still created an
original SDK surface header, while the shared release entry82441708 had already
been replaced by native resource dispatch.

The pinned original image is analysis/simpsons.pe, SHA256
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0.
Direct original-byte disassembly establishes:

- 82751118 uses100-byte rows at82DFEF00, count82DFEBA0. It publishes signed
  dimensions at row+0/+2, reciprocal scales at+4/+8, and live byte+60.
- The ten eight-byte profiles at82150988 request auxiliary surfaces in slots
  3..7: half, quarter, eighth,256-square and64-square. The first three rows
  correspond to1280x720,640x720 and640x720 viewport dimensions.
- 8275135C calls82543188 to recover the original CPU descriptor. 82751384
  calls82440698 with its dimensions,182801B6, no multisampling and a nonnull
  three-word placement record. 82751388 stores the returned handle at
  row+38+4*slot. All other slots contain zero surface handles.
- 82440698 would allocate a30-byte SDK resource header and build its fields.
  Its nonnull placement branch does not allocate pixel storage there.
- 82750FA8 preserves row0 when its argument is1, or retires all rows for0.
  8275101C calls82441708 for each nonnull surface; 82751020 clears the slot.
  The rest of the original loop clears descriptor pointers and row state.
- 82751510 copies active viewport metadata without retaining these handles.
  These copies are borrowed, not additional resource references.

EngineViewportSurfaces replaces82440698 before any SDK header allocation.
It checks the exact caller, row, original profile bytes, construction state,
slot/order, dimensions, format, placement ABI and native context. Five actual
RGB10A2 native allocations per row have unique non-addressable identities.
The original instruction publishes each identity after the adapter returns.
The shared release dispatcher recognizes only registered identities and checks
the actual destructor's row, slot, loop counter and publication before retiring
the backing. Original instructions perform all field clearing. Driver shutdown
requires no remaining owners; terminal shutdown reports incomplete cleanup.

This is an allocation/lifetime adapter. Pixels are uninitialized. EDRAM placement
has no host address meaning, and these auxiliary surfaces are not admitted to
binding, sampling, resolving or drawing. No equivalence of overlapping EDRAM
storage or rendered output is claimed. Existing camera targets remain separately
owned by their established raster adapter.

OriginalViewportSurfaces runs original startup to its audio checkpoint, then
creates the other two rows with82751118. It checks all15 backing dimensions
and identities, rejects malformed release/publication, executes the original
preserve-row0 cleanup, recreates rows1/2 using82751118, then executes full cleanup.
Weak resource observations verify release and stale identities are rejected.
No fixture writes or synthetic input are part of the production menu flow.

Validation: the full build passed the101 existing suites. After correcting the
new fixture's startup checkpoint assumption, OriginalViewportSurfaces passed
in4.58 seconds (`build/viewport-surface-focused-test.log`). Its focused build is
recorded in `build/viewport-surface-focused-build.log`. Boot189 independently
reached all ten original row1/2 releases after choosing an empty save slot. It
then recreated cameras and continued to game-resource loading before a separate
ITXD cache check stopped it. The surface release correction is live verified;
the main menu remains unverified.
