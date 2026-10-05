# EXm0: next bounded integration slice

Frozen 2026-09-10. **The next useful implementation is an owned prepared-source/quota assembler, with an explicit native PCM policy.** Native FFmpeg float PCM is a legitimate policy; matching the console's exact BE16 quantization is not a prerequisite. The necessary integration gates are verified live sample origin/skip/count, bounded source framing and ownership, full-quota publication, and teardown. The current native codec does not implement those engine contracts by itself, so simply removing `8233FAF8` remains insufficient. Do not introduce stock offset 576 or return temporary starvation as a successful short guest quota.

This investigation only wrote this document and `build/exm0-next/analyze.py`, `report.json`, and `verification.txt`. It executed no game, codec, build, or emulator and produced no media. Original/reference files and production sources were read-only. It reuses the frozen [bridge contract](K:/SimpsonsNativeCopy/docs/native-exm0-bridge-contract.md), [trim/converter proof](K:/SimpsonsNativeCopy/docs/native-audio-trimming.md), and [multilayer probe](K:/SimpsonsNativeCopy/docs/native-xma-multilayer-probe.md).

## Numerical policy: explicit native choice, optional hardware comparison

Original `8233F250` produces values from the finite lattice `signed16 / 32768`. The existing raw decoder output does not generally belong to that lattice. An exact integer/rational check of the **existing**, hash-pinned mono output finds:

- Short fixture, raw sample 448: binary32 `B8169C65`; multiplying its exact value by 32768 gives `-9870437/8388608`, not an integer.
- Documented fixture, raw sample 448: binary32 `36CA1DA5`; multiplying its exact value by 32768 gives `13245861/67108864`, not an integer.

These are concrete counterexamples to claiming raw floats are bit-identical to the original converter's output. They do **not** prohibit a native float output policy. Adopt `NativeRawF32` explicitly for the first assembler: retain the finite raw decoder values and logical component order, with no extra quantization, clamp, reorder, hidden trim, or EOF. A separately defined signed-16 policy would also be possible; it must name its rounding/saturation behavior rather than claim an unproved hardware rule. Neither choice changes the required sample origin or original skip/count arithmetic.

If exact hardware comparison is later desired, useful evidence would be original BE16 output **before `8233F250`** for a pinned compressed sequence, including startup and a segment transition with actual F/skip/carry recorded. Ties, extrema and any stateful numerical differences would need further coverage for a general equivalence claim. That evidence is currently absent and is a fidelity limitation, **not a mandatory oracle for native audio progress**. Independently, the runtime must identify where its actual submitted source begins and derive its skip/count from the original live contract; software offset matching is not permission to alter that timeline.

## New bounded result: quotas fit recorded block prefixes

[report.json](K:/SimpsonsNativeCopy/build/exm0-next/report.json) checks the recorded greedy-drain CSVs against each EA layer's adapted packet boundary. It never reads ahead to packets belonging to the next block when computing available frames.

Under the **conditional** profile “first segment F=0, subsequent segments F=1, initial carry/skip zero,” all **45 layer/block prefixes** have at least `384 + cumulative declared samples` raw frames available; **39 have exactly zero spare frames**. This comprises 32 prefixes from the owned multilayer probe and 13 from the frozen mono capability probe. The existing owned-codec fixture independently matches the mono PCM hashes; this pass did not rerun either decoder. The flag profile is a condition of this accounting exercise, not a new observation of every live engine enqueue.

The first block supplies exactly 5120 raw frames for 4736 declared frames in all eight layer streams. It requires one packet for the mono/stereo cases and two for the four-/six-component layers. Last-prefix surplus is 256 frames for short mono, 11 for documented mono, 241 for complete stereo, zero for each four-component prefix layer, and 39 for each six-component layer. Adding the unproved extra 192 samples makes **43 of 45 prefixes short**. Thus the software evidence supports a bounded same-block quota assembler with the original skip hypothesis; it does not justify another trim, EOF, or invented samples. Four-component evidence is a selected loop-body prefix, not loop/reset qualification.

## Exact current adapter gap

[audio/native_xma_codec.cpp](K:/SimpsonsNativeCopy/audio/native_xma_codec.cpp:110) already owns each accepted 2048-byte packet. `NeedDrain` means retry the identical packet after productive reads; it does not advance the input cursor. `read` at line 121 returns at most the remainder of **one** 512-frame AVFrame. For example, after consuming a 384-frame prefix, a request for 256 returns 128 from that frame, then another read is required. No asynchronous wait can turn this into an atomic guest quota.

The pinned primary [FFmpeg send/receive contract](K:/SimpsonsNativeCopy/build/audio-codec/install/include/libavcodec/avcodec.h:155) explicitly makes progress depend on changing send/receive state, not elapsed time. The wrapper has no source-generation transaction, EA layer parser, skip/carry accumulator, guest-output staging or hardware quantizer. It also exposes no EOF operation. Keep `reset()` separate from ordinary empty reads and from the original error-reset contract.

At **823628E0** (`4E800421`), the unbuffered original consumer invokes `V+14(V, output, quota)`. **823628E4/E8/EC/F0** (`7FC4F378`, `7FE3FB78`, `7F9EE214`, `4BFFFD71`) immediately reuse saved quota and call `82362660`; they ignore the decode result. Therefore a native callback must prepare every layer's complete quota before exposing output and committing decoder-visible progress. An unrecoverable short/error result must remain an explicit failure, not a normal return. Already accepted compressed decoder state cannot be rolled back by restoring guest fields.

## Input transaction and ownership changes required

The current [owner](K:/SimpsonsNativeCopy/runtime/engine_audio_owners.cpp:28) has layer channel counts and **null codec pointers**; it is deliberately unconfigured. `owned()` at line 106 validates V+3C/+48/+4C zero, V+55 one, saved untouched V+38/+50, zero layer state except +C, and an entirely empty generic queue. `destroyBegin()` at line 248 calls that same validation. Merely enabling enqueue would make normal destruction reject the new legitimate state. `lease()` protects the native instance generation; it does **not** lease a compressed source allocation.

The first useful component is **bounded source preparation plus native quota staging**, using `NativeXmaFactory::create`, `send`, and `read` unchanged. This can be implemented and tested now under `NativeRawF32`; no hardware-quantization evidence is required. The runtime bridge supplies the live source proof and original accounting before replacing its guarded callback for that profile. An initial integration profile can be mono, selector 3/48 kHz, ordinary zero aux/offset, no seek/loop/reset; matching fixture bytes alone is not a live source-allocation proof.

Proposed narrow host interface, with no guest pointers or SDK descriptors:

```text
PreparedSegment:
  instanceGeneration, segmentSequence, engineSlot
  continuity = FreshContext | ContinueContext
  declaredFrames, initialConsumedFrames
  skipFramesPerLayer[]                  # explicit caller-verified counts
  layers[] = { XmaFormat, ownedPackets[] }
  policy = NativeRawF32
  limits = { maxCompressedBytes, maxStagedFrames }

OwnedPacket: exactly 2048 owned bytes; input spans are copied/moved into ownership

prepare(PreparedSegment&&) -> SourceReceipt
stage(SourceReceipt, quotaFrames) -> CompleteQuotaTicket | NeedInput
commit(CompleteQuotaTicket)           # validated, nonallocating logical commit
retire(SourceReceipt)                 # after real original consumption/retirement
close()                              # terminal host cancellation/release
```

`prepare` validates all extents, layer formats, explicit progress, generation/sequence and bounded storage before admission; it must not accept a borrowed span for deferred reading. Each stream layer retains its own codec, pending packet and partial AVFrame across continuation segments. `FreshContext` is admitted only at a proved source start; it is never inferred from a temporary empty read. `skipFramesPerLayer` supplies the exact pending skip at this segment boundary. There is **no 384 constant, inferred F value, or stock delay correction in this host interface**. The runtime derives those values using the retained original rules; the 45-prefix experiment supplies conditional fixtures, not live defaults.

`stage` loops through retained AVFrame tails and packet send/drain until every layer has the full requested quota after its explicit skip. It retains all surplus samples in order and returns owned logical-component planes plus expected source/progress identity. Quota must be positive and no greater than the segment's remaining logical extent; `initialConsumedFrames` is progress metadata, not an implicit second trim. A zero-work result may be separate. Nonfinite output, budget overflow, decoder error or stale identity is explicit failure. `NeedInput` stays a host-side preparation state; the original full-quota callback cannot return it as success.

Once a packet is accepted, compressed codec state may have advanced even if a ticket is not committed. Retain the staged samples and pending skip/progress transaction so an uncommitted ticket can be retried without decoding twice. If continuation cannot proceed, mark the owner terminal and release it through the real lifecycle. Do not restore guest fields and pretend the decoder rolled back. The bridge preflights all guest output/metadata writes, serializes against destruction, publishes only a complete ticket, then performs the nonallocating commit. The bridge owns the checked original V+48/skip/carry updates; original outer queue advancement and actual source frees remain original CPU work. `close` releases only native ownership and does not fabricate those guest frees.

Before making that component a successful original input callback, these exact boundaries must be covered:

1. **Producer preflight:** use `823424D8` before its stores, for the recognized EXm0 voice. Incoming r3 is the stream owner, r4 the outer block, r5 the voice index; r6/r7 determine the existing ordinary/seek enqueue arguments. `82342560/564/568/574` already write producer record +E voice, +D status 1, +8 progress, and +4 owner before `823425C0` calls enqueue. Consequently the current `8234E768` guard is before **codec queue** publication, but after producer mutation. A recoverable preparation failure needs earlier preflight or explicit producer rollback; current terminal failure does not promise rollback. This is not an all-caller rollback point either: the streamed caller at `823424C4` has already stored the allocation record in stream record +0 before its call at `823424CC`. On that specific path, original `8238D568` supplied r29, `[r29+8]` supplies the block pointer, and `[r29+4]` is charged to the outstanding-byte count at `8234248C..98`. This gives a concrete allocation-record association for a narrow snapshot/lease bridge; other caller paths still need their own association. A header-supplied size and mapped RAM alone are not ownership evidence.
2. **Queue publication/notification:** original `8234E768` stores the slot before `8234E7D0` calls `8233FAF0(V,index)`, LR `8234E7D4`. That callback is a tail branch to hardware feeder `8233F000`. Only after it returns does the original body write V+1C when appropriate and advance V.byte2F. Preparation must finish before slot mutation; post-acceptance failures require terminal native state, not “restore guest and retry.” Retain original queue capacity **20 decimal** and distinguish slot occupancy from callback return zero, which is also a valid first slot index.
3. **Packet framing/configuration:** layer length is `(BE32(header)>>2)-4`; each layer keeps its own packet cursor. Original `8233F16C/1EC` calls `8233EEC0` with `min(remaining,2048)`. The complete copied helper body contains no tail fill: its remainder calls memcpy at `8233EFF4`, then returns. The probes' FF restoration is a qualified software adaptation, not an original guest write. It can be the explicitly selected native adapter for its validated EA framing profile. Do not infer that arbitrary tail contents are harmless: pinned `wmaprodec.c:1775` can save remaining packet bits for a following frame. The bridge must validate the live source's profile/bounds and perform that adapter before handing the assembler owned full packets; `NativeXmaCodec::send` itself requires all 2048 bytes.
4. **Consumption/retirement:** maintain separate identities for input accepted by the codec, raw output available, full guest quota committed, and original source record released. Native quota staging must not advance V.byte31 or clear slot+C: original `82362660` owns those writes after callback success. Original `823417B8` observes remaining samples and performs the real source free through `8238D640` at `8234187C`. Keep that release, and make configured/pending/failed instance teardown valid without reviving a stale V generation. Do not write native IDs into the original layer hardware-record field.

These are the smallest concrete missing ownership/state transitions; none is supplied by the existing raw codec test. No new production interface or hook was implemented here.

## Guard decision and verification

Implement the prepared-source/quota assembler next under the explicit native float policy. Then replace the input/decode guards only for a live profile with proved packet origin/framing, supplied original skip/count, checked ownership and complete-quota commit/teardown. **Hardware BE16 matching is not one of those required gates.** The existing raw codec and conditional prefix evidence provide a concrete starting point for that implementation without guessed trim or dummy success.

The only immediate decode-entry relaxation without any of those engine dependencies is **r5 quota = 0**: original `8233FB20/24/28/2C` returns r3=0 before reading V/output or invoking hardware. This is a genuine no-work case, but it does not advance actual nonzero decoding and should not be presented as the next audio milestone.

Reproduce from `K:\SimpsonsNativeCopy`:

```powershell
python -B build/exm0-next/analyze.py --self-test
python -B build/exm0-next/analyze.py --verify
```

Three analysis self-tests pass; the report pins the original flat-image SHA-256, **34 instruction words**, **five complete bodies**, frozen reports/CSV hashes, and current adapter source hashes. It checks 45 software prefixes and two exact lattice counterexamples. `--verify` is read-only and also rejects changes to the recorded current-source snapshot; rerunning without it intentionally refreshes only this task's JSON. These checks do not claim hardware equivalence, an executed original enqueue/decode lifecycle, or any production guard removal.
