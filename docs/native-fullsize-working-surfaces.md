# Full-size working surface continuity

The native camera owner retains the driver's working color and depth backing
for the qualified 1280x720 type-5/type-1 views. Each view still receives its own
logical surface identity and original raster list node. Creation rollback,
camera/cache selection, binding reset and retirement retain their original
ownership rules. Retiring a private view invalidates its identity while the
driver continues to own the shared pixels.

The original default/full-size color descriptors are the same single-sample
182801B6 surface, pitch1280, placement{0,0,0}, tiles[0,720). The corresponding
1A220197 depth descriptors use placement{720,0,0}, tiles[720,1440).
See [color evidence](native-movie-composition.md) and
[depth evidence](native-fullsize-depth-alias.md) for pinned original bytes.
The native owner rejects a full-size view if its default storage dimensions or
shared association differ. Existing format, original owner, raster metadata,
pure CPU placement and logical identity checks still apply.

Front0/front1, colorCopy and depthCopy remain separate resolve destinations.
The original equal-size predicates, depth-only copies and presentation remain
unchanged. No extra copy or draw was added. Different logical IDs still cause
camera/reset viewport transitions even when their native backing is identical.

Viewport pass/reset tests exercise both directions of pixel visibility using
the original clears. They retain original camera begin/end, matrix, cache,
reset rollback, ABI and stale-owner checks. True resolved textures must stay
unchanged through working clears and view retirement. Copy tests require
separate source/destination storage. Camera lifecycle tests require the driver
backing to survive full-size view retirement while logical IDs become invalid.

This correction qualifies only identical full-size working views. Other sizes
still use the previous native allocations; physical partial overlap, differing
pitches, reinterpretation and associated lifetimes need separate evidence and
implementation. Those existing allocation tests do not establish physical
console-memory parity. This document alone establishes no movie playback,
display acceptance, menu or gameplay milestone.

Build185 and actual-run validation are recorded in STATUS.md after execution.
