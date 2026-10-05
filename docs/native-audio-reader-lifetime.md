# Original audio reader allocation and lifetime

**Executed-fixture correction:** The later
[reader fixture](native-audio-reader-lifecycle-test.md) supersedes the
Q48/default-Q4C fixture-lock prescription below. The live Dac worker holds Q48
for its entire lifetime. Use original root wrappers 823392C8/823392F0 and the
installed Q40/Q44 mutant callbacks; retire through the actual 8233D950 command
enqueue. The successful test verifies 34 real allocations/frees and normal
root/worker teardown without acquiring Q48.

Frozen bounded evidence, 2026-09-10. **The streamed EXm0 ring belongs to an original reader-group allocation G, not to its manager M or codec V.** `8233D5F8` allocates G, its reader records and all ring slices in one call. `8233D520` destroys each manager before freeing G through the matching saved/default allocator. This closes the allocator-origin question in the [admission contract](K:/SimpsonsNativeCopy/docs/native-exm0-admission.md).

A copy lease must cover **both** individual release `8238D640` and bulk reset `8238D480`; the latter can change even a claimed node to state 2. Manager lock M+8 alone cannot supply this exclusion. A finite observer/gate contract follows; all original allocation, reader, queue and destruction bodies remain AOT. No runtime/config/test changes or live game execution are part of this evidence task.

Original bytes: `analysis/simpsons.pe`, base `82000000`, size `EC0000`, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461522c5d442083a0`. Addresses/offsets/sizes are hexadecimal unless marked decimal. Register meanings below are entry/call-site meanings, not inferred decompiler types.

## Actual startup allocation, including the entire borrowed extent

Original startup `82816804: 4BB26DF5` calls `8233D5F8` with:

```text
r3=2EA8FB98 group identifier
r4=4 reader count n
r5=64000 bytes per ring R
r6=A => manager entry count = r6+3 = D (13 decimal)
r7=app[348]=Q, the real audio root
r8=app[330], the actual allocator adapter
r9=1000, not consumed by the reviewed constructor
```

The return G is stored at app+338 at `8281680C: 907F0338`. Do not misinterpret the caller's r9 as a ring alignment or accepted chunk-size argument. `8233D5F8` always gives its manager factory chunkLimit=0 on this path.

For positive, nonoverflowing counts and a 16-byte-aligned real allocation:

```text
extent = n*R + ((20*n + 3F) & ~F)
records = (G+33)&~7 = G+30
ring0 = align16(records + 20*n)
ring[i] = ring0 + i*R, length R
G+0=Q; G+4=records; G+18=allocatorOverride; G+1C=identifier
G.byte20=n; G+24 is the original global-list link
record[i]+14=H[i]; H[i]+4=M[i]
```

Thus this startup allocates **1900B0 bytes**. Records start G+30; the four rings start G+B0, G+640B0, G+C80B0 and G+12C0B0, ending exactly G+1900B0. They are slices of the same heap allocation, with no separate per-ring physical allocation/free. Preserve guest aperture/address identity; do not canonicalize an unrelated alias into ownership.

`8233D618/61C/638` computes the request; `8233D640: 480004A9` calls `8233DAE8(Q,&stackOut,0,extent,16,allocatorOverride)`. That helper uses r8 when nonzero, otherwise Q+14 (`8233DB10..18`), and invokes allocator vtable+4 at **8233DB3C**, LR **8233DB40**, with `(adapter,extent,0,1,16,0)`. The real return is written to stackOut at `8233DB44`. The group receives it at `8233D644`; nothing native should substitute this allocation.

The verified application adapter is `app[330]`, also retained in Q+14 by original root initialization. Its vtable `8215CA2C` forwards aligned allocation through `8274B140` to backing=adapter[4], backing vtable+0, with `{2,16,0}` allocation options; free uses `8274B1A0` to backing vtable+4. Record the actual adapter/backing and callbacks at allocation; do not invent which physical arena underlies the backing heap. Exact G base/extent and the original matching allocator are sufficient for this ring lease.

Each iteration calls **8238CD08** at `8233D6C4: 4804F645` with `(entryCount=D,ring[i],64000,chunkLimit=0)`. It allocates M with size **218** using the separate reader-system allocator `BE32[82E36B94]`, vtable+8 at **8238CD48**, then calls `8238C710` at **8238CD6C**. The constructor receives `(M,D,ring[i],64000,0)`, allocates D entries of size138, and creates a real initial H (14 bytes) and filter record (10 bytes). Its computed default M+208 is **11000**, from rounded-up `(64000/6)` to 800 bytes.

M+64 is the original ring base; M+6C is its end. **M+68 is mutable**, not another immutable base: wrap handling writes it at `8238C5D4`. M+88 is the oldest protected payload boundary, M+8C the unpublished-parser cursor, M+90 the end of input currently present. The wrap copy at `8238C5DC` moves only `[M[8C],M[90])`, the unparsed suffix; it does not rewrite a claimed node's payload pointer. The writer leaves a gap before M+88 (`8238C548..618`). The first unreleased node, state0 or state1, stops reclamation (`8238C410..420`). Native copying therefore does not need to pause all normal file reads while the claim remains protected, provided reset/release/destruction cannot invalidate it.

The reader allocator's origin is separately explicit: `823226C0(config)` sets `82E36B94=config[4]` at `823226E8: 906B6B94`. Global reader shutdown `823227A8` clears it at `823227DC: 916A6B94`. Managers must be gone before that shutdown; snapshot this allocator at M creation and require it remains the same for original M/node/entry frees. It need not be assumed equal to G's allocator.

The sole direct initializer call is `827159CC ->823226C0`. `82715920` retains its incoming r4 allocator in r31, passes it as r5 to CPU config constructor `82323158` at `827159C4`, and that constructor writes config+4 at `82323164: 90A30004`. Thus the global comes from a real caller-supplied allocator, rather than an XMA hardware context or a guessed heap address. The fixture must verify this service/allocator already exists; `Runtime.load` plus mapped stack alone does not initialize it.

## Destruction and reuse are different operations

**Group retirement request `8233D980` takes r3=an 8-byte CPU command record whose +4 is G, not r3=G.** `8233D990: 83E30004` proves this. It unlinks G+24 from the list rooted at `82E37310`, invokes active record callbacks, then registers deferred cleanup through `82348058(Q+60,G+8,1)` at `8233DA6C`. It writes G+C=`8233D520`, G+10=G, and increments Q+F0. The callback/list operations and actual callbacks must remain original. An early Closing state on G prevents new admissions, but existing original source-release callbacks must remain able to drain it.

`8233D520(G)` first checks every H's M+70. **Any nonzero value returns without freeing** (`8233D550/558`). When all are zero, it calls `8238CE90(H)` for each record at `8233D588`, removes G+8 from Q+60 through `823480C0`, decrements Q+F0, chooses G+18 or Q+14, and invokes allocator vtable+C at **8233D5E8** with r4=G,r5=0. This is the single actual free that releases all borrowed rings. `8233D5EC` is both an early deferred-return target and the post-free continuation: an exit observer must know whether the free call was actually executed, and never read G after that call.

`8238CE90(H)` retains M=H[4], calls **8238D480(H)** at `8238CEA8`, yields while M+70==1 (`8238CEB0..BC`), sets M+0=0, and, when M+1AC is nonzero, retains original job/file cleanup `823220F0` and `82322530`. It then calls `8238CF20(M,1)` at `8238CF04`. The latter calls **8238CA18** at `8238CF34`; that body frees queued N nodes, filter records, H records and entry table through the real reader allocator, then zeroes ring/cursor fields. Finally **8238CF54** invokes reader allocator vtable+C to free M. Neither body frees G's borrowed ring. Retain this quiescence path; a native copy counter is not a replacement for original I/O/job completion.

**Handle reuse:** `8233D900(group,record)` decrements record+18. At zero it calls `8238D480(H)` (`8233D92C`) and clears record.byte1A; H and M remain allocated for reuse. New entries get a token from `8238BC38`: low8 is entry index, upper bits advance global `82E3426C` by100, with wrap-to-zero replaced by100. This original finite token can repeat eventually; it cannot replace a nonwrapping native generation.

**Bulk cancellation danger:** `8238D480` cancels/removes source entries, clears H pending-byte counts, accounts outstanding M+84 bytes, then walks M+58 and unconditionally writes **N+10=2** at **8238D52C: 914B0010**. It does not exempt state1 claims or acquire M+8 around that final walk. Individual `8238D640(H,B)` likewise sets **B+C=2 before its later M+8 lock** at `8238D660`. By comparison `8238D300(H,token)` only converts matching state0 nodes to2 (`8238D3A4..3EC`), leaving state1 claimed nodes alone. Keep it AOT; do not confuse it with the bulk reset.

## Finite capture and serialization contract

These hooks observe/gate and fall through. They do not replace an allocator, manufacture H/M, return success, or execute an SDK command. Scope group creation initially to the exact startup caller above; reject new ownership profiles until their enclosing allocation is associated. Untracked managers used by other subsystems are not claimed as safe EXm0 rings.

1. **G creation:** entry `8233D5F8` captures the argument tuple and nonwrapping group generation. Pre-call `8233DB3C` plus return `8233DB40`, only within the scoped helper from `8233D640`, records real allocator identity/request/return before CPU constructor writes. At `8233D644` associate the returned allocation with G. A simpler single post-helper capture there is sufficient when allocator request/identity is already observed elsewhere; its caller's saved r29=count, r26=ringBytes, r30=Q, r27=override remain available. Precompute overflow-checked slices before allocation or copying.
2. **M creation:** `8238CD6C` captures actual M in r3, count r4, base r5, extent r6, chunk r7, with containing G transaction. At **8233D6C8**, after the manager factory returns H, verify H[4]=M, M+64/+6C match the slice, and save H/M generations; record+14 is stored later at `8233D6DC`. Commit G Live at `8233D70C` once all managers exist; startup publication app+338 then stays AOT. Failed partial construction retains failure provenance; do not claim rollback that the original factory lacks.
3. **Claim operation:** bracket `8238D568(H)` entry and **8238D634** (all normal exits). Retain M's operation lifetime before its first dereference/lock, and on nonzero return register B=N+4 with the exact token, payload, byte extent and a fresh native claim sequence. Return0 creates no claim. The operation is not complete until after native registration; bulk reset/close must wait for it. This closes the race between the original state1 store and registration. Capture cleanup on exceptions in the native call scope; an unmatched operation must not turn into an endless close wait.
4. **Copy:** under a short native registry mutex, require G/M Live, matching reset epoch and a registered Claimed B; increment activeCopies, then release that mutex. Validate/copy only the exact nonwrapping claimed bytes within the recorded slice. The copy scope calls no AOT and holds no original critical section across its release. Its RAII completion decrements activeCopies and wakes native waiters even on cancellation. After this owned copy exists, codec processing must stop touching guest ring bytes.
5. **Individual release:** before the first instruction of `8238D640`, mark the registered claim Releasing, prevent new copies, and wait for existing copies to finish. Then drop the native mutex and allow the entire original body. Observe completion at **8238D6CC** using the saved call identity, without dereferencing B: its nested reclaimer may already have freed N. Remove that claim; retained PCM/codec receipts have separate lifetimes. Reject duplicate/stale registered releases explicitly rather than opening a second gate.
6. **Reset:** at entry **8238D480**, change M to Resetting (or retain Closing when nested under teardown), close new claim/copy admission, and drain in-flight claim operations/copies before any bulk invalidation. Let original reset run without the registry mutex. At **8238D54C**, invalidate old claims and advance the native reset epoch; return M to Live only for ordinary handle reuse. Pending original file work is not EOF or new native input. New claim records must use the new epoch and original new entry token.
7. **Close/free:** `8233D980` marks G Closing before active reader callbacks; `8238CE90` marks a registered M Closing before reset. Wait only for native copy/claim operations, not for callbacks while holding the registry mutex. Gate direct `8238CA18/8238CF20` on the same close scope; for a registered M, a bypass lacks original quiescence and must fail. Capture pre/post M free at **8238CF54/CF58** and G free at **8233D5E8/D5EC**, with generation/address/allocator held natively. Free-return observers never reread freed memory. Quarantine failed/exception-unwound operations; do not revive identities or silently certify successful original free.

Also preflight reader-system stop **823227A8**: for registered managers, require all M/G leases retired before the original global allocator/service is cleared. This is an owner dependency check, not a replacement of global shutdown. Allocation callbacks can be reentrant: no host registry mutex remains held while calling original functions or allocator/free callbacks. A same-context reset invoked recursively from inside an active copy should be rejected; waiting for that copy on its own thread would deadlock. The proposed copy scope itself contains no such callback.

Partial allocation is a concrete limitation of the original factory: after a null M allocation, `8238CD74` sets r3=0 and **8238CD78 still loads H from r3+4C**. The outer group loop has no failed-manager cleanup branch and not all record H fields would be initialized. Treat this as terminal failed construction with a recorded allocation prefix; do not invoke normal `8233D520` on uninitialized records or fabricate an H to continue. Ordinary successful group teardown is fully identified above; recovery from this original failure requires a separately implemented transaction.

Use a manager allocation generation, a reset epoch, and a claim sequence, all nonwrapping. A slot address/token pair alone fails both manager-address reuse and original token-wrap cases. M+8 may still be used by retained AOT for its lists, but is not the native lifetime gate. Native copied packets can outlive the original claim; they cannot authorize later guest reads. The per-V lease already exposed by `EngineAudioOwners` does not supply any of these reader lifetimes.

## Smallest actual-AOT fixture for main to integrate

Use existing `tests/test_dac_lifecycle.cpp` setup: `Runtime.load`, `initialize`, actual startup and its diagnostic observation at **828166FC**, after Q4C release and with the real muted Dac worker active. Save the initialized entry context for fresh `EngineCpuCalls`; do not resume the context unwound by the observation. This point is **before** the normal group creation at82816804, so the fixture owns only the group it subsequently creates.

Take the real Q48 critical section through original `82329720(Q[48],821CA430)` before temporarily acting as the CPU command executor. This excludes the Dac worker while publishing the group and retirement request; release through the original lock-counter/leave sequence (decrement the critical section's +20, then original `82CC28B4`) on every path. Never wait for worker completion or join while holding Q48 or Q4C.

In a fresh CPU context set r8 to the actual Q[14] adapter and invoke `8233D5F8(2EA8FB98,4,64000,A,Q)`; r8 is the sixth argument and must be set through `registers()`, since `EngineCpuCalls::invoke` has at most five explicit arguments. Its r3 return must be a **real** 1900B0-byte allocator result. Check all four M/H objects, exact ring slices, entry count D, default chunk11000, G global-list linkage, unchanged SP/LR/nonvolatile registers, and native captures. The managers are empty; no source file, invented node, SDK voice or fake successful reader operation is needed.

This direct CPU fixture invocation does not execute the application call site `82816804`; admit it only through an explicit test scope, keeping production caller restrictions intact. Alternatively extend the real startup observation to just after `8281680C` and use the G actually published there. Neither approach should forge an observed call-site claim from a manually assigned LR.

For cleanup provide a mapped **CPU command fixture** of eight bytes `{BE32(8233D980),BE32(G)}` and call the actual `8233D980(commandAddress)` while Q48 still excludes the worker. This is the verified command-record ABI, not a fabricated manager/root. The command can be released after the call: its retained deferred callback lives in G+8, not the temporary command. Require Q+F0 and the original registered callback effects, then release Q48 and let the real original worker run `8233D520`. Observe all four actual M frees and the final G free via forwarding allocator observers; do not poll G after its free. Use a bounded native event wait, then invoke the already tested original root destructor/join. A second create/retire cycle must get new native generations even if the allocator reuses addresses.

This fixture exercises real allocation, manager construction, deferred original teardown and enclosing ring free. A claim-copy race test additionally needs the actual original reader to produce a B; it must not synthesize state1 metadata as source proof. Once the bridge gates exist, pause a **native copy** after a real claim, invoke actual individual release and bulk reset on separate checked guest contexts, verify neither state2 store nor free occurs until the copy exits, then verify old-epoch copies reject. The empty-manager fixture is the smallest useful first gate and is not evidence of asynchronous file-read cancellation completion.

No AOT fixture was executed here. Original read/write I/O scheduling remains retained; the exact callbacks and shutdown calls above are evidence of the lifecycle to preserve, not a claim that inspecting M+70 alone proves every external job has returned.

## Frozen evidence and reproduction

[analyze.py](K:/SimpsonsNativeCopy/build/audio-reader-lifetime/analyze.py) and [report.json](K:/SimpsonsNativeCopy/build/audio-reader-lifetime/report.json) verify **96 original instruction words, 20 complete `.pdata` bodies, 17 host boundary checks**, and the reviewed direct-call sets. The checks independently derive the four slice offsets/total allocation/default chunk, reject wrapping/truncated host admission inputs, and demonstrate original token repetition. These are byte/arithmetic checks, not executed thread-race or allocator tests.

```powershell
python -B build/audio-reader-lifetime/analyze.py --self-test
python -B build/audio-reader-lifetime/analyze.py --verify
```

Only this document and `build/audio-reader-lifetime/analyze.py`, `report.json`, and `verification.txt` were written. No derived media, build, guest execution, original/reference edits, runtime/audio/config/test changes, or player investigation was performed. Main owns later hooks, production admission and the actual-AOT fixture.
