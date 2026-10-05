# Native dynamic buffer startup and cleanup

Status: bounded implementation complete; focused build/tests passed; files frozen. Scope:
first-start four-buffer capability path and its matching cleanup, real original
CPU pools/callbacks and native StartupResources vertex backing. No rendering,
buffer locks/growth, console device substitute or driver-ready return.

## Entry and original ABI proof

Input is flat `analysis/simpsons.pe`, VA minus `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The existing offline SimpsonsDisasm.exe was read/executed without regeneration.
All guest field values are big-endian.

Start is `823FCF60`, ending at `823FD004`. **Stop is `823FCD58`**, ending at
`823FCF58`; `823FCEE0` is a compare inside stop, not a function entry. Start
calls stop at `823FCF74`; driver stop calls `823FCD58` independently.

Start then invokes original CPU pool creator `823FBEA8` four times with
`r3=elementSize,r4=count,r5=4,r6=00040411`:

- `823FCF90`: (16,16), stores pool at `82D0D0C8`.
- `823FCFB0`: (20,100), stores pool at `82D0D0CC`.
- `823FCFC8`: (8,100), stores pool at `82D0D0D0`.
- `823FCFE0`: (20,42), stores pool at `82D0D0D8`.

`823FBEA8` is a CPU wrapper: r8=r6, r7=0, r6=1, then tail-call `823FBD20`.
Keep the real original allocations and allocator callbacks, not host-made pool
headers. The original caller does not check pool creation failures; native
startup must explicitly fail and unwind completed ownership instead.

`823FCFE8 -> 823FCAD8` sets `82D0D0DC=0`. With caps `82E3DFA0+1C` bit 10000
set, it loops four times: cursor `D0E0+4*i=0`, size `D0F0+4*i=40000`, and
`823FC6D8(r3=40000,r4=D100+4*i)`. If the bit is clear, the original chooses
one buffer and clears the other three slots/sizes/cursors. This assignment
supports only the observed four-buffer path, rejecting the other path explicitly.

`823FC6D8` manages a linked list at `82D0D0D4`; records are 20 bytes:
`+00=size,+04=inUse,+08=resource,+0C=ownerOutputAddress,+10=next`.
With an empty startup list it calls `E+138(pool=D0D8,hint=00040411)` at
`823FC79C`, links the new record at the head, then calls SDK vertex creation
at `823FC7CC` with `(size,8,capabilityDerivedPool)`. On the verified capability
path that pool argument is zero. The native implementation replaces only that
resource ownership with references to the four real 40000-byte StartupResources
VBs; it preserves the original record allocator, fields, output slots and order.
Start returns 1 unconditionally at `823FCFEC`, even if reservation failed; native
API success must instead require every real pool, record and buffer owner.

## Stop order

`823FCD58` sets D0DC=0 and iterates four slots: clears size/cursor, finds each
slot's resource in D0D4 and clears record.inUse/ownerOutput, then clears the slot.
It traverses D0D4 in head order (reverse startup allocation), releases each
resource at `823FCDFC`, zeros record.resource, clears any remaining owner output,
and invokes `E+13C(r3=poolD0D8,r4=record)` at `823FCE28`. Head advances at
`823FCE34`. It destroys poolD0D8 via `823FB708` at `823FCE48`, then zeros D0D8.

There is a separate CPU mesh ownership list at D0C4. Stop drains its CC/D0 child
records and C8 root records (and may release extra SDK resources), then destroys
pools in D0,CC,C8 order at `823FCF18/30/48`, zeroing each slot. This list is empty
in the bounded startup scope. A nonempty/foreign list requires another verified
owner contract and must be rejected before this component mutates/frees it.

The component reuses the parent's `runtime/engine_cpu_calls.h`, namespace
Simpsons, without duplicating or modifying that utility. It checks active runtime,
base, owner thread and currentContext against its registers() context. Before
each original callback, LR is set to the verified return PC for that original
call: create pools CF94/CFB4/CFCC/CFE4, allocate record `823FC7A0`, free record
`823FCE2C`, destroy pools `823FCE4C/823FCF1C/823FCF34/823FCF4C`. The shared
utility supplies the checked guest frame/backchain and restores the parent TLS
context. Its temporary registers isolate caller-clobbered values from incoming
PPCContext; real original memory/allocator effects still use the same runtime.

## Implemented owner and integration

Public API, in `runtime/engine_dynamic_buffers.h`:

```cpp
Simpsons::EngineDynamicBuffers dynamic;
dynamic.start(runtime, cpu, base, startupResources);
// dynamic.initialized() describes only this resource component, not the driver.
// Guest D100..D10C now hold checked native IDs backed by the corresponding VBs.
dynamic.stop(runtime, cpu, base);
```

Both start/stop take `Runtime&, EngineCpuCalls&, uint8_t*`; start additionally
takes `const Graphics::StartupResources&`. `buffer(id)` returns a real owned
`shared_ptr<Graphics::Buffer>` only for a currently initialized ID. No buffer
locks, write conversion, binding or drawing service is implemented by this API.
Parent must not run original `823FCF60`/`823FCD58` over these native owners.
The equivalent native service calls only original CPU `823FBEA8`, `823FB708`
and the current E+138/+13C callbacks, retaining actual guest pool allocations.

Start requires empty D0C4/D0D4 ownership lists, zero original pool fields and
zero VB output slots. It rejects a second start before stop, foreign ownership,
missing callbacks, the unsupported single-buffer capability path, missing or
aliased native buffers, wrong runtime/context/base and wrong owner thread before
allocating. It clears stale cursor/size/current-index words exactly as the
original initial stop would on that empty ownership state. No other pools or
lists are fabricated, skipped as successful, or implicitly adopted.

Four distinct real Vertex buffers of exactly 40000 bytes are retained from the
bundle. Native IDs occupy a checked, process-wide monotonic range
`00D00001..00DFFFFF`, disjoint from the parent's Bxxxxx declaration and Cxxxxx
scratch-index IDs. They are never reused, even after rollback; exhaustion or a
mapped guest page at a selected ID fails explicitly. This is native owner
identity, not a guest SDK/COM object. All remaining original SDK consumers,
including dynamic reserve/growth/lock/recreate functions, require guards or later
native implementation. This component does not alter CAF8, CB08 or engine state.

The four CPU records are allocated through original E+138 and inserted at D0D4
head in slot order 0..3. All final original fields are preserved. Native buffer
creation is already complete in StartupResources, so there is no SDK allocation
call, placeholder success or console resource memory. Creation publishes each
record/slot only after its writable ranges are checked. Pool failures returning
zero are detected, unlike the original unchecked code.

Stop verifies the entire owned pool/list/output graph before any release. The
unsupported D0C4 mesh list, foreign pools, reordered/cyclic links, altered record
fields or output identities cause explicit failure without draining unknown
ownership. It performs original array/record detachment, drops each native VB
reference and returns records through E+13C in reverse order. Pools are destroyed
through the original function in D8,D0,CC,C8 order, clearing their guest slots.
The bundle or an independent host owner can keep a real VB alive after this
logical release; stale IDs cannot retrieve it. Inactive stop is idempotent.

## Failure, lifetime and cancellation contract

Startup exceptions trigger cleanup of the recorded partial ownership. Callbacks
and publication are ordered so zero/throwing allocation before side effects,
failure checking a newly allocated record's writable range, and partial buffer
setup unwind completed pools/records. A failed cleanup before a callback's side
effects leaves a tracked cleanup phase, allowing stop to resume without repeating
completed frees. Normal success requires all four records and a final
checkRunning(), including cancellation after the fourth allocation callback.

Cancellation is never converted into successful cleanup or ignored. All entry,
allocation and cleanup stages honor Runtime::checkRunning; original callback
memory checks also honor cancellation. If shutdown prevents original cleanup,
startup reports both its original failure and incomplete rollback and retains
ownership state. A fixture clears stopping only to test later cleanup; production
must not clear a real shutdown request to force guest execution. This service
cannot guarantee unwind of an original callback that throws after unreported
internal allocation/free effects; no such effects are guessed or silently
repeated. Such failure requires the parent's fatal runtime teardown policy.

Caller must keep Runtime and an appropriate EngineCpuCalls scope alive through
explicit stop, and serialize all owner access on the constructing thread. The
destructor releases host shared references but does not call guest code through
a potentially dead callback frame; if guest ownership remains, it emits an
explicit incomplete-stop diagnostic. Parent must stop the component before
throwing its next intentional startup-boundary Failure and before destroying
the caller/runtime. A start exception already attempts that partial unwind;
`hasOwnership()` exposes whether it remains incomplete. This is deliberate
callback-lifetime handling, not an implicit successful guest cleanup.

## Validation and parent integration commands

`tests/test_engine_dynamic_buffers.cpp` compiles production dynamic ownership,
the shared caller utility, and the real graphics implementation. It supplies a
small Runtime fixture with actual reserved/committed native guest memory and
checked PPC accesses. Native AOT fixture functions enforce the exact pool/callback
arguments, LR/frame/backchain, reverse free order and guest field effects; they
allocate real guest-addressable pool/record storage and poison released records.
They are **not the original game's internal pool implementation**. The parent's
next actual boot is the integration test for those original AOT functions.

The focused ClangCL C++20 build passed `/W4 /WX /EHsc /MD /clang:-mssse3` and
the executable passed with genuine WARP backing. It checks four independent
40000-byte native VBs by full upload/readback; retained references after bundle
reset/stop; stale IDs and repeated starts; every guest pool/list/slot/cursor/size;
preflight rejection without mutation; TLS/caller register isolation; 16 zero/
throwing allocation cases (four pools + four records); eight stop callback
interruptions with retry; one post-allocation mapping-check rollback; cancellation
after allocations 1/5/8; and 64 additional start/stop cycles. No frames are drawn
or presented. With the image argument, exact selected original ABI/callsite words
are also checked. Full image hash is pinned above and remains parent/loader
responsibility. No parent build, AOT regeneration or original execution was run.

Suggested parent integration (not applied to parent files here): register
`runtime/engine_dynamic_buffers.cpp` in `config/native_sources.json`; include the
new header from engine_driver and bracket the native dynamic stage with explicit
start/stop while the shared CPU caller and StartupResources remain alive.
Do not execute the original mixed stop as an additional cleanup.

```cmake
add_executable(EngineDynamicBuffersTests
  tests/test_engine_dynamic_buffers.cpp runtime/engine_dynamic_buffers.cpp)
target_compile_definitions(EngineDynamicBuffersTests PRIVATE
  SIMPSONS_DYNAMIC_BUFFERS_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(EngineDynamicBuffersTests PRIVATE
  "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
target_compile_options(EngineDynamicBuffersTests PRIVATE /clang:-mssse3)
target_link_libraries(EngineDynamicBuffersTests PRIVATE SimpsonsGraphics user32 bcrypt)
add_test(NAME NativeEngineDynamicBuffers COMMAND EngineDynamicBuffersTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
```

Run `EngineDynamicBuffersTests K:/SimpsonsNativeCopy/analysis/simpsons.pe`.
Standalone output was created under TEMP, not build/generated. Only the new
header, implementation, test and this doc were written. No parent runtime,
config/CMake, old bundle, material/plugin owner or reference files were edited.
