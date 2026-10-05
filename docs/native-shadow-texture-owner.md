# Native shadows texture owner

Build152 passes all57 suites. The implementation replaces the three resource allocations
inside original shadows constructor `827064C0`, plus their lock, unlock and
release entries. Original CPU construction, field publication, pixel writes,
typed registration and deletion remain ahead-of-time compiled original code.
No SDK texture header, device layout or GPU command stream is constructed.

The independently checked source contract is
[native-shadow-textures-contract.md](native-shadow-textures-contract.md).
`EngineShadowTextures` owns two separate1024-square native depth/stencil
resources and one32-square writable native RGBA8 texture. They are separate
from the two cameras' four native surfaces. The original O+F8 reference to the
driver's depth-copy role remains borrowed. Creation allocates actual D3D11
storage immediately, with no invented initial pixels.

The three exact constructor return addresses, register tuples, original r31
typed owner, vtable and creation order bound admission. Native identities come
from the driver's existing monotonically allocated target namespace; none is
guest-addressable. The original instructions publish O+F0, O+F4 and O+FC.
Later queries verify those fields against their host owner records. The
metadata does not claim that an allocated depth texture contains rendered data.

The border texture progresses through allocated, locked and uploaded states.
Its original level0/null-rectangle lock receives the real CPU data ABI:
256-byte pitch and a guest-writable pointer. The adapter owns8,192 bytes of
mapped runtime memory for this CPU staging lifetime. Its use of the existing
physical-memory allocator provides checked address translation and release;
the memory is never interpreted as console GPU storage or an SDK object.
Both output words and the complete extent are preflighted before publication.

The retained original caller clears all8,192 bytes and writes its32-square
white border. Unlock checks every visible and padding byte against that exact
procedural profile, then copies128 visible bytes from each256-byte row into
a4,096-byte owned upload. The native backend takes the bytes before returning.
Only then does the adapter free staging and mark the texture uploaded. It
stores no caller stack pointer. Color-texture access rejects an unuploaded
resource; no depth lock, extra mip, rectangle or second lock is admitted.

All four SDK entries are replaced together: `82440578`, `82440238`,
`8243E040` and `82441708`. Unsupported callers, kinds or lifetime states
fail explicitly. The original destructor's three release calls retire the
native texture IDs and owners; the original tail clears each object field.
Only the established initial reference is supported. Shader retention and
rendering cannot silently add aliases. Driver stop rejects remaining owners;
terminal cleanup releases host backing and any outstanding CPU staging while
reporting incomplete original cleanup.

The later SDK resolve entry `82455570` is explicitly guarded before its first
device/header access. Original manager finalization `826B7218` remains guarded
because typed reflection, annotations and parameter upload are not implemented.
No shadow sampling, depth resolve, general20e4 depth rounding, draw or visual
result is established by these resource lifetimes.

The complete25-row registration fixture is unchanged in scope. It now also
checks the three resource identities, independent backing, original upload
phase, actual GPU border pixels, paired texture releases and stale-ID rejection.
It explicitly accounts for the separate normal-shadows cleanup defect: the
original destructor leaves four camera raster associations and does not
establish release of the two CPU state extensions. It does not substitute the
isolated `82714220` cleanup helper or claim complete shadow teardown. The
writable-texture backend has separate WARP and hardware ownership/update tests.
The complete first25-effect fixture passes18,441 checks across two cycles;
writable texture tests pass306 checks each on WARP and hardware. Actual muted
boot094 creates and uploads these resources, completes the first25 registrations,
then stops at the second catalog's unqualified `simpsons_skin` source82006348.
No original game screen, shadow draw or gameplay is verified.
