# EXm0 ownership implementation review

Read-only review, initially frozen against build119 inputs on 2026-09-10. **The one concurrency finding below is resolved in build120**, as recorded in the appended follow-up. The original review is retained for provenance. The constructor, original CPU allocation/free path, generation handling and guarded unconfigured-instance boundary otherwise match the bounded contract. Retained leases and complete normal engine shutdown have the limits described below.

Only this document was written. The actual public header is `runtime/engine_audio.h`; there is no separate `engine_audio_owners.h`. Reviewed `runtime/engine_audio_owners.cpp`, that header, `runtime/engine_audio.cpp`, the `NativeXmaFactory` addition, the EXm0 lifecycle fixture and the EXm0 configuration/generated hooks. Output/MMIO implementation is outside this review.

## Finding: P2 — protect the region-vector scan with vmMutex

[engine_audio_owners.cpp:194](K:/SimpsonsNativeCopy/runtime/engine_audio_owners.cpp:194) traverses `s.runtime.regions` while holding only the audio owner's mutex. Runtime mappings use a separate synchronization domain. [threads.cpp:15](K:/SimpsonsNativeCopy/runtime/threads.cpp:15) erases that vector during unmapping, under `vmMutex`; a normally exiting worker reaches it through `Runtime::unmap` at [threads.cpp:173](K:/SimpsonsNativeCopy/runtime/threads.cpp:173). Thread creation can also grow/reallocate the vector.

Thus an EXm0 constructor on one guest thread and a worker's exit or creation on another can race on the vector, invalidating the allocation scan's iterators and producing host undefined behavior. It does not require corrupted guest metadata. `State::mutex`, `audioMutex` and atomic page-access entries do not protect this vector.

**Bounded fix:** hold `runtime.vmMutex` while examining the region vector, or obtain a protected snapshot. No original CPU call needs to execute under this lock. Keep the existing audio transaction lock for the owner map. The relevant reviewed owner-source SHA-256 was `813464b4e4e94f01ff94a9ab69f204980c1cd58adec90412293925b890ad7d97`.

This is a source-level race finding, not a reproduced crash. The current lifecycle fixture exercises repeated real allocations on its caller thread; its passing result does not establish synchronization with a concurrent region mutation.

## Constructor and failure path: no additional defect established

Original bytes independently confirm the retained control flow:

- `823404A8` saves a `0xB0` frame, calls the original size query, then **82340730** at `82340530`, LR `82340534`. That helper invokes the current Q allocator and writes the original base vtable at `8234078C`. The allocation hook at **82340548** follows this genuine allocation/base initialization and precedes the generic owner/release/channel stores.
- `82340558..64` writes V+C=release, V+10=0, V.byte2E=channels and V+4=incoming r7. The indirect constructor call at **82340570**, LR **82340574**, is the exact scope checked by the native constructor.
- **8233E9C8** initializes V+0/+34/+3C/+44/+48/+4C and bytes54/55, and clears precisely the layer array. It leaves V+28/+38/+40/+50 and surrounding padding untouched. The native code preserves that footprint. Native layer+0 remains zero, and no native token occupies an original hardware-record pointer.
- After success, original **8234057C..82340680** initializes the remaining generic fields and clears only each queue slot's +0 and +C. The native implementation leaves this CPU body AOT.
- Original null allocation returns through **82340540 → 82340684**, without invoking a constructor or release. The creation transaction can terminate with address zero on this path.
- On a false constructor result, **82340628..30** invokes original **823402E8**. Its call at **82340310**, LR **82340314**, performs native retirement; its call at **8234035C**, LR **82340360**, performs the genuine original free. `destroyEnd` erases host bookkeeping only after that call and does not read the freed V.

The original resource-borrow failure path was also inspected. Successful borrowed slots receive their channel byte only at **8233EAE8/8233EAF0**; a failed borrow branches around those stores at **8233EA70..8C**. In the bounded serialized admission model, the native failure writes the successful prefix's channel bytes and leaves unavailable slots zero. Capacity failure retains an exact failed-creation transaction so the original generic cleanup may run despite success-only fields being uninitialized. Pool pointers/list mutations are backend-private and are intentionally not copied.

The real original release body assumes original pool records, including during the legacy failure path; running it against zero native layer pointers would be wrong. The complete native release replacement is required. This review does not qualify the old SDK pool's own partial-failure safety or arbitrary concurrent borrow interleavings.

Host allocation exceptions are a different path: preparing the native instance can throw after the real guest allocation exists. The transaction/record remains for terminal diagnosis; the implementation does not return false or claim that normal guest rollback happened. Native RAII disposes partially allocated host objects. Normal capacity failure is tested; host `bad_alloc` and an original allocator returning null are not injected by the reviewed fixture. A small follow-up fixture could force the original allocation callback's null result and require a clean subsequent retry, without substituting a successful allocation.

**Correction to the frozen bridge document:** its sentence “byte32=20h” is a notation error. Original **82340588 = `39200014`** loads **20 decimal (`0x14`)**, and **823405C0 = `993F0032`** stores it. The generic allocation adds **400 decimal (`0x190`)** for twenty `0x14`-byte slots. The implementation and fixture use the correct value. Incoming r6 is not used to choose this capacity.

## Shared helpers, Q and hook coverage

No blanket rejection of other codecs was found in the ordinary generic-helper paths. The entry at 823404A8 discriminates exact EXm0 descriptor D=`82D073AC`; allocation/end hooks return when no EXm0 transaction exists. Generic destruction and enqueue discriminate an owned record, EXm0 vtable `821DCAE0`, or EXm0 release callback `8233EC58`, then leave other valid codec objects to AOT. These common callbacks still validate the current runtime/context and mapped object header.

The production methods keep one creation/destruction scope per `PPCContext`. A nested other-codec allocation while that same context already owns an unfinished EXm0 transaction can be rejected by the later scope checks. No such original call was established in this bounded constructor/allocator path; it is not reported as a reached regression. Ordinary non-EXm0 generic-helper passthrough is worth adding to the fixture before broadening codec integration, because the current tests create only D instances.

The Q restriction is supported for the recovered caller: **8233DC24/28** copies `BE32[82E31BCC]` to S+4, and **82342B90 = `80FF0004`** passes that retained S+4 in r7 to **82342B98 → 823404A8**. Requiring r7 to equal current Q is a bounded live-root profile. It does not prove construction against an obsolete S after Q replacement; rejecting that profile is honest. Both original generic allocation and final free actually resolve `Q+14` through the global Q. The native record pins that allocator object and its release callback across construction/destruction.

Provider startup preserves the shared callback installation exactly: **8233E5FC..8233E61C** tests only Q+18; if null, it installs Q+18=`82339788` and Q+1C=`82339798`; otherwise both words remain unchanged. Native factory preparation precedes those guest writes. Hardware initialized/pool globals remain zero and are checked, rather than being erased or used as native readiness.

All **22 EXm0 ownership/guard hooks** were checked against original bytes and their intended fallthrough/entry-return roles. Generated `ppc_recomp.51.cpp` places the allocation and epilogue hooks before their pinned original instruction, preserving original LR and stack setup. Provider/constructor/release/enable/disable/stop replace whole entries; generic wrapper hooks retain the CPU bodies. Input is rejected at **8234E768 before queue stores**, and the separate virtual input/feeder/decode/reset/context/deleting-destructor entries remain guarded. No decoder-readiness or sample-completion success is fabricated.

The config grew from 122 to 123 hooks during review when the independently owned output guard arrived. Its byte pin was also checked, giving 23 audio-named pins at that snapshot; its output contract was not reviewed here.

## Generations, retained leases and shutdown limits

Generation IDs are process-wide monotonic 64-bit values, with explicit exhaustion. Address reuse receives a new generation. After release, new `view`/`lease` requests reject, and `destroyEnd` removes the record only after the original free. An already locked shared lease deliberately keeps the old native Instance alive without retaining or dereferencing V. The fixture tests this across real same-address allocation reuse and verifies destruction after the last retained reference is released.

There is a **normal-stop coverage limit**, not a demonstrated dangling-pointer defect: [stop:160](K:/SimpsonsNativeCopy/runtime/engine_audio_owners.cpp:160) checks records, active transactions and layer reservations, but does not track separately retained leases after their record has been erased. Therefore `create → lock lease → destroy V → stop` can close the factory while that retired Instance remains alive. The reviewed fixture releases each held lease before testing stop/restart.

For the current milestone, that Instance owns immutable generation/channel metadata, an enabled flag and unconfigured layer slots; it has no guest source lease and no opened layer codec. It has no dependency on the factory's lifetime. `NativeXmaFactory` owns actual unopened XMA1/XMA2 capability contexts, validates versions and the raw option, and cleans partial acquisition with RAII. Its separately created codecs own their own opened contexts. No current use-after-free follows from a retained metadata lease outliving the factory.

Consequently, interpret current `stop()` as closing factory admission after guest ownership is retired, **not proof that every externally retained host object has been destroyed**. Before adding configured work/source leases, define and test retired-work admission and drain explicitly: either track/reject outstanding work at normal stop or prove that its retained ownership completes safely without the factory or guest allocation. Do not infer such a drain from `count()==0`.

Terminal cleanup remains distinct. Runtime first calls `stopThreads()`, which cancels and joins guest workers, then releases `engineAudio`, before freeing guest address space. The audio destructor reports outstanding transactions/records and releases host ownership without calling guest destructors. This is appropriate terminal cleanup; it is not an executed normal original global teardown. External inert leases may survive their registry, as above. Normal **823390AC → 8233E7E8** through the entire game's drain remains unverified by the isolated stop/restart calls.

## Verification and test interpretation

The frozen EXm0 document's read-only evidence block passed again: original image SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`, **19 pdata function hashes, seven leaves, twenty selected words, descriptor/vtable and 75 finite queue cases**. Targeted disassembly independently covered constructor failure, generic allocation/free, Q propagation and callback installation. No SDK body was executed for this review.

Read [build119's existing log](K:/SimpsonsNativeCopy/build/hundred-nineteenth-build.log): **31/31 suites passed, 43.85 seconds**. Its EXm0 lifecycle result was **61,102 checks and 106 actual allocations/frees**; the core codec test reported **662,151 checks**, including factory-created codec use. These tests validate the documented full-byte success layout, actual free ordering, partial/full capacity cleanup, generation reuse, guard immutability, active toggles and empty factory restart. The layout expectation is a fixture byte map checked here against original instructions; the test does not execute the replaced hardware constructor as a differential oracle.

The final fixture observes the real indirect Dac0 constructor guard at **823456D0**, LR **8233DC44**, and checks the unchanged `0x3108`-byte object and both SDK globals. The recorded owner/root/descriptor are **E4624C30 / E4627DE0 / 82D069B4**, matching main's boot071 report. It replaces the earlier temporary MMIO-failure observation. Only its guard/observation relationship was checked here, not the separately owned output contract. No production test binary, full build, output/MMIO probe or new original lifecycle was run by this read-only review.

Besides the region-scan race, no additional correctness defect was established within the current unconfigured, enqueue/decode-guarded scope. The actionable remaining test limits are original allocation-null recovery, ordinary other-codec helper passthrough, and stop with a separately retained lease under the explicitly chosen retirement policy.

## Build120 resolution and bounded CPU-only passthrough candidate

Follow-up frozen 2026-09-10. **The region-scan finding is resolved.** [engine_audio_owners.cpp:197](K:/SimpsonsNativeCopy/runtime/engine_audio_owners.cpp:197) now holds `runtime.vmMutex` only across the `Runtime::regions` traversal. The guard's braces release it before owner-map iteration and guest metadata publication. Exceptions also release it through RAII. No original CPU callback runs inside that scope.

The local lock order is `State::mutex → Runtime::vmMutex`. The service lookup's `audioMutex` has already been released before the owner method runs. The original allocator call has returned before `createAllocated` acquires either owner/VM lock; original free similarly executes after native release returns. Reviewed VM mapping/unmapping, thread creation/exit/reaping and TLS services do not acquire the audio owner mutex or invoke audio callbacks while holding `vmMutex`. No reverse lock-order edge was found in these paths. This is the bounded lock-order check, not a whole-runtime deadlock proof.

Read [build120's completed log](K:/SimpsonsNativeCopy/build/hundred-twentieth-build.log): **31/31 suites passed, 43.91 seconds**, with **78,088 EXm0 checks and 132 actual allocations/frees**. The extended fixture includes:

- A real host thread repeatedly mapping/unmapping 32 pages under the normal VM synchronization while the caller executes 24 real EXm0 lifetimes; it checks mapping progress and complete page cleanup.
- An explicitly injected null allocator return before any backing allocation, followed by successful original allocation and cleanup on retry. This introduces no fabricated successful pointer.
- A retired unconfigured metadata lease held across factory stop/restart, then released; stale-generation lookup remains rejected. This qualifies that limited policy, not future configured work or source-lease drain.
- Full-width GPR14..31 preservation assertions around original destruction, in addition to the existing constructor ABI checks.

The fixed owner-source SHA-256 is `44690273d99903aeb2f830a335d2cfd145e565ed4e4e9bf2341110db82a07966`; the build120 fixture SHA-256 is `fa59e2d9190dce18f2a1bbc822258db13d8bea9c12d535bedf4fe297dd737eb2`. No build or test binary was rerun by this review. The allocation-null and retained-metadata test gaps noted above are therefore closed for this bounded scope.

### P6B0: an ordinary registered descriptor with a leaf CPU constructor

There is a straightforward candidate; no broader codec scan is needed. Original startup **8281647C** calls provider **8233E1D8**, which returns literal **P=82D073CC**. **82816488** then calls the same original registry insertion helper **82340448**. This registration is after EXm0 and before the newly observed Dac0 boundary. Confirm exactly one node **P+10=82D073DC** in the existing runtime registry `BE32[Q+2C]`; do not synthesize a descriptor or alter its mutable link.

The exact descriptor words are:

```text
P+00 8233E1C8  size/alignment query
P+04 8233E1E8  constructor
P+08 00000000  no codec-specific release callback
P+0C 8233E210  decode callback (outside this fixture)
P+10 mutable registry link; zero in original image
P+14 50364230  literal tag "P6B0"
P+18 00000000  no optional generic output allocation
```

No broader format meaning is inferred from the tag. The three small functions are complete leaves:

```text
8233E1C8: r3 ignored; r4 points to a writable alignment word.
           stores BE32 0x10 at r4; returns r3=0x3C.
8233E1D8: no consumed argument; returns r3=82D073CC.
8233E1E8: r3=V; stores BE32 zero at V+34 and V+38; returns r3=1.
```

The constructor performs no calls, allocations, imports or device access. Its `8233E200` neighbor is not the descriptor's release callback: P+8 is **null**. Generic **823402E8** therefore branches directly past its callback at **82340308**, skips optional-buffer freeing because V+10 is zero, then performs the original Q allocator free at **8234035C**, LR **82340360**. Only the real generic instance allocation/free is needed.

Suggested bounded fixture sequence, using the current live Q and real registry:

1. Check the descriptor words except mutable P+10 and find P+10 exactly once in the original registry. Optionally call the original provider and require P without modifying the registry.
2. Invoke **823404A8** through `EngineCpuCalls` with `r3=registry, r4=P, r5=2, r6=0x14, r7=Q`. Incoming r3/r6 do not control the generic allocation; these values follow the existing calling convention. Use the actual Q allocator observation, not an EXm0-specific constructor substitute.
3. Require a real 16-byte-aligned V and **one `0x1D0`-byte allocation**: `align8(0x3C)+0x190`. Check V+0=`821DCAD8`, the base vtable installed by original82340730; V+34/V+38=0 from the P6B0 leaf; V+4=Q, V+8=V, V+C=0, V+10=0, V+14=`8233E210`, V+18=`50364230`, V+20=`0x1D0`, V+24=`0x40`, channel byte2E=2, capacity byte32=`0x14`, and byte33=0. Preserve untouched bytes, including the four-byte gap V+3C..3F. The original generic body initializes the same twenty slots at V+40, clearing only each slot's +0/+C.
4. Require native EXm0 count/reservations/readiness unchanged, and EXm0 `view(V)` rejected. Invoke **823402E8(V)** and observe exactly one real free. It must pass the generic native preflights without entering EXm0 construct/release. Check SP/LR and GPR14..31 as in the existing fixture.

This qualifies ordinary generic construction/destruction passthrough when implemented in main's fixture. It does not require enqueue, decode, virtual deleting destruction, audio output, or any new native service. None of those broader P6B0 operations is authorized by this construction/release evidence. The candidate is statically qualified here; its actual new fixture execution remains main-owned and was not part of build120.

Original-byte pins independently verified against the same image identity:

```text
8233E1C8 length10  396000103860003c916400004e800020
8233E1D8 length0C  3d6082d0386b73cc4e800020
8233E1E8 length18  7c6b1b783940000038600001914b0034914b00384e800020
82D073CC length1C 8233e1c88233e1e8000000008233e210000000005036423000000000
8281647C length10 4bb27d5d7c641b787fc3f3784bb29fc1
```

Lengths are hexadecimal. SHA-256 of the original 28-byte descriptor is `a7c540ce640b879c703e390518d0fffb857cc4068a7017dcd02995bae95c3309`; the constructor's 24-byte hash is `0a6ac755852395b6af81098579beec4aeb635bc3e21dfc1a9871a6f30e44ed59`. Generated AOT for these leaves matches the original words and contains no additional hooks. The current review has no remaining confirmed production defect in this bounded scope.
