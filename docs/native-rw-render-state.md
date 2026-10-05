# Bounded original RenderWare render-state dispatch

Parent integration verification: build091 passes all 24 CTest suites, including
both original AOT selector fixtures within the 4,357-check driver lifecycle
suite. Boot055 passes both reached selectors and stops at the mode-zero reset
entry `823EFDA0`, caller `823F4768`. No original draw or presentation is claimed.

Bounded evidence for boot051's `824025A8`, caller LR `82726870`, with the
boot052 selector-8 extension below. Owned outputs are this document and the
new `tests/header/test_rw_fog_contract.h` fixture. No runtime, renderer,
config, existing test, generated, build or reference files are changed.
All addresses/words are hexadecimal;
selector **14 decimal = 0E hexadecimal** is written explicitly to avoid the
different selector 14-hex route.

## Immediate result

**Allow the observed `(selector=14 decimal, value=1)` through a checked entry
preflight, then retain the entire original AOT dispatcher and helper. No new
SDK call hook is needed for this route.** `82401480` is the exact destination;
it has no direct calls, indirect calls, allocation, SDK-device read or SDK
write. Its result is always 1, including when capability bits prevent the
requested enable. Replacing it with an unconditional success would lose real
CPU cache/queue effects.

The original caller is independently pinned:

```text
82726858 38800001   li r4,1
8272685C 3860000E   li r3,14
82726860 816BCA68   lwz r11,[82D0CA68]
82726864 816B0020   lwz r11,20(r11)
82726868 7D6903A6   mtctr r11
8272686C 4E800421   bctrl                  ; LR=82726870
```

Thus this is engine callback `E+20`. The observed input is **not selector
0x14**. That distinct input would take the cull-related lookup/queue path at
`82402850` and is not enabled by this contract. No symbol name such as a
general fog service is required to implement the verified route; this report
names its concrete capability test and engine scalar ID instead.

## Exact table and branch route

At `824025BC`, compute `r11=selector-1`; the unsigned comparison at `25C8`
rejects indices above `1D`, allowing selectors 1..30 decimal. `r3` is initially
zero for the default failure return. Table bytes at `82062DE8`, length 30:

```text
53 47 4A 4D 92 5A 63 57 50 5D 60 7B 7E 00 03
17 30 EE EE 96 AF B2 B5 B8 BB BE C1 C4 C7 EA
```

The branch target is `824025F8 + (table[selector-1] << 2)`, not an address
loaded from a four-byte table entry. For selector 14 decimal, byte
`[82062DF5]=00`, so the target is `824025F8`:

```text
824025F8 7FE3FB78   mr r3,r31              ; original value argument
824025FC 4BFFEE85   bl 82401480            ; helper LR=82402600
82402600 480003B0   b 824029B0             ; original epilogue
```

The helper result survives the dispatcher epilogue. Preserve original SP,
LR and nonvolatile-register saves/restores. Entry preflight should fall
through into AOT, not jump into the case without the original frame.

## Selector 14 decimal: complete CPU effects

The helper's `r11` base is `82D10114`. Resolved fields are:

- `82D0E3E4`: current normalized engine enable cache (`base-1D30`).
- `82E3DFC4`: capability word, read only when request is nonzero and current
  cache is zero. Instruction `824014A8 = 554A05F1` selects mask **00000180**.
  PPC mask bits 23..24 count from the MSB; this is not mask 300.
- `82D10060`: pending scalar value for ID `196`.
- `82D10064`: that scalar's dirty flag.
- `82D0ED08`: queue of 32-bit scalar IDs.
- `82D10114`: number of scalar queue entries.

The original scalar layout independently agrees:
`pending = 82D0F3B0 + 8*196 = 82D10060`; the applied value is later stored
at `82E3D580 + 4*196 = 82E3DBD8` by the commit service.

Exact branch behavior:

1. Nonzero request and nonzero current cache: no writes, return 1.
2. Nonzero request, zero cache, capability mask zero: no writes, return 1.
   The request is not remembered as a deferred desired enable.
3. Nonzero request, zero cache, capability mask nonzero: write pending=1.
   If dirty was zero, append `196` to queue[count], write dirty=1 and
   increment count. Then write current cache=1 and return 1.
4. Zero request and zero current cache: no writes, return 1.
5. Zero request and nonzero current cache: write pending=0. If dirty was zero,
   append `196`, write dirty=1 and increment count. Then write cache=0,
   return 1. This disable path does not read the capability word.

Ordering is visible at `824014B4/B8/C4/DC/E0/E4` for enable and
`824014FC/1500/150C/1524/1528/152C` for disable. Final cache publication is
`82401534 = 914BE2D0`. Pending value is updated even when the dirty flag was
already set; an already-dirty transition must not append a duplicate entry.
The original leaf treats any nonzero input/cache as true. Limiting new native
entry acceptance to canonical 0/1 is a deliberate bounded preflight policy,
not a claim that the original rejects other integers.

This helper does **not** update a D3D11 state directly. Original commit
`82400040` writes changed applied values, queries the current pipeline via
`823F4670`, and calls a console SDK setter only for IDs **below 194**
(`824000B4/B8`). ID `196` does not take that call. The existing parent
`EngineRenderState::commit` has the same less-than-194 split and already
retains this CPU applied state/query. No native SDK setter for ID 196 should
be invented. Acceptance of this state operation does not certify arbitrary
future shaders consuming it.

## Bounded entry preflight recommendation

Use a fall-through preflight at `824025A8` for the observed route; the
separately verified selector-8 extension is below. Keep other selectors
rejected until their own effects are covered.

1. Require active runtime/base, graphics owner thread and initialized native
   engine-state owner. For the smallest first integration, require incoming
   LR `82726870`, `r3=0E`, a live engine and its `+20` callback still
   `824025A8`. The current parent preflight preserves the original truth
   normalization for every uint32 input; the fixture explicitly covers 2,
   80000000 and FFFFFFFF as well as 0/1. Keep CAF8=0;
   this leaf has no need for a native identity or fake SDK object.
2. Validate the pinned table byte/route, call instruction and read-only
   capability address. Preflight the dispatcher's actual stack accesses:
   aligned incoming SP, saved LR at SP-8, r30/r31 at SP-18/-10, and its
   0x70-byte frame below SP. Reject wrapping or unmapped spans before AOT runs.
3. Read the current cache and relevant pending/dirty/count words. A bounded
   initialized cache/dirty contract can require 0/1. Use the exact branch
   predicate above to decide whether any update or append occurs.
4. Preflight cache, pending and dirty writes before a transitioning call.
   When appending, require `count < 425` decimal, checked
   `82D0ED08+4*count`, writable queue slot and count word. The engine's
   scalar arrays/queue have 425 entries: `823FFEC4 = 394001A9` initializes
   that many pending entries, and the applied-array fill at `823FFE8C`
   is 0x6A4 bytes. This is not the selector table length. Dirty already set means
   no new slot is used; count may equal capacity if the queue is valid.
5. Preserve queue integrity: entries in range, no duplicates, dirty membership
   coherent with queued IDs; in particular ID 196 occurs exactly once when
   its dirty flag is set and zero times otherwise. Existing bounded state
   commit validation can be reused. Do not clear a queue or force capability
   bits merely to pass preflight. A no-change call need not manufacture writes.
6. Let original `824025A8 -> 82401480` execute and return its real r3=1.
   Preserve existing stop/cancellation propagation. No callback allocation,
   new native resource or SDK success substitute is required.

The suggested stack range is the original dispatcher's own frame, not an
extra invented callback frame. A preflight should not change guest registers,
LR, condition registers or cache values. If the owner wants transactionality
across a later fault, snapshot only the preflighted mutable CPU fields/queue
slot; do not claim this original leaf itself is an atomic transaction.

## Nearby selector 9: separate, mixed route

Selector 9 uses byte `[82062DF0]=50`, target `82402738`, call
`8240273C = 4BFFF065 -> 824017A0`, LR `82402740`. This is a stage-0 filtering
path with real CPU stores and SDK effects. **It cannot be enabled by the
selector-14 CPU-only exception.**

Its CPU base is `82D0D170`. It first reads `82D0E40C` (`base+129C`,
anisotropy request). If **signed** value >1, it stores 1 there and at
`82D0D200` (`base+90`), then calls SDK `8243BE50(stage=0,value=1)` at
`824017E0 = 4803A671`, return `824017E4`.

It next compares the requested filter selector with `82D0E404`
(`base+1294`). Equality skips the remaining work, but does not undo the
earlier anisotropy normalization. For a changed request, it stores the raw
selector, reads two BE words at `82062D00 + 8*selector`, and proceeds:

- Store pair.first at `82D0D1C0` (`base+50`), then SDK minification call
  `8240181C = 4803A225 -> 8243BA40`, `r3=CAF8,r4=0,r5=pair.first`,
  return `82401820`.
- Store pair.first at `82D0D1B0` (`base+40`), then SDK magnification call
  `82401830 = 4803A3A1 -> 8243BBD0`, same arguments, return `82401834`.
- If pair.second differs from `82D0D1D0` (`base+60`), store it there
  at `82401844`, then **inline** SDK access at `82401848..85C` replaces
  `CAF8+48C` bits 23..24 and sets dirty64 `CAF8+18` bit 31.
  This is the same mip-filter field as SDK `8243BD60`; it is not a BL to it.

The seven inspected table pairs for indices 0..6 are:
`(2,2), (0,2), (1,2), (0,0), (1,0), (0,1), (1,1)`.
The original helper performs **no index bounds check**. These rows do not
authorize index 0's unsupported min/mag value 2 or arbitrary higher indices.
A conservative nearby extension could initially accept indices 1 and 2,
which use existing supported min/mag 0/1 and base-map-only mip value 2.

Required native cut points if selector 9 is enabled:

- Three SDK BL replacements above, preserving the original CPU stores and
  original continuation PCs, with checked stage=0/value and owner scope.
- Inline mip cut at **82401848**, word **814B048C**, jumping to **82401860**.
  Original CPU mip-cache store at 82401844 remains AOT. At this cut r28 is the
  mip value, r11 is the loaded CAF8 value; validate the former and never
  dereference the latter. Hooking only calls would miss this SDK access.

Preflight all selected table reads, CPU cache writes and effective native
sampler updates before allowing the original helper's first mutation. The
native operations correspond to sampler IDs `24` (anisotropy), `14` (min),
`10` (mag), and `18` (mip). Use an **effective-owner update that executes even
after the CPU cache already matches**. Current `EngineRenderState::setSampler`
returns early when its guest sampler cache equals the requested value; the
original stores immediately before these hooks make that check true. Calling
that cache-aware method unchanged would silently skip the native update.
An explicit forced/effective-only sampler operation is required at these
cuts. Preview the entire operation before publication; keep normal owner
validation and rollback policy. No SDK layout should be fabricated.

No selector-9 runtime observation is asserted here. Its table/call/inline
evidence is a bounded adjacent extension for the parent, not permission to
unblock all selectors.

## Boot052 extension: selector 8, depth-write request

Original byte `[82062DEF]=57` gives `82402754`. The dispatcher moves value
from r31 into r3 there, then `82402758 = 4BFFF6A9` calls `82401E00`, returning
to `8240275C`. That helper's complete 78 instruction words end with
`82401F30: li r3,1; 82401F34: blr`. There are **no calls or SDK-device
accesses** in the helper. No additional SDK callsite hook is needed.

Caller wrapper `826B8488` normalizes incoming r4 to Boolean with
`cntlzw/rlwinm/xori/clrlwi`, publishes the low byte at `82CED7FC`, and sets
r3=8 at `826B84A0`. It calls engine `E+20` at `826B84C0`, LR `826B84C4`.
The wrapper's byte publication remains original AOT and is outside the
dispatcher's cache ownership. The companion fixture invokes the dispatcher
directly, so it does not modify that additional byte.

Exact guest fields:

- `82D0E3B0`: cached depth-write Boolean, from `r11=82D10114`, offset -1D64.
- `82D0E3B4`: sibling depth-test Boolean, offset -1D60; this helper never
  modifies it.
- Scalar ID `28`: pending at `82D0F4F0`, dirty at `82D0F4F4`.
- Scalar ID `30`: pending at `82D0F530`, dirty at `82D0F534`.
- Queue/count remain `82D0ED08` / `82D10114`.

Normalize the requested value by nonzero truth. If it equals the current
cached Boolean, return 1 with no writes. Otherwise, when the sibling is zero,
update pending ID 28 to the normalized value, appending 28 only if its dirty
flag was zero. Regardless of sibling, update pending ID 30 to the normalized
value, appending 30 only if its dirty flag was zero. Publish the normalized
current cache last (`82401F2C = 914BE29C`) and return 1. Enable and disable
follow the same order. Pending values overwrite existing dirty entries;
preexisting queue entries retain their order. With two clean entries and
sibling zero, the two new queue words are **28 then 30**. With sibling one,
ID 28 and its dirty/queue state are untouched.

Here scalar IDs `28` and `30` are hexadecimal (`40` and `48` decimal),
while dispatcher selector 8 is decimal/hexadecimal 8. For canonical cached
Booleans, the bounded helper contract is:

```text
requested = (incoming_value != 0)
if requested == cached_depth_write: return 1
if cached_depth_test == 0: stage_scalar(0x28, requested)
stage_scalar(0x30, requested)
cached_depth_write = requested
return 1
```

`stage_scalar` means the original pending-value store and conditional dirty
queue append, not an immediate host state change. Keep this entire helper
and its dispatcher frame in original AOT. A same-value call does not repair
stale pending values or manufacture queue entries; the sibling cache is
read-only. Input FFFFFFFF requests true, just as 1 does. The native preflight's
canonical-cache requirement is a bounded integrity check rather than an
additional original instruction.

Entry preflight must account for **both** possible appends before permitting
the first original store: require `count + additions <= 425`, selected dirty
flags canonical and their membership exact, and preflight all selected
pending words/cache/count/new slots. A full queue can still accept updates
to already-dirty entries. Rejecting only after the first append would leave
a partial depth update. Keep the native-effective depth request and applied
scalars unchanged until the real commit. Unlike ID 196, IDs 28 and 30 are
below the SDK cutoff and the existing native commit owns their eventual
effective-state update.

Additional byte hashes:

- `82401E00`, size `138`, complete reviewed leaf:
  `3569f2ee7329256e87fdd88971226bc3fe17e4fff116ef0894fac73a4e20fb4c`.
- `826B8488`, size `50`, complete original wrapper:
  `f3dd1a8bce788cd642a079d1061ba46c659630bb8fc446ec5746c15ac70ad6f1`.

Reproduce with `python -B tools/disassemble.py 0x82401E00 --count 78` and
`python -B tools/disassemble.py 0x826B8488 --count 20`.

The new header provides inline `rwFogContracts(Runtime&,EngineCpuCalls&,
uint8_t*)` and `rwDepthWriteContracts(...)`. Include it inside the parent's
anonymous namespace after `require`/`rejects`, then call both after
`applicationSamplerContracts`. It uses actual `cpu.invoke(824025A8,...)`,
RAII saves/restores the complete `82D0D170` range of `2FBC` bytes, the caps
word, caller context and 0x70-byte dispatcher stack scratch. Every success
checks the exact expected cache bytes, all 18 nonvolatile GPRs, SP/LR and
the original save/backchain slots. Every rejection checks the specific
Failure prefix, lastFunction and absence of prologue/cache mutations. Both
check all applied scalar bytes and all exposed native scalar/sampler/blend
state plus driver identity remain unchanged before commit.

Fog cases include both individual capability bits, unrelated capability
bits, nonzero normalization, cache suppression, clean/dirty enable and
disable, last-slot append, full already-dirty updates and corrupt queues.
Depth cases include all four cached/sibling combinations and inputs
0/1/FFFFFFFF, all selected dirty combinations, queue ordering and dedup,
two last legal slots, one-slot-short rejection and corrupt selected flags.
Full queues missing selected IDs intentionally contain an unrelated duplicate
to test capacity rejection; they are labelled corruption fixtures, not valid
engine states. No selector-6 scope is included.

### Frozen integration status

The header and both calls are integrated by the parent; full build090 is
running as reported on 2026-09-10. This worker has not compiled or executed
the fixture and does not claim its result. The parent reports the FP fix is
integrated and build089 passed the camera-clear/pass contracts; the current
observation guard is the mode-zero reset `823EFDA0`, before texture SDK
access. Those observations do not imply selector-8/14 fixture success.

The wrapper same-mode test must preserve its actual default stack mode 0;
the parent reports that correction is in place. The functions in this header
exercise the original render-state dispatcher directly and do not change or
certify pipeline-stack mode behavior. No runtime, header or fixture edits
are part of this final documentation update. The selector-8 appendix and
test header are now frozen for parent integration.

## Reproduction and byte identity

Source: derived original `analysis/simpsons.pe`, VA base `82000000`, size
15,466,496, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
`build/boot-051.log:109` independently records boundary `824025A8` and
caller `82726870`; the input value/selector above are proven by caller bytes.

```powershell
python -B tools/disassemble.py 0x82726854 --count 7
python -B tools/disassemble.py 0x824025A8 --count 264
python -B tools/disassemble.py 0x82401480 --count 48
python -B tools/disassemble.py 0x824017A0 --count 51
python -B tools/disassemble.py 0x82400040 --count 75
python -B tools/disassemble.py 0x8243BD60 --count 15
```

Reviewed byte ranges and SHA256:

- Dispatcher `824025A8`, size `420` (original pdata):
  `b592a312f0086b3ea5ea9ba30b9af5e0e783f7dbcce70b090b41871d759ff840`.
- CPU-only leaf `82401480`, size `C0` (entry through BLR):
  `cba4fa9461dccd4fd0563044a4b5bbc049570c779fd8d2e3773df87504ca8923`.
- Selector-9 helper `824017A0`, size `CC` (entry through tail epilogue):
  `e1a90ce47d5c354e4d8288e0877e12438ed7de92fb40e700caf7a18389a5e29d`.
- Caller slice `82726854`, size `1C`:
  `f007b22f80efb6ff07b48b047bf21e1dbe92e07e6ec1091b8c6946c6851a1771`.
- Commit slice `82400040`, size `12C`:
  `be515debed058edc1c56e0aa5cf0c1f693e3d2e1ed6903d684a5c4c5e95c50c3`.
- Route bytes `82062DE8`, size `1E`:
  `fd6364d215b09cf1081f3fec6dde020d9507579e111f81c098a8daa1e29a3125`.
- Seven filter pairs `82062D00`, size `38`:
  `fe646904a9909e33571684292e902be156c9dc835fb404c353483a2bdff31b0f`.

Validation rechecked the image identity, all seven listed range hashes,
the three distinct routes (decimal 14, decimal 9, hexadecimal 14), and the
absence of calls/CTR branches in the complete selector-14 leaf.
No new analyzer is needed for this small route: the existing disassembler
checks the original image identity, and the document pins the data bytes,
complete helper and critical words. No native/PPC fixture was executed.
Suggested parent cases: capability mask zero/nonzero; cache already enabled;
enable/disable with clean/dirty queue; full queue with/without append;
duplicate/missing dirty membership; cancellation; and rejection of selector
14-hex. If selector 9 is added, test anisotropy normalization even when filter
is unchanged, all four native sampler cut points, and the pre-store/cache
early-return hazard explicitly.
