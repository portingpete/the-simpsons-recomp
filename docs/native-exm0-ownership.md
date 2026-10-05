# Native EXm0 factory and instance ownership

Build120 passes all 31 CTest suites. Actual muted boot072 acquires the native
EXm0 factory, retains original codec registration, and stops at the newly
guarded Dac0 output constructor823456D0. It does not create a game EXm0 instance
before that stop, decode game audio, display original geometry or play gameplay.
The original allocation/lifecycle fixture exercises native EXm0 instances
separately against the actual startup-created audio allocator.

The implementation is `runtime/engine_audio_owners.cpp`, with bridge hooks in
`runtime/engine_audio.cpp`. `audio/native_xma_codec.cpp` now also provides a
real native factory which owns unconfigured XMA1/XMA2 backend contexts, verifies
versions and the raw-frame option, and can create explicitly configured real
decoders. No sample rate or encoding variant is guessed from the EXm0 constructor:
the original constructor only receives its generic object, with a channel byte.

## Retained original CPU work

Provider8233E598 returns the original descriptor82D073AC after acquiring the
native factory. It preserves the removed initializer's shared CPU branch:
if Q+18 is null, install82339788/82339798 at Q+18/+1C; otherwise preserve both.
Q is the actual audio root at82E31BCC. The private hardware pool flag, physical
allocations and record lists remain unused. A partially initialized old hardware
pool is rejected, not erased or treated as native state.

The original registry82340448 remains AOT, including duplicate suppression.
Generic allocation/construction823404A8, size query8233E9A8, generic success
publication and generic destruction823402E8 also remain AOT. Native preflight
requires channels1..6 before byte truncation, and the recovered owner profile
r7=Q. Original caller82342B98 obtains this from S+4, initialized by8233DC24/28.
This is the current supported ownership domain; it is not a claim that every
possible indirect caller has been characterized.

Construction is tracked across the real generic wrapper. Entry records the
guest context, stack, channels, root/allocator and a new host generation. The
hook at82340548 captures the actual allocation returned by82340730, its extent
and untouched fields. The native constructor8233E9C8 requires that transaction
and the real callback LR82340574. It preserves the original EXm0 vtable, channel
counts, layer placement, state initialization and exact zeroing footprint.
Layer+0 remains zero: native associations do not impersonate SDK record pointers.
Host layers are explicitly unconfigured and retain storage for their eventual
qualified decoder state.

Original generic code then publishes identity/decode pointer, total extent,
relative queue offset and **20 decimal** slots. It clears only slot+0/+C.
The return hook82340684 checks that publication and commits the live owner.
Untouched V+28/+38/+40/+50 remain unchanged. Pending native operations are
serialized, and guest address/generation leases cannot bind a reused allocation.

The native admission capacity remains256 layers. If insufficient capacity
exists, the constructor returns false with the original available-prefix
channel-byte effect. The exact failed creation is tracked so that the original
failure branch can call823402E8, invoke the native release callback, and free V
through the original allocator. Host allocation exceptions remain terminal
failures; they are not described as normal guest rollback.

Destruction entry records the real scope. Native8233EC58 retires the matching
instance before original free, retaining separately leased native storage until
its last owner releases it. The post-free hook82340360 retires the allocation
record without reading V again. The original allocator may already have unmapped
or reused it. No native hook performs a duplicate guest free.

## Unsupported work remains visible

Input enqueue8234E768 discriminates EXm0 and fails before any queue stores.
Input notification8233FAF0, feeder8233F000 and decode8233FAF8 remain guarded.
The generic unbuffered consumer ignores short decode returns, so a future
native callback must stage its complete quota before original queue advancement.
The old hardware initializer, context initialization/status, error reset,
deleting-destructor bypass and reviewed base-vtable/destructor bypasses are
also guarded. No console audio-context memory layout is implemented.

Active disable/enable serializes the current host owner set and preserves
packet/configuration state. Nonzero excluded hardware-record identities reject;
they are not interpreted as guest object addresses. This admission state has
no decoded-work effect yet because input remains guarded. Normal native factory
teardown requires no live records or construction/destruction transactions.
Terminal runtime cleanup follows worker cancellation/join and releases host
ownership; incomplete original cleanup is diagnosed separately.

Boot070 first exposed the next output SDK path through an unmapped read7FEA1800.
Build119 guards its outer Dac0 constructor823456D0 before the first object store
or SDK call. Boot071 reports S=E4624C30, Q=E4627DE0, descriptor82D069B4,
LR8233DC44. Its preceding generic graph/object allocations are still original
CPU effects. The guard does not claim that startup was fully rolled back.
See [native-audio-output-boundary.md](native-audio-output-boundary.md).

## Verification and practical limits

`tests/test_engine_audio.cpp` reaches the real Dac0 guard through original
startup. Its test-only indirect-dispatch observer calls that real body and
checks its full0x3108-byte object plus the two reviewed SDK globals are unchanged.
It never supplies constructor success. A fresh `EngineCpuCalls` from the saved
initialized entry context runs the following contracts; the exception-unwound
startup context is never resumed.

The fixture retains the actual Q, adapter and backing allocator. Observers around
original8274B140/8274B1A0 call the actual allocation/free bodies and record their
receipts. Newly allocated payloads are poisoned only after real allocation and
before the original base constructor, allowing every byte of the final object
and queue to be checked against independent original instruction effects.

Build121 passes **78,610 checks and133 actual allocation/free pairs**, covering
channels1..6, three repeated lifetimes per channel count, observed same-address
reuse, stale generation rejection, retained native leases across original free,
guarded input without CPU publication, direct cleanup/constructor bypass
rejection, active pause/resume/exclusion, teardown with live owners, 255/256-layer
capacity and original false-constructor rollback, explicit null-allocation
fault/retry, then empty factory stop/restart. A real mapping-churn thread runs
alongside24 original allocation/destruction lifetimes. Both constructor and
destructor preserve tested nonvolatile GPRs and SP/LR. Retired unconfigured
metadata is also retained across factory stop/restart; this does not establish
a drain policy for future configured decoder/source work. The raw codec suite remains662,151 checks,
and the original BE16 converter remains664 cases/225,264 exact samples.
The actual registered P6B0 descriptor also passes the same generic helpers with
one real allocation/free. Its entire0x1D0-byte poisoned payload matches original
CPU writes, including the untouched alignment gap and queue fields. Native EXm0
count/reservations/readiness remain unchanged, and EXm0 lookup rejects that V.
P6B0 decoding and other operations are outside this construction/release proof.

Build117 demonstrated that replacing the EXm0 provider dispatch slot could not
observe a direct generated BL. Build118 temporarily used the specific unsupported
device-memory failure as its observation. Build119 replaces that with the
verified outer Dac0 guard; only the original indirect constructor is observed.
These test revisions do not add a production observation callback.

Original stereo and multilayer software decoding is separately qualified by
[native-xma-multilayer-probe.md](native-xma-multilayer-probe.md): six real stereo
layers, 18 native runs, identical PCM across raw variants/partial-read schedules.
The four-channel case is explicitly a bounded loop-body excerpt. Software raw
equality and measured stock offset576 do not establish hardware PCM origin,
quantization, loop replay, guest completion or speaker routing. All game input,
decode and output work above remains explicitly incomplete.

Review found that the overlap scan iterated Runtime::regions without its VM
mutex. Build120 protects only that scan with vmMutex, while retaining the audio
transaction mutex; no original CPU callback runs under the added lock. The
concurrent mapping fixture exercises this synchronization domain. See
[native-exm0-ownership-review.md](native-exm0-ownership-review.md).

The executable SHA256 for build121 (unchanged from build120/boot072) is
`b26f7f3bcfbf3589cb648a29da8fcd27fa60606b819c7d2a707029fe44522722`.
Logs: `build/hundred-twenty-first-build.log`, `build/boot-072.log`,
`build/native-audio-lifecycle-121.log`, `build/native-audio-codec-121.log` and
`build/native-audio-pcm-121.log`. All123 hook sites are pinned to original bytes.
