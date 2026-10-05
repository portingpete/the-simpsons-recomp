# Original audio notification subscription: bounded native extension

The native import now accepts **exact mask `0x20`, maximum version `2`**, in
addition to the previously qualified exact system mask `1/version2`. It creates
the existing real waitable listener/queue, initially empty. Registration does
not publish an event, initialize a media controller, report playback success,
or change the game's audio state. Combined masks and every other import-policy
mask/version remain rejected. The generic broker is unchanged.

Build134 passed all **37 CTest suites in 52.48 seconds**, including the extended
`NativeSystemNotifications` tests. Boot077 created native category-5 listener
**0x158**, continued through two further worker creations, and reached the
separate `RtlInitAnsiString` boundary at LR **82B750C0**. This demonstrates
registration and continued startup; it does not demonstrate execution of the
audio notification consumer or an external-music transition.

## Original authority and exact constants

Original `analysis/simpsons.pe`, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
is the numerical/instruction authority. `build/notification-audio/verify.py`
checks its identity, original BE `.pdata` extents, each disassembled word,
explicit instruction/data pins, and five original import thunks. It rejects
modified/truncated images and malformed/incomplete/mismatched decoder output.
Originals and references are read-only.

- **8280A174** is `38600020`, `li r3,32`. **8280A178** calls **82432CC0**,
  whose exact bytes are `388000024888fa40`: set r4=2 and tail-call
  **82CC2704**, original XAM ordinal `28A`, `XamNotifyCreateListener`.
  The complete 64-bit mask is r3; r4 is the 32-bit maximum version.
- **8280A770/77C** construct `r4=0x0A000003`. **8280A780** calls
  **82CC24E4**, XAM ordinal `28B`, `XNotifyGetNext`.
- `0x0A000003` decodes to **category 5, version 0, local ID 3** under the
  existing notification identifier encoding. Its category selects
  `uint64_t(1)<<5 = 0x20`. Version2 listener admission does not rewrite the
  requested filter to a version2 event ID.

Research-source terminology calls this category XMP/background music and ID3
playback-controller-changed. Xenia's own source declares the matching
`kMsgPlaybackControllerChanged=0x0A000003`; its ID1/ID2 labels are state-changed
and playback-behavior-changed. These are corroborating research labels, **not
Microsoft XDK constant documentation**. The bounded official-source search did
not locate a Microsoft XDK enum/header proving these symbolic names or the
complete controller-value enumeration. Production acceptance depends on the
original numeric mask/filter and existing transport, not a guessed enum or
copied emulator initial-state policy.
[Xenia's declarations](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xam/apps/xmp_app.h).

## Owner initialization and creation

Original constructor **82809680**, `.pdata` size `BC`, publishes owner O at
**82E06E30** and installs vtable **8215C770**. It establishes these relevant
fields before listener initialization:

- **828096A0** sets r30=0. **828096DC/E0/E8/EC** store byte zero to
  **O+3C, O+3D, O+4C, O+4D**, respectively. In particular, O+4C is
  explicitly initialized by original CPU code; the native listener does not
  manufacture a default payload to initialize it.
- **828096D8** sets r11=-1; **82809700** writes **O+48=FFFFFFFF**.
- **82809708** writes O+54=0. O+54 bounds the preceding category-update loop
  in the later tick, independently of the listener.

Initialization **8280A0C8**, `.pdata` size `CC`, takes r3=O, r4=the existing
engine object. With r4 nonzero and O+3C zero, it stores O+4, calls the supplied
object's vtable+8, initializes original CPU fields/helpers, then calls the
object at **82E06E1C** through vtable+9C. A nonzero low-byte result sets O+3C=1.
Those retained original calls precede listener creation; this task assigns no
new meaning or success result to that virtual query.

The listener is created on either result of that query at **8280A178**, returned
through r3, then stored at **O+48 at 8280A180**. There is no handle-validation
branch before the store. The containing method returns its own r30 status,
not the listener handle as a method status. Native allocation/event failures
must throw, rather than produce an apparently owned handle.

Boot076 proves the actual import arguments **mask20/version2/LR8280A17C**.
No full caller backchain or concrete owner address was captured in that log;
the owner-relative field contracts above are static original-byte evidence.

## Consumer: exact filter and 32-bit predicate

**8280A640**, `.pdata` size `224`, receives r3=O and f1=its update amount.
O+3C and O+3D must both be nonzero to reach its update/poll body. The original
category-update loop runs first. The notification branch is:

```
r3 = BE32[O+48]           # FFFFFFFF skips the poll
r4 = 0x0A000003           # exact complete ID filter
r5 = SP+60               # writable 32-bit returned ID
r6 = SP+5C               # writable 32-bit parameter
8280A780: XNotifyGetNext  # return LR 8280A784
```

The stack words were already zeroed by original instructions at
8280A754/758. **r3==0 branches directly to 8280A79C**: O+4C is unchanged.
There is exactly one poll in this body and no dequeue loop.

On success, **8280A78C** loads the full unsigned parameter word, then
**8280A790/794** perform `cntlzw` and `rlwinm ...27,31,31`.
**8280A798** stores the resulting byte:

```
O[4C] = (parameter == 0) ? 1 : 0
```

This is not a frame count, time unit, volume, guest pointer or truncated
8-bit value: `0x100` and `0x80000000` are both nonzero. The consumer does not
read the returned ID after a successful exact-filter call.

At **8280A79C..7F0**, only `(O[4C]!=0 && O[4D]==0)` skips the original
calculation through **82802FA0** and retains the local float defaults at
SP+54=0 and SP+58=1. Other combinations run that original calculation, with
its original fallback on failure. The subsequent original **8280A3F8** and
**8280A198/8280A268** calls remain intact. A fabricated zero-payload event
would therefore change executable audio behavior. No event preserves the
original O+4C=0 path and its normal game calculations.

The notification branch itself makes no media-state query and requires no
startup event before continuing. This is not a claim that all transitive
virtual audio services or later game music operations have been qualified.

## Smallest truthful native availability policy

The port currently has no external/background-music override producer.
Its real listener starts empty, reports only events actually published to its
owned queue, and remains usable by a future implemented producer. An empty
queue is a current queue observation, not an assertion that all desktop media
is idle, the game owns the platform's music controller, or notifications will
remain empty forever. Windows endpoint/session volume and the native Dac0
output are separate services; their changes are not converted to category-5
events. No playback/controller event is injected for opening this listener.

The existing manual-reset event uses an explicit nonsignaled initial state;
publication and queue drainage signal/reset it. This is real Windows ownership
and wait behavior, supported by Microsoft's `CreateEventW` contract, not an
SDK-shaped token. [Microsoft event contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw).

An adjacent **optional music-control request** starts at **82808E50**. It
stores the caller's low byte at O+4D, then tail-calls **82B794B8** when nonzero
or **82B79590** when zero. Those SDK wrappers contain a version-dependent
branch and reach these independently unimplemented service requests:

- **82B79238 → 82B79270 → 82CC3344**: `XMsgInProcessCall`, application
  `FA`, message `7001B`; its stack record contains the supplied client word
  and two output pointers. One wrapper path requests this query before the
  following controller operation. No native default query output is supplied.
- **82B790E8 → 82B79144 → 82CC3874**: `XMsgStartIORequestEx`, application
  `FA`, message `7001A`, 12-byte record of its three incoming words. The
  observed static enable branch supplies `{2,0,1}`. That record is evidence
  of the request, not sufficient authority to report its completion.

These imports remain explicit failures. If reached, main can guard
**82808E50** before its O+4D publication while recovering an actual native
control policy, or leave the precise unsupported-import diagnostic. **No
adjacent music-platform implementation is required by this listener change.**
Original game audio is not replaced by, or disabled as, an unimplemented
optional external-music feature.

## Conditional original close and native lifetime

Vtable **8215C770[0]=8280CD30** calls cleanup **8280C878** before optional
original owner deallocation. Cleanup only enters its resource-release body
when **O+3C!=0**. After retained original audio/helper/object releases, it
loads O+48 at **8280C940** and calls **82432CC8 at 8280C94C** if the word is
not FFFFFFFF. The verified adapter dispatches
`BE32[BE32[82CD2760]+4]`, whose original data resolves to **82CC28C4/NtClose**.
It does not reset O+48 after close; the caller ignores its close boolean.

Thus original successful initialization has a matching conditional close
path. Since the listener is created even when the earlier virtual query
returns false, **O+3C==0 cleanup can skip that close**. This is an original
partial-initialization limitation, not permission to hand-edit O or claim
every guest shutdown path is closed. Runtime handle teardown remains the
terminal native ownership fallback. Normal full game music-owner teardown
was not executed by this task's focused tests.

## Verification and frozen files

Run `python -B build/notification-audio/verify.py`: **12 spans, 518 words,
43 explicit pins, five import thunks, seven malformed-input rejections**,
and six checks of the pinned zero-test algebra. It deterministically writes
`evidence.json` and `disassembly.txt` in that directory.

The C++ fixture adds the unchanged original **82432CC0 AOT wrapper** to the
existing `NativeNotificationTests` executable, plus the exact audio poll ABI,
full-width payload transport, real event state, system/category/version
isolation, exact-filter misses, malformed output preservation, unchanged mask
policy, and stale/close/reopen ownership. Its publications are explicitly
synthetic transport fixtures, never production events or media-state proof.
Both changed C++ files passed isolated ClangCL syntax checking; build134 then
compiled, linked and passed their integrated executable test. It does not
execute the whole original audio update/cleanup graph.

Changed production/test files are **runtime/notifications.cpp** and
**tests/test_notifications.cpp** only. This document and
**build/notification-audio/verify.py, evidence.json, disassembly.txt,
syntax.txt** are the accompanying evidence. **runtime/native_notifications.h
and .cpp are unchanged**; no config, CMake, generated, audio backend, original
or reference files were edited. Code and bounded evidence are frozen after
the successful build134/boot077 results; further music-control work is outside
this extension.
