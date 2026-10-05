# Post-present SDK recording-context boundary

Historical boot059 diagnosis: the initial guard selected was **engine
constructor `826F4988` entry**, before its
singleton/pool publication, with **SDK creator `82452540` entry** as a common
fallback. Do not let this path reach `82466800`: it has already allocated an SDK
object and constructed console command words before the observed texture call.
These are fail-fast boundaries, not authorization to return successful creation.

Build106 now retains the original CPU constructor with a scoped native
allocation replacement and paired destruction. The SDK fallback and unsupported
consumers remain guarded. Current implementation and verified limits are in
`native-recording-owner-design.md`; the static investigation follows below.

## Actual observation and its limits

`build/boot-059.log` records two completed native front copies, then:

```
824408E0, LR=82466900
r3=E2EEA300  r4=0  r5=0  r6=0000000080000000
```

The nonzero value is the **device/context**, not the texture. Original
`824668EC..FC` sets texture=0, stage=loop index, device=r31, and
`mask=(1ULL<<63)>>(stage+32)`, then calls the setter. Its first iteration exactly
matches this observation. Main reports build100's 25 suites passed and desktop
boot060 accepted both presentations; that does not change the new boundary.

The original bytes identify a **CPU-side SDK recording context**, distinct from
the engine's main native context. Numeric creation type **2** selects this path.
This document does not assign an unverified SDK enum name or claim a second
physical GPU was initialized. The path bypasses `82466F30 -> 82466E48`, which
contains the previously observed physical GPU initialization. Nothing here
authorizes creating an SDK-layout substitute for the native backend.

**Runtime update: boot061/build101 confirms the constructor entry**, with
`826F4988`, LR **823B748C**, owner **E1A5B7C0**, singleton
`BE32[82D09784]=0`, pool head `BE32[82DFE10C]=E2D768A0`, application alias
`BE32[82D6D890]=00900001`, and main `BE32[82D0CAF8]=0`. Both front copies
completed before this new guard. This verifies the immediate constructor
callsite and the stop before its publication, not a full captured backchain or
the creation arguments at `82452540` (which this run correctly never reached).
Main reports all 25 suites passed, including 29,358 driver checks. The following
remaining edges are **original static facts**; boot059 alone had not
distinguished construction from lazy recreation.

## Exact chain and creation ABI

Each arrow below is an original linked branch; brackets give its call PC:

```
[82867C08] -> 823B8000
  [823B801C] -> 823B70D0
    [823B7488] -> 826F4988
      [826F4AD0] -> 82452540
        [824525A4] -> 82467150             (creation type 2)
          [82467194] -> 82466800
            [824668FC] -> 824408E0        (observed LR 82466900)
```

At `823B7478..88`, the original game allocator `8269BD70` allocates **0x7C**
bytes. A nonnull result becomes r3 at constructor `826F4988`; its expected
incoming LR is **823B748C**. Call `826F4AD0` supplies:

```
r3=0, r4=2, r5=0, r6=0, r7=0, r8=owner+0x68
return LR=826F4AD4
```

`82452540` clears `BE32[r8]` at `82452568`, allocates/zeros **0x5700 bytes**
with alignment argument **0x80** via `82451FB0`, and retains the result in r31.
It publishes that pointer to the output only at `824525E8`, after successful
initialization; return r3 is zero on success, `8007000E` on allocation or
initialization failure. The caller constructor ignores this HRESULT and returns
the original engine owner. At the present failure, output publication has not
yet happened: `owner+0x68` is expected to remain zero on the constructor path.

The other original linked callers of `82452540` are:

- `823EE03C` in the original main engine start `823EDF20` (already replaced).
- **`826F4D8C`** in `826F4D08`: lazy recreation when `owner+0x68==0`, with the
  same six arguments, return LR **826F4D90**. This caller checks negative HRESULT.

Two one-instruction tail thunks also target it: `823ED1E0` and `826F2D20`.
The whole-text direct-branch scan finds no direct callers of those thunks; that
is not proof against computed/indirect invocation. The only direct callers of
`82466800` are `82467024` in the main-device initializer and `82467194` in the
type-2 initializer. Raw aligned pointer scans find only the `.pdata` start
records for `82452540`, `82466800`, `82467150`, `826F4988` and `826F4D08`.

For a saved-LR diagnostic, the constructor path should contain, outward from
the observed initializer, **82467198, 824525A8, 826F4AD4, 823B748C, 823B8020,
82867C0C**. Lazy recreation substitutes **826F4D90** for **826F4AD4**. These
values are expected from original BL instructions, not an already captured
runtime backchain. Log creation r4/r8 and `BE32[82D09784]` at the common guard.

## Engine CPU owner and effects to retain

The original constructor embeds and passes the literal
**`commandBufferDataPool` at `820B8570`** to `8274AA98`. This is original naming
evidence for the recording subsystem; no video subsystem was identified here.
Let O be the 0x7C owner and P=O+0x0C:

- `826F49BC` publishes **BE32[82D09784]=O**. `O+0=820B856C`,
  `O+4=820B8564`, `O+8=1` are the final constructor vtable/flag writes.
- Call `826F49F0 -> 8274AA98(P,0x34,2000,820B8570,0,4)` initializes the
  embedded **0x38-byte CPU pool**, item size/stride 0x34, allocation count 2000,
  alignment 4. It links P into global pool list **82DFE10C**, retaining the
  previous head at P+4. It does not allocate an SDK context.
- `826F4A14 -> 826A9178(P)` acquires a pool item; on an empty pool its original
  `8274ABB8` helper allocates a game-heap block and constructs the free list.
  The constructor zeroes the item's 13 words, then returns it to P's free list
  at `826F4A64..84`, reversing the acquire counts. This prewarms the CPU pool;
  it is not an unused SDK allocation that can silently disappear.
- Call `826F4A8C -> 826900B0(O+4,82D61DD0)` conditionally registers the embedded
  CPU object. A zero registry root returns immediately; otherwise tail helper
  `8268F780` updates registry counts, deduplicates or allocates/inserts a node.
  Preserve this original CPU service instead of assuming registration is absent.
- The constructor explicitly zeros O+44,48,4C,50,54,58,5C,64,68,6C,70,74,78.
  **O+60 is not initialized here**; later recording setup supplies it. Do not
  replace these writes with an invented whole-object clear. P occupies 0C..43.

For a stop-only checkpoint, `826F4988` entry avoids all of these constructor
effects, though the caller's 0x7C allocation has already occurred. The narrower
**callsite `826F4AD0`** preserves these CPU effects and stops before any SDK
creation, but leaves a published, partially initialized engine owner; failure
handling must not describe that state as transactional construction/rollback.
Guarding `823B7478` would precede even that allocation but is an interior caller
checkpoint, not the selected engine service entry.

## What already happened inside the SDK

Type 2 sets device byte +2ABC bit80 at `8245258C..98`, then calls `82456B30`
and `82467150`. The latter:

1. Calls `82466EC0` at `82467164`. It initializes device +3C, +2A80..2A8C,
   obtains an original CPU/thread value through `82432FF0`, and requests
   **0x12C0 bytes with allocator flags B5800000** through `8238E880`, storing
   the scratch pointer at device+4140. Failure returns zero. Boot059's new
   physical-protection range is consistent with this allocation path, but its
   exact identity is not inferred from the adjacent log line alone.
2. Calls `82456B30` at `82467174`, installing scratch start at device+30,
   end=start+12C0 at +34, limit=end-A0 at +38, and byte +2ABD bit20.
3. Sets +541C, +5420 and +3A40 to FFFFFFFF; calls `82465948` at `8246718C`.
   That function resets SDK descriptors/state and **writes console command
   words** through its +30 cursor at `82465A6C..82465B08`, then publishes the
   advanced cursor at `82465B0C`. Packet contents are evidence only.
4. Calls `82466800` at `82467194`. Before the observed texture setter, it
   installs/invokes all **101 scalar** registrations and stage 0's **20 sampler**
   defaults from original tables 82CD28B8/82CD2D78. The initializer intends to
   process **26 SDK stages**, not the native engine owner's bounded 16 stages.

Thus it is insufficient to add another allowed device to the null-texture hook,
replay this initializer into the main native state, or claim that this is only
an innocuous state snapshot. Even a fail-fast guard at `82466800` would permit
earlier console command construction. Entry `82452540`, before its first body
write/allocation, is the common SDK stop point for both creation paths.

## Consumers, cleanup and next replacement boundary

`826F4D08` is the engine recording-begin candidate (sole direct caller found:
`827404E8` in `82740420`). It is mixed CPU/SDK work, not a safe unmodified helper:

- Checks manager count O+50 against `82CED94C`, obtains an input value through
  `826B3F48`, lazily creates O+68, and enforces the O+4C/82CED950 size threshold.
- Publishes O+68 as the application rendering context through
  `826B78F0` at `826F4DC8`. This would replace the currently native application
  context alias **82D6D890**, even though main-device CAF8 stays zero.
- Stores incoming original context/resources at O+64 and inputs at O+58/5C/60;
  uses SDK getters on O+64; allocates a 0x3000 or 0x9000 SDK recording object
  through `82459FB0`; and starts recording through `8245A368` at `826F4ED4`.
- Transfers targets, viewport and other state into O+68 through SDK calls before
  invoking the engine record/list helper `826F4868`. Those setters and original
  resource lifetimes require an engine-level replacement, not SDK emulation.

`826F4F58` is the paired end candidate, called at `8274065C`. It queries output
sizes through `8245A7E8`, finishes recording through `8245C5F8`, records the
result and sizes in the engine record, updates O+50/O+4C and O+44/O+48 lists,
releases acquired target references through `82441708`, and restores the
original O+64 application context via `826B78F0` at `826F50C0`. Returning
unconditional success would lose these CPU results and lifetime effects.

Original manager cleanup is also CPU-visible: `823B9890..AC` obtains the
singleton and invokes vtable slot0 with delete flag1. Original table
`820B856C` selects `826F4AE0`, which calls destructor `826F3908`, then
`8269BEB0` when requested. The destructor frees pool blocks and removes the
pool from 82DFE10C via `8274AB10`, runs base event-handler destruction through
`82690790`, and clears 82D09784. **Correction from build105:** 82690790 sends
`iMsgOnDeleteEntity` through 82D5728C and unsubscribes only from the two deletion
services 82D57294/82D57284. It does **not** unregister O+4 from `iMsgPreRender`
(82D61DD0). That registration remains unchanged in the observed bounded
empty-manager callback profile; the former blanket "unregisters O+4" claim
was incorrect. **This destructor does not release O+68**. A complete separate SDK
context-release path is not established by this bounded review; do not invent
one or claim restart/lifetime closure.

The recording-owner implementation now checks preservation of the complete
pre-render registration while pairing native host release with the original
manager destructor. Repeated-lifetime tests separately call original
8268F470(O+4,82D61DD0) for **fixture-only cleanup**, outside production; no
native code hand-unlinks that node. Static descriptor/global event-pool teardown
calls have been identified, but normal application pre-render teardown and
continued-callback safety after manager deletion remain unverified. Do not
infer general restart or arbitrary callback support from this fixture. Exact
original addresses, build105 values and the correction are recorded in
[native-recording-owner-design.md](native-recording-owner-design.md#build105-correction-pre-render-subscription-has-a-separate-cpu-lifetime).

The follow-on **engine recording-owner contract** now owns a real native
deferred context, retaining the original CPU constructor/destructor and gating
unsupported recording at its identified entrypoints. Build106 passes all
26 suites (39.04 seconds), including 29,948 driver checks and three original
O/O+4 owner lifetimes. Boot066 creates the actual manager E1A5B7C0 with native
identity 00600001 and proceeds to the unrelated XamNotifyCreateListener boundary
(LR 82860F14, mask1/version2). This validates the bounded creation/native-owner
pairing, not SDK recording semantics, normal global event shutdown or gameplay.
The main native backend identity remains separate, CAF8 remains zero, and the
unsupported recording/SDK consumers remain guarded. Final evidence and limits
are recorded in the companion design document; both documents and the owner
implementation/fixture are frozen for checkpoint `native-recording-owner-066`.

## Reproducible byte checks

Only this document was added. Original data, references, runtime, renderer,
configuration and generated code were read-only. Image: base 82000000,
15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The SHA256 is enforced by the existing validation helper below.

Run from the project root. This validates original image identity, PE/.pdata,
complete function bytes and all direct initializer/creator edges; it writes
nothing. `SimpsonsDisasm` was used read-only to inspect the original words.

```powershell
@'
import sys
from pathlib import Path
sys.path.insert(0, 'tools')
from analyze_poststart_integration import validate_identity, layout, span, sha, word, branch
b = Path('analysis/simpsons.pe').read_bytes()
validate_identity(b)
sections, pdata = layout(b)
pins = {
  0x823B70D0: (0xE7C, '7f0c95fde954cf47076e58ac444bee5606de97b35e478c5cb92be4e78026b59e'),
  0x823B8000: (0x54, '1b73a1b053395c6093922b32e82156472a4bdc2d5a64bd636c3f1edbe829c0d9'),
  0x82452540: (0xB4, 'a1565254752c664aa57344d5c046e17d0b116abe0db4a803bac680c6525a1819'),
  0x82465948: (0x1D0, 'af37e49641d8e5a73077db3aa6b7352d1a9ac33b8683d084ef38388cbe07041a'),
  0x82466800: (0x268, 'b11aac256fe4eb8d493f9f4249acec09cc7e132fb4271af4ff8d00ff21cef4e2'),
  0x82466EC0: (0x6C, '51ac3dc84bf81d57674735a7093eb9c2260a87a17562f1973d629209bd8d3501'),
  0x82467150: (0x60, 'ad03ae828294fda469121bf0907c9b937997fdf90b4313d2ce7e4f16130eb244'),
  0x826F3908: (0x80, '2033d71dc48fd25fbf72cafae6aaca4eda34f11d45d2355a78a86c3bd19f820b'),
  0x826F4988: (0x158, '6fbd8758ac7fe5267468c6cffb3c3a705b0bcf55ef6331bc27deeb5e5c1ae548'),
  0x826F4D08: (0x24C, '75adba7847464eb7391a0f2b3213e10921685fe9ce5fecbd3161380787ada67e'),
  0x826F4F58: (0x194, '0aa336386b592f02a9ea54b33b396de45181b0fd7e8d8a54694761294cec1dfb'),
  0x8274AA98: (0x78, '9b2aade177ae12bf7bca852c96482f8c939d0d2f05b6d303bb3bb1ab0d8f8186'),
}
for a, (n, digest) in pins.items():
    assert pdata[a][0] == n and sha(span(b, a, n)) == digest, hex(a)
assert span(b, 0x820B8570, 22) == b'commandBufferDataPool\0'
edges = {0x82867C08:0x823B8000, 0x823B801C:0x823B70D0,
         0x823B7488:0x826F4988, 0x826F4AD0:0x82452540,
         0x824525A4:0x82467150, 0x82467194:0x82466800,
         0x824668FC:0x824408E0, 0x826F4D8C:0x82452540}
for pc, target in edges.items():
    assert branch(pc, word(b, pc)) == (target, True), hex(pc)
expected = {
  0x82466800: {0x82467024,0x82467194},
  0x82452540: {0x823ED1E0,0x823EE03C,0x826F2D20,0x826F4AD0,0x826F4D8C},
}
found = {target:set() for target in expected}
start, size = sections['.text']
for pc in range(start, start+size, 4):
    edge = branch(pc, word(b, pc))
    if edge and edge[0] in found:
        found[edge[0]].add(pc)
assert found == expected, found
print('PASS: original identity; 12 .pdata/function pins; name; 8 chain edges; full-text creator/initializer callers')
'@ | python -B -
```

Validation result: the reproduction above passed against the original image:
12 function/.pdata pins, the original name, eight call edges and the complete
direct creator/initializer caller sets. No runtime implementation was executed
or changed for this evidence task.

Search limits: direct branches and aligned literal pointers cannot recover all
indirect calls or aliases. Original instructions establish the numeric ABI and
effects; labels such as recording-begin/end summarize reviewed behavior, not
recovered symbol names. No CPU/GPU interpreter, SDK object model, command-stream
implementation or native rendering change is delivered in this scope.
