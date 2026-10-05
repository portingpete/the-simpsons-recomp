# Original EXm0 allocation fixture prerequisite

Frozen bounded evidence, 2026-09-10. The initial investigation used the permitted documentation fallback while native EXm0 constructor/release hooks were absent. Original `823404A8` unconditionally invokes descriptor constructor `8233E9C8`; its failed-construction path also invokes release `8233EC58`. Both original callbacks use the hardware pool. Main subsequently implemented the native transaction and owns `tests/test_engine_audio.cpp`; the caller-owner section below supplies the remaining fixture argument proof. No test/runtime/config source or AOT dispatch slot was changed by this investigation.

All addresses and offsets below are hexadecimal; counts explicitly marked decimal. Evidence is the pinned `analysis/simpsons.pe`, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.

## Existing setup and the actual allocator route

`tests/test_engine_driver.cpp` constructs a real `Runtime`, loads the original image, calls `Runtime::initialize`, and runs original startup under `PresentObservation` to `FirstPresentObserved`. Its `tests/header/test_recording_owner_contract.h` already allocates actual guest storage through `8269BD70` and uses original pool construction/cleanup. The resource-only fixtures which merely call `Runtime::load` and map a stack do **not** establish the audio allocator root. A readable mapped address is insufficient allocator ownership evidence.

The application's later original audio startup establishes the required objects before reaching the guarded provider:

1. `8281627C..98` copies incoming configuration `r5+0C/+10` to application `r31+328/+32C`, rejects either null, and invokes each object's vtable `+8` callback.
2. `828162C4/C8` requests a real `0xC`-byte allocation through `8269BD70`. At `828162E0`, original `82810BB0(A, BE32[app+32C], 0)` constructs the adapter. `828162F0` stores A at `app+330`.
3. `82810BB0` retains its backing object through original `8274B1C8`: writes `A+4=backing` and invokes backing vtable `+8` when nonnull. It installs `A+0=8215CA2C`, `A+8=8215CA1C`. The intermediate base-vtable stores are also original CPU work.
4. `828162EC/F4` invokes `82338C98(A, 0x20000)`. This allocates a real `0x174`-byte audio root, constructs it, initializes its original critical sections and other CPU allocations, stores `Q+14=A` at `82338D24`, and publishes `82E31BCC=Q` at `82338D2C`. It also allocates Q+58 storage and Q+20 storage; it is not equivalent to writing just Q+14. Publication occurs before all remaining setup succeeds, so merely nonzero `82E31BCC` is not proof of completed initialization.
5. `8281642C` invokes original `82340A20(Q)` to create/obtain its codec registry at Q+2C. Three prior codecs are registered before the EXm0 provider call `8281646C`, LR `82816470`.

This establishes the preferred fixture observation point: reach the existing provider guard through actual startup, verify its exact entry/LR, retain the resulting Q, A and backing allocation, and use a clean caller context with `EngineCpuCalls` for subsequent bounded calls. Do not resume at an arbitrary PC using a context left inside an exception-unwound AOT call. The existing first-present fixture is earlier and must not be assumed to have Q ready.

For an independently initialized fixture, retain the same original adapter/root constructors **after real application allocator initialization**. Do not manually install `8215CA2C`, a bump allocator, or `Q+14`. The actual backing comes from the caller's configuration; this analysis does not identify it with `82D57244` by assumption. The global allocator's vtable is a different ABI.

## Exact allocate/free ABI

For the verified adapter vtable `8215CA2C`:

```text
+00 82810C40  deleting adapter destructor
+04 8274B140  aligned allocation forwarding
+08 8274B0E8  unaligned allocation forwarding
+0C 8274B1A0  free forwarding
```

`823404A8` consumes `r4=D`, `r5=channels`, `r7=owner`; its incoming r3/r6 are not used by the reviewed body. It queries D+0 with `(channels, writableAlignmentOut)` at `823404D0`. For actual D=`82D073AC`, the query returns `0x58+0x18*ceil(channels/2)` and alignment `0x10`. Preserve the actual engine owner supplied in r7; do not infer it from Q.

The allocation call at **82340530**, LR **82340534**, is:

```text
82340730(r3=Q, r4=stackOut, r5=0, r6=totalBytes, r7=alignment, r8=0)
```

`82340730` replaces zero totalBytes with `0x34` (the EXm0 profile never requests zero), reads Q+14=A, and invokes A.vtable+4 at **82340770**, LR **82340774**, with:

```text
r3=A, r4=totalBytes, r5=0, r6=1, r7=alignment, r8=0
```

Original `8274B140` forwards to `backing=A[4]`, vtable+0, retaining r4=size and supplying r5=&threeWordOptions `{2, alignment, 0}`. It returns the real allocation or zero. It does not consume the incoming r5/r6/r8 on this verified adapter path. `82340730` publishes the returned pointer to stackOut and, only if nonzero, writes the base vtable `821DCAD8` at V+0.

After constructor success/failure handling, paired generic destruction is **823402E8(r3=V)**:

- If V+C is nonnull, invoke it at **82340310**, LR **82340314**, before either free.
- If V+10 is nonnull, free that optional storage first at **8234033C**, LR **82340340**.
- Free V at **8234035C**, LR **82340360**. Both frees read current Q+14 and invoke its vtable+0C with `r3=A,r4=allocation,r5=0`.
- Original `8274B1A0` preserves r4, forwards to backing vtable+4 with r3=backing/r5=0, and does not itself invent a successful free when the real callback runs. It is a tail call; preserve the generic caller's LR.

These CPU allocation/free paths do **not** consult Q+18/Q+1C. Those are separate physical allocation callbacks conditionally installed by the EXm0 hardware initializer. Installing `82339788/82339798` does not establish the generic allocator.

Adapter lifetime is also real CPU work: original `82810C40` calls `8274B088`, which invokes the retained backing's vtable+0C, then optionally frees A via `8269BEB0` when its delete flag bit0 is set. Do not directly free A while Q or a guest V still uses it. Full audio-root teardown and backing callback semantics beyond these dispatch relationships are not independently executed/proved here.

## Queue-size correction and integration fixture

The original word at **82340588** is **39200014** (`li r9,0x14`), stored to V.byte32 at **823405C0**. Capacity is **20 decimal**, not `0x20`. The queue occupies `0x190 = 20*0x14` bytes. The prior bridge document's `byte32=20h` is incorrect; its 20-record cleanup count is consistent with the actual instructions.

For D+18=0, total allocation is `align8(querySize)+0x190`; queue offset is `align8(V+querySize)-V`. With the requested 16-byte V alignment, mono/stereo use query size `0x70`, total `0x200`; three/four channels use `0x88`, total `0x218`. These are CPU layout checks, not authorization to enable unproved multichannel decoding.

After main's real factory/constructor/release hooks and known-family guards are installed, the bounded fixture should:

1. Use the real startup-created Q/A/backing. Verify the original descriptor, adapter table and allocation/free callsite words before testing. Keep the provider's hardware-pool fields unused; do not invoke `8233E5C0` to prepare an allocator.
2. Retain original provider registration through `82340448`; verify D's link/count changes and duplicate suppression. Pass the true owner and supported channels to original `823404A8` via `EngineCpuCalls`. Its stack callback LRs are generated by the original body, not fabricated by direct native constructor calls.
3. Verify a real aligned V allocation, ctor-entry generic fields V+C, V+10=0, V.byte2E and V+4, then success-only generic fields and exactly twenty queue slots. Empty slots clear only +0/+C; no full-zero expectation. Preserve untouched V words and slot bytes.
4. Run `823402E8` and observe native owner retirement before the original V free. Repeated lifetimes, a retained native reference, and same-address reuse must not revive a stale allocation generation. Heap ownership/accounting must be observed through the real allocator, not inferred solely from the address remaining mapped.
5. Inject a **real native construction failure after transaction admission**. Have the native callback return false; let original `8234062C` call `823402E8`. Assert release recognizes the exact partial allocation and frees V once, despite uninitialized success-only fields. An exception-only guard does not test this original rollback path.
6. Preserve SP/LR and nonvolatile registers, use `EngineCpuCalls`' FP/TLS scope, and leave enqueue/decode guarded before guest publication. Do not execute any original hardware ctor/release to complete a test.

No standalone lifecycle was executed in this evidence task. The constructor/release transaction was the initial dependency; main now owns its implementation and actual-startup lifecycle fixture. Allocator forwarding itself is resolved. This document supplies evidence for testing that real owner.

## Follow-up: r7 is the real Q, through the original stream owner

A complete aligned `.text` scan for direct branch targets finds exactly one direct call to **823404A8**: **82342B98**, word **4BFFD911**, LR **82342B9C**, inside **82342AE8(S, voiceIndex)**. This is a direct-reference result, not an exhaustive indirect-call claim.

Original `82342AF4` retains incoming S in r31. The allocation call arguments are:

```text
r3 = registry returned by 82340A20(S[4]) at 82342B48
r4 = matching codec descriptor from that registry
r5 = byte[S + BE16(S+1BC) + voiceIndex*30 + 2B]
r6 = 14                         # explicit caller constant; unused by 823404A8
r7 = BE32[S+4]                  # 82342B90: 80FF0004
```

The descriptor search reads a selector byte at `BE32(S+50)+voiceIndex*0x50+0x30` and indexes the six-word identity table **821CA02C**. Index3 is **45586D30 / EXm0**. These entries are identities, not descriptor pointers. The return V is stored into the voice record's +8 at `82342BA0`.

The required ownership link is established by original generic stream construction **8233DBE8** before calling the stream-specific constructor:

```text
8233DC24: 816B1BCC   lwz r11,1BCC(r11)   # r11 base82E30000: Q=BE32[82E31BCC]
8233DC28: 917F0004   stw r11,4(r31)      # S+4=Q
8233DC38: 81650008   lwz r11,8(r5)       # descriptor-specific constructor
8233DC40: 4E800421   bctrl
```

For the original stream descriptor `82D07168` (SnP1), that constructor is **823418E8**. It retains the already populated S+4, uses Q+14 for its real auxiliary allocation, and registers its CPU callback **823413C8** through **82348058(Q+60,S+40,1)** at **82341AA8**. That callback's sole direct call into **82342AE8** is **82341590**. Thus **r7=Q is a proved relationship**, not a placeholder owner selected merely because it is mapped. If an S already exists, use its retained S+4; do not substitute a different subsequently published global Q.

For main's bounded codec allocation/destruction test, the minimal real owner already exists at the provider observation: **Q created by original82338C98**, with its real adapter and allocations. There is no need to create S merely to obtain r7. After the real provider has returned D and the dispatch observation has unwound, create a clean `EngineCpuCalls` from the saved initialized entry context; retain original registration `82340448(registry,D)`, then call:

```cpp
// Fixture invocation of the real generic wrapper with the recovered caller ABI.
const uint32_t v=cpu.invoke(0x823404A8,registry,0x82D073AC,channels,0x14,q);
// ...main's native owner/layout/generation assertions...
cpu.invoke(0x823402E8,v);
```

Require nonzero V before destruction, and verify the same original Q/allocator association remains live. This tests the original generic allocation/cleanup plus main's real native owner; it does not claim the stream constructor or voice-selection caller executed in that fixture. The full stream path allocates auxiliary storage, registers a callback and can immediately enter source feeding after V creation. Do not manufacture S or call `82342AE8` just to bypass that wider lifecycle. Main's observation is now **after the real provider body**; the earlier before-provider observation proposal above is superseded for this fixture.

## Reproducible local byte checks

Run this block with `python -B` from the workspace. It reads only the original derived image and existing analyzer utility. Reviewed leaf `8274B1A0` has explicit extent; other extents match `.pdata`.

```python
from pathlib import Path
import sys
sys.path.insert(0, 'tools')
import analyze_poststart_integration as a
b = Path('analysis/simpsons.pe').read_bytes()
a.validate_identity(b)
_, pdata = a.layout(b)
pins = {
    0x823404A8: (0x1E4, '51b11d9f5f257c69fab285b67bfc1b4a963ad524b1c99b09b341101bcb084599'),
    0x823402E8: (0x90, '7941c14fe977ded404900f80bf9bbdf696e49d27a0d491f73f1fe46dae409cf9'),
    0x82340730: (0x74, '6358fbcb655b8bf0e49a99c71a306755787a81812174831c2e2e7c9868d24d91'),
    0x82338C98: (0x284, '565d560a7f8e32e8bb941fe9af9e75cb6a70900a2544db029e8637dfd3ae3363'),
    0x82810BB0: (0x54, '474a6d6cdcf9a3c7b847bf69b9c207088139a878c83e5c4226e729070310cef2'),
    0x8274B1C8: (0x58, 'ce46183f13c240c81c383b2fe50108dd318d4db0ed25c8715368e443f831e65c'),
    0x8274B140: (0x5C, '025838c3d7b634b3d94b11469c997444faa25a8f61a76f23ddc9afd65461d7f8'),
    0x8274B1A0: (0x28, 'd88bdbf505a228429fb9079129cd62c3a892d9f747e1b1165a47bcb1ba4a8cd5'),
    0x8274B088: (0x5C, '79c5e277235c69009e0f55cf3bfbb694074ab188dd6122080562d94714d99af1'),
    0x8269BD70: (0x70, 'fb7bbd9cc373fd70b70c15cbac31866894a1483b5f2e3a1250812115aaeee28c'),
    0x8269BEB0: (0x60, '73038838936ffa5b4a04e12c95e20e74e41c4871ceadeecb4beab892f2b781a4'),
    0x82340A20: (0x48, '484106a82b095ccfaf3afaaa23552f79eba5d18bde6a4beb057a80e536d0a6dc'),
    0x82342AE8: (0x1E0, 'e1af39e77af1f2d6b802970c1b6117bf6c2ddd470463f704c14b4023b78ba6bc'),
    0x8233DBE8: (0xAC, 'd60c81127cc4ca36efbcbd7ca87e84595407246cbe4b017e3ecc0e1eccae5d5a'),
    0x823418E8: (0x1D0, '7ea7d6ef9b34c04fc257dac9b3f053094d7974992e38c07b0d087ebee8dd5629'),
}
for va, (size, digest) in pins.items():
    assert a.sha(a.span(b, va, size)) == digest, hex(va)
    if va != 0x8274B1A0:
        assert pdata[va][0] == size, hex(va)
assert [a.word(b, 0x8215CA2C+4*i) for i in range(4)] == [
    0x82810C40, 0x8274B140, 0x8274B0E8, 0x8274B1A0]
words = {
    0x828162C8: 0x4BE85AA9, 0x828162E0: 0x4BFFA8D1,
    0x828162F4: 0x4BB229A5, 0x82338D24: 0x93BF0014,
    0x82338D2C: 0x93EA1BCC, 0x8281646C: 0x4BB2812D,
    0x82340530: 0x48000201, 0x82340770: 0x4E800421,
    0x82340570: 0x4E800421, 0x8234062C: 0x4BFFFCBD,
    0x82340310: 0x4E800421, 0x8234033C: 0x4E800421,
    0x8234035C: 0x4E800421, 0x82340588: 0x39200014,
    0x823405C0: 0x993F0032, 0x823404F0: 0x3BAB0190,
    0x82342AF4: 0x7C7F1B78, 0x82342B90: 0x80FF0004,
    0x82342B94: 0x88BC002B, 0x82342B98: 0x4BFFD911,
    0x82342B48: 0x4BFFDED9, 0x82342B8C: 0x38C00014,
    0x8233DC24: 0x816B1BCC, 0x8233DC28: 0x917F0004,
    0x8233DC40: 0x4E800421,
}
for va, word in words.items():
    assert a.word(b, va) == word, hex(va)
assert a.word(b,0x82D07168+8) == 0x823418E8
assert [a.word(b,0x821CA02C+4*i) for i in range(6)] == [
    0x58617330,0x454C3330,0x50364230,0x45586D30,0x58617331,0x454C3331]
sections,_ = a.layout(b)
start,size = sections['.text']
direct = []
for pc in range(start,start+size,4):
    w = a.word(b,pc)
    if w>>26 == 18 and a.branch(pc,w)[0] == 0x823404A8:
        direct.append(pc)
assert direct == [0x82342B98]
for channels, query, total in [(1,0x70,0x200),(2,0x70,0x200),(3,0x88,0x218),(4,0x88,0x218)]:
    assert 0x58+0x18*((channels+1)//2) == query
    assert ((query+7)&~7)+20*0x14 == total
print('PASS', len(pins), 'function pins, adapter vtable,', len(words), 'words, 4 layout cases')
```
