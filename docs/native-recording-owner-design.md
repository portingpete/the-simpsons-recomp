# Bounded native recording-owner decision

**The bounded native constructor/paired-destruction milestone is verified by
build106; original recording/build/playback consumers remain guarded.**
Creating and owning an actual D3D11 deferred context is a meaningful native
capability. It does not establish original recording, translated state,
executable command lists, playback, or draw support. None of those operations
may return successful execution in this milestone.

The original bounded evidence pass did not claim a complete whole-program
alias/call graph or change implementation files. The subsequently authorized
constructor/paired-destruction implementation and its validation status are
recorded at the end of this document.
Construction details already established are in
[native-post-present-boundary.md](native-post-present-boundary.md).

## Runtime proof and the immediate question

Boot061/build101 confirms **826F4988, LR 823B748C, O=E1A5B7C0**, before the
constructor publishes its singleton. At this guard, `BE32[82D09784]=0`, pool head
`BE32[82DFE10C]=E2D768A0`, application context `BE32[82D6D890]=00900001`, and
main-device CAF8=0. This proves the immediate constructor callsite. It is not a
captured full backchain, a completed constructor, or an observed first consumer.

**No O+68 read occurs in the original constructor suffix `826F4AD4..4ADC`.**
It returns O and restores its frame. The following main caller starts creating
other CPU subsystems. This investigation has **not** proved that every function
executed between that return and recording-begin avoids O+68. In particular,
public singleton getters expose O, so absence of another literal O+68 access
is not an alias proof.

The identified manager-level O+68 consumers are:

- **826F4D08**: recording-begin, including the null test/lazy creation and all
  recording-context transfers. Its one discovered direct caller is
  `827404E8` in `82740420`.
- **826F39E0**: recording texture setup; load at **826F3AE8** and SDK texture
  call at **826F3B5C**. Its one discovered direct caller is `82740530`, after
  begin, in `82740420`. It is not playback.
- **826F4F58**: recording-end; O+68 loads at **826F4F80/4FBC**. Its one
  discovered direct caller is `8274065C`, also in `82740420`.
- **826F3690**: independently callable leaf `lwz r3,0x68(r3); b 8245A7E8`.
  No direct caller or aligned literal pointer was found. Guard it nevertheless.

Begin publishes the recording owner through `826B78F0` at `826F4DC8`, changing
**82D6D890**. It also calls the separate CPU context setter **826FF6D8** at
`826F4EA8`, changing **82D63028**. End restores the saved O+64 context through
these setters at `826F50B8/50C0`. Creation must not publish its identity to either
alias. `826FF6D8` itself has no SDK dereference: nonzero r3 is stored at 82D63028
and returns zero; zero returns 80004005 without changing the alias.

## Creation that retains the original CPU constructor

Keep original `826F4988` and all its pool/registration calls. Replace only its
**BL at 826F4AD0**, word **4BD5DA71**, resume **826F4AD4**, retaining the original
call LR and nonvolatile ABI. At this point require exactly:

```
r3=0 r4=2 r5=0 r6=0 r7=0 r8=O+68
BE32[82D09784]=O
BE32[O+68]=0
```

Use a scoped constructor-entry preflight to associate O with this original
callsite, validate its writable 0x7C extent and reject duplicate/live/stale
owners. A mapped extent alone is not proof of original heap allocation; the
actual allocator/caller fixture must establish allocation provenance. Check
the completed original pool/vtable/registration invariants at the callsite.
Do not broadly admit arbitrary type-2 calls at `82452540`; keep that SDK entry
guarded, including the lazy retry at `826F4D8C`.

The native registry should own O's generation, a fresh unmapped identity,
the actual deferred-context COM owner and the corresponding native device
ownership. Its supported capability is **native deferred context allocated**;
original recording translation and playback remain **Unsupported**. Do not
initialize a synthetic 0x5700 SDK structure, copy SDK command words, or claim
the deferred context contains the original 101 scalar/26-stage state. No
native shader/draw readiness follows from this allocation.

Publish the identity to O+68 and return r3=0 only after actual host creation and
registry insertion succeed. Preserve all other original constructor bytes,
including the deliberately untouched O+60. The identity has no SDK layout and
must not be dereferenceable as one. Require unchanged CAF8 and context aliases
through this operation.

The original constructor has already published O, prewarmed its CPU pool and
possibly registered its embedded callback before this BL. A host allocation
failure here is therefore **not** automatically an atomic constructor rollback.
Throw explicitly and retain/report partial guest ownership for cleanup; do not
return fake success. Preparing the host context in the scoped entry preflight
can avoid host-allocation failure after guest publication, while still retaining
the original CPU body. This is an implementation option, not an original effect.

## Engine gates before creation is enabled

The earliest common identified engine service is **82740680**. It chooses
record generation/reuse/playback and has three direct callsites:
**8273B4DC, 8273B860, 8273BB5C**. Gate its entry while this capability is
unsupported. It begins by passing CAF8 to CPU setter 826FF6D8; with current
CAF8=0 that setter rejects without storing. The gate also prevents subsequent
pipeline callbacks, cache changes and unrelated drawing alternatives in this
unported service; do not let it silently select another drawing path.

Add lower entry guards to cover direct or unexpected calls:

```
82740420  complete recording-build operation
  82740A5C -> 82740420
  827404E8 -> 826F4D08   begin
  82740530 -> 826F39E0   recording texture setup
  8274065C -> 826F4F58   end

827402F0  complete cached-record playback operation
  82740AF8 -> 827402F0
  82740348 -> 8245C7A0   SDK playback

826F3690  independent recording-size query -> 8245A7E8
```

Guard **826F4D08, 826F39E0, 826F4F58** themselves as well as the outer operations.
These entries must fail before mutation, rather than first failing at a later
SDK call after CPU state or lists have changed. `827402F0` first changes other
engine state through calls at 82740318/24/38, so its SDK BL is too late as the
only rejection point.

Other identified thin engine SDK wrappers have no discovered direct callers
and only `.pdata` literal pointers (or no literal pointers for the leaf). They
are additional guard coverage, not evidence that the current boot uses them:

- **826F2F40**: SDK recording-resource creation at `826F2F5C -> 82459FB0`,
  writing output `[incoming r6]` and returning HRESULT.
- **826F2FA0**: recording-begin at `826F2FAC -> 8245A368`.
- **826F2FC8**: single tail branch to `8245C5F8`, recording-end.
- **826F2E40**: size query at `826F2E4C -> 8245A7E8`.
- **8273FB38**: playback at `8273FB44 -> 8245C7A0`.

Keep SDK creator/begin/end/query/playback entries explicitly unsupported as a
fallback. Engine gates are primary; an unmapped identity provides a checked
failure for an unrecognized SDK dereference, not proof that all possible
indirect consumers have been recovered.

## Record deletion is separate from manager destruction

**82737400(R)** deletes a 0x34-byte CPU record. Before its SDK release it can
already free CPU patch storage R+30, zero R+30/+20, and later splice resource
links. It releases nonzero R+28 through `82441708` at **8273743C**, then removes
the record from the manager list through `826F4BE8` and returns it to the CPU
pool through `826F39B0`. Do not use the SDK release guard as the first check.

Its discovered direct callsites are **826F4DB0** (begin's capacity eviction)
and **827374D4** (resource plugin deletion). The independent partial cleaner
**826F3560**, called at **826F50A0**, likewise frees R+30 before releasing
nonzero R+28 at **826F35A0**. Both need entry preflight: require a valid owned
CPU record and **R+28==0** for the bounded no-recording milestone. Reject any
SDK/native executable recording payload before the first CPU mutation. Empty
CPU records may use the original cleanup; do not fabricate successful deletion
of an unsupported recording.

**827374B0(resource,offset)** is an indirect RenderWare plugin destructor,
not an unreachable helper. Registration **82737500** constructs its address at
**8273751C/24** and passes it in r6 to **823CD1F8 at 82737530**, alongside
size **0x10**, constructor **82737348** and copy callback **827372D0**. The
returned offset is stored at **82D6D850**. The destructor walks four heads at
`resource+offset`, repeatedly calling 82737400 until each chain is empty.
Preflight all four bounded, acyclic, pool-owned chains and their nonzero R+28
payloads before the original walk if rejection must leave the whole callback
unchanged. A check only inside the per-record deleter can fail after earlier
records were already removed. Keep the original plugin registration and CPU
list/pool semantics.

Original record links are distinct: R+8 is a backlink to the resource-list
slot and R+0C its next record; R+0/+4 belong to the manager's separate list.
R+28 is the SDK recording resource, **not O+68**. A manager identity must never
be placed in that field.

## Paired manager lifetime and O+68 clearing

The established manager deletion route is:

```
823B9890 loads singleton; 823B98AC invokes vtable slot0 with r4=1
820B856C -> 826F4AE0
  826F4AFC -> 826F3908   original CPU destructor
  826F4B10 -> 8269BEB0   frees O when delete-flag bit0 is set
```

The embedded O+4 interface also has a deleting entry:
`820B8564 -> 826F39A8`, which subtracts four from r3 and tail-branches at
**826F39AC** to 826F4AE0. This must reach the same owner-generation checks;
do not treat O+4 as another independent host object.

**826F3908 is the verified host-release pairing point.** Retain its original
calls **826F394C -> 8274AB10** (free pool blocks/unlink global pool list) and
**826F3954 -> 82690790** (base event-handler destruction, detailed below). It sets the base
vtable and clears singleton 82D09784 at **826F396C**, then returns; the outer
deleting destructor can free O afterward. Preflight before any cleanup:

- owner/generation, singleton and original vtables still match;
- no active recording or host command lists, no executable records, no live
  borrowed pool records, and the original pool/registration chains are valid;
- O+44/+48 manager lists, O+4C/+50 counts and O+54 recording resource are empty;
  O+68 is exactly this native owner identity. Ignore the uninitialized O+60.

Retire/release the actual host deferred context after successful original CPU
cleanup and before the deleting wrapper frees O. A paired entry/after-cleanup
scope is suitable: **826F396C (word 916A9784)** is after both helper calls and
before the original singleton clear. Preserve the original register state and
store. Clear the native identity field as part of the native ownership policy
while O is still mapped, and invalidate the host generation; explicitly identify
this O+68 clear as a **native adaptation**, because the original destructor does
not clear or release O+68. A second destruction must reject. Do not release the
backend device first while this deferred-context owner is alive.

If CPU cleanup throws after freeing some blocks, do not claim rollback or a
completed destructor. Terminal runtime ownership must still release remaining
host COM ownership once; this is separate from successful guest cleanup.

Other established O+68/lifecycle writes:

- **826F3628** zeros O+54, **O+68 at 826F3630**, O+58/+5C/+6C/+70/+74/+78,
  O+50/+4C and returns. It performs **no release**. No direct caller or aligned
  pointer was found. Gate it while a host owner is live until reset semantics
  are ported; otherwise it can orphan the native identity.
- **826F37B8** publishes a base owner singleton; **826F37D0** clears it;
  **826F3808** clears it and optionally frees the object. No direct calls were
  found, but 826F3808 is present in the base vtable at **820B8560**. Reject these
  bypasses on a live registered native owner unless entered through a verified
  destruction scope. They do not release O+68 either.
- Constructor output and lazy-recreation output writes were established in the
  prior report. **No other original release of O+68 was established here.**
  This is a bounded finding, not proof that no computed alias exists elsewhere.

## Minimum milestone validation

Before removing the constructor guard, test the actual original constructor
with the scoped BL replacement: real deferred context exists, original pool
prewarming/registration occurs, O+60 sentinel survives, only O+68 contains the
native identity, and both context aliases/CAF8 remain unchanged. Exercise host
creation failure without claiming rollback of earlier CPU effects.

Invoke every identified guarded entry directly and verify rejection before CPU
cache/list/alias changes or host command submission. Exercise the plugin deletion
callback with empty records and with a later nonzero payload to check whole-walk
preflight. Then execute original manager deletion through both O and O+4 routes:
CPU pool cleanup and the base deletion-notification/two-deletion-service effects
must occur once, while the custom pre-render subscription is preserved in this
bounded profile. Actual COM ownership must be released once. Stale/duplicate identities, foreign thread/runtime, attempted
reset and backend stop with a live owner must fail explicitly. These are required
implementation checks, not tests already run by this read-only task.

## Guard/effect pins and verification

All addresses use the original image and hash from the prior report. Function
entry instructions below are 7D8802A6 (`mflr r12`); complete function SHA256 and
`.pdata` sizes distinguish the actual boundaries:

```python
PINS = {
  0x826F3560: (0x6C, '28a38d509c3f0646754d5080ca36a58a745f0c540b3b4d26ca5b042a69848956'),
  0x826F3808: (0x54, 'd3dc37e72b9872b634b72de14cbbff9b468da83459d400eeab30fab6e2505a64'),
  0x826F3908: (0x80, '2033d71dc48fd25fbf72cafae6aaca4eda34f11d45d2355a78a86c3bd19f820b'),
  0x826F39E0: (0x194, '30ab7e89c4113d0d1474d66cf5b4c9f45a84651d3cdbc5dc3caf417499f8af4a'),
  0x826F4988: (0x158, '6fbd8758ac7fe5267468c6cffb3c3a705b0bcf55ef6331bc27deeb5e5c1ae548'),
  0x826F4AE0: (0x50, '9d8659609bdbcc39db9700cce38e70c6c2cb0c678f7efc5b43da4fcf0c4f4db4'),
  0x826F4D08: (0x24C, '75adba7847464eb7391a0f2b3213e10921685fe9ce5fecbd3161380787ada67e'),
  0x826F4F58: (0x194, '0aa336386b592f02a9ea54b33b396de45181b0fd7e8d8a54694761294cec1dfb'),
  0x82737400: (0xAC, 'ad7cbbf910da2b884b338a697fabaee3df9b6b9f3e0943606b7c474370f8677a'),
  0x827374B0: (0x50, '00dfdd1c5f58f2a4611ab7d38254812eee79b6af8d5f5cb11eef51241346b371'),
  0x82737500: (0x50, 'fe67c3b481ce19296e445bb39b38c627ea5cfadf5a523b7eaa2c75309c8446ab'),
  0x827402F0: (0x78, '5b14c218e8b1e65218537296c4c13abea3ca17846b833e08f0792d701697083e'),
  0x82740420: (0x260, '904be6cc4701d6df6fa8785590dec7e523c638fb1b8b8fdce90a4d67f1f1f7ad'),
  0x82740680: (0x56C, 'be51c268553e3962c9c7f32fc53a358053de4b9c77cd9d28aed5b27f4b7463b4'),
  0x8273FB38: (0x24, '55ea448545dd2d1d5031f97f4ef846b631858725a735eca2f6b882475ae841d5'),
  0x826F2E40: (0x24, '053d602b9542aadfdbc3166ac94821c6982808ca3b7f715543fd8e66b0dca788'),
  0x826F2F40: (0x60, '781631a8f681e1fb3b83f73513ade24fc41a3f7cf1e41dbe3efbca5c04680e8e'),
  0x826F2FA0: (0x24, 'b6b57a2ebb686593020a82a84960ba6b44bc7de24c1496175e115a1ff6872782'),
  0x82690790: (0x8C, '22de7c58af2f090fe32fe290f0aa619cfcf3dbc94e868c203151852261c57391'),
  0x82690AE8: (0xCC, '16741009554bb500448711919f7509456f965df1c7a7ad9ad9ba5c661a9a0bfe'),
  0x826B7CF0: (0x90, '1afba704cfe7802f0e306fbe427bd7212b5011d6d8aab292be090412fa4ede09'),
  0x826B7D80: (0x68, '3a1cee7c6263d3804bd8d1c668ea035f04fae70abeadf3280c4feeffb1e5eaa7'),
}
WORDS = {
  0x826F4AD0: 0x4BD5DA71, # replace this BL only; resume 826F4AD4
  0x826F396C: 0x916A9784, # after original CPU destructor helpers
  0x826F3690: 0x80630068, 0x826F3694: 0x4BD67154,
  0x826F3628: 0x39600000, 0x826F3630: 0x91630068,
  0x826F37B8: 0x3D60820C, 0x826F37D0: 0x3D60820C,
  0x826F39A8: 0x3863FFFC, 0x826F39AC: 0x48001134,
  0x826F2FC8: 0x4BD69630,
  0x8273751C: 0x3D408273, 0x82737524: 0x38CA74B0,
  0x82737530: 0x4BC95CC9,
  0x8268F4A4: 0x7F071840, 0x8268F4C0: 0xB1440004,
  0x8268F4D4: 0xB14B000C, 0x8268F530: 0x4807D648,
  0x82867C6C: 0x4BE50115, 0x82867CE4: 0x4BE28E05,
  0x82690B84: 0x83FE7274, 0x82690BA8: 0x4800B369,
}
```

Reproduce from the project root without creating any files (the two literal
dictionaries above are parsed, not arbitrary document code executed):

```powershell
@'
from pathlib import Path
import ast, re, sys
sys.path.insert(0, 'tools')
from analyze_poststart_integration import validate_identity, layout, span, sha, word
doc = Path('docs/native-recording-owner-design.md').read_text()
pins, words = [ast.literal_eval(re.search(r'\b'+name+r' = (\{.*?\n\})', doc, re.S).group(1))
               for name in ('PINS', 'WORDS')]
b = Path('analysis/simpsons.pe').read_bytes()
validate_identity(b)
_, pdata = layout(b)
for a, (n, digest) in pins.items():
    assert pdata[a][0] == n and sha(span(b, a, n)) == digest, hex(a)
    assert word(b, a) == 0x7D8802A6, hex(a)
for a, expected in words.items():
    assert word(b, a) == expected, hex(a)
print('PASS:', len(pins), 'function/.pdata pins and', len(words), 'guard/effect words')
'@ | python -B -
```

Validation passed against the original image: **22 complete function/.pdata
pins and 22 guard/effect words**, including the cleanup correction. See below for the
separate native-owner and lifecycle-fixture status.

The direct caller and aligned literal scans were bounded to the named services;
the callback address formed at 82737524 was separately verified in original
instructions. Indirect targets, future aliases, complete post-constructor
execution and a faithful native recording format remain unresolved. Keep these
limits visible when deciding to remove the creation guard.

## Implemented constructor milestone and frozen fixture

`runtime/engine_recording.h/.cpp` now implement `EngineRecordingOwners` using
the backend's actual D3D11 deferred-context owner. The four integration points
are `createBegin` at 826F4988, `createCommit` replacing BL 826F4AD0 (resume
826F4AD4), `destroyBegin` at 826F3908 and `destroyCommit` immediately before
the original 826F396C store. The entry hooks continue the original CPU bodies.
Creation commit sets LR to 826F4AD4, matching the replaced BL, and returns
r3=0 after successful ownership publication. Destruction commit leaves registers
unchanged so the original singleton clear executes.

The entry prepares the real host context before any constructor CPU publication.
Commit requires the proven 823B748C caller, original 0x80 frame/backchain,
exact SDK-call arguments, expected owner/subobject/pool vtables, zero record
fields, original 2,000-item prewarmed pool, CPU registry insertion/registration,
unchanged O+60, unchanged context aliases and CAF8=0. It publishes a monotonic
process-wide unmapped identity in 00600001..006FFFFF only to O+68. The native
identity is never a guest SDK pointer. `identity`, `validateOwner`, `count`,
`requireEmpty` and a weak opaque `nativeContext` diagnostic are exposed; none
provides recording, finish, playback, or draw operations.

Destruction enters only through the original deleting wrapper's LR 826F4B00.
It requires empty record/state fields, the intact prewarmed pool and a live
native owner. Both context aliases must still equal their construction-time
snapshot; publication of this or any other identity is rejected. The
original body directly calls **8274AB10** to free/unlink the CPU pool and
**82690790** for base event-handler destruction. This preserves the custom
pre-render subscription, as confirmed below. It does **not** invoke the separately guarded
base destructor/constructor entries, so these helpers need no guard exception.
At 826F396C the implementation verifies the resulting base vtables, removal of
exactly its pool, unchanged pre-render registration, frame/LR and unchanged aliases, then releases the
real host context and explicitly clears O+68. The original singleton store and
conditional outer-wrapper free still run. Freed P+30/P+34 values are never
followed after the CPU helper returns.

Prepared or partially destroyed ownership remains counted if an original CPU
helper or commit validation fails. This is fatal partial ownership, not an
atomic guest rollback: terminal destruction logs it and releases host references
without claiming original cleanup. Successful driver stop requires `requireEmpty`
before resource release. Unknown/live records remain unsupported; the parent
integration is narrower than the possible empty-record cleaner described above:
both per-record deletion services remain guarded and plugin 827374B0 may run
its original body only when all four heads are zero.

**Actual evidence:** build103 compiled the application, and `build/boot-063.log`
shows constructor ownership committed for O=E1A5B7C0, identity=00600001
(terminal phase=1). Execution continued into the original CPU subsystem called
at 823B74BC and later failed at the unimplemented `RtlRaiseException` import;
its saved caller was 8232B2FC. Both earlier front copies completed. This verifies
the actual constructor profile. That run used terminal host release; it did
not exercise the paired original manager destructor or any recording operation.

The frozen `tests/header/test_recording_owner_contract.h` exports
`recordingOwnerContracts(runtime,cpu,base)`. Include it inside the existing
driver fixture namespace after `require`/`rejects`, with `engine_recording.h`
included outside that namespace. Call it after startup while submission and
the original game allocator are live, before the application creates its own
singleton. The parent has integrated this include and call.

The fixture uses original allocator 8269BD70 and three real original
constructor/destructor lifetimes. It deliberately retains the same allocated
owner storage for two nondeleting lifetimes, exercising stale IDs at exactly
the same address; the last O+4 deleting-vtable route executes original free
8269BEB0. It checks O and O+4 vtable routes, original pool prewarm/registry
effects, base-vtable transition, untouched O+60, alias preservation, full-width
nonvolatile registers and stack/LR restoration, weak host lifetime, live-owner
stop rejection, wrong caller/context/address space/thread, unscoped commits,
and 21 malformed metadata/list/alias inputs rejected before CPU mutation.
It never dereferences freed owner/pool storage.

Validation at final freeze: parent build106 passes all 26 suites, including
29,948 driver checks and the three original recording-owner lifetimes detailed
below. The agent independently read the build/test logs and also syntax-checked
the integrated C++20 fixture; it did not run a full build. The standalone
backend's 350 deferred-context checks are separate evidence,
not a substitute for the now-passing original-CPU lifecycle checks. Host allocation
failure and failure during an original CPU helper are not injected by this
fixture. The runtime permits only one active Runtime, so wrong-base/context
checks do not claim a simultaneous two-Runtime integration test. Pool scalar
diagnostics identify offset/expected/actual values, and both commit ABI failures
report actual/expected registers and frame words. A successful constructor log
identifies the owner, native identity and prewarmed CPU pool without claiming
recording support.

## Build105 correction: pre-render subscription has a separate CPU lifetime

Build104 passed 25/26 suites but its paired-destructor fixture stopped after
29,248 checks. The earlier assumption that 82690790 removed the custom
subscription was wrong. Build105's diagnostic establishes these **actual**
post-helper values: descriptor 82D61DD0 root E2D61A80 unchanged, descriptor
count 1 unchanged, one node E2D33040 unchanged, receiver E1A52A64, refs=1,
priority=8000 and receiver vtable changed to the original base 82001660.
The separate deletion-event descriptor 82D5728C points to E2D61E20. This is
not root replacement or deferred removal of this subscription.

The original bytes explain the distinct effects:

- 826B7D10/14 registers descriptor **82D61DD0** with the original literal
  **`iMsgPreRender` at 820B7404**. Constructor 826F4A8C subscribes O+4 through
  826900B0 → 8268F780. The node carries receiver at +8, refs at +C and priority
  at +E; descriptor +4 counts subscription references.
- 82690790 writes the base receiver vtable, sends **`iMsgOnDeleteEntity`**
  through descriptor **82D5728C** at 826907E4 → 82690168, and explicitly calls
  8268F470 only for **82D57294** (`iMsgDeleteEventHandler`) and **82D57284**
  (`iMsgDeleteEntity`) at 826907F4/82690804. Names are the literal strings
  at 820B60E4/+18/+2C, installed by 82690A84..82690AB8. It does not iterate
  every event root and does not call an unsubscribe for 82D61DD0.
- 826F3988, this manager's pre-render callback, only checks that descriptor
  and zeros receiver+4C; it supplies no deferred unregister. General event
  dispatch 82690168 may sweep nodes with null receivers through 8268F918,
  but Build105's receiver remains nonzero with refs=1. That sweep cannot be
  claimed as this node's cleanup.
- Static teardown instructions contain **82867C6C → 826B7D80**, whose
  826B7D94/98 releases descriptor 82D61DD0 through **82690648**. Descriptor
  release is distinct from subscriber removal: 826906E4..826906F8 frees an
  event root only when its root reference count and subscriber head are both
  zero. Later static call **82867CE4 → 82690AE8** leads to teardown of the global event table,
  event-root pool (82D57278), and subscriber-node pool (**82D57274**).
  In the node-pool tail 82690B84..82690BA8 it saves the backing allocation,
  resets/frees the pool owner, clears its global and calls original 8269BF10
  on the backing allocation. These are identified global teardown instructions,
  not observed normal application teardown or proof that the retained pre-render
  node is safely retired in the real shutdown sequence. No per-manager
  unsubscribe, queued automatic removal, or continued-callback safety after
  owner deletion is established by this static path.

The native fix now validates equality of the complete pre-render snapshot
before/after the original CPU destructor: root, descriptor reference/count
fields, root type/flags, ordered node addresses/receivers/refcounts/priorities,
with original forward/backlink validation. It still verifies CPU pool unlink
and releases the real host context at the same original boundary. It neither
hand-unlinks the event node nor claims the native registry owns that CPU node.
The native context is empty and original recording consumers remain guarded.
The preservation contract is limited to the observed empty-manager callback
profile. Additional deletion-notification listeners or event mutations outside
that profile are not authorized; the complete snapshot comparison rejects a
changed result. Normal global pre-render teardown remains runtime-unverified.
This result does **not** authorize deleting a live manager and continuing
pre-render dispatch, or a general application restart without event cleanup.

The repeated-lifetime fixture must keep the event system live. It now first
asserts that descriptor/root/node bytes survived the actual destructor exactly,
then separately invokes the original **8268F470(O+4,82D61DD0)** as explicit
fixture cleanup before its next construction. It checks no active/deferred
dispatch flags, restores the prior descriptor subscription count and node list,
and performs no manual guest link writes. Original 8268F4A4 compares r3 against
node+8; it does not dereference r3, so the final deleting-wrapper test may pass
the former receiver address after O was freed. Original 8268F4C0/+4D4 decrement
descriptor/node refs; the zero-ref, non-dispatch path repairs links and tails
to 8270CB78 at 8268F530, returning the node to its real CPU pool. The fixture
does not mistake this extra test cleanup for the original manager destructor.

Build105 established the mismatch; build106 verifies the correction below.
The agent's earlier targeted build attempt stopped at the AOT input-verification
gate without regenerating or bypassing it. Parent regeneration supplied the
subsequent runtime validation.

## Final verification and freeze: build106 / boot066

`build/hundred-sixth-build.log` records **26/26 suites passed in 39.04 seconds**.
`build/native/Testing/Temporary/LastTest.log` records **29,948 driver checks**
and all three original recording-owner lifetimes passing:

- Owner **E1A52A60** and pool backing **E2D7A3B0** are reused across identities
  **00600001, 00600002, 00600003**. Old identities reject at the same address.
- The actual O and O+4 vtable destructor routes complete, with original pool
  cleanup, native host release, preserved pre-render registration and the final
  original owner free. The explicit original unsubscribe between live fixture
  lifetimes restores the event list/count; it is not a production effect.
- The ABI, sentinel, malformed-input, thread/context, stop-with-live-owner and
  ownership checks in the integrated fixture all pass. This is a bounded
  native-resource lifecycle result, not successful recording or gameplay.

`build/boot-066.log` independently shows the actual application creating native
owner **E1A5B7C0**, identity **00600001**, CPU pool **E1A5B7CC**, available=2000.
It then applies/reads back thread name **VfxCullStateManagerThread** and reaches
the unrelated unimplemented **XamNotifyCreateListener**, LR **82860F14**,
mask **1**, version **2**. On that terminal path the recording owner remains
committed and releases host references; the actual application still has not
completed its original manager/global event shutdown.

The production header/source, test header and both recording-boundary documents
are frozen for checkpoint `native-recording-owner-066`. Normal global
pre-render teardown, arbitrary deletion-notification callback profiles,
continued rendering after manager deletion and general application restart
remain unverified. Recording translation, command-list finish/replay and draws
remain unsupported and guarded. No further code/test changes were made after
the build106 freeze.
