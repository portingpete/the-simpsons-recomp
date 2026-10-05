# Native pipeline index owner

Status: focused implementation/tests complete; these four files are frozen. This
component owns only the additional native index buffer used by original
`82416C58/82416BC8`. It does not implement a plugin ctor, declarations, input
layouts, shaders, binding, rendering or driver readiness.

## Verified hook contract

Input: flat `analysis/simpsons.pe`, VA minus `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Existing offline SimpsonsDisasm.exe was used read-only; no regeneration.

Initializer `82416C58` writes cursor `82D507EC=0` at `82416C9C`. Then:

- Hook `82416CA0`, instruction `4802AD69` (BL `82441A08`), followed by
  `82416CA4: 907BFFF4` (store r3 at r27-12). r27=`82D507FC`, so output is
  `82D507F0`. Incoming r3=1FFFE byte length, r4=8 usage, r5=1 index format,
  r6=0 on the retained capability path. Source `82416C70..8C` proves arguments;
  r6 is `(capsWord & 0x10000) ? 0 : 2`, derived from caps+1C bit 10000.
  Clearing that capability would request r6=2; this bounded owner rejects that
  unverified pool mode. Native backing is a real Index16 buffer of 1FFFE bytes.
- Set r3 to a checked native owner ID and LR=`82416CA4`; configure jump_address
  `82416CA4`. Original result store, zero test, stack declaration stores and
  calls `823EF838` at `82416D6C/DDC/E30` continue untouched.
- Original creation calls the three declaration outputs F4/F8/FC using the
  already-proven 12-byte records, including their unwritten +B bytes. Keep
  EngineScratchResources (the general declaration bridge scope) alive through
  creation and cleanup. Do not duplicate declaration creation in this owner.
- The original initializer returns success after index success even if later
  declaration services returned zero. Existing declaration bridge exceptions
  must propagate, and partial cleanup must run before parent scope destruction.

Cleanup `82416BC8` checks F0 for zero, then:

- Hook `82416BEC`, instruction `4802AB1D` (BL `82441708`), followed by
  `82416BF0: 39600000` (r11=0) and `82416BF4: 917FFFFC` (store r11 at r31-4).
  r31=`82D507F4`; incoming r3 must be this scope's native F0 ID. Release the
  native owner's logical reference, return r3=0 and LR=`82416BF0`, and configure
  jump_address `82416BF0`. The original store clears F0.
- Original cleanup then calls engine declaration release `823EFA18` for FC/F8/F4
  at `82416C04/C1C/C34`, clearing each output. No SDK declaration release is
  needed at this level because the existing engine service bridge owns it.

Creation failure in the original zero-result path calls `82416BC8` at
`82416E40`. Native exceptions do not execute that branch automatically: the
parent must invoke original cleanup for already-published ownership while both
scopes and the checked EngineCpuCalls frame remain alive.

## Bounded SDK callback and CPU-side-effect review

Read-only review of the same original image found no material-style optional
creation-veto slot in index constructor `82441A08..82441AB0`. In particular,
there is no corresponding test/call of shader callbacks `82D51544/548`.
Its two allocation calls and the failure free are:

- `82441A28: 4BF4CE59` calls `8238E880(0x20, 0x64800000)` for a zeroed SDK
  header. `82441A74: 4BF4CE0D` calls the same wrapper with size `1FFFE`, flags
  `B2800000` for this usage=8 path. If the second fails, `82441A88: 4BF4D079`
  frees the header through `8238EB00`, flags `24800000`, and returns zero.
- Success writes header +0=`20100002` (resource kind 2), +4=1, +14=`FFFF0000`,
  +18=guest allocation address, +1C=size. Header +8/+C/+10 remain zero from
  allocation. These are replaced SDK ownership fields, not missing engine pool
  or list stores. The original constructor does not read r6; retaining the
  current r6=0 guard avoids expanding the caller contract based on that alone.

The allocation wrapper is not side-effect-free. It dispatches through the
allocator object at `82D57244`: vtable +0 for the header at `8238E9B4`, and
vtable +20 for physical backing at `8238EAB8`. Free dispatches vtable +4/+24
at `8238EB68/7C`. These are genuine guest allocation/free callbacks, not optional
graphics veto callbacks. `8238EAEC/EAF0` additionally write the last allocation
pointer/size to +4/+8 of the TLS block loaded from `[r13]`; `8238EB88` uses that
pair as an allocation-size cache. Native backing has no guest address. This
owner intentionally replaces those SDK allocations and their guest-heap/TLS
accounting with host ownership; it must not put a native ID into that cache or
invoke guest frees on it. Original engine CPU allocations remain separate and
continue through their original callbacks.

There is also a first-use allocator side effect: if `82D57244` is zero,
`8238E8A8/E8AC` calls `8268E7F0` and stores its result. That resolver gates
construction of static object `82D5724C` on `82D5726C` bit 0, calls
`8268E510`, calls `82A3CC28` with `82CC1468`, and calls `8274A828`, which publishes
the object at `82DFD8C4`. The native index hook does not reproduce this general
allocator initialization. If integration can reach this stage before the game
allocator is initialized, retain its original CPU initialization earlier or
fail explicitly on the zero allocator precondition; do not silently claim the
SDK constructor's lazy-init effects occurred. A nonzero allocator pointer is
normal and must not be rejected as though it were an optional shader veto.
The null-allocator fallback `824315E0/82431678` delegates to SDK memory services;
it adds no separate graphics creation-veto slot in those wrappers. Arbitrary
replacement allocator vtables are not certified by this bounded review.

Final SDK release `82441708` decrements +4 and calls `82441050` at zero. The
original jump-table byte `[8206A0A9]=0x30` selects index case `82441168`
(`0x824410A8 + 4*0x30`). That case calls `824574B8` at `82441184`, with r5=11,
only when header +8 and the context loaded via `[82000710]` are both nonzero.
It then frees backing and header through `8238EB00`. The fresh, unbound index
has +8=0, so this extra service is unreachable in the supported init/cleanup
sequence. Its wider binding/deferred-release contract has not been implemented.

Guard conclusion: no new material-style callback guard is justified for this
fresh native index lifetime. Preserve the existing exact ABI/owner/empty-field
checks and the restriction to init/unwind before SDK binding or locking. Do not
reuse these hooks for an already-used SDK resource. General allocator lazy
initialization, if still needed at the integration point, is the separate CPU
precondition above; the component is not a general `82441A08` replacement.

## Owner API and boundaries

`EnginePipelineResources(NativeBackend&)` creates the actual 1FFFE-byte Index16
buffer immediately, then installs its thread-local scope only after successful
allocation. It requires the active Runtime and backend owner thread. Nested
pipeline scopes fail without replacing the existing scope. Keep the object,
runtime and backend on that thread through original cleanup and destruction.

```cpp
// Existing EngineScratchResources scope must enclose all declaration calls.
Simpsons::EnginePipelineResources pipeline(backend);
cpu.invoke(0x82416C58);
pipeline.validateCreated(PPC_LOAD_U32(0x82D507F0));
// Later, including partial-start unwind, while both native scopes remain alive:
cpu.invoke(0x82416BC8);
pipeline.requireReleased();
```

`index(nativeId)` returns the live real `shared_ptr<Graphics::Buffer>` for native
work/tests. Independent retained references survive logical release; the original
ID stops resolving immediately. One create/release lifetime is supported per
scope; restart constructs a fresh owner/allocation. IDs are monotonic in the
checked unmapped range `00A00001..00AFFFFF`, disjoint from declaration B, scratch
C, dynamic D and material E ranges. Exhaustion or a guest mapping collision
fails explicitly. IDs are not SDK/COM layouts, guest addresses or device objects.

`requireUnowned()` is the partial-start/stop check: it accepts an unused Allocated
scope with real preallocated backing or a clean Released scope, while rejecting
Created state, a live native ID, or nonzero original F0. It also rejects backing
remaining in Released state. `requireReleased()` remains strict and rejects an
Allocated scope. Both retain the runtime/thread/scope and cancellation checks.

Creation checks exact arguments, r27 destination base, active runtime/base,
current callback context, capability bit and the original empty cursor/index/
three declaration fields. It prechecks the writable original output range and
publishes only r3/LR plus host ownership. It does not write the original globals.
Release checks the exact live ID, actual F0 value, r31 destination base, context
and writable original clear before releasing its reference. Only original AOT
performs the following stores and declaration lifecycle. Failed checks preserve
the host owner; cancellation propagates through checkRunning and checked reads.

`validateCreated` establishes native index ownership only. The enclosing
declaration bridge must establish/retain the three real declaration resources;
this owner neither adopts their IDs nor drains their registry. After cleanup,
check F4/F8/FC clears as appropriate; call the enclosing scratch scope's
requireReleased only after its other resources have also been cleaned up.

Native allocation failures and declaration exceptions must propagate. An unused
allocated scope releases backing during destruction. A scope destroyed after
publication but before original cleanup releases its host reference and logs an
explicit incomplete guest-cleanup diagnostic; it never fabricates cleared guest
fields or invokes original code through a potentially dead callback frame.
Original AOT exceptions may leave its guest register/stack frame partway through
the call; parent unwind must use an appropriate checked caller context. Real
shutdown must not be cleared merely to force original cleanup. No claim of
automatic successful guest rollback during cancellation is made.

The supported r6=0 path is current startup only. Pool mode 2, subsequent draws,
index-data endian conversion, buffer locks, shader/declaration binding and
plugin-wide completion remain unsupported. No CAF8, CB08 or lifecycle writes,
console GPU command decoding or runtime CPU interpretation are introduced.

## Exact parent hook configuration

Parent owns config and production source registration. Add
`runtime/engine_pipeline_resources.cpp` to `config/native_sources.json` and
these callsite hooks to its config (not applied by this task):

```toml
[[midasm_hook]]
address = 0x82416CA0
name = "SimpsonsNativePipelineIndexCreate"
registers = ["ctx", "base"]
jump_address = 0x82416CA4
evidence_hex = "4802ad69907bfff4"

[[midasm_hook]]
address = 0x82416BEC
name = "SimpsonsNativePipelineIndexRelease"
registers = ["ctx", "base"]
jump_address = 0x82416BF0
evidence_hex = "4802ab1d39600000"
```

Do not replace the whole initializer/cleanup or add SDK-wide success hooks.
The existing engine declaration entry hooks remain required. Actual plugin
040A constructor/pipeline allocation is parent work beyond this component.

## Validation and exact dependencies

Focused standalone ClangCL C++20 build passed `/W4 /WX /EHsc /MD
/clang:-mssse3`. WARP tests passed 9,953 checks using this production owner,
production EngineScratchResources declaration hooks and real DeclarationRegistry.
The test reserves/commits actual guest memory and enforces checked accesses.
It tests exact original instruction words/arguments, original following-store
boundaries, 1FFFE-byte native upload/readback, immutable declaration creation,
nested/wrong-thread/context/base rejection, readonly output, mapping collision,
unsupported capability, cancellation, double create/release, stale IDs, retained
native references, partial declaration failure after 0/1/2 successes, 32 repeated
lifetimes and an unrelated declaration surviving pipeline cleanup. No frame is
drawn/presented. Full image identity is pinned above; tests check image length
and 21 selected original evidence words, not the complete hash themselves.
Partial-start coverage also verifies requireUnowned accepts unused preallocation
and completed release, rejects Created even with F0 cleared, rejects foreign F0
in either accepted phase, and leaves requireReleased strict before creation.

The test has two deliberately distinct build modes:

- **Default parent mode** links SimpsonsRuntime and calls actual regenerated
  original `82416C58/82416BC8`, using the installed callsite hooks. This mode's
  translation unit also compiled cleanly. Parent reports build49 all 17 CTest
  suites passed, including this actual-AOT fixture, before requireUnowned was
  added. The added method/tests passed the focused standalone rerun; the updated
  parent-mode translation unit compiled cleanly again. No original plugin
  constructor is run by this test.
- **SIMPSONS_PIPELINE_RESOURCES_STANDALONE** replaces Runtime only with the small
  checked-memory fixture. It directly invokes production hooks and reproduces
  the following stores plus already-verified declaration inputs in test code.
  It is not proof that original AOT initialization executed. Only this mode was
  linked/run here; no parent build or AOT generation was performed.

Recommended parent actual-AOT test, after source/config integration:

```cmake
add_executable(EnginePipelineResourcesTests tests/test_engine_pipeline_resources.cpp)
target_link_libraries(EnginePipelineResourcesTests PRIVATE SimpsonsRuntime)
add_test(NAME OriginalPipelineResourceOwnership COMMAND EnginePipelineResourcesTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
```

Standalone target (do not link SimpsonsRuntime in this mode):

```cmake
add_executable(EnginePipelineResourcesStandaloneTests
  tests/test_engine_pipeline_resources.cpp
  runtime/engine_pipeline_resources.cpp runtime/engine_resources.cpp)
target_compile_definitions(EnginePipelineResourcesStandaloneTests PRIVATE
  SIMPSONS_PIPELINE_RESOURCES_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(EnginePipelineResourcesStandaloneTests PRIVATE /clang:-mssse3)
target_include_directories(EnginePipelineResourcesStandaloneTests PRIVATE
  "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
target_link_libraries(EnginePipelineResourcesStandaloneTests PRIVATE
  SimpsonsGraphics user32 bcrypt)
add_test(NAME NativePipelineResourceOwnership COMMAND EnginePipelineResourcesStandaloneTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
```

For direct standalone clang-cl compilation without the existing graphics library,
also compile `renderer/declaration_resources.cpp`, `native_backend.cpp`,
`screen_pipeline.cpp`, `depth_resources.cpp`, and `material_resources.cpp`; add
the existing `build/shaders` include directory and link `d3d11.lib dxgi.lib
user32.lib bcrypt.lib`. All these dependencies were read/compiled, not modified.
Run either executable with `K:/SimpsonsNativeCopy/analysis/simpsons.pe`.

Only `runtime/engine_pipeline_resources.h/.cpp`,
`tests/test_engine_pipeline_resources.cpp` and this doc were written. Test
artifacts are under TEMP; parent runtime/config/CMake, original AOT, prior owners,
reference projects and original files were not changed.
