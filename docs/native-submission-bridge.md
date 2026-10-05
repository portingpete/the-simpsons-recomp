# Native post-start backend registration

Boot033 completes the original post-start cache reset and both original memory
allocations, registers the actual native D3D11 backend, then enters original
application initialization. It stops at the first unimplemented raster callback:
`823F7070`, LR `824081C0`, flags 2, 1280x720, depth field 0, extension offset 34.
No original pixels or playable milestone are established.

## Boundary and original CPU work

The engine getter `823EE8F8` retains original `823EDD38` through a checked ABI
caller. It returns an opaque identity for the existing native backend. IDs use
`00900001..009FFFFF`, are never reused and cannot overlap mapped guest memory.
They have no SDK layout. Native context lookup checks the runtime, owner thread,
active lifecycle and exact identity. The actual console device field `CAF8`
remains zero.

Original `82875DA0/DE8` allocation callbacks, protection effects and output stores
remain AOT. Only the BL at `82875E20` is replaced, continuing at `82875E24` with
the original r30 and caller frame intact. Native registration validates:

- Exact six-word descriptor `{0,20000,first,600000,second,0}`, matching real
  original globals `82E0759C/A0`, alignment and writable extents.
- Original allocator object/vtable and the proven allocation/free entries.
- Read/write/cache protection 404 on every page, nonoverlapping physical ranges,
  and the separate 600000-byte, 64 KiB-page physical allocation with its original
  `20000404` request.
- A completed original engine/plugin start, empty writable integration aliases,
  no previous registration on this driver and no overlap with retained records.
- The actual D3D11 immediate context, its owning device and device availability.

D3D11 device creation supplies native submission machinery. This registration
does not create a console ring, reproduce SDK fields, call a console kernel
submission import or inspect/copy/interpret payload words. The two original
allocations preserve original CPU accounting as compatibility reservations.

The following original stores still execute: `82875E2C` publishes the native
identity at `82D5DA74`; `826B78F0 -> 82723968` publishes it at `82D6D890`.
Preserving nonnull identity checks does not expose an SDK object. Known unported
application graphics consumers and SDK callback registration paths fail
explicitly. Remaining unknown consumers still encounter checked, unmapped
identities rather than writable fake device storage.

Original `82867A48` remains intact. Its CPU object at `82D576A0`, timebase
initialization and camera construction now run before the raster guard. Loading
textures and its later service registrations have not yet been reached.

## Ownership and unsupported observers

`Runtime::graphicsStorage` records accepted reservations with their allocator,
free callback, context identity, addresses, physical ranges and sizes. Original
heap/physical allocation records remain authoritative even when validation
fails. Registration does not free, rewrite or hide those original allocations.

The bounded original investigation established no matching early free for the
caller-supplied buffers. They remain occupied until runtime address-space
teardown. This is an explicit native lifetime policy; faithful application
restart/reclamation for these reservations remains unverified. Native driver
retirement invalidates its borrowed aliases but does not invent an earlier
guest allocator free. Original normal application shutdown already clears the
aliases before driver stop.

Console submission can invoke optional SDK observers. Their registration is
unsupported and guarded at `82461500`, `82460D38`, and `82460DC0`, before SDK
access or capture-side effects. General SDK configure `82458260` remains guarded
for every other caller. This is one verified engine callsite implementation,
not a generic successful SDK replacement. The evidence and callback qualification
are in native-poststart-integration.md.

## Verification

Build056 tested the native callsite hook directly at the previous SDK stop,
including exact return/LR, unchanged arbitrary payload words at both ends of
both original allocations, invalid descriptors, identity checks and duplicate
rejection. Build057 connected the actual AOT callsite. Build058 passes all 21
suites and the real original driver fixture passes 207 checks.

The fixture now observes the first raster guard, checks actual application
initialization and both original identity publications, then exercises original
engine destructor/stop/close/reopen APIs. It covers stale identities, partial
start rollback, callback-registration rejection, reservation retention, alias
invalidation, invalid storage descriptors and rejected reuse by another driver.
These are engine lifecycle tests after a bounded partial application startup;
they do not certify full application teardown, restart, drawing or gameplay.

Build059 tightens all three SDK observer guard checks: each requires the intended
`Failure` diagnostic and exact guarded entry, so an accidental original memory
fault cannot satisfy rejection. The material bridge checks its three SDK guards
the same way (210 checks). All 21 suites still pass. Production code is unchanged
from build058 / boot033.
