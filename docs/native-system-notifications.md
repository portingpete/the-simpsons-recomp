# Bounded original system-notification contract

**Implement a real listener/queue/handle lifetime at the platform imports while
retaining the original UI-owner CPU code. No verified startup event sequence has
been recovered.** An actually empty queue can return false for a poll; this does
not establish that notifications stay empty forever, that a user is signed in,
or that system UI/profile services have been implemented. UI and sign-in events
have substantial game effects and must follow implemented native state.

Only this document is owned/changed in this task. Original bytes in
`analysis/simpsons.pe` (base 82000000, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`)
are the instruction authority. Generated AOT/import mappings and read-only
references corroborate names, not otherwise unproved console behavior.

## Reached constructor and native handle ownership

Boot066 reaches **82860F10**, LR **82860F14**, in constructor **82860EA0**,
owner **E1A5BEA8**, mask **1**, version **2**. Caller 823B7540/44 allocates
**0x44 bytes** with original 8269BD70; 823B7550 calls the constructor and
returns to 823B7554. This is an actual reached call, not an inferred caller.

The constructor retains these original writes before the import:

- O+0=8215F7A8 (vtable), O+4=1, O+8=8215F7A4 (secondary interface vtable);
  singleton **82D08D64=O** at 82860EF0.
- O+10=0 (queued UI request count), bytes O+3C=0 (UI active/busy), O+3D=0
  (pending profile-change reaction); global **82E07198=0**.
- **O+14..3B** are five eight-byte UI request records, interpreted only through
  the queue count. Their unused contents and **O+40** are not initialized here.

**82432CC0** is exactly `li r4,2; b 82CC2704`, preserving the 64-bit mask in
r3. The returned 32-bit listener handle is stored to **O+C at 82860F28**.
There is no handle-success branch before that store or subsequent registrations.
A native creation failure must not be reported as a successfully owned handle.

Original calls register and subscribe these messages:

- 82E071B4: `iMsgShowMarketplace`, literal 8215F790; calls 82860F30/3C.
- 82E071C0: `iMsgShowAchievements`, literal 8215F778; calls 82860F54/60.
- 82E071A4: `iMsgShowSignin`, literal 8215F768; calls 82860F78/84.
- 82D573A0: `iMsgRunningTick`, literal 820B61D4; subscription 82860F94.
- 82D57390: `iMsgPausedTick`, literal 820B620C; subscription 82860FA4.

The original message handler is vtable slot +4 at **8215F7AC = 828616A8**.
It receives r3=O and r4 pointing to the original CPU message; message word +0
is compared to the descriptor's current root, not to a literal enum.

## Poll: one notification per running/paused tick

Both tick messages select **82861804..18**:

```
r3 = BE32[O+C]        listener handle
r4 = 0               no ID filter
r5 = SP+54           32-bit notification ID output
r6 = SP+58           32-bit parameter output
82861814: bl 82CC24E4 (XNotifyGetNext)
```

r3=0 exits at 82861AB0 before reading either output. A nonzero result consumes
the returned ID: 9 branches to 82861934, A to 82861834, and other IDs exit.
There is **no loop back to poll** in this function. Preserve one dequeue per
invocation and the actual queue order. The two BE output words and boolean
success are verified here; exact console empty/error output writes, nullable
output behavior and filtered polling are not established by this caller.

### ID 9: UI state is executable game behavior

82861934..4C computes `O[3C] = (parameter != 0)`. It accepts every nonzero
32-bit parameter as true; no other parameter bits are used in this branch.

- **Zero/closed:** calls 828611E8 at 82861958 and 82861340 at 82861960.
  828611E8 may report O+40 to an existing game UI object, starts the next
  queued request if O+10>0, otherwise writes O+40=4, and invokes the object
  at 82D08B14's vtable+4. Thus it can set O+3C back to 1 when another request
  starts. The closed notification must not be flattened to a final flag write.
- **Nonzero/open:** calls 82861270 at 82861AAC. On a running tick it first
  executes the input/UI object update path at 82861A6C..A4, ending in the
  object at 82D08B14's vtable+8. The analogous closed-on-paused-tick path
  at 82861974..AC ends in vtable+4. Do not invent names for those virtual calls.
- 82861270 increments global **82E07198**. Its zero-to-active path writes 1
  to four bytes at `BE32[82D09658]+74+i*38`, i=0..3, calls an existing object
  at 82D08D74's vtable+40 with r4=1, and invokes original pause-side helpers.
  82861340 reverses the corresponding byte/object effects and drains the
  nonzero counter through the original resume helpers. Their full downstream
  input/audio/video contracts are not recovered here; retain original AOT.

A fabricated opening notification can pause the game indefinitely without a
matching real close. Repeated opening notifications also change the counter.
Even a fabricated initial close can invoke original virtual services; it is
not a harmless initialization value.

### ID A: re-query profiles, not a selected-user parameter

This branch **does not read SP+58**. It obtains the separate profile owner
**P=BE32[82D08D68]**, calls **827B2E68** at 82861880, and ORs its byte result
into O+3D. If still false it calls **827B2868** at 828618A4 and ORs that result.

- 827B2E68 uses P+10 as user index; only indices <4 reach
  **827B2EA8 → 82431F08 → 82CC25A4 (XamUserGetXUID)**, with flags 7 and a
  writable eight-byte output. It compares that XUID to **P+48**, requires a
  changed nonzero XUID and additional original checks before updating profile
  data through 827B2788/827B2CE0. An event alone does not supply an account.
- 827B2868 only queries a selected index <4 with cached XUID!=0. It calls
  **82B75DB0 at 827B28BC**, the original sign-in-info adapter, with flags4.
  Its special **0x525** result clears P+34..40/P+48, resets P+10/P+20 to4,
  clears P+14 and marks P+C. Other query outcomes return false in this helper.
- A pending O+3D reaction waits while O+3C is nonzero. If UI is closed it
  selects existing game-state behavior using 8239AFF8/8239B100,
  byte 82D090F9 and original 827B2FC0. That helper emits the literal message
  **`iMsgExitGameDueToSignInChange`** through descriptor **82E01B34**, then
  runs conditional original input/message operations. Branch 828615B0 builds
  the original **`$x360_signout`** dialog. The precise higher-level meanings of
  8239B100's predicate and byte 82D090F9 are not established here. It is not safe to
  assert that ID A is ignored during startup or only changes a UI label.

## Initial profile and queued UI requirements

Immediately after this UI constructor, **823B7554..64** allocates 0x50 bytes
and calls **827B2BB0**, which publishes P at 82D08D68. It explicitly writes:
P+10=4, byte P+14=0, P+20=4, P+48 (64-bit cached XUID)=0, P+34..40=0.
827B25C0 returns 4 while selection flag P+14 is false. These are original
**no-selected-user** defaults, not proof that all platform accounts are signed
out. They provide no authority to manufacture player0, an XUID or a sign-in
transition. The constructor also creates other CPU/profile objects, whose
unimplemented platform calls remain separate boundaries.

The three UI messages enqueue at most five records `(kind,payload)` at O+14:
achievements=0, marketplace=1, sign-in=2. First queued work starts immediately
only when O+3C is false. **82861098** sets that byte to1 before processing its
first record, shifts the remaining four records and decrements O+10. Its
verified platform requests include:

- Kind0: selected-user query; **82861100 → 82432E20 → 82CC27B4**,
  XamShowAchievementsUI (r4 forced0).
- Kind1: **82861154/68 → 82432E30 → 82CC27D4**, XamShowMarketplaceUI,
  including original 64-bit offer/title construction. No fabricated offer.
- Kind2: **82861180 → 82432DA0 → 82CC2724**, XamShowSigninUI(r3=1,r4=0).
- Kind3 is supported by the local original switch and other queue entry
  82861480: gamer-card UI through 82432E18/82CC27A4. Its full caller scope
  was not investigated; leave unsupported rather than extrapolate it.

These requests need real platform behavior or explicit failure at their own
entry. Returning fake UI success while never producing a corresponding close
can strand O+3C and later requests. The earliest common UI-launch boundary
**82861098** precedes that busy-byte/queue mutation. The earlier whole-owner
guard **82860EA0** precedes singleton publication if listener creation itself
cannot be supported. A guard at **828616A8** precedes dequeue/reaction for
unported message handling, but blocks all this owner's CPU message behavior.

## Close and CPU cleanup: retain the exact original body

O's first virtual slot is **82861430**, which calls **82860FD0** at 8286144C,
then original 8269BEB0 at 82861460 only when delete-flag bit0 is set. The O+8
interface uses **82860FC8** to subtract8 and tail-branch to the same deleting
wrapper. Original application cleanup dispatches O from singleton 82D08D64
at 823B9864..80.

The full destructor removes running/paused tick subscriptions, unsubscribes
marketplace and achievements, and releases those two message descriptors.
**82861054/58 loads O+C and calls 82432CC8.** That wrapper loads
`BE32[BE32[82CD2760]+4]`; original data pins **82CD2760=82CD2734** and
**82CD2738=82CC28C4 (NtClose)**. It converts nonnegative status to1; negative
status uses 82433B98 then returns0. The destructor ignores that boolean,
clears singleton 82D08D64, sets its base metadata and calls 82690790.

It does **not** zero O+C after close, nor unsubscribe/release the sign-in
descriptor **82E071A4** in this body. Do not repeat the earlier recording-owner
mistake of calling base 82690790 a blanket unregister operation. Full CPU
message shutdown/restart is unverified; native handle closure can be supported
without claiming that every CPU subscription has disappeared.

## Native service slice and evidence limits

**Assessment of the parent's initially-empty native queue:** supported as an
honest bounded implementation when no native platform event has occurred.
This constructor makes no initial UI/sign-in query and does not poll; a later
false poll is explicitly handled without consuming outputs. The next original
profile constructor initializes its own no-selected-user state. No mandatory
initial ID9/A is demonstrated in this path. Do not dispatch guest notification
handlers synchronously inside listener creation: the UI constructor has not
stored its handle or completed subscriptions yet, and the profile owner is
constructed afterward. Queue creation does not authorize fabricated accounts,
claim reproduction of console startup scheduling, or make a permanently empty
notification service complete. Future real platform transitions must enqueue
their corresponding notifications; unimplemented producers/services still fail
explicitly at their own reached entries.

For this reached slice: own a listener object, mask64/version32, a synchronized
queue, a native manual-reset waitable event and normal checked handle references.
Poll with filter0 removes one queued pair only after validating the handle and
writable BE outputs. Close retires the actual handle reference, unregisters
delivery at the appropriate final object lifetime, and preserves valid duplicate
handle/object references according to the existing kernel owner rules. Creation,
poll and close must not require a mapped guest listener structure.

FIFO, version/category filtering, event signaling while nonempty, and first-ID
matching are corroborated by [Xenia's listener](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xnotifylistener.cc).
Its [key layout](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xnotifylistener.h)
uses ID bits0..15, version16..24 and category25..30. Thus IDs9/A select category0
and version0, admitted by mask1/version2. The
[entry-point reference](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xam/xam_notify.cc)
also zeroes outputs on an empty/invalid poll and supports nullable parameter
output. These are reference behaviors, not facts demonstrated by this game's
current four nonnull arguments. The current owner never waits on its listener
in the recovered functions; global wait/duplicate/filter consumers were not
scanned. A generic service must qualify those separately.

The read-only ReXGlue derivative in K:/Simpsons/RexGlueCurrent corroborates the
queue mechanics but uses a different startup injection policy than the Xenia
sequence described by the parent. Neither policy is evidence of this title's
initial OS notifications. No fixed startup sequence, required initial9/A,
profile availability, live-service state, or notification order on real hardware
is established. Bind producers to actual native UI/profile/input state as each
service is implemented; preserve explicit failures for unsupported services.
No events, runtime code, tests, generated files or reference files were changed.

## Reproducible instruction evidence

Run the following read-only check from K:/SimpsonsNativeCopy using `python -B -`
(pass it via a PowerShell here-string). It checks the original image identity,
complete function/.pdata sizes/hashes, leaf/data words and semantic branch pins.
It does not execute the game or assert that platform behaviors have been tested.

```python
from pathlib import Path
import sys
sys.path.insert(0, 'tools')
from analyze_poststart_integration import validate_identity, layout, span, sha, word
b = Path('analysis/simpsons.pe').read_bytes()
validate_identity(b)
_, pdata = layout(b)
PINS = {
  0x82860EA0: (0x124, '90168e3e6cb619a0db8965081e1defe4a3a5c0b0744fa98891afabd2ab1adecc'),
  0x82860FD0: (0xC4, '00dcd7ae2e4f8c1122b0c615069c96e8b0a1fc5202c8530dbc6e8ded84f5099e'),
  0x82861430: (0x50, '87ac18f565dd1bbc8c53a114abe94a051441fb6a13d4c9e8715c37938a2cfc57'),
  0x828616A8: (0x410, '47d3abbb48e5f07924d2d3ec4093e84e282527e7ad4c09a4ce41ba6c5622edb4'),
  0x82861098: (0x14C, '28658b94f151a20d978e5211e0bf847439b0d6eafb217974d63d2d7406aee03b'),
  0x828611E8: (0x84, '3935bf56f6ab76ab53ae17441abeceffec790f5afe8a666ac0c3af54ea268a3d'),
  0x82861270: (0xD0, 'd78fe7f0dcd49772821339912fe688d7d7ed4f1adfe538ad16ece920eb6f81d5'),
  0x82861340: (0xEC, '5a883014f05f47aa9d11bdab022867f74f5d70ab1de385289cf774aa0f29f3e1'),
  0x827B2BB0: (0x124, 'bf5f7cef1bcaabdfd65296d99a4d787a2a0c4bfb56e356e358940ceb95143fa7'),
  0x827B2E68: (0xB8, 'e901a3c152d7f4eb5987bfd36590c2c1983ccb9d6251bfde8b73ce41faafba06'),
  0x827B2868: (0xB0, 'cd087f52b150f68d8aa0e57b3e935b1397db3ac1f9bb5a595f647e7c7b85dd48'),
  0x82432CC8: (0x48, '6830a7057434b81725fb0bfcf7ec8916b6dafa80089423707ab4389316645b61'),
}
WORDS = {
  0x82432CC0: 0x38800002, 0x82432CC4: 0x4888FA40,
  0x82860F10: 0x4BBD1DB1, 0x82860F28: 0x915F000C,
  0x82861814: 0x48460CD1, 0x8286181C: 0x419A0294,
  0x82861824: 0x2B0B0009, 0x8286182C: 0x2B0B000A,
  0x8286194C: 0x997F003C, 0x82861898: 0x997F003D,
  0x82861054: 0x807F000C, 0x82861058: 0x4BBD1C71,
  0x82CD2760: 0x82CD2734, 0x82CD2738: 0x82CC28C4,
  0x8215F7A8: 0x82861430, 0x8215F7AC: 0x828616A8,
  0x82860FC8: 0x3863FFF8, 0x82860FCC: 0x48000464,
  0x823B7540: 0x38600044, 0x823B7550: 0x484A9951,
  0x823B7554: 0x38600050, 0x823B7564: 0x483FB64D,
  0x827B2C2C: 0x90BF0010, 0x827B2C30: 0x997F0014,
  0x827B2C40: 0xF97F0048,
}
STRINGS = {
  0x8215F768: 'iMsgShowSignin', 0x8215F778: 'iMsgShowAchievements',
  0x8215F790: 'iMsgShowMarketplace', 0x820B61D4: 'iMsgRunningTick',
  0x820B620C: 'iMsgPausedTick', 0x8215A348: 'iMsgExitGameDueToSignInChange',
  0x8215F7C0: '$x360_signout',
}
for a, (n, digest) in PINS.items():
    assert pdata[a][0] == n and sha(span(b, a, n)) == digest, hex(a)
for a, expected in WORDS.items():
    assert word(b, a) == expected, hex(a)
for a, text in STRINGS.items():
    assert span(b, a, len(text)+1) == text.encode('ascii')+b'\0', hex(a)
print('PASS:', len(PINS), 'function/pdata pins,', len(WORDS), 'words,', len(STRINGS), 'strings')
```

Validation completed: **12 complete function/.pdata pins, 25 instruction/data
words and 7 original strings passed** against the pinned image. A bounded
branch check found only the two forward tick-message branches (828616D0/DC)
entering the poll setup at 82861804, consistent with one poll per invocation.
Import names/ordinals corroborate XamNotifyCreateListener (xam 650),
XNotifyGetNext (xam 651), XamUserGetXUID (xam 522), XamUserGetSigninInfo
(xam 551) and NtClose (xboxkrnl 207). Creation remains the actual observed
boot066 boundary; notification delivery, UI/profile transitions and this
owner's normal shutdown have not been runtime-verified by this analysis.
Evidence scope frozen: no runtime/code/test/configuration changes.
