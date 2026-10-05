# Original Dac SDK operation contract

Frozen bounded evidence, 2026-09-10. Original/reference directories are
read-only. This investigation owns this document and `build/dac-sdk/*` only;
the PCM fixture and its document remain frozen. No SDK, emulator, audio playback
or production code is executed here. Addresses/offsets are hexadecimal unless
explicitly stated otherwise.

Evidence is `analysis/simpsons.pe`, base82000000, sizeEC0000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
`build/dac-sdk/probe.py` validates that identity before dumping original
instructions and recording function extents, hashes and direct branches in
`build/dac-sdk/report.json`. `.asm.txt` files are byte evidence, not generated
code edits. Public SDK names are not inferred merely from wrapper signatures.

## Immediate fixture lock correction

**Application startup holds Q+4C, not Q+48, across the Dac constructor.**
At `82816320: 4BB22FA9`, original startup calls `823392C8(Q)`. With the
original default Q+40 callback null, that helper loads Q+4C at823392D8 and
tails82329720, the original critical-section acquisition wrapper. Graph
creation `828166C0 -> 8233C5C8` performs no additional Q48 acquisition.
It calls generic node construction at8233C77C; Dac is reached inside that
original loop. The existing constructor observer therefore unwinds while
startup still owns Q4C.

The real worker823460D0 takes **Q48** at823460EC/F0 and retains it until
823462DC..EC. Its first original82339458 update attempts **Q4C** (or Q+40's
installed callback). Consequently a constructor observer followed by a normal
worker join can deadlock even though the startup thread does not own Q48.
Do not acquire Q48 to join a worker while retaining Q4C.

The first explicit startup release is **828166F8 -> 823392F0(Q)**, LR
**828166FC**, after graph publication at828166D4 and command3 enqueue at
828166EC. On the default path823392F0 decrements Q4C+20, then tail-calls
82CC28B4 (LeaveCriticalSection); the alternate path invokes Q+44.
The next acquisition is `823392C8` with LR **8281670C** when the saved
generation is nonzero, or LR **82816740** when it is zero. An entry observer
restricted to those LRs, **before acquisition**, is a bounded observation point
after the release. Verify actual Q+40/+44/default lock ownership in the fixture.
Stopping at graph return828166C4 is too early. This is a proposed fixture cut,
not evidence that any full application shutdown has executed.

Root creation82338C98 explicitly initializes Q+40/+44 to zero at82338F00/04;
821CA430 containsFFFFFFFF, so82329720 takes its blocking critical-section
path. The two lock objects are separately allocated within the real root's
reserved storage. A fixture must release only its own known acquisition, not
reset guest lock counters or assume another thread's Q48 ownership is stale.

## Objects and creation record

Use `S` for the real3108-byte Dac CPU object, `V=S[40]` for the original SDK
wrapper, and `U=V[4C]` for its underlying PCM source. These SDK layouts describe
evidence only; native code must not recreate them or publish fake SDK objects.
`docs/native-audio-output-boundary.md` owns the already established CPU
construction, packet fields and two1800-byte PCM buffers.

Original8234591C calls82C40468 with a zero-filled5C-byte record R:
byte+0=0, byte+4=6, word+8=48000 decimal, byte+38=0, byte+3B=2,
float+3C=0, byte+40=1, callback+4C=823463B0,
callback+50=823463D0, callback+54=0, context+58=S.
82C4CB68 copies the format's first38 bytes and builds a lower50-byte record L.
L+8 is R's format record, L+44=R.byte3B, L+45=R.byte40,
L+48=R[50], L+4C=R[54], L+4=R[58].

**R.byte3B=2 is the number of preallocated packet descriptors.**
82C3E180 forwards it to82C41508; that initializer calls82C52670, which allocates
count*78 bytes and inserts exactly count nodes into its free list. This is not
a guessed XAudio2 creation flag. Source factory dispatch is concrete:
82C4BFE8 constructs82C4BDF8 (table821B2A48); adjusted factory+18=82C4BA50
selects the R/L type0 record in821B26B0, whose create callback82C3E440 selects
82C47A08 for format0. It creates U of size114 with82C471D8.

R.byte40=1 becomes U+10C, the DSP descriptor's flags word+54. In82C57E40,
bit0 permits the equal-rate or exact half-rate specialized converter; bit80
selects a different kernel family. This is not an endian flag or a Windows
voice-creation flag. At the actual format0/six channels/48000-to-48000 profile,
flags1 selects index104 decimal in821B2B90: word821B2D30=82C6A0D8.
That kernel falls back to82C64960 if input/output are not16-byte aligned or
the count is not a multiple of8. Original aligned S plus44 gives an unaligned
PCM source, so the ordinary S-owned buffer path takes that scalar fallback.
Its six `lfs` reads prove native guest big-endian float input and its six
plane stores retain component index. No byte-swap option is inferred from1.

R.float3C=0 is copied to L+40. The reviewed type0 factory and PCM constructor
do not use it to set source gain:82C41508 instead initializes both U+70/+74 to
the literal1.0 at82000BB0. Do not treat the record's zero as initial mute or
give it an unverified public SDK parameter name. Other factory types are outside
this profile. R.byte39/3A are zero:82C4C590 substitutes one send and six route
entries; it preserves R+4C as the processing callback and R+58 as context.

## Five required SDK operations

**Final82345A3C -> 82C3F7A8(V,0): activation.** The wrapper holds the SDK
critical section82D01464 and calls82C4D5A0. With flags0 it removes pending-list
membership through82C4CC48 (80004004 means not pending and is tolerated), applies
effective gain through V.vtable+5C, activates U through U.vtable+30=82C46848,
then calls82C6DC10. That latter path updates wrapper activity bits and enables
its processing connection. The unobserved flags bit0 selects deferred activation
through82C4D1D0. The observed operation neither submits a packet nor merely
sets an engine Boolean. Native start must enable real processing after ownership
is established; public naming is secondary to these effects.

**82345EB0 / 82345F0C -> 82C3FB00(V,f1=1/0): gain.** It stores the requested
float at V+8C and dispatches V.vtable+5C=82C4C540. That leaf multiplies it by
`singleton[84+4*category]` for V.byte90 category0/1, using original `fmuls`;
other categories use the float constant82000BB0. The observed record selects
category0. It forwards to U.vtable+58=82C463E0, which stores the resulting scalar
at U+70 and U+E0 under the SDK lock. **Enable1 is not proof of unity gain:**
the category multiplier is part of the operation. Original engine enabled/fade
flags and queued commands remain separate CPU effects.

The singleton's category0/1 floats are initially **3F800000**:82C49CA0 loads
that word and82C49CE0/E4 store it at+84/+88. During processing82C49148 calls
**82CC3844 / xboxkrnl ordinal1F7 XAudioGetVoiceCategoryVolumeChangeMask** at
82C49184; for set category bits it calls **82CC3834 / ordinal1F8
XAudioGetVoiceCategoryVolume** at82C491B4 and writes the actual result there.
82C4C450 reapplies V's gain when singleton+8C reports its category changed.
The names/ordinals are from the verified executable import records, not a guessed
Windows service. The native initial-unity policy is evidenced; silently ignoring
later platform category changes is not proved equivalent. Xbox system/category
policy has no demonstrated Windows replacement in this task.

There is also **gain interpolation**, beyond scalar storage. In the actual
PCM DSP descriptor D=U+B8, D+24 is previous/current gain and D+28 is target
(U+E0). At first activation82C474B8..D0 copies the requested gain into both,
so it does not ramp from an invented initial zero. On later processing the
scalar six-channel kernel82C64960 computes float32
`step=(target-current)/(destinationFrames-destinationProgress)`; each frame
multiplies its six inputs by current, then performs `current += step` with
original `fmuls`/`fadds`. It stores the advanced current at82C64AA4. For a
fresh full256-frame block the denominator is256. The vector kernel has its own
arithmetic scheduling; this is not a claim of arbitrary kernel bit equivalence.
An immediate host SetVolume does not independently prove this ramp. Preserve
the engine's separate CPU fade as well; it is not this SDK interpolation.

**823461F4 -> 82C3F4E8(V,&byte): packet capacity.** V.vtable+30=82C4C348
queries U.vtable+2C=82C460C0 and returns `V.byte3D | U.byteAC`. U.byteAC
(source base E=U+10, E.byte9C) begins20. Submission clears20 only when its
descriptor free list becomes empty (`82C41B78..B0`); retirement restores20
at82C423F0..82C4240C. Thus the tested20 bit means a free packet descriptor,
not device-ready, playback-complete or nonzero sample capacity. For this record
there are exactly two slots. A native capacity result must reflect actual
unretired owned submissions, not merely buffers accepted by a host API.

**82346230 -> 82C3F638(V,packet,0): enqueue borrowed PCM.** The path reaches
82C41D68(E,packet,flags). It removes a free78-byte node, appends it to the
active queue and assigns a nonzero serial. It retains packet[0] as the PCM
pointer, divides packet[4] by E.half5C (24 bytes/frame here), retains loop/play
fields and packet[54] as node+24 completion context, then calls82C41860.
The normal zero-flags Dac record is1800 bytes =256 six-channel float frames,
with no loops. No PCM payload copy occurs in this enqueue path. Empty free
list returns8007000E. Native submission must hold its own converted PCM lease
until real completion; original ready/submitted words alone cannot own it.

**82345A88 -> 82C3F1D8(V): reference release.** Under82D01464 it invokes
V.vtable+8=82C3C748. This decrements V+4 and invokes V.vtable+C only when
the new count is zero. The destructor chain is82C47EA8 -> V.vtable+0
(82C4D1A0) ->82C4C9C0. Do not call this an unconditional stop or assume every
release immediately destroys pending audio. The terminal cleanup closure is
reviewed separately below.

## Packet callback timing already resolved

In the source processing body82C472E8, after a non-looping packet reaches its
consumed sample limit,82C47660 invokes E.vtable+5C=82C3EE28(E,node,0).
82C3EE28 first calls82C422A0: removes the node from the active queue, appends it
to the free list, resets source progress and restores capacity bit20. Only then,
when a node and callback exist, it invokes the callback with a stack record:
`{context S, node[24] completion-word pointer, reason r5}` at82C3EE74.
For normal consumption the reason is0. This reaches original823463D0, which
clears the slot word and wakes the event. **Acceptance is not completion.**
The packet callback follows source consumption inside the SDK processing path;
it is not proof of physical DAC presentation time or host wall-clock equality.
The optional loop callback is absent in this Dac record (R+54=0).

**823463B0 is a processing-pass callback, not merely a state-change callback.**
V+2C receives R[4C]; V+30 receives S. Active-list processing82C48C70 invokes
V.vtable+44=82C6D398. At82C6D3E4 that method invokes V+2C with a stack
record `{S, processing destination returned by V.vtable+48}`. It then invokes
V.vtable+54=82C4C450, which performs the source processing through82C6DAF0
and U.vtable+18=82C472E8. Thus the pass callback precedes packet consumption
and its possible completion callback. It can wake the Dac pump during an active
pass even without a packet completion. Original823463B0 only consumes S and
the run flag; it does not inspect the destination field. A native wake source
must cover real processing demand/underflow as well as completion. No fixed
wall-clock callback schedule or physical speaker latency is established here.

The16-byte guest event remains a real CPU allocation. Main independently pinned
82C411C8 ->82C41480, whose argument forwarding tails8238E880, and the paired
82C41248 ->82C41490 ->8238EB00 free. Its output allocation pointer overwrites
the relevant output location; no82E2D998 read is required. Keep original
allocation/init/free, and use separately owned native wake storage for the native
wait replacement; never place a fake native handle in the guest KEVENT.

## Terminal release and outer shutdown

On final V release82C4C9C0 first invokes V.vtable+3C=82C4D3F0 with r4=1.
If not pending, that reaches U.vtable+34=82C46938; flags1 immediately clears
source state bits57 through E.vtable+54. 82C6DE28 removes the active connection
and invokes V.vtable+50=82C6D038 to unlink it and release active destination
counts. On successful stop,82C4CA0C invokes U.vtable+20=82C460B0, which tails
**82C41570(E)**. That helper refuses a still-active source (8000FFFF).
Otherwise it loops through E.vtable+5C=82C3EE28 with node0 and reason
**80004004**, until no active packets remain. Therefore final release invokes
the same completion callback for cancelled packets, after returning their
descriptors. Dac823463D0 ignores the reason and still clears/wakes its slot.
Only afterward does the wrapper unlink its global entry, release U at82C4CAF4,
clear V+4C and run base cleanup82C6CE38. Native cancellation must retire real
host data/callback use before applying the equivalent guest release effects.
This source-level closure is not proof that every original SDK thread is joined.

The original outer shutdown **82811018(app)** queues graph destruction calls
8233C808 under Q4C and releases that lock at828110F0. Later **8281119C calls
82338FA0(Q)**. This root destructor:

1. Acquires Q4C (or Q+40 callback), appends the8-byte CPU command
   `{82339200,Q}` at Q[20]+Q[D0], and releases Q4C at82339024/30.
2. Acquires **Q48 at8233903C**. The Dac worker holds this critical section
   across823460D0, so acquisition serializes against that worker body. This is
   not a wait on the Q54 HANDLE or proof that its OS thread has fully returned.
3. Under Q48, drains residual CPU commands and Q+F0 work through82339458,
   then releases Q48 at82339084. It subsequently frees root registries,
   callback storage, command/work buffers and Q with the real allocator.

82339200 removes all Q+58 and Q+10 graphs by calling8233C838. That function
invokes each node's vtable+0 at **8233C87C**, which is Dac82345A50 for S,
then its nondeleting base destructor before freeing the graph allocation.
With a running worker, command consumption occurs inside82339458 on that
worker, holding Q4C and Q48. Dac teardown clears the run flag; after update
returns, the worker checks it and branches to its Q48 release. It need not
read S's PCM fields again on this exit path. **The Dac destructor can therefore
execute on its own worker.** A replacement must not blindly self-join or try
to acquire Q48 inside that destructor. Retire host source/callbacks there,
retain any native owner required for the worker epilogue, and place the actual
OS join at an outer owner boundary before reclaiming thread-owned state.
Main owns that implementation and its tests. This analysis does not claim a
successful native root shutdown, nor authorize zeroing Q54 as a join substitute.

## Six-channel route: numeric order proved, speaker labels unresolved

The default route is derived from original calls, not from a Windows6-channel
default. R+48 is null. V.vtable+24=82C6CED8 invokes82C6CC60 with no explicit
send definition; its zero default at821B343C selects the singleton's actual
output object at+40. It queries that object's format and calls
**82C40D28(sourceChannels,destinationChannels,&mapping)** at82C6CCF8.
For6-to-6 it returns count6 and **821B2718**: six8-byte records, each with
source byte, destination byte, two zero bytes, float1.0:

```
00 00 00 00 3F800000   01 01 00 00 3F800000
02 02 00 00 3F800000   03 03 00 00 3F800000
04 04 00 00 3F800000   05 05 00 00 3F800000
```

Combined with82C64960's component-preserving deinterleave, this proves the
six interleaved Dac components remain in order through the source and default
six-channel send. The previously verified engine-plane permutation is
`[0,2,1,5,3,4]`; no second guessed swap should be added. The nearby4-to-6
mapping821B2778 routes0->0,1->1,2->4,3->5. It corroborates index structure,
but does not name any physical speaker.

**Still unproved:** whether original output indexes4/5 designate back or side
speakers, and the complete original index-to-speaker labeling/downmix/output
configuration contract. No original XAudio1 header or official primary source
establishing that link was located in this bounded pass. The import
XAudioGetSpeakerConfig exists (82CC2884, ordinal1FF); its single direct caller
8225879C is not the Dac source-creation routing path. Merely finding it does not
give the zero-filled source record a WAVEFORMATEXTENSIBLE channel mask.

Microsoft documents native XAudio2's implicit6-channel order as
FL,FR,FC,LFE,SL,SR, and treats standard six-channel back/side layouts one-to-one.
That is a **host policy**, not evidence that this older source SDK uses it.
[XAudio2 channel mapping](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping)
Windows channel masks explicitly distinguish back bits4/5 from side bits9/10.
[Channel-mask definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/channel-mask)
Therefore a native6-channel float source and identity-index matrix are viable
ownership/numeric representations; labeling it as faithful0x3F/0x60F output,
or accepting automatic Windows stereo downmix, remains gated. A muted transport
fixture establishes none of those speaker/downmix equivalences.

Main separately verified lower engine modes emit shorter PCM blocks. This
transport profile must validate F9D=6, fixed256 frames and1800 bytes, and reject
unsupported mode mutation before sending. The constructor's FA1=1 and mode3
table constrain the observed path; no four/two/one-channel expansion is implied.

## Integration limits and reproducibility

The five required SDK cuts now have concrete activation, gain, capacity,
submission and final-release meanings. Preserve real CPU event allocation,
graph queue commands, mixer allocations and callbacks, and separate host
submission ownership from guest slot state. Remaining explicit fidelity gates
are physical speaker/downmix policy, category-volume policy, exact SDK gain
transitions in the host transport, and implementation/testing of normal worker
and callback quiescence. No production readiness or audible equivalence is
claimed. No PCM fixture changes or additional runtime/config/test edits were made.

From the workspace, existing evidence can be verified without regeneration,
SDK execution or audio. `python -B build/dac-sdk/probe.py ADDRESS[:BYTE_SIZE]`
regenerates only its owned disassembly/report; explicit leaf extents are listed
in that report. Run the following independent read-only check to verify the
existing report, critical callsites, vtable relationships and route bytes:

```python
from pathlib import Path
import json, sys, struct
sys.path.insert(0,'tools')
import analyze_poststart_integration as a
b=Path('analysis/simpsons.pe').read_bytes(); a.validate_identity(b)
_,pdata=a.layout(b)
r=json.loads(Path('build/dac-sdk/report.json').read_text())
assert r['image_sha256']==a.sha(b)
for pc,f in r['functions'].items():
    v=int(pc,16); n=f['size']
    assert a.sha(a.span(b,v,n))==f['sha256'],pc
    if f['extent']=='.pdata': assert pdata[v][0]==n,pc
pins={
 0x82345A3C:0x488F9D6D,0x82345EB0:0x488F9C51,0x82345F0C:0x488F9BF4,
 0x823461F4:0x488F92F5,0x82346230:0x488F9409,0x82345A88:0x488F9751,
 0x82816320:0x4BB22FA9,0x823392D8:0x8063004C,0x828166C0:0x4BB25F09,
 0x828166F8:0x4BB22BF9,0x82816708:0x4BB22BC1,0x8281673C:0x4BB22B8D,
 0x8233903C:0x4BFF06E5,0x82339224:0x48003615,0x82339258:0x480035E1,
 0x8233C87C:0x4E800421,0x82C47660:0x4E800421,0x82C3EE74:0x4E800421,
 0x82C415CC:0x4E800421,0x82C6D3E4:0x4E800421,0x82C49184:0x4807A6C1,
 0x82C491B4:0x4807A681,0x82C49CA0:0x3CE03F80,0x82C49CE0:0x90FF0084,
 0x82C49CE4:0x90FF0088,0x821CA430:0xFFFFFFFF,0x82000BB0:0x3F800000,
 0x821B2A78:0x82C3C748,0x821B2AA0:0x82C4C348,0x821B2ACC:0x82C4C540,
 0x821B29B0:0x82C472E8,0x821B29B8:0x82C460B0,0x821B29C4:0x82C460C0,
 0x821B29C8:0x82C46848,0x821B29CC:0x82C46938,0x821B29F0:0x82C463E0,
 0x821B2964:0x82C3E980,0x821B296C:0x82C3EE28,0x821B2D30:0x82C6A0D8,
}
for v,w in pins.items(): assert a.word(b,v)==w,hex(v)
expected=b''.join(bytes((i,i,0,0))+struct.pack('>f',1.0) for i in range(6))
assert a.span(b,0x821B2718,0x30)==expected
assert a.word(b,0x821B343C)==a.word(b,0x821B3440)==0
print('PASS',len(r['functions']),'function hashes,',len(pins),'words, numeric route')
```

Executed read-only verification on2026-09-10: **PASS70 function hashes,
39 words, numeric route**, exit0. No parent build, PCM rerun or SDK execution.
