# Native application sampler bridge

The original application dispatcher `82723C80` now runs its AOT CPU instructions
for cache suppression, saved-register restoration and state-stack dirty bits.
Only its SDK lookup/call block `82723CD8..82723D04` is replaced, resuming at
`82723D08`. The native context remains an unmapped identity. There is no SDK
device memory or GPU command-stream implementation.

Evidence and exact original equations are in
[native-application-samplers.md](native-application-samplers.md) and its pinned
JSON report. The runtime now supports the sixteen application sampler stages.
RenderWare still owns its original eight-stage cache. Native stages 8..15
inherit SDK defaults, including point minification/magnification; the first
eight inherit the verified RenderWare overrides. Neither stage count implies
support for SDK stages 16..25 or additional texture bindings.

## Ownership and publication

Entry arguments remain `{r3=owner,r4=stage,r5=selector,r6=value,low8(r7)=force}`.
The live driver accepts the original static object `82D5DB78` and preflights its
full `41D4`-byte extent. The entry observer validates the original selector and
SDK setter tables, all twenty categories and their inverse mapping, count 20,
the requested native value and all cache/frame addresses. Negative depths and
depths beyond eight reject as corrupt state.

The mid-block callback rechecks the original saved registers and derived cache
offsets before applying a temporary copy of the native effective owner. On
success it restores the proven call continuation, context identity and setter
identity. The original suffix writes its own cache and dirty words. Original
equal-value suppression and force-byte masking therefore execute unchanged.
Entry validation alone does not publish native state on the cache-hit path.

The separate RenderWare sampler cache at `82D0D170` is neither consulted nor
written. A matching RenderWare cache value cannot suppress an application
update. No native sampler update is permission to draw: resource, shader,
target and screen-state validation remain independent.

## Bounded state support

All twenty identified offsets now accept a bounded subset. U/V/W addressing,
border selector, minification, magnification, mip filter, LOD bias,
minimum/maximum mip and maximum anisotropy retain their earlier restrictions.
The separate Z magnification/minification/enable fields accept canonical
Booleans. Six other auxiliary fields accept only their original SDK baseline:
trilinear threshold, anisotropy bias, horizontal/vertical gradient bias and
white-border-W are zero; point-border-enable is one.

The effective two-bit volume filter follows the retained normal mag/min flags
when separate Z filtering is off, and the separate Z flags when it is on.
The screen gate rejects separate volume filtering and remains restricted to
its verified repeat/linear/base-map policy. No volume draw is implemented.

Six alternate entries are explicitly guarded because they bypass this single
selector bridge and still read or write SDK state: `827238B8`, `82723858`,
`82723AB0`, `82723B40`, `82724038`, and `82724230`. Their getter synchronization
or combined-update semantics need their own implementations.

## Verification

The original driver fixture executes the actual hooked dispatcher at all
sixteen stages. It checks nonvolatile registers, SP/LR, force values `100/101`,
forced equal updates after deliberate host/cache divergence, and set/clear of
the exact dirty bit at stages 0, 7, 8 and 15 in all eight original pushed frames.
Invalid owners, stage/selector/value bounds, appended/corrupt category maps,
invalid depths and alternate SDK entry paths reject without changing the
application or native state. The separate RenderWare cache remains byte-exact.

The state bridge fixture additionally executes original SDK setter/getter
pairs on isolated memory and compares every accepted bounded sampler value
at all sixteen stages with the native owner. Anisotropy bias has a meaningful
getter distinction: retained raw `+0` returns float `-0` after the original
getter's negation. The native raw-state query is not represented as an SDK
getter replacement. For all 32 normal/Z flag combinations at all sixteen stages,
every intermediate setter result is compared against the original effective
two-bit filter. This test memory is never used as the live graphics context.

Build074 passed all 23 test suites with the initial dispatcher tests. Boot047
now executes the complete original scalar/sampler passes and reaches the
camera-clear guard `823EE940` from `823F1BA8`. Latest aggregate validation is
recorded in local development logs; no original frame has been rendered.
