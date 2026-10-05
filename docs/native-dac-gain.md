# Original and native six-channel Dac gain contract

Frozen bounded CPU processor and fixture, 2026-09-10. Runtime/CMake, original
assets, reference projects and generated code are unchanged by this task. The earlier
`docs/native-dac-sdk-contract.md` and `build/dac-sdk/*` remain frozen: their
read-only verification passed 70 original function hashes, 39 words and the
numeric routing table. This task owns only `audio/dac_gain.h/.cpp`,
`tests/test_dac_gain.cpp`, this document and isolated `build/dac-gain/*` outputs.
The earlier original-only fixture passed parent build124's full34-suite gate;
the native processor below is independently tested, not yet an activated source.

## Result and implementation guidance

The retained original AOT paths passed **41 calls and31,176 exact sample
comparisons**, including the real dispatch path. Every successful original case
now also compares native output and gain/progress bits. The extended fixture
passed **21 independent integer IEEE anchors, 53 native processed calls,
29 native rejection cases and3 native no-work calls**. The original helper needs
only a bounded CPU DSP work descriptor; the native processor uses native spans
and explicit state. Neither creates an SDK voice, audio root, device or worker.

For the observed six-channel, equal-48000-Hz, format0 source, preserve the
following numerical behavior before native transport:

- First activation copies the requested gain into both current and target.
  A later target change retains current and interpolates during processing.
- For a call, `remaining = destinationTotal - destinationProgress` and
  `n = min(sourceTotal - sourceProgress, remaining)`. Compute the binary32
  step as `round32(round32(target-current) / round32(remaining))`.
- Each frame multiplies all six source components by the same current gain,
  rounding each product to binary32. Only then advance current using one
  binary32 addition. Repeat exactly `n` times and persist that current value.
- The denominator is the remaining **destination** frames, including when
  only a shorter source fragment is available. Recompute it on the next call.
  Do not substitute the number of samples or the smaller consumed frame count.
- Do not snap current to target at the end. Rounded steps can overshoot or
  become zero. Immediate host `SetVolume`, a double-precision accumulated ramp,
  or a fused closed-form ramp does not establish this behavior.

This establishes the tested scalar arithmetic under guest nearest-even with
gradual underflow. It is not a claim of arbitrary VMX kernel equivalence,
hardware bit-exactness, physical speaker mapping, or complete SDK gain policy.

## Native API and rejection domain

`audio/dac_gain.h/.cpp` defines `Simpsons::Audio::processDacGain(source,
destination,state)`. `DacGainState` contains explicit `sourceFrames`,
`sourceProgress`, `destinationFrames`, `destinationProgress`, `current` and
`target`. Source is `std::span<const float>` of interleaved native binary32;
destination is `std::span<float>` of six planes, with the original fixed
256-float stride. This is not an SDK descriptor or a guest-memory view. The
caller owns live native float storage for the complete synchronous call. The
processor neither retains spans nor acquires SDK/host-output ownership.

`DacGainResult` contains `DacGainWork::Processed` plus consumed frame count,
or explicit `NoWork,0`. Counts must be0..256 and progress must not exceed total.
Current/target must be finite even for an empty request. A valid exhausted
source or destination returns NoWork before checking/accessing the spans and
changes nothing. That is an explicit safe dispatch result, not execution of the
original scalar loop's unsafe empty-count case.

Positive work requires both spans to have at most1536 elements. Source must
cover `sourceFrames*6` elements; destination must cover at least
`5*256+destinationFrames` elements. The last requirement includes unchanged
plane gaps/tails, so a short block is not a tightly packed `6*n` destination.
Range/size arithmetic is checked before reads, and both complete supplied spans
must be disjoint from each other and the state object. Even overlap confined
to an inactive tail is rejected. Caller-owned validity, float alignment and
absence of concurrent writes remain normal native-span preconditions; the API
cannot validate an invented pointer or expired allocation. No arbitrary raw
pointer probing, native memory fault recovery or overlap equivalence is claimed.

The supported arithmetic domain is finite current/target, finite **consumed**
input samples, finite rounded target-minus-current, finite step and finite
current after each processed frame, including the final increment. Unconsumed
source positions and old destination bits are not numerically interpreted.
Both signed zeros, signed subnormals and finite extrema are supported. Finite
sample products may overflow to signed infinity, matching the existing original
fixture: this processor does not clamp or reject those output infinities.
Input NaNs/infinities, gain subtraction overflow or later current overflow throw
`DacGainError`. No NaN propagation, infinite-gain multiplication, guest FPSCR
exception behavior or saturation behavior is invented.

All validation precedes external writes. A fixed1536-float local staging array
holds products while single-precision gain progression is checked; only a
completed result is copied to the active output positions and committed to
progress/current. Target and totals stay unchanged. Therefore even a late
arithmetic-domain failure leaves output/state unchanged. This stronger native
rejection contract does not claim the original kernel itself rolls back faults.
There is no allocation on the successful path. Concurrent access is unsupported;
this commit is not an inter-thread atomic transaction.

Each subtraction/division/multiplication/addition is an explicit scalar SSE
binary32 operation under private MXCSR1F80: nearest-even, gradual underflow and
masked exceptions. `/fp:strict` and precise/fenv compiler pragmas prohibit
reassociation; no fused gain calculation is substituted. The full incoming
MXCSR, including exception masks and pending flags, is restored on success and
exception unwind. Failure preflight classifies floats by integer bits, so even
signaling-NaN rejection needs no floating arithmetic. Native input/output are
host float values; BE guest conversion remains the caller's separate boundary.

The bounded processor does not initialize first-activation gains, choose a
category multiplier or issue host SetVolume. The caller explicitly supplies
current/target derived from those separately verified lifecycle policies.

## Original bytes and actual selection

Original derived `analysis/simpsons.pe`: base82000000, sizeEC0000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
`Runtime.load` verifies that identity. The fixture additionally hashes:

- 82C64960, 150 bytes hexadecimal, through its `blr` at82C64AAC:
  `1ca0a94c553d1c67e6a37b8688f5e3f94886438c2c7709da2e9222fa613cd718`.
- Dispatcher82C57E40, .pdata extent1BC:
  `7f564327c4955401b299e362a74d742a06e281c748246ea5c74601f3362a3da8`.
- Optimized entry82C6A0D8, .pdata extent350:
  `8054ed90040b68c5125cc26e0a396a437892f6e7cd48970ac36917b9d0dab68e`.

Exact disassembly is preserved read-only in corresponding
`build/dac-sdk/ADDRESS.asm.txt` files. Addresses and offsets below are hex;
frame counts, rates and channel indices are decimal.

The original creation record supplies format0, channels6, rate48000 and flags1
(see the frozen SDK contract's creation-record closure). Dispatcher82C57E40
selects the equal-rate family4 when D+54 bit0 is set and bit80 is clear. For
format0 and six channels, its pointer-table index is104 decimal:
**821B2D30 contains82C6A0D8**. It caches that pointer atD+4C, writes the float
rate ratio atD+2C and clears D+50. The fixture verifies these writes across the
whole descriptor page, then checks actual AOT trace entries.

82C6A0D8 checks the effective source and destination addresses for16-byte
alignment and the smaller remaining count for divisibility by8. Failure calls
**82C6A144:4BFFA81D -> 82C64960**, LR82C6A148. Original aligned S owns PCM
buffers atS+44 andS+1844: both are4 modulo16; advancing by24 bytes per frame
alternates that remainder between4 and12. Thus that ordinary S-owned source
path takes the scalar fallback, even for256 frames. This fixture executes the
dispatcher with that source alignment and proves the fallback, without running
application output. It separately proves count255 forces fallback with aligned
endpoints, and executes the true vector path for aligned256-frame, steady unity
gain with exact normal inputs only. The vector path uses different arithmetic
scheduling, including `fmadds` at82C6A1FC; no arbitrary ramp comparison is made.

## CPU-only descriptor and ABI

The synthetic work descriptor represents D=U+B8 in original source processing.
It is an ordinary mapped CPU record, not a constructed U/SDK voice. Only these
verified fields are initialized; surrounding bytes remain canaries:

- +00: interleaved guest BE-float source pointer.
- +04/+08: total available source frames / consumed source frames.
- byte+0C/+0D: format0 / channels6.
- +10: source rate48000; +20: destination rate48000.
- +14: destination planar guest BE-float base.
- +18/+1C: total destination frames / filled destination frames.
- +24/+28: current / target gain, raw BE binary32 bits.
- +4C: invalid initial cached kernel pointer, replaced by real dispatcher.
- +50: dirty flags3; +54: mode flags1.

Fixture counts/progress are bounded to0..256, positive rates and nonoverlapping
mapped source/destination regions. The dispatcher also initializes +2C=1.0.
No vtable, native identifier, callback, speaker label or hidden SDK global is
needed. The fixture leaves original singleton words82E2D9F0/4 and root82E31BCC
unchanged and acquires no audio owner, thread, handle, heap or physical allocation.

**Scalar ABI:** r3=D; no other argument. It leaves r3=D and writes only D+08,
D+24 and D+1C. It writes BE64 remaining destination frames at incoming SP-10
for `fcfid`/`frsp`; it does not allocate a frame or call another function.
The fixture supplies nontrivial r4..r7 to avoid assuming spare arguments and
checks all nonvolatile GPR14..31/FPR14..31, r2/r13, CR2..4, SP/LR, sticky SAT
and the exact direct-kernel stack footprint. `EngineCpuCalls` provides the
checked caller frame and restores TLS/host FP state.

**Dispatcher ABI:** r3=D; returns consumed source frames in r3, including0
on exhausted source or destination. Its empty check precedes kernel selection
and PCM accesses. The fixture uses invalid PCM pointers for these exhausted
cases and confirms no descriptor-cache or output mutation.

Do not call the scalar kernel directly with zero/negative remaining work. Its
loop starts without an empty-count guard. The fixture tests the dispatcher's
safe empty behavior instead. Non-six-channel descriptors, giant counters,
overlapping input/output and malformed nonempty cache pointers are outside this
bounded contract.

## Arithmetic, persistence and activation evidence

At82C649E4 (`EDAD0028`) the kernel subtracts current from target with `fsubs`.
82C649E8..F4 converts the signed positive remaining destination count through
`std`/`lfd`/`fcfid`/`frsp`;82C649F8 (`EDAD6024`) performs `fdivs`. It reads
and stores lanes in order5,4,3,2,1,0, but preserves each component index:

`source + 4*((sourceProgress+j)*6+lane)` becomes
`destination + lane*400 + 4*(destinationProgress+j)`.

Each product is an original `fmuls`;82C64A54 (`EC0D002A`) adds step after the
frame. Final writes at82C64A90/AA4/AA8 update source progress, current and
destination progress. Target+28 stays byte-identical. Source/output guards cover
entire2000-byte regions and the descriptor guard covers an entire1000-byte page.

The integer oracle decodes finite IEEE binary32 significands/exponents and
performs addition/subtraction, multiplication and rational division using
integers with nearest-even rounding and gradual underflow. It does not obtain
expected samples from host float math or reuse generated arithmetic. Anchors
include normal/subnormal ties, cancellation, signed zero, 1/3 and finite
multiplication overflowing to infinity. Output inputs contain finite normal
values, both signed zeros, signed subnormals and maximum finite values.

Meaningful persistence cases include denominator7 with only6 source frames,
a destination-limited3-frame call, and one17-frame destination filled over
successive source availability3/8/17. In the last case, current and progress
persist across actual calls; each recomputes the remaining denominator.
With current bits00000001, target00000003 and3 destination frames, the rounded
step is one minimum subnormal and current ends at00000004. With current00800000
and target00800001 over3 frames, step rounds to zero and current stays00800000.
Both are checked explicitly to reject an invented final snap-to-target.

First activation remains static evidence: with the source's first-activity bit2
set,82C474B8 (`C01A0070`) loads requested U+70;82C474C0 (`D01A00E0`) stores
target U+E0=D+28;82C474C8 (`C01E0028`) reloads it;82C474D0 (`D01E0024`)
stores current D+24. The fixture pins these four words and tests the resulting
equal-current/target precondition against an actual ramp. It **does not execute
the surrounding SDK-owned activation routine** or fabricate its voice fields.
Later original setter82C463E0 writes U+70 and U+E0, retaining current U+DC;
activation/callback/category locking remains separate integration work.

### Separate source starvation and processing-time policy

The enclosing source processor82C472E8 has effects intentionally absent from
the pure gain API. On its pass-setup path, **82C47474:3BDA00B8** sets D=U+B8,
**82C47478:39400100** loads256, **82C47480:915E0018** writes D+18 and
**82C47484:92FE001C** writes zero to D+1C. r23 is the preserved zero initialized
by **82C472F8:3AE00000**. Thus a new source processing pass establishes a fresh
256-frame destination; fragments within it still use the remaining-frame
denominator proved above.

The no-queued-packet decision is **82C47770:2B1D0000** (`cmplwi r29,0`) and
**82C47774:419A00F8** (branch82C4786C when null). After reacquiring its SDK
lock, **82C478A8:3BDA00B8** recovers D; **82C478B0:C01E0028** loads target
D+28 and **82C478B4:D01E0024** writes current D+24. This enclosing starvation
path **does snap current to target**, before calculating and clearing remaining
destination frames. It does not change the scalar helper's unsnapped arithmetic.

Exact clear setup:82C478B8/BC loads progress/total, **82C478C4:7D4B5050**
subtracts them; **82C478D8:555B103A** forms remainingFrames*4;
**82C478DC:557C103A** forms progress*4; **82C478E8:38800000** supplies zero;
**82C478F0:4BDF4B51** calls82A3C440 with that byte count. The channel loop
advances destination by400 at **82C478F8:3B9C0400**. The empty-work result of
`processDacGain` does not execute or impersonate this source-starvation branch:
it leaves current and output unchanged. Future source ownership must explicitly
implement the enclosing snap/zero policy at the verified starvation point.

With an actual queued packet, **82C477B4:4801068D** calls82C57E40 after loading
that packet's source pointer, extent and current consumption progress. The
scalar kernel then reads current/target during processing. Baking gain into
PCM at packet acceptance would not establish equivalence when the target
changes while packets remain queued; it would commit the earlier target before
the original consumption-time read. The future source bridge therefore needs
an explicit processing/consumption timing and synchronization contract. This
fixture does not select that timing or implement callbacks, starvation, packet
ownership or a voice. These are byte-derived integration requirements, not an
executed source lifecycle claim.

`python -B build/dac-gain/verify_queue_policy.py` passed all20 original words
above against the verified image and preserved `queue-policy.json`. It executes
no original SDK/source routine and leaves the earlier SDK report frozen.

Both test passes use a guest nearest-even/no-flush FP context. The host starts
either normally or with FTZ/DAZ plus round-toward-zero; exact host MXCSR restores
on successful and rejected AOT calls. Guest nondefault rounding, NaN payloads,
FPSCR exception fidelity and general VMX/subnormal behavior are not claimed.

Three checked-memory failures verify the exact first fault and no preceding
PCM/descriptor writes: invalid descriptor read3008; read-only output's first
store61480 (lane5); and first source load52000 beyond a mapped boundary. There
is no transactional rollback claim for a later fault after earlier stores.

The native comparisons use separate owned float arrays initialized from the
same BE sample bits. Every successful original call compares complete output
planes, inactive tails, progress, current, target and totals; prefix/suffix guards
and source preservation are checked separately. The fragmented-source sequence
retains native progress/current across calls while the caller exposes3/8/17
available frames. It does not reset native state from the original between calls.

Native-only failure tests cover every counter bound, short/oversized/empty spans,
exact and partial overlap, overlap with state float members, late consumed
NaN/infinity, nonfinite gain, difference overflow and **late current overflow**.
For the latter, target7F7FFFFF from current0 over10 frames overflows on its tenth
rounded addition (step7DCCCCCC); the integer oracle checks that fact, and the
processor rejects without publishing earlier staged products. These rejected
exceptional cases do not assert general original exceptional equivalence.

Native FP tests additionally execute all four caller rounding directions with
FTZ/DAZ each on/off, all exception masks clear and pending flags set, using both
subnormal-producing0.5 gain and overflowing2.0 gain. Exact output/state and full
MXCSR restoration pass. Rejection tests use the same unmasked hostile setting.
Exact minimal destination extent, nonfinite source bits strictly outside the
consumed positions and empty zero-size state are also exercised.

## Reproduction and integration

Executed independently against existing parent static libraries on2026-09-10:

```powershell
python -B K:/SimpsonsNativeCopy/build/dac-gain/run_probe.py
```

Compile exit0 with no compiler diagnostics, run exit0, result:
`PASS original/native Dac gain: 41 AOT calls, 31176 exact sample comparisons,
21 integer-oracle anchors; 53 native processed, 29 native rejections,
3 native no-work; guards/ABI/MXCSR; no output device`.
The three memory-failure diagnostics in `build/dac-gain/run.log` are expected
rejection cases. `compile.log`, `command.json` and `result.json` preserve the
exact ClangCL command, native source/header/original/test/executable/library
hashes and result. Only the owned native source and isolated fixture are
compiled; no CMake, regeneration or parent build
is invoked, and no audio output or SDK device code runs.

Parent integration selected: add `audio/dac_gain.cpp` to `SimpsonsAudioOutput`,
whose existing options are `/fp:strict /W4 /WX`. Its existing Runtime dependency
makes the helper available without source/worker activation. An isolated
compile of the frozen native source with those exact warning/FP options passed
exit0 with no diagnostics; `compile_strict.py`, `strict-compile.log` and
`strict-compile.json` preserve that command and source/header hashes. Keep the existing
`DacGainTests` source/links (`SimpsonsRuntime`, `bcrypt`) and `OriginalDacGain`
CTest, timeout30 seconds, command
`DacGainTests <absolute analysis/simpsons.pe>`. No runtime source registration
or hook/worker activation is needed. Keep the usual native
codec DLL search path required by the runtime library; the fixture itself does
not create a codec. The standalone script imports only the existing
`tests/test_host_fp.py.toolchain()` helper, then links the existing Runtime/PPC/
Audio/Graphics/AudioOutput and codec import libraries; the complete list is in
command.json. Linking the output library satisfies current runtime references;
the gain fixture does not call it or acquire an output device.

Physical speaker labels and Windows downmix policy remain unproved. Component
indices are preserved, not renamed FL/FR/FC/LFE/side/back. Category0 starts at
unity in original code, but later original platform category-volume events do
not yet have a proved native replacement. This fixture accepts a supplied
effective target; it does not declare requested gain1 equivalent to unity under
every category policy. It also does not replace the separate engine CPU fade,
establish audible output fidelity, or implement worker/self-destruction lifetime.
