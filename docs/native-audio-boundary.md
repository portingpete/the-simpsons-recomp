# Reached audio boundary: original EXm0 codec and XMA pool

**Guard the original codec provider 8233E598, or its pool initializer 8233E5C0,
before the first mutation. Do not return a fake XMA context or bypass audio.**
The promising native replacement is the original **EXm0 codec callback
contract at 82D073AC**, with real software decoding and original engine buffer
ownership. Import substitution alone does not isolate hardware state: the
original decoder also dereferences the returned XMA context directly.

Parent integration update: build110 implements that provider-entry guard with
the original twelve-byte pin `7d8802a69181fff89421ffa0`. All28 CTest suites pass.
Actual boot069 stops at8233E598, caller82816470, with initialized flag0, both
physical pools0, CPU pool owner0 and free count0. It never enters the XMA pool
initializer. The native codec itself remains unimplemented. The analysis below
preserves boot068's reached import and independently verified original contract.

This document is the only changed file. Original instructions in
`analysis/simpsons.pe`, base82000000, sizeEC0000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
are authoritative. Addresses, offsets and sizes are hexadecimal unless stated
otherwise; counts explicitly called decimal are decimal. No runtime, generated,
asset or reference code is edited. No decoder or output device is implemented.

## Actual boot068 and a diagnostic correction

Parent reports build109 **28/28 tests passed**. Independently read boot068:

- Native mutant14C is created with initial-owner0.
- Physical allocation **A=E112F000**, size120000, protection404, alignment800;
  physical backing address01130000.
- Physical allocation **B=E0FAF000**, size180000, protection4, alignment800;
  physical backing address00FB0000.
- XMACreateContext fails with output pointer **E4626120** and LR **8233E744**.
  The saved outward LRs are **8233E5A8, 82816470, 8280D214, 8280D680,
  826D951C, 823B7718**. Terminal logs show the native workers exit afterward.

**82329660 is not the XMA wrapper.** It is a 74-byte critical-section
constructor: clears its CPU fields, conditionally calls
**82CC2894 RtlInitializeCriticalSection**, and returns the object. It contains
no XMA call. The actual instruction is **8233E740: bl82CC35A4**, in
**8233E5C0**; its LR is 8233E744. The logged last-function value82329660
matches an earlier helper at 8233E6D0 and must not override the original words.

The create call supplies **r3=&record.context**. The local reference declares
XMACreateContext with that single output-pointer argument. Logged r4=0, r5=1,
r6=18 are residual registers, not proof of XMA flags, channel count or format.
The original zeroes the output word at 8233E73C, calls create, then uses the
word without checking the returned status. Failure must remain explicit.

The engine caller is **82816248**, reached from the original startup chain.
At **8281646C** it calls provider **8233E598**. After return it passes the
descriptor in r4, registry in r3, to **82340448 at 82816478**. The provider
calls the pool initializer at 8233E5A4 and then returns literal **82D073AC**.
A guard at provider entry stops before this codec's allocations, flag/list
publication or hardware import. It does not undo earlier audio/CPU setup in
82816248. Guarding the much broader caller is unnecessary for this first boundary.

## Pool initialization and paired cleanup

Global byte **82E36B82** is the original initialized flag. **8233E5C0** exits
if it is nonzero; otherwise **8233E5F0 sets it to1 before allocations succeed**.
It reads audio CPU root **Q=BE32[82E31BCC]**. Q+18 and Q+1C are allocation/free
callbacks. When allocation callback is null, original writes **82339788** and
**82339798** respectively. These are physical-allocation wrappers:
82339788 rearranges size/alignment/protection into 824337F8; 82339798 tails
to82433908. Do not confuse these addresses with 82349788/98.

The allocation calls are **8233E640** for A (120000, alignment800, protect404)
and **8233E684** for B (180000, alignment800, protect4). They are stored at
**82E36CAC=A**, **82E36CB0=B**. The allocator object Q+14, vtable+4, then
allocates **1C30 bytes**, requested alignment10, at **8233E6B8**.
Let that allocation be H. Original 82329660 constructs H's critical section;
the following original call initializes it again. Preserve the proven CPU
locking/count contract if retaining this pool. Globals become:

- **82E36CB4=H**, the allocation/critical-section owner.
- **82E36CA8=H+30**, the first record R0.
- **82E37318/+4/+8**: free-list head, tail and count, initially0.
- **82E37314**: active-list head, initially0.

The exact loop creates **100 hexadecimal = 256 decimal** records, stride1C.
For record i (0 through255), Ri=H+30+i*1C:

```
Ri+00  XMA context output word; initialized0 before create
Ri+04  input0 = A+i*1000                 capacity800
Ri+08  input1 = A+i*1000+800             capacity800
Ri+0C  output PCM = B+i*1800             capacity1800
Ri+10  work = A+100000+i*200             capacity200
Ri+14  intrusive next
Ri+18  intrusive previous
```

The list node is Ri+14, not Ri. First100000 bytes of A contain all paired input
buffers; its final20000 bytes contain work slices. B contains all PCM slices.
The loop's work cursor begins100000, adds200, stops at120000; output adds1800,
record pointer adds1C. Its end bounds exactly match both physical allocations
and H+30+1C00=H+1C30. Boot's first output pointer implies **H=E46260F0**;
this is arithmetic from the verified ABI, not an independent allocation log.

After each create, **8233E77C -> XMADisableContext(context,1)** executes, then
the node is appended to the free list and count incremented. Create and disable
results are not checked here. Returning success without real decoding capability
would leave an apparently populated engine resource pool.

Paired pool shutdown is **8233E7E8**, called by original audio teardown
**82338FA0 at 823390AC**. The latter queues CPU cleanup and waits for its own
outstanding work before the pool call; that broader synchronization is not
replaced or claimed runtime-verified here. The pool shutdown:

1. Checks initialized flag, clears it, calls **XMAReleaseContext at8233E81C**
   on all256 record words. It does not use a successfully-created count.
2. Frees H through Q+14's vtable+C at8233E850; clears82E36CB4/82E36CA8.
3. Frees A and B through Q+1C at8233E870/884; clears82E36CAC/82E36CB0.

This is a full-initialization cleanup contract, not a proven partial-failure
rollback. A native port must track actual acquired resources and reject unsafe
teardown; it must not claim the original early initialized flag proves readiness.
Free/active list globals are not all cleared by this shutdown body. No normal
audio shutdown, pool exhaustion or reuse has yet run in boot068.

## Original engine codec registration is the useful replacement boundary

Verified descriptor words at **D=82D073AC**:

```
D+00  8233E9A8  instance size/alignment query
D+04  8233E9C8  instance construction; boolean return
D+08  8233EC58  release borrowed pool records
D+0C  8233FAF8  consume compressed stream / emit PCM to engine
D+10  00000000  mutable registry next link
D+14  45586D30  original four-byte identity "EXm0"
D+18  00000000  extra-allocation field initially zero
```

Do not enlarge this proven descriptor based on nearby data. The next word at
D+1C points to unrelated-looking text; no descriptor meaning is assigned here.
**82340448** scans a registry through nodes descriptor+10, compares identities
at descriptor+14, returns an existing matching descriptor, or links the new
descriptor and increments the registry count. Keep this CPU behavior. Returning
null from provider is not a supported way to omit the codec: registration reads
the supplied descriptor.

Generic instance creation **823404A8** proves the callback ABI:

- Calls D+00 with r3=full incoming channel count, r4=pointer to alignment word
  (823404D0). **8233E9A8** writes10 and returns
  `58 + 18 * ((channels+1)>>1)`.
- Allocates the original CPU instance V, stores D+08 at V+C and incoming
  channel count's low byte at **V+2E** (82340560), then calls D+04 with r3=V
  at **82340570**. It treats the returned low byte as boolean.
- On success stores D+0C at V+14 and D+14 at V+18; retains original input queue,
  allocation owner and surrounding engine metadata. On failure it invokes
  **823402E8**, which calls V+C at **82340310** if nonnull and frees the
  engine-owned allocation(s). A failed native constructor must therefore leave
  a state its paired callback can safely destroy.

**8233E9C8** allocates no new physical storage. It sets up layer records at
V+34, aligned with `(V+5F)&~7`, stride18, count **V+44=ceil(V[2E]/2)**.
Each layer borrows one pool record: pop free-list node using8233E3F8, link it
into active head82E37314 under H's lock, store Ri at layer+0, and store channels
1 or2 at layer+C. Thus odd channel counts end in a mono layer. If any pop fails,
it returns already-acquired records to the free list and returns0; success1.

**8233EC58** returns all borrowed records to the free list under the same lock;
it does not free the engine instance or call XMAReleaseContext. Contexts belong
to the global pool until its shutdown. Global helpers **8233E8A0** and
**8233E920** traverse active records to disable/enable their contexts. They
are additional engine lifecycle boundaries if a native implementation removes
this original hardware pool. Their callers outside the bounded audio range were
not exhaustively scanned; this is not a claim of complete guard coverage.

A viable later port can replace this codec's provider plus complete callback
lifecycle with real native decoder owners, preserving the generic engine CPU
registry, allocations and buffer queues. It must also cover the pool teardown
and active enable/disable helpers if those original paths remain reachable.
The original physical/context pool is private backend storage in the recovered
codec path; a native codec need not recreate a hardware-context memory layout.
That observation is bounded to these consumers, not a whole-program proof that
the pool globals have no other users. Preserve or explicitly replace the known
CPU ownership effects; do not merely set the initialized flag or invent pool
entries to claim successful startup.
It must not publish an apparently ready EXm0 provider whose operations only
return silence or fabricated progress. The current justified action is a guard.

## First per-stream XMA ABI and direct context dependency

**8233F000** obtains an original queued segment via **8233E108**. Queue slots
have stride14, a pointer at+0 and nonzero sample extent at+C; the helper advances
the original byte read index V+30 modulo V+32. It returns null if the next slot
has no sample extent. Those CPU queue/lifetime effects must survive a port.

On first input, **8233F0A4..B0** uses the low two bits of the first layer word
as r4 to **8233ED78**, and stores it at V+50. Each layer word's upper30 bits
give its byte extent including that word. The engine skips the four-byte layer
word, retains the data pointer, and copies at most800 bytes at a time into one
of the two input slices via **8233EEC0**, an original byte-copy helper. It marks
input0/1 valid using XMA imports only after copying, alternating layer+D, retaining
remaining byte counts and advancing the source pointer. Exhausted temporary
input is not evidence of end of stream.

At **8233EE4C**, XMAInitializeContext receives r3=context,
r4=SP+50, pointing to **38 bytes / fourteen BE32 words**, initially zeroed.
The following fields are proved by the original stores (reference names below
are corroboration, not an independently recovered hardware structure):

```
+00 Ri.input0             +04 1 input packet
+08 Ri.input1             +0C 1 input packet
+10 20                   input bit offset
+14 Ri.output            +18 18 output blocks
+1C Ri.work              +20 4 subframes requested
+24 layer_channels-1     +28 first_layer_word & 3
+2C/+30/+34 0            loop-related words remain zero here
```

The read-only reference declaration `XMA_CONTEXT_INIT` in
`K:/Simpsons/RexGlueCurrent/src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp`
has exactly these offsets/names and size56 decimal. Original arithmetic
independently proves two800-byte packets and output block units100 bytes
(18*100=1800). The sample-rate field is an encoded selector, not the literal
48000. The checked original assets' selector is3; the general selector-to-Hz
map is outside this pass. Layer channel encoding is explicitly channels-minus1.

Initialization order is **disable(context,1), initialize, set-output-valid,
set-input0(pointer,1), set-input1(pointer,1), enable** at
8233EE38/4C/5C/74/8C/9C. Hardware command scheduling, context memory bitfields,
MMIO and completion timing are not ported by documenting these arguments.

**Direct context reads prevent an opaque import-only replacement.**
8233FAF8 retrieves Ri.context through layer+0, then at **8233FE64/70** reads
bytes context+8 and context+C. It tests each byte's80 bit, with null context
also entering the error path. **8233E568** contains the same standalone test.
The consumer also calls XMA write/read-offset and validity APIs. A small native
ID cannot be passed back as if it were an SDK memory-layout pointer. Replacing
the codec callback path avoids a need to reproduce that hardware context model.

## Output PCM, trimming and what remains unproved

**8233FAF8(V, output_descriptor, requested_samples)** uses r3/r4/r5 and returns
the accumulated produced sample count in r3 at8233FF70. Request0 returns0.
The output descriptor has sample storage pointer at+4 and plane stride in
float elements at+E (BE16). Layer index i selects channel planes2*i and2*i+1;
mono last layers use only the first plane. This is a planar engine float output,
not a direct speaker submission API.

The hardware output ring is **1800 bytes per layer** of signed BE16 mono or
interleaved stereo PCM. **8233F250** converts it into channel-separated floats:
scalar `lha` at8233F3C0 and stereo loads8233F830/854 multiply by the exact
constant **821DD2AC=38000000 = 1/32768**; vector unpack/`vcfsx ...,15` agrees.
Samples therefore map -32768 to -1 and32767 to32767/32768. This proves the
engine's conversion contract, not bit-exact equivalence of a software codec.

The consumer interprets XMA output write offsets in **100-byte blocks**
(8233FC20 shifts by8), maintains its byte cursor in layer+8, and wraps at1800.
Published read offsets are rounded down to multiples of **400*layer_channels**
bytes, converted back to100-byte units at8233FDD4..E8. It disables, updates
read offset, marks output valid and re-enables when that published value changes.
Availability also depends on validity and first-input state; equal read/write
offsets cannot simply mean empty in every case.

Original trim evidence matters for software decoding:

- For a segment whose byte+10 flag is0, **8233FBA0** adds **180 samples
  (384 decimal)**, plus carried V+4C, to each layer's skip count. This is
  CPU evidence of skipped decoder output, not authority to apply an unrelated
  library's default priming skip as well.
- **8233FF28..4C** computes carry toward a **200-sample (512 decimal)**
  boundary using the segment sample extent and that180 addition. Byte+10's
  full meaning and all seek/loop transitions remain unverified.
- **8233FF80** resets/reinitializes hardware and marks V+54; the original
  decode path can then zero output at8233FEF4. This is an original error path,
  not permission to present muted fallback as successful native decoding.

This bounded pass does not prove partial-final-packet padding, channel speaker
ordering beyond sequential pairs, sample-exact software priming, loop/seek
behavior, all callback consumers, or asynchronous scheduling equivalence.
The plain input copy can be shorter than800 bytes; the import still sees a
one-packet slot. Do not silently pad or send decoder EOF without proving the
original expected bytes and sample extent.

## Original asset corroboration and minimal next implementation target

Independently reran the existing read-only format inspector on these original
files under `Simpsons Game, The (USA)`:

- `audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu`: **19328 decimal bytes**,
  SHA256`bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9`.
  EAAC version0/codec3, mono,48000Hz,54901 samples;11 blocks. Audio startsB00.
  First bytes`000006b80000128000001ac308000000`: block6B8, sample count1280,
  one layer extent6B0 including its word, selector3. Compressed bits stay opaque.
- `audiostreams/gts_mus.mus`: **25248896 decimal bytes**,
  SHA256`4f582d0dcad83cc70dd288ea4b83b02ef8e58c006f041c0a189a3b5f18831931`.
  Six indexed streams. Entry0 header100=`0314bb80401fdd1c`, six channels,
  48000Hz,2088220 samples; audio180, extent395680,408 blocks/1224 layers.
  The three-layer grouping agrees with the original ceil(channels/2) contract.

These checks verify framing, layer spans and declared sample totals, not audio
decoding. [The existing asset evidence](assets.md) documents the larger SNU/MUS
corpus and EA-XMA format attribution. Movie audio is separately EA ADPCM R3 in
the inspected VP6 containers; this XMA pool does not prove a movie-audio contract.

Read-only `K:/DarkRecomp/runtime/native/XMA.md` and `xma_raw_decoder.h/.cpp`
provide a useful **separate-game reference** for an actual CPU XMA decoder:
owned2048-byte packet input, retained partial frames and no synthetic EOF on
temporary starvation. Its SDK-context bridge, game-specific skip behavior,
tests and output claims do not establish this game's contract and are not copied.

During this task the parent reported a separate decoder agent's successful
native FFmpeg XMA2 probes on two original mono48000Hz SNU streams, producing
their declared8064/54901 finite sample counts, and an actual XAudio2 two-buffer
muted host probe. Those results were not run or independently inspected here.
They support the feasibility of native decode/output; they do not prove the
EXm0 callback's raw-frame/384-sample trim contract, engine ownership, channel
ordering or unmuted game playback. In particular, finite file decoding may have
already applied trimming that must not be applied a second time in this callback.

**Minimal next target:** validate one original mono EXm0 stream offline against
a real CPU decoder, preserving original layer framing and sample extents, then
prove the180/200 trim arithmetic, signed16 quantization and planar float output
with original packet/PCM comparisons. A subsequent native codec owner can fit
the four original callbacks without exposing a mapped XMA hardware context.
Keep codec creation/decode and audio presentation explicitly unavailable until
their real behavior exists. XAudio render-driver registration/frame submission,
mixing, speaker configuration, sound-device lifetime and audible output are
separate unrecovered boundaries, not successes implied by codec registration.

## Reproducible original-byte verification

Run from K:/SimpsonsNativeCopy via `python -B -` with a PowerShell here-string.
It reads only, writes no report/cache, validates image identity and `.pdata`,
and pins the actual callback/provider/context-access instructions. Assertions
are evidence checks, not game execution or decoder tests.

```python
from pathlib import Path
import json, sys
sys.path.insert(0, 'tools')
from analyze_poststart_integration import validate_identity, layout, span, sha, word
from inspect_assets import inspect_snu, inspect_mus
b = Path('analysis/simpsons.pe').read_bytes()
validate_identity(b)
_, pdata = layout(b)
PINS = {
  0x82329660: (0x74, '1a01395086c744ef60692301a8ef5e7b2c1a1cea321d466b6e54a738297e3208'),
  0x8233E598: (0x28, '60158be362cf49b7a15a55465d89a01aa9637cde5f70820016d52f0a56732316'),
  0x8233E5C0: (0x228, '82694d1f6f56426c04d506a6f599422e72718c8d3c5a1babd35e10456f8baed8'),
  0x8233E7E8: (0xB4, '34e3021f1912dae619b0dc9100ef4eb14c3a9616484a5b09075f4c1c0c57b23b'),
  0x8233E8A0: (0x7C, '10614d4cbaa36f3400c03a4f8c918cdb742ed0f603ac22fca22c0577b092402e'),
  0x8233E920: (0x84, '694ae1f70df9e56efd3d4b5627385d7f4981092219c3cd8b02170b9e2963c01d'),
  0x8233E9C8: (0x228, 'ce0b448956bac299b2d91dd95db7ad48effa5f0d91273b857d9c16d9aa312eb2'),
  0x8233EC58: (0x11C, '02d5ed1ffda69cb7eff80167fab15522f318a124b4b31f015c1fbac2344ea3aa'),
  0x8233ED78: (0x144, '80c53a31a4056769565e245cd4d1f5e54c9f0e0703706fc7e94bf7cbe0e596f2'),
  0x8233F000: (0x24C, '574a68fbfc334602c29df3d71c754d1b54ab63720e4c0e143efa9533fae02e5c'),
  0x8233FAF8: (0x484, 'feae5c3a4877b181a3101fc3969699c88d67883d1ec92951b4447043a1004276'),
  0x823404A8: (0x1E4, '51b11d9f5f257c69fab285b67bfc1b4a963ad524b1c99b09b341101bcb084599'),
  0x823402E8: (0x90, '7941c14fe977ded404900f80bf9bbdf696e49d27a0d491f73f1fe46dae409cf9'),
  0x82816248: (0x784, '9bfd8fa2a5fc19899e6c05844b747d4780f78a2a9956d42186af79e024cd78a7'),
  0x82338FA0: (0x260, '8eb9d979f9c92dbab25eb580433c2e2fa7a67f2dc2c046616d143f39b5c3bc6a'),
}
RANGES = {
  0x8233E9A8: (0x1C, 'f036195085ab6e1bc204906c3ad7f757dc3ae60b419c8b2172b339456c23d0bd'),
  0x8233E568: (0x30, 'f506ebc30d8e8d26272b447d687187d126f4ed7172c22b8b1b0ef8554b83f4b8'),
  0x82339788: (0x14, '35d264772eb864532864add529c96c34f24c04e7cd0faa58b8af499ba4be31ac'),
  0x82340448: (0x60, '7b31a31a93763e32556fa68a87f0853648e1ec1045d4bbc314e97cfda35d7807'),
  0x82D073AC: (0x20, 'f54791541a8b80f448bd7cad7c4ea3c2389225f00587e8c9fea75f0ae7e6769e'),
}
WORDS = {
  0x8281646C: 0x4BB2812D, 0x82816478: 0x4BB29FD1,
  0x8233E5A4: 0x4800001D, 0x8233E5F0: 0x996A6B82,
  0x8233E638: 0x3C600012, 0x8233E67C: 0x3C600018,
  0x8233E6AC: 0x38801C30, 0x8233E6D0: 0x4BFEAF91,
  0x8233E740: 0x48984E65, 0x8233E77C: 0x48984DB9,
  0x8233E7B8: 0x3B9C0200, 0x8233E7CC: 0x3B5A1800,
  0x8233E7D0: 0x3BDE001C, 0x8233E7DC: 0x4198FF5C,
  0x8233E81C: 0x48984D39, 0x823390AC: 0x4800573D,
  0x8233EE4C: 0x489846C9, 0x8233F0AC: 0x556407BE,
  0x8233FE64: 0x894B0008, 0x8233FE70: 0x896B000C,
  0x8233F354: 0x1180024E, 0x8233F370: 0x100F634A,
  0x8233F3C0: 0xA9280000, 0x8233F3DC: 0xEDAD0032,
  0x821DD2AC: 0x38000000, 0x8233FD78: 0x4BFFF4D9,
  0x82340560: 0x9ADF002E, 0x82340570: 0x4E800421,
  0x82340310: 0x4E800421,
}
for a, (n, digest) in PINS.items():
    assert pdata[a][0] == n and sha(span(b, a, n)) == digest, hex(a)
for a, (n, digest) in RANGES.items():
    assert sha(span(b, a, n)) == digest, hex(a)
for a, expected in WORDS.items():
    assert word(b, a) == expected, hex(a)
assert span(b, 0x82D073C0, 4) == b'EXm0'
imports = {x['address']: x['name'] for x in json.loads(
    Path('analysis/executable.json').read_text())['imports'] if x['kind'] == 1}
for a, name in {
    0x82CC35A4: 'XMACreateContext', 0x82CC3534: 'XMADisableContext',
    0x82CC3554: 'XMAReleaseContext', 0x82CC3514: 'XMAInitializeContext',
    0x82CC3544: 'XMAEnableContext', 0x82CC2894: 'RtlInitializeCriticalSection',
}.items():
    assert imports[a] == name, hex(a)
root = Path('Simpsons Game, The (USA)')
snu = (root/'audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu').read_bytes()
mus = (root/'audiostreams/gts_mus.mus').read_bytes()
assert sha(snu) == 'bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9'
assert sha(mus) == '4f582d0dcad83cc70dd288ea4b83b02ef8e58c006f041c0a189a3b5f18831931'
s = inspect_snu(snu); m = inspect_mus(mus)
assert (s['header']['channels'], s['header']['sample_rate'], s['header']['samples'],
        s['audio']['block_count']) == (1, 48000, 54901, 11)
e = m['streams'][0]
assert (m['entry_count_audio'], e['header']['channels'], e['header']['sample_rate'],
        e['header']['samples'], e['audio']['block_count'],
        e['audio']['layer_count']) == (6, 6, 48000, 2088220, 408, 1224)
assert snu[0xB00:0xB10].hex() == '000006b80000128000001ac308000000'
assert mus[0x100:0x108].hex() == '0314bb80401fdd1c'
assert 256*0x1C+0x30 == 0x1C30
assert 256*0x1000+256*0x200 == 0x120000
assert 256*0x1800 == 0x180000
print('PASS:', len(PINS), 'function/pdata pins,', len(RANGES), 'ranges,',
      len(WORDS), 'words, EXm0 identity, 6 import mappings, 2 original assets, pool bounds')
```

Validation completed: **15 full function/.pdata pins, 5 ranges, 29 words,
EXm0 identity, 6 import mappings, both original asset hashes/framing/sample
totals, and pool bounds passed**. These are read-only evidence checks; this
task ran no runtime build, software audio decode or playback. Boot068 remains
the reached original XMACreateContext failure. Document frozen for this bounded
scope; no source, generated, configuration, test or original-data changes.
