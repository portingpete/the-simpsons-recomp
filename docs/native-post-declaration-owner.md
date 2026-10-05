# Native ownership of the second FX family's cached declaration

The original five edge/AA types share global82D099A0. Their base constructor
823CA338 creates a declaration only while that global is zero. Their base
destructor823CA3A8 releases it once if nonzero, then clears it. Later family
destructors observe zero. The source is an independently pinned36-byte record
at82061674, byte-identical to the original quad's position/UV declaration.
The complete source/caller/vtable evidence is in native-post-effect-catalog.md
and build/post-effect-catalog/original-disassembly.txt. The two base functions
cover49 original words; no SDK declaration layout is required by their caller.

EngineQuadDeclarations now owns a separate native logical token for this
global. A single call-site hook at823CA378 consumes the exact original source,
validates the base vtable82061698, original globals base and incoming LR, and
returns a fresh unmapped ID. It advances LR to823CA37C, preserving the original
BL result. Original code publishes the global and retains all five typed CPU
objects. Other users of824458E0 remain unchanged.

The native CPU declaration registry may share immutable bytes with the quad's
separate AC-field declaration. Each has its own logical reference and native
ID. Release checks the complete set of native logical owners, so either table
can be cleaned first without retiring the other's reference. Cache snapshots
can retain immutable metadata after logical release; retired native IDs cannot
be queried. This cache policy does not invent a GPU input layout or a bind.

The existing graphics-resource release dispatcher recognizes this ID. At the
original823CA3DC release call, it verifies LR823CA3E0, the current base object,
the globals base and the published ID, then retires one logical reference.
Original code clears82D099A0. Additional declarations, retained guest aliases,
binding and drawing remain unqualified.

Build153 passes all58 suites in85.38 seconds. The combined whole-catalog
lifecycle fixture passes16,006 checks: original first25 plus second24 against
the live shared pool, both cleanup orders, immutable record sharing with
distinct native logical references, unchanged edited values and stale-ID
rejection. The original first25 fixture still passes18,441 checks. Actual
muted boot095 creates this declaration as00F00010 and completes all49 effect
registrations, stopping at the guarded manager finalizer826B7218. The shared
test helpers were extracted unchanged from the prior first25 fixture.
Normal shadows camera/raster
cleanup remains the separately documented production gap; it must not be
reported as repaired by these declaration or FX lifetime checks.
