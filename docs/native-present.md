# Native presentation: history, completion and display boundary

Integration status: build100 passes all 25 CTest suites, including the 29,082-check
original driver lifecycle. Boot060 on the visible desktop completes two real
front copies and receives two accepted native presentations. Its unedited window
capture is `build/captures/native-present-060.jpg`: black clear only, zero original
geometry draws. No loading artwork or playable milestone is established.
The original byte-evidence investigation below preceded this implementation.

The implemented backend keeps RGB10A2 through both copies and uses an explicit
full-range SDR G22/P709 swapchain color space with alpha ignored by composition.
It does not add a shader-side gamma conversion. GPU event queries distinguish
submission from completion; source/front references survive abandoned receipts.
The backend completes front-to-swapchain transfer before reusing the front and
waits for preceding GPU work before the original four cursor resets. Windows
owns swapchain display lifetime. None of this proves original scanout timing.
The native policy uses [SetColorSpace1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-setcolorspace1)
and [Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

Both WARP and hardware presentation tests preserve all 1,024 RGB component
codes and all four stored alpha codes through actual resource and swapchain
copies. They also verify state preservation, completion and resource lifetime.
Those hidden-window tests report occlusion; the separate desktop boot060 is
the accepted-presentation and visible-window evidence.

**There is no discovered CPU wait or arithmetic consumer of CF94/CF98.** In the
reviewed static reference set, CF94 is read only to shift it into CF98, and CF98
is never read. The SDK waits use separate device-owned tokens. This removes a
specific obstacle to native presentation: the engine histories need not become
SDK objects or a simulated console counter. They can identify genuinely
submitted native work, with completion and resource ownership managed by the
native backend. This is a bounded replacement contract, not proof that arbitrary
guest aliases or SDK wait APIs can accept native receipts.

The appropriate replacement is the platform callback **823EE820**, preserving
its original outer wrapper **82408030**. Do not replace the whole camera/raster
wrapper and lose its CPU list work. The existing camera report remains the
authority for resolve arguments and target formats; this document closes the
history/consumer question and qualifies display completion.

## Exact reference inventory

All addresses are hexadecimal; guest fields are BE32. Global prefixes below are
`82D0`. Original flat image base `82000000`, size 15,466,496, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.

The analyzer checks 24 code ranges against original bytes and `.pdata` where
present, then independently byte-checks 2,434 disassembled instruction words.
The four global words initially contain zero. All **14 reviewed memory uses**:

- **CF8C:** creation result stored at `823EE158`; stop loads/releases at
  `823EE3DC/3E8`, clears at `823EE3F0`; present loads at `823EE854`, stores the
  other front identity at `823EE878`, loads the display argument at `823EE8A0`.
- **CF90:** creation result stored at `823EE130`; stop loads/releases at
  `823EE3F4/400`, clears at `823EE408`; present loads the resolve destination
  at `823EE85C`, stores the other front identity at `823EE874`.
- **CF94:** load at `823EE880`; new submission result stored at `823EE890`.
- **CF98:** store of prior CF94 at `823EE888`. No load found.

The only exact address formations discovered are `823EE1BC` (CF90 base for
stop) and `823EE83C` (CF94 base for present). Neither escapes as a known address
argument/store in the search. No four-byte BE literal occurrence of any of the
four addresses exists anywhere in the original flat image, including unaligned
positions. A separate low-immediate sweep finds nine candidates; seven refer
to other high halves or are raw words in `.text`, not established absolute
references to these globals. The report preserves their original words.

Search scope is explicit: all 2,772,793 aligned `.text` words, 7,337 LIS
82D0/82D1 seeds, constant address formation through addi/addis/addic, ori/oris,
mr and D/DS-form memory, forward control-flow alternatives and ABI call
clobbers. There are 112,493 visited address states, no budget-limited seeds,
and 1,343 recorded backedges that are **not unrolled**. Loaded aliases,
arbitrary indexed/bulk accesses, other arithmetic and indirect callees are not
resolved. Unconstrained loop unrolling would incorrectly run neighboring
matrix/cache-array walks into these globals. Accordingly, this is a reproducible
direct-reference inventory, not a proof of absence of every computed alias.
Keep unknown SDK consumers guarded.

Neither the original start nor stop body explicitly resets CF94/CF98. Their
initial zero is image state. A native stop/restart policy must not silently
reuse receipt IDs while old histories remain; retain archived receipt identity
metadata or reject unsupported stale consumers. Front release/zeroing is
independent of history retention.

## What the submission token does and does not mean

In `823EE820`, the original order is:

1. Swap front fields, resolve bound color slot 0 into **old CF90**, now CF8C.
2. CF98 = old CF94. Call `82457E30`; store its return in CF94.
3. Call `824544E8`, then `824544F0(device, CF8C, 0)`.
4. Call CPU leaf `823FC5B8`, then return 1.

`82457E30` captures `device+2A9C` at `82457E40`, stores it at device+2AB0,
calls submission helper `82457CC8` at `82457E48`, and returns the captured
value. This occurs **before the display submission**, not after scanout.
Its normal submission chain reaches `82457210`, which writes the current token
into submission data at `824572BC` and increments +2A9C by **2** at
`824572EC/F0`. Conditional SDK paths differ, and submission can occur outside
present. A per-frame increment is not the original token contract.

Wait helper `824574B8` reads current token C from device+2A9C and completed
token D from `BE32[BE32[device+2A90]]`. Its initial pending test for token T is:
`T != 0 && uint32(C-T) < uint32(C-D)`. This unsigned modular comparison, including
wrap and zero, is tested offline. It may submit current work, poll, and leave
through `82452018` returning zero; merely reaching its return is not universally
a successful hardware completion proof. None of these accesses reads CF94/98.

In the display function, `824549D0` captures another current token, then
`824549D4` submits. The wait at `82454A08` receives **device+3A44**, an older
SDK display/submission history, with r5=3/r6=0. The ordinary branch stores the
newly captured token into +3A44 at `82454A58`; a mode-dependent branch instead
performs its own counter-based pacing. Do not replace CF98 with a wait target
on the assumption that it is this SDK history.

`824544E8` is also not an empty success leaf or an unconditional CPU wait. It
tail-calls `82454048(device,0)`, which queues synchronization/pacing work with
callback **82453EB0** through `82457580` at `82454178`. The callback address is
constructed at `82454158/60`. The later SDK submission publishes that work.
The SDK packet encoding is evidence only; the analyzer neither interprets nor
emits a command stream.

## CPU callbacks and timing constraints

Two optional CPU callback registrations must be supported or rejected:

- **82454BF0(device,r4=callback)** stores `device+4084`. In `82453EB0`, nonzero
  +4084 is invoked at **82453FAC** with r3 pointing to six BE words: a nonzero
  command-value flag, SDK callback count, current timing count, previous target
  timing count, bounded elapsed percentage and proposed target timing count.
  The final word is read again at `82453FB0`; the callback can affect pacing.
  It is not safely omitted if registered.
- **82454BE8(device,r4=callback)** stores `device+4088`. Timing handler
  `82453DB0` invokes it at **82453E94** with three BE words: device+408C,
  device+40A0, and zero. It also advances SDK timing counters/queued releases.
  Its discovered direct caller is `824568DC`. No claim about exact host timing
  or interrupt-context equivalence is made.

Whole-text direct-branch and whole-image raw function-pointer searches find
**no call/literal pointer to either setter**. This supports an absent-callback
first path; it does not authorize ignoring a future indirect registration.
Guard both setter entries while unsupported. Existing PIX/generic callback
guards from the poststart contract remain necessary. `824544F0` calls capture
helper `8246D3A0` at `824546CC`; the helper first tests byte `82D55BCE` (zero
in the pinned image), and display also tests SDK +5404. Do not invent a native
SDK object just to make those optional branches appear absent.

State 178's setter `8243BA10` stores the request at device+3504. At
`82454100..148`, requests **0 and 1 both choose internal interval 1**;
2 chooses 2, 4 chooses 3, and 80000000 chooses 0. These are original internal
selectors, not a proved desktop Present API mapping. In particular, native
request 0 must not automatically mean unsynchronized presentation. Actual
pacing also uses timing counters, timebase/frequency and optional callbacks;
matching a host interval number does not establish cadence equivalence.

## Actionable native service contract

Preflight at 823EE820 while retaining 82408030:

1. Validate the live driver/context generation, exact distinct front IDs in
   CF8C/90, real front backing, bound source color role, full matching extent,
   single sample and expected packed formats. Reject unobserved modes, capture
   or unsupported callback registrations before publication. Preflight guest
   history/rotation words and the CPU cursor/index update.
2. Acquire completion-protected ownership of old CF90 as the destination.
   Perform a real same-size copy/resolve preserving logical RGBA **10:10:10:2
   component codes**. Source and destination stay separate. Preserve stored
   two-bit alpha; **front sampling returns alpha 1** as a view policy. No
   second R/B swap or sRGB conversion follows merely from their format words.
   Resolve leaves the source contents intact in this path.
3. Associate a unique native receipt with actually submitted copy work. CF98
   can retain old CF94 and CF94 that receipt identity; the host record owns the
   real completion primitive and resource references. This is a **native
   encoding**, not a fabricated SDK +2A9C value. Submission and completion
   are distinct states; a nonzero integer is not evidence of either by itself.
4. Submit real host display work from that front owner, preserving its lifetime
   through display use. A copy-ready receipt is not a scanout receipt; retain a
   separate display lease/completion policy. A conservative initial native
   implementation may wait for actual work before recycling, explicitly
   accepting different latency rather than claiming original pacing.
5. Retain the original outer **823FA978** list splice (already performed at
   `82408054`, LR `82408058`) and the successful-platform tail
   **823FC5B8** (original LR `823EE8B0`). The latter resets **all four** D0E0/4/8/C
   cursors and advances D0DC modulo four; it does not query completion. Protect
   every associated native buffer from overwrite/release until its actual use
   completes. Rotating an index alone is insufficient synchronization.

Preflight can prevent avoidable partial publication, but submission/display
cannot be rolled back by restoring guest words. Failure after GPU submission
requires retained ownership plus explicit failure/teardown state; do not
publish a completed receipt or return original success. The outer CPU list
splice also precedes platform entry and is not automatically undone on failure.
Shutdown must retire native copy/display leases before releasing front owners.

There is **no extra engine screen quad or expanded-blend pass** in 823EE820.
Any native shader needed for host display-view conversion is a backend choice,
with blending disabled and an explicit alpha policy. It must not fabricate a
recovered original front-copy draw.

The remaining limitations are specific: VdSwap mode/descriptor-driven scaling,
crop/filter behavior, gamma/output transfer, final display quantization,
compositor/scanout timing and capture modes. The present function reaches
VdSwap at `8245473C` after modifying its local display descriptor using SDK
+3510/+3514 and receiving display-mode output. The raw packed copy does not
prove those later transformations. An 8-bit host backbuffer also cannot retain
every original 10-bit code; any conversion needs its own explicit policy and
validation. Do not silently call it exact. Engine CPU gamma tables from
82406560 are not evidence of an SDK display gamma-ramp submission.

This is enough to implement **real bounded native copy/completion/presentation**
without console command processing. It does not certify an original complete
loading frame: unsupported draw requests, expanded blending and display
equivalence remain independent gates.

## Reproduction

```powershell
python -B tools/analyze_native_present.py --self-test --image analysis/simpsons.pe
python -B tools/analyze_native_present.py --image analysis/simpsons.pe --report analysis/native-present.json
```

Without `--report`, JSON goes to stdout. `--disassembler` can select a relocated
SimpsonsDisasm binary; every returned instruction word/PC must match the image.
Only the owned report path is writable. Fifteen self-tests cover all 24 code
pins, `.pdata` corruption, initial globals, the complete bounded reference scan,
negative/nonadjacent address formation, call clobbers, loaded/overwritten address
rejection, unsigned wrap/zero wait comparisons, distinct front rotation/history,
interval mapping, callback words, disassembly mismatch, image truncation and
output-scope rejection. Tests operate on bytes/value fixtures, never a GPU or
original runtime. No parent build or runtime files are modified.

## Native driver presentation fixture handoff

`tests/header/test_present_contract.h` is frozen for the parent build099. Include
it inside the driver fixture's anonymous namespace after `require`/`rejects`.
Call `presentContracts(runtime,cpu,base)` after `cameraPassContracts`, before
`pipelineResetContracts` and teardown, retaining its returned checkpoint. Call
`presentRestartContracts(runtime,base,checkpoint)` after the original
stop/close/reopen/start sequence creates the next driver, before its stop. The
parent has integrated both calls. These checks run in **OriginalDriverLifecycle**;
the separate **NativePresentationContract** suite tests the backend.

The fixture invokes the original `82408030(raster,0,1)` wrapper four times.
Temporary AOT dispatch observers delegate the actual platform entry and original
`823FC5B8` CPU tail; they do not replace either with a successful test stub. It
checks wrapper LR `8240806C`, arguments, raster/Boolean return conversion, the
original 0x80-byte stack frame and saved registers. Four isolated circular-list
fixtures exercise empty/nonempty source and destination lists, checking that
`823FA978` has already spliced and exchanged the heads before platform entry.
Their original head, extension and scratch bytes are restored before later CPU
services; no original list allocation or payload is freed.

Four real endpoint-only color clears provide distinct diagnostic packed pixels,
including stored alpha zero. Readbacks compare the entire source and copied old
CF90, preserve the other physical front and depth contents, and check that
CF8C/CF90 rotation does not change immutable ID-to-resource ownership or the
front alpha-one sampling policy. Every original ring index is exercised, with
nonzero cursors including an exact size boundary. The original tail must run
once, after copy completion, advance the index and clear all four cursors.
CPU target, RenderWare, application and inherited host state are checked for
unintended changes. These diagnostic clears deliberately change GPU contents;
restoring temporary CPU fixture bytes is **not** a GPU rollback or an original
game-frame claim.

Native receipt metadata is bounded to the two current history words CF94/CF98.
Completion queries cover those retained histories only. Older completed receipt
IDs must explicitly reject after pruning; rejection means unavailable metadata,
not unfinished GPU work. The fixture distinguishes submitted/copy-completed,
display-transferred and display-accepted states, permits an occluded attempt
without counting it as accepted, and checks unique unmapped receipt identities.
Restart checks preserve the two history records while rejecting retired physical
front IDs. They do not establish end-to-end rendering after restart.

Malformed profile, owner, target roles, capture, ring, cursor, raster, stack and
history requests, plus a foreign thread, are tested directly at the native
service before publication. Rejections must preserve CPU words, counters,
metadata, effective state and actual source/front/depth pixels. Direct calls
deliberately bypass the outer wrapper's preceding list splice: these tests do
not promise rollback of that splice if a real wrapper call later fails. Failure
after native submission/display remains terminal and cannot be tested as a
reversible preflight rejection.

Validation at handoff: the complete parent fixture including this header passed
`clang-cl /std:c++20 /EHsc /Zs`; all **21 wrapper plus 15 tail instruction words**
were independently compared with `analysis/simpsons.pe` and matched. Parent
build099 and build100 pass the integrated driver checks. A real packed copy
and accepted host presentation still do not prove
original display gamma, scaling, scanout timing, or an original drawn frame.
