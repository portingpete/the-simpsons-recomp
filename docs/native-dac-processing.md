# Native Dac source processing owner

Frozen bounded implementation and standalone evidence, 2026-09-10. Owned
files are `audio/dac_processor.h/.cpp`, `tests/test_dac_processor.cpp`, this
document and `build/dac-processing/*`. No runtime hooks, guest ABI fields,
CMake, generated code, original media or reference projects were edited by
this task. Runtime activation and original worker/callback integration belong
to main. This component creates no guest object or SDK voice layout.

## Two separate ownership stages

`Simpsons::Audio::DacProcessor` accepts an existing `NativeAudioOutput&`.
Construction requires that backend to be configured, stopped, empty, healthy
and capacity4. The caller retains the backend and its initialized MTA through
processor stop/destruction. The processor then exclusively owns backend
submission, polling and receipt retirement. Do not run another backend poller,
submitter, retire operation or start operation concurrently. External backend
stop/failure is diagnosed; arbitrary competing ownership is not supported.

Logical source capacity is **two**, including terminal source receipts until
the caller retires them. Admission copies exactly256 frames of interleaved,
six-component finite normalized binary32 PCM. Each occupied slot owns its raw
copy through logical retirement. Admission neither applies gain nor implies
DSP processing or playback. Backpressure returns no receipt and changes no
ownership. Malformed PCM is rejected before allocation/publication.

A source receipt becomes **Consumed** only after all256 frames pass the exact
scalar gain helper, the planar results are interleaved without component
permutation, and `NativeAudioOutput::submit` actually returns Accepted. Source
`processedFrames=256` alone is insufficient: during the intervening state it
remains Processing with no downstream receipt. Processing target/current/final
gain and the accepted backend generation/sequence are preserved in the source
completion. After this commit, source current advances to the DSP result.

The downstream receipt is a separate ownership stage. Its Pending status is
not a claim that the raw source still needs DSP. The backend retains copied
processed PCM until its real natural `OnBufferEnd`, or until actual stop drains
callbacks/data access. The processor harvests terminal results and calls real
backend `retire`. Natural Consumed, stop Cancelled and Failed outcomes retain
their backend meaning; none is inferred from copying, source acceptance or a
timer. Natural `OnBufferEnd` is not an audible/device-clock completion proof.

Source retirement and downstream completion can occur in either order. Retiring
a Consumed source frees its logical slot even while the downstream receipt is
Pending. A downstream record keeps the complete source generation/sequence
after that source has been retired, so slot reuse cannot reassign provenance.
Conversely, real downstream completion does not free an unretired source slot.

Failures before the source commit mark affected queued/processing source
receipts Failed, never Consumed. A backend error occurring after a successful
source commit does not undo that already completed DSP/acceptance stage: it is
reported as a distinct downstream failure plus the terminal processor failure.
The snapshot preserves the first reported terminal error and operation, source
and downstream identities, and all still-owned source/downstream statuses.
The existing backend itself preserves the first callback error; no claim is
made to reconstruct later callback errors that it does not expose.

## API, event and bounded observation

The public API is `submit`, `setTargetGain`, `activate`, `query`, `retire`,
`poll`, `wakeHandle`, `wait`, `stop`, and `generation`.

- `setTargetGain` accepts finite [0,1], including negative zero. It changes
  target only; it does not rewrite accepted PCM or snap current.
- `activate` calls the actual backend start and, on success, seeds current
  from the latest target. Repeated activation is idempotent and does not
  reseed a running ramp. Activation failure is terminal and observable.
- `query` works for owned receipts, including terminal receipts after stop.
  `retire` accepts only Consumed/Cancelled/Failed. Queued, Processing, unknown,
  retired and foreign-generation receipts reject. There is no success for an
  unknown native identity.
- `poll` works before activation, during processing, after failure and after
  stop. Snapshot includes `active`, `stopping`, `stopped`, `workerExited`,
  current/target, source counts/statuses, downstream records and failure.
- `wakeHandle` is a borrowed manual-reset event. The owner must stay alive
  throughout every wait. The caller never closes/resets it. `wait` supports
 0..60000ms and distinguishes signal/timeout; the caller must inspect snapshot
  failure/stopped, not interpret a wake as successful audio work.

There is one snapshot/event consumer. `poll` resets the observer event while
holding the same mutex used to publish state and signal it, copies a coherent
snapshot, then acknowledges already-retired downstream records. Updates after
that critical section signal the event again. Merely waiting does not release
source receipts or acknowledge downstream provenance.

Four bounded downstream provenance slots retain Pending or unobserved terminal
records. Backend retirement happens immediately on real terminal observation;
the record itself remains until `poll` copies and acknowledges it. A full
provenance set applies backpressure until observation, even if native playback
already ended and backend storage was retired. This explicit bounded observer
backpressure prevents silently dropping receipt evidence or accumulating an
unbounded history. Parent's original-worker loop should poll each wake and
retire logical source receipts only after delivering the required original CPU
completion callback. `backendRetired` is monotonic and counts actual backend
retire operations; it is not a submission or source-consumption counter.

`processingHints` increments on observed/coalesced backend event wakes,
including the backend's real empty processing-pass notifications. The backend
provides a manual event, not an exact pass counter; these hints can also include
completion/error signals and do not identify every physical processing pass.
They are monotonic observer hints, not sample-clock timestamps. Inactive owners
can report them: an event does not authorize bypassing source activation checks.
Activation signals both processing and observer events. No queued PCM is
processed until activation succeeds.

## Scheduling, gain and empty demand

The dedicated native thread initializes its own MTA and waits on its private
stop event, work event and borrowed backend event. It processes queued sources
in receipt order only when active and downstream storage/provenance capacity
is available. The thread never reads guest memory, executes original CPU code,
invokes guest callbacks or accesses original graph locks.

The current and target gain are captured at **DSP processing**, under the
processor mutex. `setTargetGain` linearizes against that capture. The mutex is
released during the bounded gain work; a later target update applies to later
processing rather than rebaking the block already in progress. Stop can cancel
between DSP and submission. Submission/commit is serialized against public
stop requests. Processing uses the existing verified `processDacGain` with
source/destination totals256, progress0 and fixed256-frame plane stride.
There is no additional reorder, clamp, forced endpoint or denominator change.

When active backend demand is observed with no queued source packet, current
is set to target, matching the verified enclosing source starvation policy.
No synthetic zero packet, source receipt or consumption is invented. The host
backend supplies its native underflow behavior. `starvationPasses` counts these
observations; it is not an original SDK execution count.

The scalar/no-endpoint-snap evidence and the enclosing starvation snap/clear
are pinned separately in `docs/native-dac-gain.md` and
`build/dac-gain/queue-policy.json`: original82C477B4 calls the DSP dispatcher;
the null-packet branch82C47774 reaches82C478B0/B4 target-to-current copy and
remaining-plane zeroing. Processing-time target selection, not gain baking at
packet admission, follows that separation. First activation seeds both gains
from the requested target as established at82C474B8..D0.

Source capacity2 and host capacity4 are intentionally distinct. Four host
blocks represent1024 frames, approximately21.33ms at48kHz, before considering
endpoint latency; this is storage duration, not a latency measurement. The
capacity4 choice accommodates the parent's observed Windows480-frame
processing-pass cadence while retaining source256-frame blocks. It is an
explicit Windows buffering/scheduling adaptation, not proof of the original
Xbox scheduling, physical speaker mapping, audio latency or acoustic output.

The generic gain setter permits [0,1], but repeated binary32 addition can
leave current slightly outside that interval for arbitrary fractional ramps.
The existing scalar helper intentionally preserves that result. If subsequent
products violate the backend's normalized admission domain, the processor
reports a terminal DSP/submission failure, retains provenance and stops. It
never clamps away overshoot or reports such a source as Consumed. Thus a valid
setter input does not guarantee arbitrary later PCM/ramp combinations are
admissible. Main's observed runtime profile restricts original gain requests
to exact0/1; a full256-frame0-to1 or1-to0 ramp uses exact dyadic steps. Category
policy and broader fractional-ramp admission remain separate integration scope.

## Stop, failure and FP scope

`stop` is irreversible and idempotent. It rejects self-join on the private
processing thread; no client callback is invoked there, so ordinary callers
cannot enter that path. A separate original/native Dac worker may call stop.
Concurrent callers serialize the join. The join occurs without holding the
processor state mutex. The native processing thread calls real backend stop,
harvests drained terminal receipts, marks unfinished raw sources Cancelled or
Failed, and exits. `workerExited` is published only **after actual std::thread
join**, not merely when a worker body marks itself finished.

`stopped` means backend drainage and processor terminal state are available;
after asynchronous failure, `workerExited` may remain false until the caller
calls stop and joins. The caller must still stop/join before releasing the
backend, apartment, processor or borrowed event. Destruction performs that same
join; concurrent C++ destruction/public access is outside the ownership contract.
The processor does not attempt to join the original game worker; main owns that
outer graph/root lifetime boundary.

Stop before activation cancels raw slots without submitting PCM. Stop after DSP
but before accepted submission preserves `processedFrames=256`, cancels the
source and leaves the downstream identity zero. On a terminal processing/backend
failure, no automatic device reopen, format substitution, retry, successful
source completion or fabricated frame is supplied. Original callbacks and
logical receipt-to-S3100/3104 mapping remain parent runtime responsibilities.

Admission/gain validation uses integer float bits and does not raise exceptions
on signaling NaNs. The gain helper preserves MXCSR exactly. Backend operations
use the existing backend FP scope. COM initialization, terminal cleanup and COM
uninitialization explicitly use masked nearest-even/no-FTZ/no-DAZ host MXCSR1F80
and restore the incoming worker CSR. The worker's outer scope also restores its
entry CSR. No change to the backend FP implementation was necessary.

## Standalone validation and integration

Executed on2026-09-10, all output muted at both source and master:

```powershell
python -B K:/SimpsonsNativeCopy/build/dac-processing/run_probe.py
```

The final strict build/run passed: **12,288 exact component comparisons and9
actual natural OnBufferEnd receipts**, with12445 checks in that run. The check
counter includes bounded polling attempts and can vary with host scheduling.
The final test includes COM/cleanup FP restoration after those operations.
`compile.log`, `run.log`, `command.json` and `result.json` retain the exact
command, output and hashes of the tested sources/headers. No parent build,
offline regeneration, original program, guest SDK or audio media is invoked by
this standalone fixture.

Coverage includes copied PCM after caller VirtualFree, two-slot admission,
terminal-held capacity, stale/foreign generations, target changes after raw
acceptance, actual start, exact processing-time ramp, actual copied backend
payload, processing-versus-acceptance separation, real downstream completion
and independent retirement, four-record observation backpressure, empty wakes
and starvation, stop before activation and between DSP/submission, concurrent
stop, live-work destructor, and injected backend callback-handler failure.
Injection uses the real backend error handler and real DestroyVoice cleanup;
it is explicitly **not** a claim of physical device removal.

The independent numerical oracle uses exact rational values: rawN/512 and
gain(A+j*B)/512 yield numeratorN*(A+j*B)/2^18, whose magnitude fits24 bits.
These values are analytically exact binary32, so the expected component arrays
do not call the production gain helper. Tested steady and ramp blocks compare
both the staged result and the backend's actual copied payload. General
rounding/subnormal correctness remains covered by the separately frozen
original/native gain fixture with31,176 exact sample comparisons and integer
IEEE oracle; this wrapper does not broaden that equivalence claim.

The fixture's worker deliberately begins with unmasked exceptions, pending
flags, FTZ/DAZ and round-toward-zero (MXCSR E07F), then verifies exact restoration
after DSP, backend submission and all cleanup/COM calls. Invalid admissions and
caller activate/stop also check CSR preservation. CPU gates pause before DSP
or submission only in the test build; they never create fake native acceptance,
natural completion or source success. The fixture's asymmetric Windows routing
is explicitly caller-chosen test routing, not a naming of original game planes.

Production integration: compile `audio/dac_processor.cpp` into
`SimpsonsAudioOutput` with `/fp:strict /W4 /WX`; no TESTS macros there. Main
already owns that CMake change. The independent test target compiles
`tests/test_dac_processor.cpp`, `audio/dac_processor.cpp`, `audio/dac_gain.cpp`
and the unchanged `audio/native_audio_output.cpp` directly, defining both
`SIMPSONS_DAC_PROCESSOR_TESTS` and `SIMPSONS_AUDIO_OUTPUT_TESTS`, with the same
strict flags and `NOMINMAX`, `WIN32_LEAN_AND_MEAN`. Link `xaudio2`, `ole32`,
`ksuser`, `uuid`; use a bounded test timeout (the isolated runner uses45s).
No Runtime/PPC/original image is required for this test. Main separately owns
the real original Dac lifecycle fixture and all activation/readiness claims.
