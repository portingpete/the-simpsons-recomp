# Native prepared XMA source and complete-quota ownership

Frozen 2026-09-10: [audio/xma_source.h](K:/SimpsonsNativeCopy/audio/xma_source.h), [audio/xma_source.cpp](K:/SimpsonsNativeCopy/audio/xma_source.cpp), and [tests/test_xma_source.cpp](K:/SimpsonsNativeCopy/tests/test_xma_source.cpp). The final standalone strict build/run passed **402,079 checks and 397,693 exact sample comparisons**. Main owns CMake, the packet-only fixture preparation helper, and later runtime integration.

Integrated verification: main reported **full build137 successful, 40/40 CTests passing in 53.19 seconds**, with this core/header/test integrated and the packet-only fixture helper validating all six expected input hashes. This is the parent integration result, separate from the standalone evidence above. No core/header/test changes followed that result; this documentation update completes the freeze. Live guest input/decode bridge activation remains separate work.

The sole PCM policy is **NativeRawF32**: preserve the finite values returned by the real owned codec and their logical component order. No quantization, clamp, remix, output device, guest pointer, hardware record, implicit skip, container delay, EOF or guest free is introduced. Exact console hardware BE16 equivalence remains unclaimed; it is not a prerequisite for this native policy. Live source origin and original skip/count still belong to the runtime bridge, as specified in [the integration evidence](K:/SimpsonsNativeCopy/docs/native-exm0-integration-next.md).

## API and state transitions

Construct `XmaSource(factory, instanceGeneration, formats, limits)`. The factory is borrowed only during construction; the owner creates and retains actual independent `NativeXmaCodec` contexts. The factory may then be destroyed. Formats comprise one to three mono/stereo layers, with a common sample rate and variant, at most six logical components. The underlying codec validates its supported rates and XMA1/XMA2 variant. Actual sample fixtures here are 48 kHz; other rate capability is not an original-waveform claim.

`prepare(const Segment&)` requires explicit continuity: the default `Unspecified` rejects. The first segment must be `FreshContext`; later `ContinueContext` retains the same codecs, bitstream/frame state and raw surplus. Another explicit fresh context requires every earlier source receipt retired. It then resets the real codecs, accounts for discarded raw surplus and starts a new epoch. Empty input never causes a reset.

Each segment supplies a strictly increasing nonzero sequence, opaque caller slot, positive declared frame extent, initial consumed progress strictly below that extent, and one layer input per configured codec. Each layer input contains an explicit pending skip and fixed-size owned 2048-byte packets. Initial progress is accounting only; it does not cause another sample skip. There is no production constant 384 or inferred F flag in this component.

Preparation copies all supplied packet bytes before admission/codec use. It then synchronously sends those packets and drains raw output into bounded per-layer interleaved rings. A real `NeedDrain` triggers a productive read followed by retry of the **same not-yet-accepted packet**. Accepted packets are never resubmitted. Reads retain every returned frame; zero is pending input, not EOF. Input copies remain owned until source retirement/close, independently of the codec's own accepted packet storage.

The resulting receipt is `Prepared`, not consumed. Sources may be prepared ahead of logical output; their packets feed each layer in admission order. Empty layer packet lists are explicitly permitted as host pending-input metadata. `stage` may then report `NeedInput` until a later admitted continuation supplies enough real raw frames. This is not evidence that an empty original EA block is a supported live profile.

`stage(receipt, quotaFrames)` operates only on the oldest source still `Prepared`. Quota must be positive, within remaining logical frames and configured quota/storage limits. For **every** layer, pending skip plus quota must be present before it returns `Complete`. Otherwise it returns `NeedInput` with no ticket and no raw, skip or logical progress changes. It never returns short successful output.

A complete ticket owns immutable component-major float planes. Its metadata identifies source, ticket identity, epoch, progress before commit, frame/component count, per-layer skip and exact raw start frame. Staging does not call the codec or move any read cursor. Repeating the same pending receipt/quota returns the same ticket; changing a pending request rejects. Externally retained committed tickets remain bounded: when all ticket slots are held, another staging request returns `Backpressure` until a ticket is released.

`validateCommit(ticket)` checks identity, epoch, source order, expected logical/raw/skip progress and counters. `commit(ticket)` repeats these checks; its successful path allocates nothing and calls no codec. It advances each raw ring by its explicit pending skip plus the full quota, clears that pending skip, and advances logical progress by quota only. At the declared extent the source becomes `Delivered`. Double/foreign/stale commits reject.

`retire(receipt)` releases delivered or terminal source metadata and copied packet ownership. It never frees guest data. It does **not** discard raw surplus or externally retained ticket planes: those have separate lifetimes. Retired receipts reject further use, even when their caller slot is reused. Native owner identities, caller instance generations, segment sequences, epochs and ticket identities do not wrap.

`close()` is irreversible and idempotent. It cancels remaining prepared sources, releases all codecs, copied inputs and raw rings, and invalidates pending commits. Delivered and failed receipts preserve their status; cancelled/terminal metadata can still be queried and retired. An externally retained ticket remains readable after close or destruction of the assembler. There is no worker thread or callback in this owner. Public methods serialize internally while the C++ owner remains alive.

## Failure and memory contract

Invalid metadata, stale requests, wrong order and invalid quota reject before mutation. Source-count or aggregate copied-byte admission pressure returns `PrepareStatus::Backpressure` without copying or consuming input. Allocations needed for source copying precede codec mutation. Ticket allocation/copy completes before a ticket is published, so a ticket allocation failure leaves raw/logical progress intact.

Once codec preparation begins, errors are terminal: already accepted decoder state cannot be rolled back. The first failure records operation stage, source receipt, layer index and the original `exception_ptr`. Prepared sources become `Failed` and any pending commit is invalidated. Earlier delivered sources stay delivered. Snapshot, query, retirement and close remain available; preparation/staging/commit reject the failed owner. A real decoded-output storage overflow is also terminal, not an instruction to retry the source or truncate its output. The fixture exercises this after one layer successfully prepares and a second layer exceeds its raw budget.

Limits are mandatory constructor input; defaults are finite conveniences, not a live engine budget prescription:

- Source receipts: default 20, hard maximum 64; includes delivered until retire.
- Live ticket allocations: default 2, hard maximum 8; includes externally retained tickets.
- Raw frames per layer: default 65,536, hard maximum 262,144.
- Quota: default 4,096, positive and no larger than the raw-ring capacity.
- Declared extent: default 262,144, configured maximum no greater than `0x7FFFFFFF`.
- Aggregate copied packet bytes: default 1 MiB, hard maximum 64 MiB.

Pending skip must leave room for at least one quota frame, and each stage additionally checks skip plus requested quota. The raw rings are allocated to their configured capacities. Ticket storage is bounded by live ticket count times maximum quota times component count; packet copies by the aggregate byte limit. These are this owner's buffers, not a claim to account for all FFmpeg/CRT internal allocations. Synchronous preparation may discover an output expansion exceeding the raw budget after codec mutation; that is why its explicit terminal storage failure exists. Main should choose budgets from the admitted live profile.

Codec operations use the existing private nearest-even, gradual-underflow FP scope and restore caller MXCSR. Assembler stage/commit only copy floats and update integer state; no extra floating-point math is performed. Strict compilation and hostile rounding/FTZ/DAZ/status fixtures check caller-state preservation.

## Runtime integration responsibilities

Main adds `audio/xma_source.cpp` to `SimpsonsAudio`. No additional decoder library or output backend is required. The assembler reuses the existing real factory/send/read implementation unchanged.

The bridge must establish source allocation/extent and generation, parse/adapt the verified EA framing into complete packets, identify actual context continuity and derive the explicit layer skips/declared/initial progress. That includes the original 384/512 rules when actually applicable; this owner never selects those values. Guest-facing nonzero decode must obtain a complete ticket. `NeedInput`/backpressure remain host states and cannot become a normal short return from the original full-quota callback.

Before publishing a ticket, preflight the entire guest output and all progress metadata, and serialize the whole guest publication/commit against another caller or close. **`validateCommit` releases its internal mutex before returning; it is not an external transaction reservation.** This owner cannot roll back guest writes. Successful commit being nonallocating permits the bridge to perform the already-validated publication followed by logical commit under its own lifecycle lock. Retain original outer queue advancement and actual source frees; neither is represented by packet acceptance or raw staging.

Only explicit fresh/reset is supported here. Original error-reset, seeking, arbitrary loop policy, live flag provenance, source producer hooks and guest callback LRs are not implemented by this library. No original input/decode guard was removed by this task.

## Actual fixture evidence and packet-only preparation

The test reads **six existing original-derived packet streams**, hashes every input and obtains a separate direct-codec oracle in memory. That oracle uses a different partial-read/drain schedule and must match each frozen whole-PCM hash before any assembler comparison. No raw PCM or other media is written by the fixture.

Required files, exact byte lengths and SHA-256:

```text
short.packets                  4096
312a9c2a63c7744f84df913da544e3fce047289cd42aa5ae76808eb2117a9996
documented.packets            22528
9a837e71c7b71887a182202ada01c003a99515308967f55bf115e9ba8230014f
stereo-layer0.packets         57344
15b2401b70ad812144a9cc856a0018e6498fb61e32a41a178b6c1c3c837a8dca
six-mus52-layer0.packets       8192
9bbda700f82471b36310d3994942091a82b0ae9923e872f93435738cb0861c4b
six-mus52-layer1.packets       8192
ce0778603fc90ceabb60faec1f16ac3ac9f65d2b5667b66cfdedfc0e79a7fd65
six-mus52-layer2.packets       8192
414858dac1137e78c8c731fdf3c0cece7ba9f91a792c63c1f0579b7c5ccfe431
```

For a packet-only preparer, reuse the frozen original identities and parser evidence in [tools/prepare_audio_fixtures.py](K:/SimpsonsNativeCopy/tools/prepare_audio_fixtures.py), and the pure `split_blocks`/`packets_for` routines in [tools/probe_xma_multilayer.py](K:/SimpsonsNativeCopy/tools/probe_xma_multilayer.py:79); do not invoke that probe's main decode/RIFF routine. Mono sources are the short/documented SNU cases. Stereo is the complete `d_mvfe_xxx_000091c.exa.snu`; the three six-component layers are `bin.mus` index 52, in stored layer order. Require the pinned original SHA, exact bounded block/layer extents, selector 3, qualified `08 00 00 00` prefix, and defined final padding. Copy payload bits unchanged and restore only each qualified layer's stripped FF tail to a 2048-byte boundary. The assembler itself does none of this parsing or restoration.

Main has independently added `tools/prepare_xma_source_fixtures.py`, which reported all six hashes passing and places packets in `build/xma-source/fixtures`. It owns that helper and CMake integration. The test's two directory arguments may point to the same directory:

```powershell
NativeXmaSourceTests.exe K:\SimpsonsNativeCopy\build\xma-source\fixtures K:\SimpsonsNativeCopy\build\xma-source\fixtures
```

Link `NativeXmaSourceTests` against `SimpsonsAudio` and `bcrypt`, compile `/std:c++20 /fp:strict /W4 /WX`, and keep the existing verified codec DLL deployment. Main's CTest wrapper can prepare packets then invoke the test; no original image/runtime initialization is required.

Coverage includes full mono/stereo/six-component output; XMA2 plus one bounded XMA1 mono case; partial frame/plane boundaries; all eleven documented mono block transitions under explicit fixture skip/count; deliberately asymmetric layer skips; initial progress distinct from skip; real send backpressure; all-layer pending/resumption without duplicate sends; raw-ring wrap; receipt and retained-ticket backpressure; stale/foreign requests; explicit fresh surplus accounting; factory/owner/ticket lifetime separation; close cancellation; a real partial-layer storage failure; concurrent exactly-once commit; and successful commit with caller-side `operator new` disabled. Synthetic arrival/progress/skip metadata fixtures are labelled host contracts, not observed live engine profiles.

Standalone reproduction is [build/xma-source/probe.py](K:/SimpsonsNativeCopy/build/xma-source/probe.py). It compiles only the codec/source/test into a temporary directory, uses the verified owned DLLs and existing packet fixtures, then deletes the compiler products. [compile.log](K:/SimpsonsNativeCopy/build/xma-source/compile.log) is clean; [run.log](K:/SimpsonsNativeCopy/build/xma-source/run.log) records the final pass; [command.json](K:/SimpsonsNativeCopy/build/xma-source/command.json) and [result.json](K:/SimpsonsNativeCopy/build/xma-source/result.json) retain flags, commands and source/DLL hashes. No parent build, generated files, runtime hooks, original assets or reference projects were modified. Packet fixtures/any future derived media remain excluded from checkpoints.
