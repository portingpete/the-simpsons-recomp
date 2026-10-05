# Native material ownership

Frozen owner files: `renderer/material_resources.h`, `renderer/material_resources.cpp`, and `tests/test_material_resources.cpp`. This CPU resource service follows the opaque create/use/release evidence in `startup-shaders.md`; it contains no GPU interpreter, guest memory writes, renderer integration or SDK object emulation.

`originalMaterialIdentities()` exposes exactly four original screen records and sixteen startup records, pinned by VA, stage, exact size and SHA256. `MaterialRegistry::create(va, span)` accepts an exact record span, copies it, validates finite header/payload/code/prefix bounds and the full record hash, then publishes a distinct resource with one reference and capability **Uncompiled**. The runtime remains responsible for verifying its complete original image. Startup records have no recovered names or reflection table; the owner preserves that absence. Unknown header words and the payload prefix remain immutable original bytes, without invented semantics.

IDs contain a native-only 64-bit generation and 32-bit slot. Generations are process-wide, never recycled, and exhaustion fails. An ID cannot resolve in a different registry or after final release/reset; it is not a guest pointer, SDK object, COM pointer or truncatable guest token. The parent bridge must map guest owner/output slots to complete IDs. Each `create` is independent; `retain`/`release` manage explicit references. Borrowed record/artifact references require a live retained ID. Callers serialize registry access and keep any supplied PMR allocator alive for its lifetime.

`requireCompiled` rejects uncompiled or unsupported resources. `prepareForBind(id, compiler)` permits lazy compilation only through a caller-supplied adapter returning an owned `CompiledMaterial` with the matching original VA and stage. That adapter must create a real usable native shader before returning. This owner cannot certify the adapter's shader translation or driver result; it never creates an artifact itself or sets Compiled merely because original bytes were copied.

An explicit `UnsupportedMaterial` exception records capability **Unsupported** and the reason, then propagates failure; subsequent binds also fail without retrying compilation. Other exceptions, null results and mismatched artifacts leave the resource Uncompiled and release partial ownership, allowing an explicit retry. Release also works for resources that were never compiled. Allocation and validation failures preserve prior resources and publish no ID.

Compiler, diagnostic and backend teardown callbacks cannot reenter registry mutation. Final release invalidates its ID before destroying the artifact. Reset/destruction invalidate all IDs and set the live count to zero before any backend destructor runs. Backend destructors must obey their nonthrowing contract and must not try to mutate the owner. Directly destroying the registry from one of its own callbacks violates the caller's object-lifetime contract.

Original engine boundaries still matter: create `823EFB78`, release `823EFBD0`, and located bind entry `82445278` must be implemented or guarded by native services before these resources can reach them. The original binder dereferences SDK shader metadata, and original release accesses an SDK reference count. Neither can consume these IDs. An unsupported bind must stop explicitly rather than leave a guest cache claiming success.

## Validation and freeze

Standalone ClangCL C++20 `/W4 /WX /O2` compilation passed, then **10 test groups / 515 checks** passed against the supplied original `analysis/simpsons.pe`. Coverage includes all twenty actual record hashes/metadata, malformed framing and changed bytes, immutable source copies, reference limits, stale/foreign IDs, independent lifetimes, allocation failure at source-copy and slot-growth stages, partial compiler cleanup, null/wrong-stage/wrong-identity artifacts, unsupported first bind, and compiler/virtual-diagnostic/destructor reentry. A read-only review found two callback issues; both were fixed and the new regression groups passed. Test compiler artifacts are lifetime fixtures, not claimed GPU shaders. Temporary build products were removed.

SHA256 of the frozen files:

- Header: `a7959d8a6c3dc1029acaf8db293783e933c8c3d912d3f5c196c47a7b6913fd02`.
- Implementation: `85b76a26d621bf8c9d968fa930e2f19f81e7b5b108b6b442b0846c67769342c2`.
- Tests: `a92b2c8e5f590b67d5edb61e6399728d384eeb161d69e7ce0b3f3a8d369da995`.

Build the two `.cpp` files together with the workspace as an include path and C++20 enabled; run the resulting executable with the original flat image path as its only argument. Parent CMake/backend adapter integration is separate. No compilation, binding, render-state fidelity or original-frame success for the sixteen startup materials is implied by their successful CPU ownership.
