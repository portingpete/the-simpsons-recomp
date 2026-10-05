# EXm0 streamed-source admission contract

Frozen evidence, 2026-09-10. The smallest useful bridge is the **ordinary streamed, non-seeking EXm0 path into `823424D8`, LR `823424D0`**, retaining the original reader, producer, codec queue, consumption accounting and source-release calls. The native owner can configure the frozen `XmaSource` from the existing CPU header fields, copy the claimed block before producer writes, and associate its receipt with the original completion record and codec slot. This is an implementation specification; no live admission hook, decoder, game or output device was executed here.

The original flat image is `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461522c5d442083a0`. Original instructions, not generated code or inferred decompiler types, establish the fields below. Hexadecimal addresses/offsets; sample counts and capacity 20 are decimal. Existing [assembler contract](K:/SimpsonsNativeCopy/docs/native-xma-source.md), [trim proof](K:/SimpsonsNativeCopy/docs/native-audio-trimming.md) and [earlier bridge evidence](K:/SimpsonsNativeCopy/docs/native-exm0-bridge-contract.md) remain applicable.

## Admission ABI and the real source claim

At **entry** `823424D8` (before its prologue changes registers):

```text
r3=A stream owner; r4=Block; r5=i voice index
r6=original start/continuity input; r7=original seek input
W = A + BE16[A+1BC] + 30*i       voice record
P = BE32[A+50] + 50*i           stream-state record
j = byte[P+32]; R = A+54+10*j  producer completion record
V = BE32[W+8]                  actual allocated EXm0 instance
H = BE32[P+28]; M = BE32[H+4]  reader handle and manager
B = BE32[R+0]                  claimed buffer record
```

The specific caller `823423D0` obtains `B` by `BL 8238D568` at `82342474` (`4804B0F5`), finds a free R through original `82341898`, charges `P+18 += B[4]`, writes `R[0]=B` at `823424C4` (`93AA0054`), loads `r4=B[8]` at `823424C8` (`809D0008`), then calls the producer at `823424CC` (`4800000D`). Validate entry LR plus preserved `r29=B, r30=P, r31=A, r28=i` and incoming arguments. Other direct producer callers (`823428C4`, `82342988`, `82342A58`, `82342C88`) do not establish this lease and should remain rejected in the first profile.

`B` is **not a heap allocation whose payload follows it**. Original `8238BE00` separately allocates a 24-byte node N through allocator `BE32[82E36B94]` vtable+8 (`8238BF10` requests `18`; actual call `8238BF20`). It writes:

```text
N+0 next; N+4 entry token; N+8 byte length; N+C ring payload pointer
N+10 claim state; N+14 reader-handle ID
B=N+4 => B+0 token, +4 length, +8 payload, +C state, +10 handle ID
```

`8238BF48/4C/50` (`9163000C/91430004/91230008`) publish payload/token/length. At claim, `8238D5C0` (`3BAB0004`) returns N+4 and `8238D5C4` (`912B0010`) sets state 1. The original lock on M+8 also protects removal from H's pending queue and subtraction from H+C and entry+130. Require B+C=1, B+10=H+8, token index `< M[3C]`, and matching full token at `E=M[38]+138*(B[0]&FF)`. E+128 must be original block callback `823412C0`, E+12C must be A. `8238D100` stores these callback/owner fields at `8238D190/1A8`; the CPU configuration supplies them at `82342174/180/190`.

The manager constructor `8238C710(M, entryCount, ringBase, ringBytes, chunkLimit)` receives a **borrowed original ring**: M+64 and +68 initially equal ringBase, M+6C=ringBase+ringBytes (`8238C838/844/848`), and +88/+8C/+90 initialize to that base. Its caller `8238CD08` allocates M itself with extent `218` at `8238CD38/48`, then calls the constructor at `8238CD6C`. Admission must check the full `[Block, Block+B[4])` inside the live ring's captured extent, in addition to current readable mappings. Use the original constructor arguments to register a non-owning manager generation/extent if no existing allocation record supplies it; retain the actual AOT constructor. Capturing the extent does not transfer allocation/free ownership to EXm0. M+64/+6C are a consistency check, not permission to invent an allocation from arbitrary mapped memory. The physical/heap allocator that supplied the borrowed ring was not traced in this bounded pass.

Copy under a validated live claim, serialized against original source/manager teardown. M+8 alone is insufficient: `8238D640` writes B+C=2 **before** taking that lock. A native instance lease also does not keep A, B or M alive. Establish bridge lifecycle serialization, or capture/copy on the original claimed-source thread with teardown excluded; do not hold the native owner mutex across reentrant AOT calls. Source-record pointer reuse needs a new monotonically increasing native source identity, not pointer-only identity.

## Earlier CPU configuration and exact supported metadata

Retain `823418E8` (A's CPU constructor), `82341B20/82341F78` (queued CPU configuration), `823425E8` (header parsing), `82342748` (ordinary/seek setup), and `82342AE8` (real codec allocation and first enqueue). Do not substitute a fabricated source/root/descriptor.

`823425E8` reads bits MSB-first through `82341148`: 4 version bits, 4 codec bits, 6 channel-minus-one bits, 18 literal rate bits, 2 storage-type bits, 1 loop flag, 29 total-sample bits, then conditional loop fields. Verified writes:

- `82342648: 997E0030` writes codec ID to P.byte30; require **3 / EXm0**.
- `82342650/65C: 39630001 / 997F002B` writes channels = parsed6+1 to W.byte2B. Match the existing native instance's channels and `ceil(channels/2)` layers; first tested profiles are 1, 2 or 6 components. Each layer has 2 channels except an odd final layer has 1.
- `82342660..680` converts the literal 18-bit rate to exact binary32 at W+10. Require **48000 (`473B8000`)**, independently of the layer-word rate selector.
- `82342694: 997E0031` writes storage type to P.byte31; require **1, streamed**. `823426AC` writes total samples W+14. Nonloop configuration stores W+18=`FFFFFFFF` at `823426D0`; require this first profile. The parser ignores the version nibble rather than storing it: qualify the original header bytes at `823425E8` if a version check is required; it cannot be reconstructed from P later.
- `823427E8..42808` is the ordinary, no-seek branch: W+20/+24, P+3C/+40/+44 become zero and **P.byte4C=1** (`82342800: 995F004C`). P+48 is not initialized on this branch and must not be demanded zero as a guessed invariant; the producer does not read it when incoming r7.low8=0.

`82342AE8` takes Q's original lock/callback, resolves EXm0 descriptor `82D073AC`, calls real generic allocator `823404A8` at `82342B98`, and stores V at W+8. The existing owner captures V's allocation generation, extent and Q. Its exact extent is `align8(58+18*layers)+190` (20 queue records of 14 bytes). No literal rate or XMA1/XMA2 selector is passed to that constructor. Configure one owned XmaSource **later**, after the first source/header preflight, with the captured instance generation and explicit limits.

At admission require i<A.byte1C2, j<20, R.status=0, P/W/R within their original CPU allocations, V live in the native registry, matching Q=A[4]=V[4], P14+declaredFrames within W14 without wrap, and a writable free codec queue slot `V+V[24]+14*V.byte2F` (+C=0, index<20). Require the entry token matches P+2C as well as B+0 and E+0, and E+4 is nonzero. Validate writable producer fields and P14 before decoding. Use checked wide host arithmetic for every extent; reject guest address wrap.

## Framing, sample origin and explicit native policy

The reader callback **`823412C0`** first requires at least 8 available bytes. It extracts bit31 as the terminal marker, checks **low31(blockWord) <= available**, writes that length through r9, and, for a terminal block, clears bit31 in the **original guest header** at `8234132C..338`. Return 2 denotes this marked block; return 1 denotes an ordinary complete block; return 0 means insufficient input. The reader retains these real effects before publishing N.

Therefore, at the supported `823424D8` entry require **full BE32[Block] == B[4]**, positive and fully within the claimed ring interval. Do not mask an unexplained high byte: the producer itself later returns `Block+BE32[Block]` (`823425CC: 7C6BFA14`) without masking. Do not infer an EOF operation from the now-cleared terminal bit or an empty input queue.

DeclaredFrames is **BE32[Block+4]**, positive and within configured bounds. Starting at Block+8, walk exactly V[44] layer records in file order. For each `word`, length=`word>>2` includes its 4-byte header, payload starts at header+4, payloadBytes=length-4; require positive payload and no overflow/overlap/overrun. Require the final cursor equals Block+B[4] for the first profile. Original `8233F0CC..110` proves this walk. Require all layer low-two-bit selectors **3**, and the supported original packet framing. The original first initialization takes the first layer's selector at `8233F0A4..B0`; it is a rate selector, **not an encoding-generation bit**.

Use explicit **NativeRawF32 + native XMA2** as the qualified software policy for the existing tested EA streams. XMA1 and XMA2 produced the same tested raw output, but neither the EA codec ID 3 nor prefix `08000000` proves a universal hardware generation classification. A selector/profile outside this bounded evidence must fail explicitly.

Split each layer's own payload into 2048-byte packets without crossing into the next layer/block. The original copy helper `8233EEC0` copies only the requested final bytes and does not fill the tail. **Native FF tail restoration is an adapter policy, not an original write or zero-input/EOF rule.** For a short final packet, admit only an explicitly qualified payload extent/hash/sequence from the frozen prepared-source fixtures (or new independently validated evidence); use the exact recorded FF count to 2048. A prefix alone does not qualify arbitrary stripped tails. Full packets require no restoration. Existing packet preparation records all six original stream identities and spans in `build/xma-source/fixtures/manifest.json`; reading/copying those packets during this investigation is unnecessary. No padding bytes are written to original guest storage.

The ordinary first enqueue supplies r6=P.byte4C=1 and r7=0, so the producer passes **F=(r6.low8==0)=0**, initialConsumed=0, auxWord=0, auxByte=0. With a newly constructed instance's carry/skip zero and V.byte55=1, original `8233FB64..FBB4` establishes **384 skipped frames per layer**. Supply this explicitly to `Segment`, with `FreshContext`; do not place 384 inside the generic assembler.

Normal subsequent streaming is also pinned: `82341698/69C/6A8` sets r6=0, r5=0 and calls `823423D0`, hence producer incoming r6=0 and queued **F=1**, with no additional skip. Use `ContinueContext`, explicit skip=0, initialConsumed=0, and retain codec/frame/raw surplus. `82342828`, by contrast, supplies r5=1 at `82342868` before calling the same streamed caller: this is another **F=0** route, not ordinary continuation. Reject that restart/loop route for now; F=0 does not by itself justify resetting the codec. Reject seeks/nonzero initial progress, reset/error state, format changes and unsupported discontinuities before producer writes.

Carry must still reflect original output completion: for N declared frames, `r=(N+(F==0?384:0))&511`; nonzero r stores 512-r, zero r **retains** previous carry; error forces zero. This guest update belongs to output consumption, not early input preparation. F=1 does not consume/clear carry. The first-F0/later-F1 subset avoids predicting a later restart's skip from guest carry before earlier output has completed.

## Minimal runtime transaction and retained boundaries

1. At `823424D8`, recognize only the live EXm0 streamed claim above. Capture a bounded admission identity `(V generation,A,i,R,B,token,codecSlot,sequence)` and exact metadata. Copy all qualified layer packets into native ownership; lazily create the real XmaSource, call `prepare` once, and require `Accepted`. Keep the resulting receipt separate from raw surplus and eventual committed output. Retry must reuse the existing prepared receipt, never decode accepted packets again. Use R's index as the assembler's opaque slot, with a separate codec-slot association: original codec-slot reuse can precede R/B cleanup, so a retained delivered receipt must not collide with a newly published codec slot.
2. Fall through the **original producer** and `8234E768`. Replace its existing EXm0 rejection only when this prepared admission matches r3..r9 and LR `823425C4`. Keep all actual stores and queue-capacity logic. Original `8234E7D0` (`4E800421`) invokes `8233FAF0(V,slot)`, LR `8234E7D4`, after the slot writes but before V.byte2F advances. Replace this input callback with validated native receipt association, with no SDK call. A raw return zero is not an error protocol: zero is also a valid original slot.
3. The replacement owns `8233FAF0/8233F000` **as engine operations**, not hardware imports. Retain original CPU input-pop `8233E108` at the corresponding completed input admission and check its exact returned slot. It advances V.byte30, but does not clear occupancy. Publish V+50 selector and V.byte55=0 only after real native configuration/admission; no fake layer+0 hardware record. Native compressed/raw counters stay private. Do not emulate input-slot toggles or hardware-read cursors merely to satisfy the old validators; replace those validators with explicit configured-owner invariants.
4. On return, original enqueue sets V+1C when input/output indices match and advances V.byte2F. Original producer stores the slot in R.byteC and increments P14. An optional after-enqueue observer at **823425C4**, followed by fallthrough, can verify this expected slot/receipt before original R publication; it must not resubmit packets.
5. The actual nonzero decode callback `8233FAF8` still needs complete-all-layer ticket staging, checked guest publication and commit under lifecycle serialization. Native `NeedInput` or `Backpressure` cannot return as short success because `823628E0..F0` ignores callback r3 and advances the requested quota. Keep that guard until the output bridge exists; input admission alone is a valid smaller milestone. No extra 192 samples, stock576 trim, synthetic silence or implicit EOF.

The callback association in step 2 does not claim DSP consumption. The source is merely prepared and its compressed packets actually accepted. Do not wait for guest producer stores after `8234E7D4` inside that callback: they have not happened yet. Preflight/receipt state must be keyed by the actual incoming PPCContext/call depth so unrelated or nested enqueue calls cannot borrow authorization.

Do not inherit the assembler's raw-storage default without sizing the admitted profile. Twenty original input records can be queued before consumption; the already tested complete stereo source alone has 18 blocks and **90,624 raw frames**, exceeding the default 65,536-frame ring if prepared ahead. A bounded 131,072-frame budget per layer covers that tested whole-source extent; it is a native storage choice, not an original hardware limit. Derive/check aggregate copied-packet and raw-prefix budgets from the qualified source set, within the assembler's hard caps. New assets do not acquire this certificate from matching channel count alone. Source-pressure rejection before codec mutation and terminal decoded-storage overflow remain distinct outcomes.

**Failure point:** `823424D8` precedes its own producer stores but is not a wholly side-effect-free caller boundary. The original caller already claimed B, charged P18 and wrote R0. Invalid input/preparation pressure here must remain an explicit terminal failure with retained source-claim cleanup provenance, unless the original enclosing failure/unlock path is deliberately completed. A native restore-and-retry must not release/reclaim a live B or pretend the original Q lock has unwound. Failure after real codec mutation is terminal; retain its cause and accepted-packet state, never roll back just guest bytes and retry.

## Completion, teardown and current code changes needed by the implementer

Original mixer `8234F1D8` changes R.status 1 to 2 at **8234F6D8/DC** (`39400002/994B0061`), and advances A.byte1C6 modulo 20. This indicates original processing has reached the record, **not** that native admission may free its payload. Preserve this AOT loop.

Original `82362660` advances actual consumed samples, clears slot+C only on completion and advances V.byte31. Original `823417B8` then requires zero remaining samples, clears R.status, subtracts B[4] from P18 and calls **8238D640(H,B)** at `8234187C` (`4804BDC5`). That release marks B+C=2 at `8238D660` (`914B000C`), calls original byte accounting `8238BB88`, and may restart the reader through `8238C3F0`. The latter explicitly refuses to reclaim node state 0 or 1 (`8238C410..420`). Keep both accounting and reader wakeups. Retire native source metadata at the matched original release; codec receipt retirement does not free guest data, and raw surplus must survive.

Early voice teardown **`82342CC8`** deliberately destroys V first (`82342D0C: 4BFFD5DD`), then clears that voice's R records and invokes `8238D640` at `82342D5C` (`4804A8E5`) even for unconsumed input. Therefore configured instance destruction must close/cancel XmaSource, release its copied/raw/ticket ownership safely, and retain a bounded cleanup tombstone for outstanding R/B associations until these AOT source-release calls finish. Do not make original B release depend on dereferencing an already freed V. The generic destructor `823402E8 -> native8233EC58 -> original allocator free -> 82340360` remains unchanged in allocation/free ownership.

Current authoritative [engine_audio_owners.cpp](K:/SimpsonsNativeCopy/runtime/engine_audio_owners.cpp:106) still has constructor-only invariants: V.byte55=1, V+50 untouched, all queue slots empty, carry/remaining zero and layer fields zero except channels. `destroyBegin` calls this same `owned` validator. Merely allowing enqueue will therefore break valid destruction. The implementer must split immutable allocation/descriptor validation from Unconfigured/Configured/Failed/Closing state validation, retain the existing generation/allocator checks, and close configured/failed instances on the same original destructor path. `Instance::Layer::codec` is currently null: adding XmaSource must not create a second unused parallel set of decoders. The public `lease` only leases the native instance; it does not prove source-ring lifetime.

Leave direct hardware configuration `8233ED78`, reset `8233FF80`, context query `8233E568`, deleting-destructor bypasses and all unqualified entry paths guarded. Native factory enable/disable must serialize admission while retaining configured contexts; global teardown must reject or close outstanding owners consistently. No runtime/source/config changes are made by this report.

## Frozen checks and remaining bounded qualification

[analyze.py](K:/SimpsonsNativeCopy/build/exm0-admission/analyze.py) checks **96 instruction words, 26 complete original bodies, five direct producer callers and 15 analysis boundary cases**. The [report](K:/SimpsonsNativeCopy/build/exm0-admission/report.json) stores the image identity and body hashes. The small model cases check the reader's actual low31 length/terminal normalization and nonwrapping host bounds; they do not execute the AOT or certify a codec profile.

```powershell
python -B build/exm0-admission/analyze.py --self-test
python -B build/exm0-admission/analyze.py --verify
```

The required next live capture is concrete: A/i/W/P/V generation; claimed B/token/H/M and recorded ring extent; the original configuration header; each post-reader block/layer length and payload identity; first/continuation F, zero initial offset and initial carry/skip. This pass supplies exact field origins and call sites but makes no claim that boot's next asset already matches the six prepared streams. A new short tail needs explicit framing qualification before FF restoration. The borrowed ring's upstream allocator/free pair and exclusions against manager teardown must come from the runtime's real allocation/lifecycle record or the constructor capture above. Neither missing observation is permission to fabricate ownership, padding or EOF. Hardware BE16 numerical equality remains an optional comparison; the explicit NativeRawF32 policy is legitimate and is not the remaining admission gate.

Only this document and `build/exm0-admission/analyze.py`, `report.json`, and `verification.txt` were written. Production/audio/runtime/config/tests, generated output, original assets and reference projects remain unchanged by this task.
