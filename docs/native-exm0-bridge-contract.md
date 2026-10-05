# EXm0 native bridge: lifecycle, CPU queues and source leases

The engine boundary is the original EXm0 provider/instance callback family, not
`XMACreateContext`. A native factory and real instance owner can be implemented
before decoding is qualified, **provided input notification and decode remain
explicitly guarded**. In particular, guarding only 8233FAF8 is insufficient:
generic enqueue calls an EXm0 virtual input feeder. Also, EXm0's generic output
caller ignores a short decode return and advances by the requested quota.
Returning `NeedDrain` as zero samples would falsely release CPU-owned work.

This document owns no implementation changes. Original instructions in
`analysis/simpsons.pe`, base82000000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
are authoritative. Addresses/field offsets are hexadecimal; equations and
explicit sample counts are decimal. Native design recommendations below are
distinguished from original effects. The prior
[trim/converter proof](native-audio-trimming.md) remains frozen: build113 passed
29 tests, including 664 original converter cases / 225,264 exact float samples.
No new hardware quantization or frame-origin equivalence is claimed here.

## Essential boundary decisions

1. Retain generic CPU registration **82340448**, allocation/construction wrapper
   **823404A8**, and destruction wrapper **823402E8**. Replace their EXm0
   callbacks with a native owner keyed by the real guest allocation V and its
   generation. Do not put native pointers or fake SDK records in the guest.
2. Generic enqueue **8234E768** can remain AOT only with preflight before its
   stores and a native/guarded EXm0 input-notification callback. It calls
   `V->vtable[0]` at **8234E7D0**, with r3=V and r4=slot index. EXm0's slot is
   **8233FAF0**, an unconditional tail branch to hardware feeder **8233F000**.
3. Generic consumer **823626D8**, unbuffered branch **8236288C..F8**, calls
   V+14 at **823628E0**, then advances by its saved requested quota regardless
   of the returned r3. EXm0 initializes V+33=0 and takes this branch. A native
   callback must satisfy that quota, block/resume through a separately proved
   engine operation, or fail explicitly before returning to that caller.
4. Source completion belongs to original **82362660** and **823417B8**. Native
   packet acceptance or raw frame production alone must not accelerate it.

## Exact provider and instance callback ABI

**Provider 8233E598** has no consumed incoming argument. It calls the global
initializer 8233E5C0 and returns literal **D=82D073AC** in r3. Startup's call is
**8281646C**, LR82816470; CPU registration is the following call82816478.
Keep D's actual identity and callbacks:

```text
D+00 8233E9A8  size/alignment query
D+04 8233E9C8  instance constructor
D+08 8233EC58  instance resource release callback
D+0C 8233FAF8  decode callback
D+10 mutable registry link, initially0
D+14 45586D30 = EXm0
D+18 0, controls optional generic output buffering/allocation
```

A native provider should prepare a real factory before returning D; let the
original registry insert D and update its own link/count. Do not set the
original physical-pool initialized flag to impersonate factory readiness.
Native readiness and original SDK-pool globals are different representations.
Keep the original hardware initializer guarded even after replacing provider.

Retain one **shared CPU effect of that initializer**: with
`Q=BE32[82E31BCC]`, if Q+18 is null, original8233E5FC..E61C installs **both**
Q+18=82339788 and Q+1C=82339798 (allocation/free wrappers). If Q+18 is nonnull,
it preserves both words. These are real original CPU callback addresses, not
native IDs; their conditional installation must not be silently lost just
because the private hardware allocations are replaced. Validate Q before
publication and preserve this branch exactly. Track factory readiness in the
host. Require the original pool fields to be in the expected unused profile
before taking over; do not erase an already partially initialized hardware pool.
Native global teardown must use native ownership even while the original pool's
initialized flag remains zero.

**Size query 8233E9A8**: r3=full channel count, r4=writable alignment output;
writes BE32 `0x10` at r4 and returns `0x58 + 0x18*((channels+1)>>1)`. Retain AOT
with channel/bounds preflight before generic allocation. The generic wrapper
later truncates channels to V.byte[2E]; require an actually supported count
before this truncation. No literal 48000 or XMA variant is passed to this query
or to the constructor.

**Constructor 8233E9C8**: r3=V, returns low-byte boolean 1/0. The original call
is indirect at **82340570**, LR82340574. The generic wrapper has already set
V+C=D+8, V+10=0, V+2E=low channel byte, V+4=its incoming r7 owner. It has not yet
initialized the queue indices and descriptor identity/decode callback.

Original non-pool constructor effects, distinct from the removed hardware
borrowing/list operations:

```text
V+00 = 821DCAE0                         original EXm0 vtable
V+34 = (V + 0x5F) & ~7                 layer array
V+44 = ceil(V.byte[2E] / 2)             layer count
V+3C = 0; V+48 = 0; V+4C = 0
V.byte[54] = 0; V.byte[55] = 1
zero exactly 0x18*layer_count bytes at V[34]
each layer+C = 2 except an odd final channel uses1
each layer+0 = borrowed original hardware record (backend-private operation)
```

The original body does **not** initialize V+38, V+40 or V+50 here. Do not
invent guest defaults for these untouched words. Native format/cursor state
can have explicit initialization in the host owner instead. Preserve required
guest layout/channel effects when replacing the body, and keep the native
layer association outside guest memory; layer+0 must not be a fake XMA record.
All original users that expect such a record must be replaced or guarded.

After successful callback, original **8234057C..C8** publishes V+8=V,
V+14=decode entry, V+18=EXm0 identity, V+20=generic total allocation size,
V+24=relative segment-array offset, V+1C=0, V.half[2C]=0, bytes2F/30/31=0,
byte32=14h (20 decimal), byte33=0 for this descriptor. The earlier notation
“20h” was incorrect: instruction82340588 loads immediate0014. It clears each queue slot's +0 and
+C; it does not zero all slot bytes. Keep this original CPU code.

**Release callback 8233EC58**: r3=V, no defined success value. Original
**82340310**, LR82340314, invokes V+C. The callback returns borrowed resources
to the global pool; it neither frees V nor releases global XMA contexts, and
does not clear the layer records. The native counterpart must retire the
matching host instance and its accepted packet/frame storage, then return so
original823402E8 can free optional V+10 storage and V through the original
allocator. No early free of V, no duplicate guest free.

**Construction failure is a real destructor path.** 823404A8 calls823402E8 if
the constructor returns false. The native release callback must recognize the
same allocation's failed/partial construction state safely even though the
success-only generic fields above are not initialized. Do not accept arbitrary
unknown V as an idempotent destroy; track that exact creation transaction.
If the native constructor throws fatally instead, retain explicit terminal
cleanup ownership and do not claim normal guest rollback occurred.

**Decode 8233FAF8**: r3=V, r4=output descriptor, r5=requested frames per channel.
Original returns accumulated produced frames, but this is not honored by the
unbuffered generic caller. The native callback must validate identity, live
allocation, queue/lease, supported format and all output spans before any
publication. Zero request originally returns0; it is not an EOF signal.

The actual EXm0 vtable **821DCAE0** contains only the two reviewed entries:
slot0=8233FAF0 input notification, slot4=8233EC00 deleting destructor. The latter
changes V's vtable to821DCAD8 and, if r4&1, frees V through8269BEB0; it does not
invoke the codec resource-release callback. Guard this bypass unless separately
ported to retire native ownership correctly. Likewise do not mistake the base
vtable setters/destructor8233EBF0,8233EC48,8233E4C0/D0/E0 for native cleanup.

## Queued segment fields and exact enqueue effects

**8234E768(V, source, samples, flag, initialConsumed, auxWord, auxByte)** maps
r3..r9 in that order. Let `i=V.byte[2F]`, capacity=V.byte[32],
`Q_i=V+V[24]+0x14*i`. It requires Q_i+C zero; if nonzero it returns0 immediately.
On success it writes:

```text
Q_i+00 = r4                  borrowed compressed layer-data pointer
Q_i+04 = r8                  auxiliary word, preserved opaque here
Q_i+08 = r7                  initial consumed sample position
Q_i+0C = r5                  sample extent / nonzero occupancy sentinel
Q_i+10 = low8(r6)            codec continuity/skip-control byte
Q_i+11 = low8(r9)            auxiliary byte, preserved opaque here
Q_i+12/+13                  untouched
```

It then invokes the virtual input notification, sets V+1C=Q_i+8 if the write
index equals current output index V+31, increments V+2F modulo capacity, and
returns the old index. **Zero can be a valid slot index or the full-queue failure
return.** A bridge must preflight occupancy/indices rather than treating every
zero return as rejection. Queue publication precedes the notification; a
constructor-only milestone should guard at enqueue entry for an EXm0 V, before
these stores, instead of waiting to fail after publication in8233F000.

The real caller **823424D8 ->8234E768 at823425C0** supplies source=block+8 and
samples=BE32(block+4). It copies both words bytewise to an aligned stack before
loading; no host-endian assumption is needed. It computes the stored flag as
`F=(low8(incoming_r6)==0)`. If low8(incoming_r7)==0, initialConsumed=0,
auxByte=0 and auxWord=0. In the alternate branch, initialConsumed comes from
voice+20, auxWord from its stream-state+3C, auxByte from stream-state+48.
Those are exact producer relationships. The general meaning/valid ranges of
the two auxiliary fields and the alternate seek/loop profile are not proved.
Do not rename +4 as a compressed byte length, or +10 as a last-packet marker.

The ordinary zero-offset/zero-auxiliary profile is a defensible initial supported
domain. Reject nonzero auxiliary/initialConsumed requests at preflight until
their effects are qualified; do not silently erase them. Preserve opaque bytes
even while rejecting unsupported semantic profiles. Existing frozen trimming
states the zero/nonzero consuming behavior; this report adds producer evidence
without revising that frozen report.

**Input pop 8233E108**: r3=V; reads indexV+30 and returns its slot if +C is
nonzero, then advances V+30 modulo V+32. Empty returns0 without advancing. It
does not clear occupancy or transfer/free the source allocation. Retain this
pure helper AOT if native feeder state can accept and track that segment, or
perform its exact CPU effects only after validating/acquiring the real source
lease. Do not pop again when retrying `send` after `NeedDrain`.

**Current slot query 8233E498**: r3=V; returns current V+31 slot or0 based on
+C, without mutation. **Remaining query823410C0** takes r3=V,r4=index(low byte),
returns zero for an empty slot, else `slot+C - (V+1C if current else slot+8)`.
Both are pure AOT candidates with index/range preflight.

## Output progress, quota and the actual source release point

**82362660(V, count)**, r3/r4, is a leaf CPU advancement helper:

```text
V.consumed += count                          # V+1C
if V.consumed == current_slot.samples:       # exact equality, not >=
    current_slot.samples = 0                 # clears +C, not source pointer
    V.output_index = (V.output_index+1) % capacity
    V.consumed = next_slot.initialConsumed   # +8, even if next slot is empty
```

Consequently count must not exceed the current remaining extent, and the next
slot's full allocation must remain mapped even when empty. Empty slot metadata
is not automatically zero. Native decoding should not clear source pointers or
set V+1C/V+31 independently when this original helper will run afterward.

**823626D8(V, output, count)** retains generic buffering and queue progression.
For EXm0 V+33=0, it checks current occupancy and computes
`quota=min(requestRemaining, slot+C-V+1C)`, then:

```text
823628E0: call V[14](V, output, quota)
823628E4..F0: ignore callback r3; add quota to produced; advance(V, quota)
```

Returning partial PCM here is false progress. Even a valid decoded prefix must
stay host-owned until the entire quota for all layers is ready. Otherwise
throw/stop before returning; a resumable scheduler change requires an explicitly
ported higher engine boundary. Do not spin indefinitely on `NeedDrain` or zero
reads. The buffered V+33!=0 path does use callback return and copies partial
frames, but EXm0 does not select it: changing D+18/V+33 to borrow that behavior
would be a new engine contract, not retention of the original callback.

The bounded actual outer caller **8234F1D8**, at8234F498..F508, calculates
remaining samples of its tracked request segment before calls8234F548/564.
It discards a bounded prefix in chunks <=256 and requests the bounded remainder.
This explains why the recovered EXm0 callback need not append across segments
inside one call on this path. The generic unbuffered helper itself passes the
same output descriptor base on repeated iterations; do not invent a general
cross-segment output append behavior from it. For this first profile require
one decode quota within the current segment and retain the bounded outer caller.

**Original source-owner cleanup823417B8** scans 20 request records, each16 bytes,
based at A+54 with status byteA+61. For record j:

```text
R = A + 0x54 + 0x10*j
R+0 allocation record retained by the stream owner
R+4 original source allocator/owner
R+8 progress metadata
R+C queued segment index
R+D status (2 selects completed-consumption check)
R+E voice index
```

For status2 it resolves the voice's codec V, checks the same sample remaining
formula above, and only when empty/fully consumed sets status0. If R+0 is
nonzero it subtracts that allocation record's +4 extent from the stream-state
account at +18, calls **8234187C ->8238D640(r3=R[4],r4=R[0])** when owner is
nonzero, then clears R+0. It does not clear every record byte. This is the
verified release point; the native feeder must not call that free itself or
mark the request status2 merely upon packet acceptance. Status1 is published
in823424D8; the wider status1->2 transition is retained engine work, not assigned
to the native decoder by this proof.

Two independent lifetimes therefore matter: original source allocation and
queue ownership, and native owned packet/frame storage. `send(Accepted)` permits
discarding only the submitted temporary native copy according to its real API;
it does not retire the original request. Retain a generation-checked guest
source lease or own the necessary bytes before original completion can run.
Validate layer extents within the tracked allocation, not merely mapped RAM.

## Native feeder and decoder owner recommendation

These are proposed host fields, not invented guest structure offsets:

```text
Factory: runtime identity/generation, validated codec capability, active set,
         admission state, synchronization, acquired-resource ownership
Instance: guest V + allocation generation/extent, channels/layer count,
          lifetime phase, decode capability, segment/input/output generations,
          current source lease(s), remaining samples, carry, error state
Layer: channels1/2, optional real NativeXmaCodec once format is proved,
       pending owned 2048-byte packet, accepted byte position,
       retained raw frames/read cursor, pending skip, staged output prefix,
       enabled/disabled admission state
```

Separate `Unconfigured`/`Configured` lifetime from `DecodeGuarded`/`Qualified`
capability. A live real software decoder does not by itself establish the guest
trim/quantization contract. Sample rate and XMA variant are not constructor
arguments. Configure only after validated source evidence supplies them; the
original first layer word's low two bits is a rate selector, not literal Hz.
The verified original profile uses selector3/48000, but an unexplained selector
must fail explicitly. Two successful XMA1/XMA2 software probes do not classify
every asset's original encoding generation.

Use the existing proposed `NativeXmaCodec` API without converting codec flow
control into guest completion:

1. Parse/validate a complete segment's per-layer extents. Each BE layer header
   gives `length=word>>2`, including its four-byte word; compressed input starts
   at header+4. Acquire source ownership before advancing V+30. Packet framing
   must be proved: the original can copy a final piece shorter than2048 while
   still exposing a packet slot. Do not silently concatenate unrelated layers
   or choose padding/EOF from temporary short input.
2. Keep one pending packet until `send(packet)==Accepted`. On `NeedDrain`, read
   available real frames and retry **the identical pending packet**. Do not
   advance guest/native source positions twice. Bound attempts by actual
   progress, preserve partial compressed-frame and PCM state between calls.
3. `read(destination)` returns interleaved float frames per channel; zero is
   temporary unavailability, not EOF. Retain surplus frames and per-layer skip.
   Apply the proved 384/512 equations only against a qualified raw frame origin.
   Stock576 removal does not authorize an extra192 guest discard.
4. Stage a full guest quota across all layers before output publication or
   V+48 reduction. A faster layer must retain its prefix while a slower layer
   drains/refills. A native codec cannot roll back accepted compressed input;
   retain its ownership/state on failure or make the failure terminal. Do not
   claim a CPU snapshot also rolls back the codec.
5. Original output is BE16 converted to guest planar float, not arbitrary raw
   float output. Float-to-BE16 hardware quantization remains unqualified. Keep
   decode guarded until the native numerical policy and its evidence limits are
   explicitly resolved. No implicit silence, clamping or fabricated tail.
6. On success preserve the codec's exact per-segment remaining/skip/carry effects
   and return the full quota. Let original82362660 perform the generic consumed
   index/occupancy changes. Explicit stream reset, source discontinuity and
   temporary starvation are separate events.

The source header flag controls these previously proved equations:

```text
segment start, F==0: each layer.skip += carry+384; carry=0
segment start, F!=0: preserve skip/carry
skip B=min(available_bytes,2*channels*skip); skip-=B/(2*channels)
segment complete: r=(samples+(F==0?384:0)) & 511
                  if r!=0: carry=512-r; else retain carry
                  if original error flag: carry=0
```

Store residual skip/carry through split input and partial requests. Do not
derive global EOF from an empty next queued slot. A final drain/loop/seek profile
still needs explicit source semantics beyond +C being zero.

## Output descriptor capacity and sparse clear

`out+4` is a guest sample-storage pointer; `BE16[out+E]` is per-channel capacity/
stride in floats. With stride S, layer i selects `base+8*S*i` and (if stereo)
`base+8*S*i+4*S`. Require quota<=S, writable whole output extents and no harmful
source alias. Original8233F250 has scratch return, no allocation/free, and
performs exact signedBE16/32768 conversion. If retaining it, supply real BE16
bytes and the separately proved r4/r5 planes,r6 ring,r7 offset,r8 channels,
r9 tail/r10 head ABI; do not manufacture a hardware context.

For 128-byte aligned left output, mono clears `floor(quota/32)` and stereo
`floor(quota/16)` **32-byte DCBZ chunks**, beginning at left+128*k. No clear runs
for unaligned left. Conversion then writes4*quota bytes per plane. Preserve
clear-only chunks and the untouched96-byte gaps; this is a CPU memory effect,
not additional PCM. With original contiguous planes and quota<=S, chunks fit
within the pair's8*S allocation. A synthetic split-plane fixture needs separate
writable left-side capacity through128*(chunks-1)+32. No generator change.

## Active disable/enable and global teardown

**8233E8A0** consumes **r4=excluded original pool record**, ignores incoming r3,
locks the original pool, and calls `XMADisableContext(context,0)` on every active
record except that record. It does not reset codec cursors, queues or instances;
return is not a boolean contract. **8233E920** consumes no incoming object and
enables all active records under the same lock. There were no direct BL/B callers
to these two entries in the targeted original `.text` reference query; this is
not proof that indirect/data-driven callers are absent.

A host implementation can serialize its active owner set and pause/resume
work admission without discarding packet/frame/skip state. A synchronous codec
has no hardware work to wait for, but its native owner still needs concurrency
and exclusion semantics. If no guest pool records exist, **nonzero r4 cannot be
treated as a V or silently ignored**. Require a proved association or reject
that exclusion profile. Zero-exclusion over a real host active set is the
bounded natural mapping. `NativeXmaCodec.reset()` is not pause/disable.

**8233E7E8** global teardown consumes no incoming object. Original call
**823390AC**, LR823390B0, follows the broader original CPU teardown's work drain.
The original body releases the hardware pool and its allocations. A native
replacement must instead stop factory admission, require/retire all real
instances and leases according to the engine drain, and release only acquired
host resources. Do not execute the original hardware pool body against zero or
native IDs. Preserve the surrounding CPU teardown; its normal concurrency and
all instance-destruction ordering are not yet runtime-verified. Terminal runtime
cleanup may release real host owners while diagnosing incomplete guest cleanup;
that is not a successful normal engine shutdown.

**8233FF80(V)** is an error reset, not an ordinary pause. It sets V.byte54=1,
reinitializes with V+50, zeroes layer+4/+8/+10/+14 and byte+D, sets V+30 to
V+31+1 modulo capacity, clears V+3C, and calls the feeder. **8233ED78(V,selector)**
stores V+50 and initializes each hardware context. Both must remain guarded or
be replaced as complete operations; invoking them as alleged pure CPU helpers
would touch the removed backend. The standalone context-status reader8233E568
also directly reads hardware bytes and is not a native-owner query.

## Honest first milestone and required gates

A legitimate bounded first milestone is a real factory plus native instance
storage/ownership, original CPU registration/allocation/destruction, partial
construction cleanup, generation protection and leak-tested host retirement.
An instance may be explicitly unconfigured until source format arrives, like a
material owning source before a supported bind. Its existence must not be
reported as qualified decoding or audio playback. Construct actual codec
objects only when their required format is established; do not choose defaults
solely to claim that construction succeeded.

Before removing the provider guard, implement/guard the entire known family:
provider8233E598, ctor8233E9C8, release8233EC58, input notification8233FAF0/
feeder8233F000, decode8233FAF8, active disable8233E8A0, enable8233E920,
global teardown8233E7E8. Guard old initializer8233E5C0, context init8233ED78,
error reset8233FF80, context query8233E568 and deleting-destructor bypass8233EC00
(including base bypasses if they are made reachable). Preflight EXm0 enqueue
8234E768 before publication while input handling remains unqualified. Generic
helpers remain common to other codecs: discriminate the verified EXm0 owner,
not every audio V indiscriminately.

Keep the size query, generic allocation/registry, queue queries, index advance,
tracked source release and the existing outer CPU caller AOT with appropriate
bounds/owner checks. Keep decoding guarded until full-quota operation, raw frame
origin, packet restoration, quantization and the supported source profile are
qualified. Initial +4/+8/+11 nonzero profiles, excluded active-record identity,
normal teardown synchronization and wider indirect consumers remain explicit
limits. This is a concrete ownership milestone, not permission for a ready
codec that returns silence, fake accepted input or false sample completion.

## Evidence pins and reproducible checks

The following block reads only the original image. It checks original `.pdata`
extents for non-leaves, explicit reviewed leaf extents, the descriptor/vtable,
critical enqueue/consume words and mathematical queue boundaries. It does not
execute SDK code, model XMA hardware, or claim dynamic lifetime coverage.

Validation completed successfully: **19 `.pdata` function hashes, seven leaf
hashes, 20 selected words, the original descriptor/vtable and 75 finite queue
cases**. This is byte/contract verification; no new game lifecycle was executed.

```python
from pathlib import Path
import sys
sys.path.insert(0, 'tools')
import analyze_poststart_integration as a
b = Path('analysis/simpsons.pe').read_bytes()
a.validate_identity(b)
_, pdata = a.layout(b)
FUNCTIONS = {
    0x8233E5C0: (0x228, '82694d1f6f56426c04d506a6f599422e72718c8d3c5a1babd35e10456f8baed8'),
    0x8234F1D8: (0x6BC, '337fa26ac69a977f15dd4827085c155723fe7b9c2dfb63bc4a4cec583998850b'),
    0x8233E598: (0x28, '60158be362cf49b7a15a55465d89a01aa9637cde5f70820016d52f0a56732316'),
    0x8233E7E8: (0xB4, '34e3021f1912dae619b0dc9100ef4eb14c3a9616484a5b09075f4c1c0c57b23b'),
    0x8233E8A0: (0x7C, '10614d4cbaa36f3400c03a4f8c918cdb742ed0f603ac22fca22c0577b092402e'),
    0x8233E920: (0x84, '694ae1f70df9e56efd3d4b5627385d7f4981092219c3cd8b02170b9e2963c01d'),
    0x8233E9C8: (0x228, 'ce0b448956bac299b2d91dd95db7ad48effa5f0d91273b857d9c16d9aa312eb2'),
    0x8233EC58: (0x11C, '02d5ed1ffda69cb7eff80167fab15522f318a124b4b31f015c1fbac2344ea3aa'),
    0x8233EC00: (0x44, 'b8614a91fa8d4dbb9fc1ee27dfed7ed1926f1a67e10768c52449983518aab901'),
    0x8233F000: (0x24C, '574a68fbfc334602c29df3d71c754d1b54ab63720e4c0e143efa9533fae02e5c'),
    0x8233ED78: (0x144, '80c53a31a4056769565e245cd4d1f5e54c9f0e0703706fc7e94bf7cbe0e596f2'),
    0x8233FF80: (0xB8, '452e533af99aadc160a98dc92dfbcddda168de42877ea1a56bb9f81d8097338a'),
    0x8233FAF8: (0x484, 'feae5c3a4877b181a3101fc3969699c88d67883d1ec92951b4447043a1004276'),
    0x823402E8: (0x90, '7941c14fe977ded404900f80bf9bbdf696e49d27a0d491f73f1fe46dae409cf9'),
    0x823404A8: (0x1E4, '51b11d9f5f257c69fab285b67bfc1b4a963ad524b1c99b09b341101bcb084599'),
    0x823424D8: (0x10C, 'd54a0bc2269f6fc19038c368195872019d136eb91c0787385a2ed943fee07387'),
    0x8234E768: (0xB8, '4048aa5b73fa3ab0389409ee325d74e1097931bbc4b61ae2e1c9f86f58ba765a'),
    0x823626D8: (0x230, '43413d24246a5e98f9e6ecd5f862a73587fdf25172f11f1225de7f8e1213dde0'),
    0x823417B8: (0xE0, 'fed4b7941a662ce9703042b680bf989e2173a221f11432003d87eaaad3a24c9c'),
}
LEAVES = {
    0x82362660: (0x78, 'b10e0c8dc327999d0ceeaee4cac3783f47af52780b29a6db50f7114fb064dbb4'),
    0x8233FAF0: (4, '2afec35bdffd94c2fc6a943acc2242eec610c46141324cc21ca9292ffddb8a80'),
    0x8233E9A8: (0x1C, 'f036195085ab6e1bc204906c3ad7f757dc3ae60b419c8b2172b339456c23d0bd'),
    0x8233E108: (0x58, '32416150b5862ff66014c66f5f4642e0e70244268999456d3fce51941979abf6'),
    0x8233E498: (0x28, 'a044c0331d9d4da0d47fa2e2be0284d05e6bca4ec18ff8bb76c6b0490f21da34'),
    0x823410C0: (0x48, 'dbd70b5203698a7f5365bd7717fcd1aa0010d974a188dd1664ad65706ce1857c'),
    0x82340448: (0x60, '7b31a31a93763e32556fa68a87f0853648e1ec1045d4bbc314e97cfda35d7807'),
}
for va, (size, digest) in (FUNCTIONS | LEAVES).items():
    assert a.sha(a.span(b, va, size)) == digest, hex(va)
    if va in FUNCTIONS:
        assert pdata[va][0] == size, hex(va)
    elif va == 0x8233FAF0:
        assert a.branch(va, a.word(b, va)) == (0x8233F000, False)
    else:
        assert a.word(b, va+size-4) == 0x4E800020, hex(va)
assert [a.word(b, 0x82D073AC+4*i) for i in range(7)] == [
    0x8233E9A8, 0x8233E9C8, 0x8233EC58, 0x8233FAF8, 0, 0x45586D30, 0]
assert [a.word(b, 0x821DCAE0+4*i) for i in range(2)] == [0x8233FAF0,0x8233EC00]
WORDS = {
    0x8234E7A4: 0x909E0000, 0x8234E7AC: 0x911E0004,
    0x8234E7B4: 0x90BE000C, 0x8234E7B8: 0x993E0011,
    0x8234E7BC: 0x98DE0010, 0x8234E7C0: 0x90FE0008,
    0x8234E7D0: 0x4E800421, 0x8234E7E8: 0x917F001C,
    0x823628E0: 0x4E800421, 0x823628E4: 0x7FC4F378,
    0x823628EC: 0x7F9EE214, 0x823628F0: 0x4BFFFD71,
    0x8236267C: 0x9163001C, 0x82362688: 0x4C9A0020,
    0x82362690: 0x912A000C, 0x823626D0: 0x9163001C,
    0x8234187C: 0x4804BDC5, 0x823425C0: 0x4800C1A9,
    0x8233E8B4: 0x7C9D2378, 0x8233E8E8: 0x38800000,
}
for va, w in WORDS.items():
    assert a.word(b, va) == w, hex(va)
# Count conservation in the intended finite, nonnegative queue domain.
cases = 0
for extent in (1,16,128,512,8064):
    for consumed in (0,extent//2,extent-1):
        for request in (0,1,127,512,8192):
            quota = min(request, extent-consumed)
            assert 0 <= consumed+quota <= extent
            complete = consumed+quota == extent
            assert complete == (quota == extent-consumed)
            cases += 1
print('PASS',len(FUNCTIONS),'pdata functions,',len(LEAVES),'leaves,',len(WORDS),
      'words, descriptor/vtable,',cases,'finite queue cases')
```

Run only this block with `python -B` from the workspace for reproduction. The
bounded direct-reference queries followed the explicitly named callbacks and
queue-index writers; no absence claim covers arbitrary indirect calls. No game
or codec executable was run by these evidence checks.
