# Native audio output boundary after EXm0 acquisition

Frozen bounded finding: **guard original Dac0 constructor 823456D0 at entry**.
This is the output engine owner, before its first CPU/global write or output SDK
call. Throw an explicit unsupported-output failure; returning success or setting
the SDK-disable global is not a native output implementation.

Only this document is owned by this investigation. Original instructions come
from `analysis/simpsons.pe`, base82000000, sizeEC0000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses, offsets and sizes below are hexadecimal unless explicitly decimal.
The allocator/EXm0 bridge documents remain frozen. No runtime, hook, generated,
test, original media or reference source changes are made here.

## Immediate fixture guard and exact caller

Boot070 acquired the native EXm0 factory at provider return82816470, then failed
reading7FEA1800, width4, at function82C4A290, LR82C3EEA8, SP0203F200. Build117's
dispatch-slot observer cannot intercept the direct generated provider BL at
8281646C. Observe an explicit entry-hook failure at823456D0 instead; restore
the fixture's saved entry PPCContext for later EngineCpuCalls, rather than
resuming a guest frame unwound by the exception.

- Constructor entry pin, 16 bytes:
  `7d8802a6486f6cf5dbe1ffd09421fee0`. Its .pdata extent is380 bytes.
- **r3=S**, a real engine stream/object allocation. Generic CPU constructor
  **8233DBE8** has already initialized S+4=Q (from82E31BCC), S+8, S+10,
  S+14/+18, S+1C, S+20/+21. Do not fabricate S or Q.
- **LR=8233DC44**: `8233DC38: lwz r11,8(r5)`, `8233DC3C: mtctr r11`,
  `8233DC40: bctrl` (`4e800421`). At dispatch r5 points to descriptor
  **82D069B4**, whose +8 word at82D069BC is823456D0. r4 is the first word
  of the generic configuration record (`8233DC34: lwz r4,0(r6)`); this
  particular constructor does not read incoming r4. Do not give residual
  registers invented output-device meanings.
- Descriptor +4 at82D069B8 is **82345618**, which returns **3108** bytes;
  provider **823456B0** returns82D069B4. This descriptor has its process
  pointer at +10 (82D069C4), tag **44616330 = Dac0** at +28 (82D069DC),
  and a back-link at +14 (82D069C8) to the preceding header82D069B0.
- The constructor first publishes its vtable **821DCB90** at82345720.
  Guarding entry precedes that publication and the later output allocations.
  It does not undo generic object/graph allocations made before entry, or the
  already acquired EXm0 factory. This is a terminal startup observation point,
  not proof of a fully rolled-back application startup.

The exact failing chain is:

```
823456D0 Dac0 constructor
  823458C0: 488faba1 bl82C40460       LR823458C4
  82C40460: li r4,0; b82C3FE28
  82C3FE28 SDK singleton initialization/retain dispatcher
  82675890 chained initialization adapter
  82C3EE88 underlying initialization callback
    82C3EEA4: 4800b3ed bl82C4A290    LR82C3EEA8
    82C4A29C: 7d605c2c lwbrx r11,0,r11  [r11=7FEA1800]
```

Do not merely guard or neutralize the hardware load:82C3FE28 already holds
the critical section at82D01464. Its callback table is82CD0E58=82675890,
82CD0E5C=82C3EE88; the first adapter advances to the second. The MMIO helper
stores the byte-reversed value to82E2D9F4. The underlying allocator/constructor
82C4A0D0 later publishes its real SDK object to **82E2D9F0** (823458C0 has
not returned yet). The engine entry guard precedes all of this.

## CPU construction to retain, and output ownership to replace

The enclosing application function82816248 registers Dac0 via its provider at
82816348, before the later EXm0 acquisition. After acquisition it builds the
graph at **828166C0: bl8233C5C8**, with r3=FF, r4=app[310] count,
r5=app[30C] records, r6=app+2FC, r7=Q returned by82338F28. Construction of the
Dac0 graph node invokes the callback above. Once graph creation returns, the
application finds Dac0 and queues command3 at828166EC through8233DD20. Do not
skip graph creation, its other plugin constructors, or this command.

The Dac0 object has a 3108-byte queried extent. Proven constructor effects:

- **S+0=821DCB90**, S+0C=S+28, S+24=0. The generic header and Q link remain
  original CPU ownership. The three proven Dac0 vtable methods are +0 teardown
  82345A50, +4 parameter/command service82345AF8, +8 deleting base destructor
  82346528. Adjacent words are not assumed to belong to this vtable.
- **82345778 calls823543C0(Q)**. This CPU helper returns Q+34's existing
  statistics/timing object or initializes the real static object82E33DE8 and
  links Q+34. Its result is published to82E31F98. It performs no SDK call.
- **823457B0 calls82346418(Q,&S[24],...)**. That helper calls Q[14]'s real
  allocator vtable+4 with size30080, alignment80 (r5=0,r6=1,r7=80,r8=0),
  publishes the result to S+24, and calls CPU initializer82353BC8 on success.
  This initializes mixer arena metadata and Q's existing CPU work arena; it
  is not a console output allocation. The constructor additionally stores
  Q at mixerArena+30008. Retain allocation, initializer and matched free.
- Constants establish **S+30=48000.0f**, S+28=3.0f, S+38=0.0f;
  **Q+DC=48000.0f**, Q+D8=Q+B4=256.0f/48000.0f, Q+E0=3200000000.0f.
  The last is an original timing scale, not permission to assume a native
  processor frequency. Literal addresses are pinned below.
- Initializes CPU globals82E37324 (Q, counters/timing fields),
  82E31F9C=0, F9D=6 channels, F9F=0, FA0=1 (worker run flag),
  FA1=1 (configuration count),82E33800=3 (mode). F9E is a count obtained
  from scanning the original float table821C838C; retain that computation.
- If **82E36CB8!=0**, the original skips all remaining output SDK creation
  and returns1. Setter823456C0 merely writes this flag. This is a real
  alternate original branch, **not** a faithful native-output implementation;
  using it as a shortcut also skips the buffers/event/worker described below.

On the observed zero-flag path the first SDK initialization call is823458C0.
It receives r3=&stack[58], a twelve-byte zero-initialized record with its +4
word set to821B26E0. Wrapper82C40460 sets r4=0 for82C3FE28. After that call:

- **8234591C ->82C40468** creates an SDK voice into **S+40**. Its stack
  record at SP+90 contains byte+4=6, word+8=48000, float+3C=0,
  byte+3B=2, byte+40=1, callback+4C=823463B0,
  callback+50=823463D0, and context+58=S. These are byte/offset facts,
  not a fabricated XAudio2 or console SDK structure declaration.
- **S+44..S+3043** contains two PCM buffers, each1800 bytes, initially
  cleared. **S+3050** and **S+30A8** are two original submission records,
  stride58: +0 points to its PCM buffer, +4=1800, +54 points to its
  completion word **S+3100 / S+3104**. These words begin0. Cursors
  **S+3044** (submit) and **S+3048** (produce) begin0.
- **82345990 ->82C411C8(0,1)** allocates a real sixteen-byte guest event
  object, initially signalled, stored at **S+304C**. It is an object pointer,
  not an Nt handle: wait82C412A0 reaches import ordinalB0
  KeWaitForSingleObject; signal82C41270 reaches ordinal9D KeSetEvent.
  Replacing this with an arbitrary handle in original guest code is invalid.
- **823459E4 ->8232A920** starts an engine worker with r4=823462F8,
  r5=S, r6=&stack[70] configuration, r7=BE32[82E36BB4]; its label is
  **"RWAudioCore Dac" at821C8400**. The real worker handle returned in r3
  is stored through Q[54] at823459FC. The temporary thread-owner reference
  is then decremented through the existing CPU lifetime logic.
- **82345A3C ->82C3F7A8(S[40],0)** is a final SDK voice operation before
  constructor return1. It must be covered along with initialization; this
  investigation does not assign an unverified public SDK method name to it.

The native owner should therefore own the real Windows audio engine/source
voice, callback context, outstanding host PCM leases, wake mechanism and worker
lifetime as one transaction. Preserve S/Q/mixer allocations and CPU fields;
never pass a native voice identity into the original SDK functions. A partial
constructor failure must retire acquired host resources before CPU storage is
freed. The original constructor does not check all SDK returns, so leaving it
running after a fake success is particularly unsafe.

## Original CPU pump and PCM cut point

Worker entry **823462F8(r3=S)** calls **823460D0(S)** and returns0. The worker
holds Q[48]'s CPU critical-section wrapper around its lifetime, waits on S+304C,
and exits when82E31FA0 becomes0. Its useful CPU work must survive replacement:

1. **82339458(Q)** processes the original engine update. It preserves optional
   Q+40/+44 lock/unlock callbacks, Q+5C update list, Q+60 callback lists, queued
   commands in Q+20 with length Q+D0, high-water count Q+D4 and generation Q+EC.
   It calls each queued CPU function and advances by that function's return
   byte count. This is an engine CPU command queue, not a GPU packet decoder.
2. If82E31F9F is set, **82345F18(S)** builds the CPU mix descriptor at82E33860,
   calls **82354120(S[24],&descriptor)**, writes Q+E8 timing, and advances the
   double Q+8 by Q+D8. Retain original graph DSP callbacks:82354120 dispatches
   graph entries through823542BC, using the real graph descriptors and objects.
   This closure is not yet a claim that every future DSP callback is ported.
3. Dac0's graph process pointer is **82D069C4=82353B40**. It calls
   **82346320(S)** at82353B4C, then returns1. This obtains the original mixer
   output descriptor from S[24]+3000C, calls823544D0, conditionally performs
   the original fade82345630, and clamps the mixed floats with82354480.
   These CPU helpers are suitable to retain as AOT code. Do not replace their
   floating-point/NaN behavior with an assumed host min/max operation.
4. At the default six-channel setting, **823544D0** writes **256 frames** to
   **82E32000**, each frame six big-endian float32 samples, total1800 bytes.
   The exact planar-input to interleaved-output permutation is
   **[0,2,1,5,3,4]**. For source plane stride N=BE16[inputDescriptor+E],
   input base P=BE32[inputDescriptor+4], frame j writes values from
   `P+4*(j+[0,2,1,5,3,4]*N)`. The loop advances24 bytes/frame and stops
   after400 bytes of one plane. Physical speaker labels for these six planes
   are **not established here**. Four/two-channel branches also exist; they
   are outside the default-mode proof.
5. Worker823461C4 copies exactly1800 bytes into the next S-owned PCM slot when
   the CPU mix result equals1;823461D0 otherwise clears that slot. This is
   the original conditional silence policy, not permission to generate silence
   instead of calling the mixer. Its completion word changes0 ->1 (ready).
6. SDK capacity query **823461F4 ->82C3F4E8(S[40],&stack[50])** gates on
   returned byte bit20. The worker changes ready1 ->2 (submitted), then calls
   **82346230 ->82C3F638(S[40],&S[3050+58*i],0)**, advancing both cursors
   modulo2 in their original order. This helper dereferences SDK voice+4C and
   another vtable; it cannot consume a native identity.

The callback **823463D0(r3=C)** performs `*BE32[C+4]=0`, then signals
S[304C], where S=BE32[C]. It establishes the submitted-slot release/wakeup
effect. Callback **823463B0(r3=C)** signals the same event only while the run
flag is nonzero. Native completion contexts can carry owned S/slot identities
without recreating C's console SDK layout. Do not recycle a host PCM lease
merely because SubmitSourceBuffer accepted it.

**Candidate native PCM boundary:** retain the real CPU update/mix and exact
completed1800-byte output, convert guest BE float representation into owned
host float samples without dropping/reordering values, submit through a real
voice, and release only after genuine completion. The known six-channel,
48kHz/256-frame boundary is narrower than replacing the game's mixer with
individual XAudio2 voices. Channel-to-speaker mapping and downmix policy remain
separate required proofs; a stereo default Windows mix is not that proof.

## Parameters, stop and destruction

**82345AF8(S,selector,payload)** is a CPU command service, not just a device
setter. Selector0 queries supported mode data. Selectors1/2 append twelve-byte
records calling82345C20/82345DA0; selector3 appends an eight-byte record calling
82345DA8; other values append82345E18. Original82339458 executes these later.
Preserve deferral and returned record lengths.

- **82345C20** chooses an allowed mode, updates S+28 and channel byte82E31F9D
  from original table821C83F4; an already enabled output is disabled/re-enabled.
- **82345DA8** handles enable, calling82345E60 and resetting original timing;
  **82345E18** handles disable, calling82345ED0 and CPU statistics823538A0.
- **82345E60(S)** sets fade flag82E31F9C=1 and eventually enabled byte
  82E31F9F=1. Its mixed call **82345EB0 ->82C3FB00(S[40],f1=1.0)** requires
  native coverage. **82345ED0(S)** clears enabled and tails at82345F0C to
  the same SDK helper with f1=0.0. That helper writes SDK voice+8C and calls
  vtable+5C under the SDK lock. Preserve the CPU flags; do not let it touch a
  fake voice. A gain interpretation is suggested by its float argument and
  use, but complete SDK gain/DSP behavior is not proved in this bounded pass.

Paired teardown **82345A50(S)** has this exact ordering:

1. Clears global worker run flag82E31FA0.
2. If82E36CB8==0: **82345A88 ->82C3F1D8(S[40])**, which calls the SDK
   object's vtable+8 under the SDK critical section; zeroes S+40.
3. **82345A94 ->82C41248(S[304C])**, frees the sixteen-byte event object;
   zeroes S+304C. **82345A9C ->82C3FF00()** releases through the global
   SDK object's vtable+8 under that same lock.
4. Writes0 through Q[54], calls82345ED0(S), then frees nonzero S+24 through
   Q[14]'s allocator vtable+C, r4=allocation,r5=0, at **82345AD8**.
   It does **not** itself free the encompassing S allocation.

The shown Dac0 destructor contains no explicit thread join. This pass does not
prove the outer graph shutdown's worker-quiescence ordering. Therefore a native
owner must prove its worker and callback retirement before freeing S, mixer
arena, completion words or wake storage; clearing the run flag is insufficient.
Do not claim restart or complete output teardown merely from this static sequence.
The separate deleting base destructor82346528 writes821DCAE8 and conditionally
calls8269BEB0 according to r4 bit0; it is not a substitute for output teardown.

## Minimum implementation closure and limits

The entry guard is ready now. A future implementation needs **Dac0 start,
parameter/enable/disable services, pump submission/capacity/completion, and paired
stop/destruction** together. Host creation alone is not enough to remove it.
Retain the original CPU construction segment and helpers identified above; cover
all mixed SDK sites before their original consumers can run. The exact SDK cuts
in this owner are823458C0,8234591C,82345990,82345A3C,8234612C,823461F4,
82346230,82345EB0,82345F0C,82345A88,82345A94,82345A9C plus callbacks
823463B0/823463D0. The original CPU worker-create call823459E4 must also be
coordinated if a native pump replaces823460D0, so two workers cannot process Q.
This is a bounded owner closure, not proof that no other SDK alias exists.

XAudio2 is a viable host transport for the recovered PCM boundary, with explicit
ownership requirements. Microsoft requires submitted audio bytes to remain valid
until OnBufferEnd, and processes them in submission order.
[SubmitSourceBuffer](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-submitsourcebuffer)
Native callbacks should only record completion/signal the pump: Microsoft warns
against blocking, storage access or substantial work in callbacks. In particular,
do not run the original Q locks, graph mixer or stream I/O there.
[XAudio2 callbacks](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-callbacks)
DestroyVoice waits until callbacks/data reads cease and may not be called from a
callback; retire the source before its buffers/callback object and mastering voice.
[DestroyVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-destroyvoice)
An explicit source-to-destination matrix can express proven routing; this pass
does not invent one from the six-channel count.
[SetOutputMatrix](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-setoutputmatrix)

The parent's muted XAudio2 capability probe is documented separately in
`docs/native-audio-output-probe.md`. It does not establish original Dac0 speaker
mapping, SDK effects, timing, underflow or end-to-end mix fidelity. The read-only
reference `K:/Simpsons/RexGlueCurrent/src/kernel/xboxkrnl/xboxkrnl_audio.cpp`
substitutes emulator render-client tags and configured speaker flags; that is
not an engine-level native ownership contract and is not reused here. EXm0
priming/partial input/codec fidelity gates remain independent of this output task.

## Reproducible byte checks

Run this Python block from the workspace (`python -B -` in PowerShell's
single-quoted here-string). It reads only the derived image. Existing
`python -B tools/disassemble.py 0x823456D0 --count 224` displays the constructor;
use the other pinned entries below for paired evidence. No build or audio playback
is required. These checks verify source evidence, not runtime lifecycle success.

```python
import hashlib, struct
from pathlib import Path
b = Path('analysis/simpsons.pe').read_bytes()
assert len(b) == 0xEC0000
assert hashlib.sha256(b).hexdigest() == '6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'
def raw(pc, n): return b[pc-0x82000000:pc-0x82000000+n]
def word(pc): return struct.unpack('>I', raw(pc, 4))[0]
pins = [
 (0x823456D0,0x380,'7dfad0d82d1072dbcccf160b2c18f9ee0f28a37a2fdb469a55f89a6f47a0ceec'),
 (0x82345A50,0xA4,'40885061c7a419720a0ca289a8493fe9eed0fb00b38a25bbdb2ab177cdb9c543'),
 (0x8233DBE8,0xAC,'d60c81127cc4ca36efbcbd7ca87e84595407246cbe4b017e3ecc0e1eccae5d5a'),
 (0x823460D0,0x228,'055db028232cab0e62d8a9e5c41b9b3beabda5b3e1551d597b8c4c5753eb1bf6'),
 (0x82346320,0x8C,'39f6dfec5410a987a9b09f2b0cdd49efdc3492ee7f1f5353e52a6afa979ca674'),
 (0x823544D0,0x1A8,'003add46a8e145930f5eb288a39e326d9a814a1b2dc3bc8a2fc7d4ade94d98d2'),
 (0x82339458,0x230,'4beda65695fa496312d2b39a012bae0552b751418e9faa8ba039a8fbb9f7d2ad'),
 (0x82354120,0x29C,'877346f654918e20e127595e6fbbc4bacb985f94608a6ed45222a66f6fa9181f'),
 (0x82675890,0x108,'d5e43a07de8ab54b977068f24efe53eb85b06a460f721f8a6d00e49df2fac936'),
 (0x82C3FE28,0xD4,'e0b38be9605a2b217c5608726e75d45d46292173004019bc593750c3daf3f20a'),
 (0x82C3EE88,0x4C,'cc173549deb951744c6ba51a61593b6c053eb8890281815c6e1e98590d097a0c'),
 (0x82C4A290,0x18,'1b91258cb0871fbbdcd2e683567bd0275bab6485d9eef2020c535305e10b208d'),
]
for pc,n,h in pins: assert hashlib.sha256(raw(pc,n)).hexdigest()==h, hex(pc)
words = {
 0x8233DC38:0x81650008, 0x8233DC40:0x4E800421,
 0x82D069B8:0x82345618, 0x82D069BC:0x823456D0,
 0x82D069C4:0x82353B40, 0x82D069C8:0x82D069B0,
 0x82D069DC:0x44616330, 0x82345618:0x38603108,
 0x821DCB90:0x82345A50, 0x821DCB94:0x82345AF8,
 0x821DCB98:0x82346528, 0x823458C0:0x488FABA1,
 0x8234591C:0x488FAB4D, 0x82345990:0x488FB839,
 0x823459E4:0x4BFE4F3D, 0x82345A3C:0x488F9D6D,
 0x82CD0E58:0x82675890, 0x82CD0E5C:0x82C3EE88,
 0x82C3EEA4:0x4800B3ED, 0x82C4A29C:0x7D605C2C,
 0x823461F4:0x488F92F5, 0x82346230:0x488F9409,
 0x82345EB0:0x488F9C51, 0x82345F0C:0x488F9BF4,
 0x82345A88:0x488F9751, 0x82345A94:0x488FB7B5,
 0x82345A9C:0x488FA465, 0x82345AD8:0x4E800421,
}
for pc,w in words.items(): assert word(pc)==w, hex(pc)
for pc,f in [(0x821DD28C,48000.0),(0x821DD364,256.0),
             (0x821DD0E0,3.0),(0x821DD214,3200000000.0),
             (0x821DD0D8,0.0),(0x821CA0E4,1.0),(0x821DD110,-1.0)]:
 assert struct.unpack('>f',raw(pc,4))[0]==f, hex(pc)
assert raw(0x821C8400,16)==b'RWAudioCore Dac\0'
assert 256*6*4 == 0x1800 and 0x44+2*0x1800==0x3044
assert 0x3050+0x58==0x30A8 and 0x3104+4==0x3108
print(f'PASS: {len(pins)} function pins, {len(words)} words, seven float literals, label and buffer extents')
```
