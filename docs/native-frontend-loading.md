# Original frontend loading and CPU state

Native readonly file opens now preserve FILE_NO_INTERMEDIATE_BUFFERING (bit8).
The original frontend loader requests0x68 at82B75190. Windows performs aligned
I/O directly into checked original memory, including native length/offset/buffer
errors, partial EOF and file-position changes. Both guest open entry points are
covered by the native filesystem test. Original data and write times are preserved.
See `docs/filesystem.md` for the host alignment constraint and primary contracts.

The earlier null-object read is no longer the first failure. Actual boot128 reaches
the frontend's retained RenderWare state query82401260, caller826D47B0. Boot129
passes that query and reaches the state setter824025A8. Boot130 passes the newly
qualified frontend setters and reaches the actual immediate draw82409308,
caller826D5760. No UI object, resource lookup, loader status or draw success is
fabricated. Original loading artwork continues to render while assets load.
Boot131 still observes UI global82D090F0 as zero at the guarded draw. This does
not establish completed bundle loading or successful UI construction; that
later original path remains to be verified.

The complete query is136 original words plus its30-byte switch table. It has no
device calls and now runs unchanged as AOT code. Tests cover all30 selectors,
the coupled U/V result, float field, invalid-selector behavior and caller ABI.
Queries return the original retained CPU requests, which can differ from native
effective state changed by another layer.

The existing setter preflight now admits five more CPU paths: depth test6,
source/destination blend10/11, vertex alpha12 and alpha reference30. Every
dispatcher/helper instruction remains original AOT. The native preflight checks
the selected original table route, conversion indices, cached Boolean fields,
queue membership/capacity and writable extents. Original code owns normalization,
cached-value suppression, pending writes, dirty insertion order and return ABI.
The native-effective state and applied CPU words remain unchanged until the
original commit. Existing depth-write8, fog14 and null-raster1 behavior remains.

The 15,817-check frontend setter fixture compares all CPU cache bytes, complete
pending queue order, original prologue/epilogue and unchanged native state. It
covers every blend-table value, depth coupling, vertex/texture alpha coupling,
all256 alpha-reference values, noncanonical true values, existing dirty entries,
last-slot/full-queue behavior, malformed inputs and unchanged state on rejection.
The existing real startup/stop/close/reopen fixture now passes45,794 checks.

The new immediate draw is still guarded. Its actual caller provides four
28-byte XYZRHW/color/UV vertices for an original triangle strip. Qualifying its
resource ownership, original shader schedules, transforms, raster/depth/blend
state and lifecycle is the next rendering task. This progress does not verify a
complete menu, world, gameplay, progression or save/load.
In boot131, all four XY positions and colors are zero, Z/RHW are1, and UVs
span0..1. Degenerate geometry is not permission to omit the original buffer,
binding, state and lifecycle effects. The boundary remains rejected.

Evidence: `build/unbuffered-assets/evidence.json`,
`render-state-query-evidence.json`, `frontend-state-evidence.json` and the
word-checked original listings alongside them. Build/run hashes and final
regression results are recorded separately in the milestone summary.
