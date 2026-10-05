# Original profile startup: no selected user and the second listener

**Keep 827B2BB0 and its child constructors in original CPU code.** They establish
a real no-selected-user state, create a second notification listener and start
a worker that waits on that listener. They do not require inventing user 0,
a gamertag, XUID, sign-in event or a successful content service.

Boot067 now confirms this constructor path completes: native listeners **140**
and **144** are created, and the original thread request has startup **82439460**,
entry **827AEFB0**, argument **E1A5C0DC**, stack size 0 and flags 0. The next
reported failure is **NtCreateMutant**, LR **82B76618**, after the profile
constructor has returned. Parent reports build108 **27/27 tests passed**.
This analysis independently checked the listener/thread/failure and backchain lines in
`build/boot-067.log`; it did not run those runtime tests.

Only this document is changed. Instruction authority is the read-only flat
original `analysis/simpsons.pe`, base **82000000**, size **EC0000**, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Original import metadata corroborates API names. The checks below pin complete
functions, their `.pdata` extents, leaf instructions, callsites and data.
Addresses, allocation sizes and structure offsets below are hexadecimal.

## Constructor and original no-selection state

**823B7554/58** allocates **50 bytes** through original 8269BD70;
**823B7564 calls 827B2BB0**, with r3=P. The constructor publishes
**82D08D68=P at 827B2C18**, sets P+0=8215A378, P+4=1 and P+8=8215A374,
and establishes these original fields:

- P+C byte=1; P+10 word=4; P+14 byte=0; P+18 float=+0.0; P+20 word=4.
- P+28 doubleword=0; P+30 word=6; P+48 doubleword=0.
- P+34, +38, +3C and +40 words=0. Do not infer initialization of padding or P+44.

The getter **827B25C0** proves the selection contract: return P+10 only if
P+14 is nonzero; otherwise return **4**. P+48 is the cached XUID used by the
subsequent original selection/change logic. This is **no game user selected**,
not evidence that all four platform slots are signed out.

P+28 must not be confused with the XUID. Immediately after construction,
**823B7570..7C** passes **45410809** to **827B25A8**, which stores that value
as a doubleword at P+28. P+48 remains a separate field. The immediate is a
startup title/content value; its exact wider API interpretation is not needed
to implement notification ownership.

The constructor registers `iMsgAdditionalContentManagerProceed` at 82E01B48
and `iMsgExitGameDueToSignInChange` at 82E01B34. It allocates four children,
using 8269BD70 and the following original constructor calls, in this order:

- **827B2C78 -> 827B0590**, allocation 204 bytes, singleton 82D08B98.
- **827B2C8C -> 827B1AE8**, allocation 144 bytes, singleton 82D08D84.
- **827B2CA0 -> 827B50E8**, allocation 4C8 bytes, singleton 82D08D8C.
- **827B2CB4 -> 827B5760**, allocation 20 bytes, singleton 82D08DB4.

It then subscribes P to the original running-tick descriptor 82D573A0 at
827B2CC4. Preserve these allocations, registrations, subscriptions and stores;
the constructor is not a replaceable successful platform query.

### Initial running tick also has a no-query path

Vtable slot **8215A37C = 827B2E40** recognizes the running-tick message, and
only when P+14=0 tails to **827B2788**. At **827B27A0/A4**, P+30=6 branches
directly to **827B282C**. That path reinstates index4/invalid-selection,
clears P+34..40 and P+48, and calls **82C73400** at 827B2858.

82C73400 is a CPU-only helper: read object=BE32[82E2DA48] and set bit3
(numeric mask **00000008**) in object+C if not already set. It does not call
an import. The object must remain genuine initialized original CPU state;
do not skip this side effect or manufacture its owner.

The other branch of 827B2788 first calls **82431880 at 827B27B0** with
r3=P+30. That wrapper tails to **82CC2514, XamUserGetSigninState**. Only
a nonzero sign-in result leads to **82431F08 at 827B27C4**, which passes
the same index in r3, flags **7** in r4 and an eight-byte output pointer in
r5 to **82CC25A4, XamUserGetXUID**. Successful selection then writes the
actual returned XUID to P+48 and marks P+14=1. These are future platform
boundaries, not queries forced by the initial P+30=6 profile.

No-selection does not authorize a generic implementation of those APIs.
Unimplemented account services must still fail if reached. The nearby
827B2918 helper, for example, can query all slots 0..3 when no user is selected;
its existence prevents treating sentinel4 as proof that no future slot query
can occur. Its startup invocation is not established by this bounded pass.

## Second listener: storage/content manager and an actual waiting thread

Let S be the **204-byte** child constructed by **827B0590**. It publishes
82D08B98=S at 827B05D8 and owns an original record array S+188: allocation
**4E00**, capacity **40**, initial active count S+18C=0. The worker's later
record stride is **138**. Additional original arrays are retained at S+1E0,
S+1EC and S+1F8. It registers `iMsgAllContentScanned` at 82E01AE0 and
subscribes to 82E01B48 before creating the listener.

**827B06F8 -> 82432CC0 -> 82CC2704** creates a listener with 64-bit mask1
and version2. The wrapper is `li r4,2; b 82CC2704`. **827B0700** stores the
returned handle through r28=S+194. This is a different listener from the UI
owner's handle at its own +C; each must own its own queue/delivery state.

**827B071C -> 82433660 -> 82434EC8** receives:

```
r3=0  r4=0  r5=827AEFB0  r6=S+194  r7=0  r8=0
```

At **82434F0C -> 82CC2BA4 (ExCreateThread)** this becomes:

```
r3=wrapper SP+50   output handle
r4=0              requested stack size
r5=0              no thread-ID output pointer
r6=82439460       original CRT startup
r7=827AEFB0       original worker entry
r8=S+194          pointer to guest listener-handle field
r9=0              creation flags
```

The wrapper returns the actual handle on nonnegative status; on failure it
calls 82433B98 and returns 0. **827B0728** stores the result in **S+198**.
No original success check follows either listener or thread handle store in
this constructor. Do not turn failure into a valid ownership claim.

After starting the thread, the constructor creates three CPU scan-task records
for original literals `*.lvl`, `*.costume`, `*.lua`, with callbacks
**827B0440**, **827B0190**, **827AEB48** respectively. These records are
configured here; this is not evidence their scans have run. Scheduling can
start the worker before construction returns. Boot067's argument E1A5C0DC
corresponds arithmetically to **S=E1A5BF48**; this is derived from the proven
S+194 ABI, rather than a separate runtime owner log.

### Wait, dequeue and actual event-dependent services

The worker **827AEFB0** saves incoming r3 as the address A=S+194. Each iteration
reloads handle=BE32[A]; it does not capture an independent handle value forever.

1. **827AEFE8 -> 824337A8 -> 82434F90** receives r3=handle, r4=FFFFFFFF.
   824337A8 sets r5=0. **824394E0** translates that timeout to a null pointer.
   **82434FC4 -> 82CC2BB4 (NtWaitForSingleObjectEx)** receives r3=handle,
   r4=1, r5=0, r6=0: non-alertable wait with no finite timeout.
2. Any result other than 0 immediately retries the wait at 827AEFE0.
   Negative NTSTATUS is converted to FFFFFFFF by the wrapper. A replacement
   that repeatedly reports failure/timeout can spin; it is not cancellation.
3. After wait result0, **827AF004 -> 82CC24E4 (XNotifyGetNext)** receives
   r3=BE32[A], r4=0, r5=fixed worker frame+50 (ID), r6=frame+54 (parameter).
   False returns to wait without consuming those output words. True dispatches
   one event and then returns to wait.

An initially empty native queue must therefore be **genuinely unsignaled and
waitable**, with terminal cancellation integrated into the runtime's worker
lifetime. Do not report a synthetic successful wake, spin on failure, or inject
sign-in/UI events just to advance this thread.

The two recognized events have concrete effects:

- **ID9** at 827AF108: store byte **82E01AD8 = (parameter != 0)** at
  827AF120. The original image initializes this byte to zero. No XUID or
  selected-user field is written by this branch.
- **ID B** at 827AF014: lock 82E01AF0 via 82329720, read current S from
  82D08B98, and traverse its S+18C active records in S+188. For each device ID
  at record+4 not already successfully checked during this invocation,
  **827AF0B8 -> 82432570 -> 82CC26A4 (XamContentGetDeviceState)** receives
  r3=device ID and r4=0. Return0 adds the ID to the local deduplication list;
  nonzero invokes optional callback S+19C, preserving that result as r3.
  **827AF100 -> 823297A8** unlocks. At construction the count is zero, so
  even this event would not query a nonexistent initial device record.

Other event IDs return to wait. **823B75A4**, after the profile constructor,
sets S+19C=**8286DDF8**. That callback's wider behavior is outside this pass.
An implemented storage-event producer must qualify it and the storage query
before claiming content availability. No console storage device is fabricated.

### Bounded ownership result: normal worker shutdown remains unresolved

The virtual destructor slot **8215A088 = 827AFAE8** calls **827AF4C0**,
then optionally frees S through 8269BEB0. 827AF4C0 closes **S+184**, if nonzero,
via **827AF4F4 -> 82432CC8**. That field is distinct from **S+194 listener**
and **S+198 worker**; it must not be described as closing the listener.

The same destructor unsubscribes CPU messages, releases arrays/tasks, frees
S+188, clears singleton82D08B98, then calls original deletion notification
82690790. Its recovered body has no direct close of S+194/S+198 or worker
stop/join. The complete worker has no normal return/stop branch. A nearby
helper **827AF128** repeats listener/thread creation and stores to those same
fields; this pass does not establish its callers or a safe replacement cycle.

This is a limit of the bounded evidence, not proof that no global shutdown
path exists. Do not claim normal game teardown or restart of this manager is
implemented. At terminal runtime destruction, workers must be quiesced before
guest memory, the singleton, or their pointed-to handle field can disappear.
Do not invent a production unsubscribe, join, close sequence or hand-unlink
original ownership on this evidence.

## Remaining constructors and the actual next boundary

The **827B1AE8** child retains CPU metadata, an **80000-byte** custom allocation
arena through **826CECF0 -> 826CEB00**, and lazy children **827B5DA0** and
**827B4998**. Its allocator callbacks **827B1A18/827B1A70** use the arena at
child+68. Their registration can itself allocate; it is not just two pointer
stores. Newly initialized 82261338 objects have +30=0, making their immediate
82261600 cleanup call return without running the conditional release path.

The apparent service call **827B1CE8** resolves through original logger
singleton **82E31BB0**, constructed by **822442B0** with vtable821D7834.
Slot+4 is **82244420**, exactly `stw r5,4(r3); stw r4,C(r3); blr`.
The caller passes r4=r5=0: it clears two logging callback fields, not a profile
or system-state query. Lazy logger construction allocates a 400-byte buffer,
clears it, and initializes a critical section through
**8241FD08 -> 82CC2894 (RtlInitializeCriticalSection)**. Original literal
`PlasmaLoggingCrit` and message `iMsgPlasmaReconnect` corroborate this subsystem.

The **827B50E8** and **827B5760** constructor paths initialize original CPU
objects rather than querying accounts. The conditional release call in
827B5760 is bypassed by its freshly initialized zero length. No successful
profile/sign-in result is needed to explain boot067 getting past these objects.

The next reached failure belongs to the subsequent startup path. The recorded
boot067 backchain is **8231EA0C -> 82816314 -> 8280D214 -> 8280D680 ->
826D951C -> 823B7718**, with NtCreateMutant LR82B76618, r3 output0203F530,
r4 attributes0, r5 initial-owner0. The original instruction **823B7714** calls
826D9478 and returns to 823B7718, after the profile setup above. This task
does not analyze or implement that mutant service; the parent owns it.

**Current implementation decision:** preserve the completed original profile
construction and no-selection state; support real listener/wait/thread lifetime;
retain explicit boundaries for user, UI and storage services until actually
implemented. No initial system event sequence or profile setup query is required
by the paths proved here. This does not guarantee that later game code will
never ask for an account, storage, input or online service.

## Reproducible byte and framing checks

Run the following from K:/SimpsonsNativeCopy with `python -B -`, for example
through a PowerShell here-string. It reads the original image and existing
read-only analysis helpers. It creates no report, bytecode cache or other file.
Function hashes verify evidence identity, not execution coverage or API behavior.

```python
from pathlib import Path
import json, sys
sys.path.insert(0, 'tools')
from analyze_poststart_integration import validate_identity, layout, span, sha, word
b = Path('analysis/simpsons.pe').read_bytes()
validate_identity(b)
_, pdata = layout(b)
PINS = {
  0x827B2BB0: (0x124, 'bf5f7cef1bcaabdfd65296d99a4d787a2a0c4bfb56e356e358940ceb95143fa7'),
  0x827B0590: (0x21C, '56963bc4a562750535a4142e61b588219aa9e6eafcc18604d9cfc726ee92a010'),
  0x827AEFB0: (0x178, '64dc4faa83d8d97598f7469e6ccb5518e260021bb92b274b7ac4559d52bb609a'),
  0x827B1AE8: (0x248, 'e4f73ae5fc4dd6799b9470843ea1e55b2806934402d53da3b85558d0a3ee85f4'),
  0x827B50E8: (0xD4, 'd7080b88301874159ded29edb9d0cc9b81adc9a2fc19d2b1a1b4fc1fe8e5800e'),
  0x827B5760: (0x9C, '101311c0d9af976764fbff8506f2ac9543f27ecf416d4fc1f39e5282a94830fe'),
  0x827AF4C0: (0x110, '8eeb00aa1f3f9be60460b5516710e1c4f550ba338cf43b0b70d8dec281ef5bfb'),
  0x822442B0: (0x9C, '13cbea18a5c3b6c179ae2daeb6f254f8b593e99ea8ec4b5c84d48ea5420a7b48'),
  0x82434F90: (0x64, '908eceaf882b8afe1dbed734ac96a65c4bf9237d87786d9ef22151b02caa02a3'),
  0x82434EC8: (0x70, '734029e9533e2a473f1c5ba128b6927d6263936057448813a1c3fa7c6fd45fd2'),
  0x827B2788: (0xDC, 'e815810d8cc023b29edf3df49bdd4171d5f79ced2537f12f7e01a9fefa28ce72'),
  0x827B5DA0: (0x74, '51ef79ab34824b530f7d488b97de797410c7f2734988fec4ff83ff60e8fbda82'),
  0x827B4998: (0x78, '42757ea460895d49a5a402e02fb45355ff8477500fed2770302fe44b8cf23af6'),
  0x827AF128: (0x64, '9a5206467c4ec04f57688ca0aedb75210c9f931ffe2b7f029b4b0ba0f7d942b9'),
}
LEAVES = {
  0x827B25C0: '896300142b0b0000419a000c806300104e800020386000044e800020',
  0x827B2E40: '3d6082d581440000816b73a07f0a58404c9a0020896300142b0b00004c9a00204bfff9284e800020',
  0x82C73400: '3d6082e3816bda48814b000c554907382b0900004c9a0020614a0008914b000c4e800020',
  0x82244420: '90a300049083000c4e800020',
  0x824394E0: '2f04ffff409a000c386000004e800020788b00201d6bd8f0f96300004e800020',
  0x82433660: '7d0943783900ffff48001860',
  0x824337A8: '38a00000480017e4',
}
WORDS = {
  0x823B7564: 0x483FB64D, 0x823B757C: 0x483FB02D,
  0x827B2C78: 0x4BFFD919, 0x827B2C8C: 0x4BFFEE5D,
  0x827B2CA0: 0x48002449, 0x827B2CB4: 0x48002AAD,
  0x827B06F8: 0x4BC825C9, 0x827B0700: 0x907C0000,
  0x827B071C: 0x4BC82F45, 0x827B0728: 0x917F0198,
  0x827AEFE8: 0x4BC847C1, 0x827AF004: 0x485134E1,
  0x827AF0B8: 0x4BC834B9, 0x827AF120: 0x99741AD8,
  0x827B1CE8: 0x4E800421, 0x821D7838: 0x82244420,
  0x827AF4E0: 0x807F0184, 0x827AF4F4: 0x4BC837D5,
  0x827B27A4: 0x419A0088, 0x827B27B0: 0x4BC7F0D1,
  0x827B27C4: 0x4BC7F745, 0x827B2858: 0x484C0BA9,
  0x8215A37C: 0x827B2E40, 0x82432570: 0x48890134,
  0x82431880: 0x48890C94, 0x82431F14: 0x7C852378,
  0x82431F18: 0x38800007, 0x82431F1C: 0x48890689,
  0x821DD0D8: 0, 0x82E01AD8: 0,
}
STRINGS = {
  0x8215A324: 'iMsgAdditionalContentManagerProceed',
  0x8215A348: 'iMsgExitGameDueToSignInChange',
  0x8215A0B8: 'iMsgAllContentScanned',
  0x8215A0B0: '*.lvl', 0x8215A0A4: '*.costume', 0x8215A09C: '*.lua',
  0x821D069C: 'PlasmaLoggingCrit', 0x8215A2B8: 'iMsgPlasmaReconnect',
}
IMPORTS = {
  0x82CC2704: ('XamNotifyCreateListener', 650),
  0x82CC24E4: ('XNotifyGetNext', 651),
  0x82CC2BA4: ('ExCreateThread', 13),
  0x82CC2BB4: ('NtWaitForSingleObjectEx', 253),
  0x82CC26A4: ('XamContentGetDeviceState', 613),
  0x82CC2514: ('XamUserGetSigninState', 528),
  0x82CC25A4: ('XamUserGetXUID', 522),
  0x82CC2894: ('RtlInitializeCriticalSection', 302),
}
for a, (n, digest) in PINS.items():
    assert pdata[a][0] == n and sha(span(b, a, n)) == digest, hex(a)
for a, text in LEAVES.items():
    expected = bytes.fromhex(text)
    assert span(b, a, len(expected)) == expected, hex(a)
for a, expected in WORDS.items():
    assert word(b, a) == expected, hex(a)
for a, text in STRINGS.items():
    assert span(b, a, len(text)+1) == text.encode('ascii')+b'\0', hex(a)
imports = {x['address']: x for x in json.loads(
    Path('analysis/executable.json').read_text())['imports'] if x['kind'] == 1}
for a, (name, ordinal) in IMPORTS.items():
    assert (imports[a]['name'], imports[a]['ordinal']) == (name, ordinal), hex(a)
print('PASS:', len(PINS), 'function/pdata pins,', len(LEAVES), 'leaf ranges,',
      len(WORDS), 'words,', len(STRINGS), 'strings,', len(IMPORTS), 'import mappings')
```

Validation completed against the pinned original: **14 complete function/.pdata
pins, 7 exact leaf ranges, 30 instruction/data words, 8 strings and 8 import
mappings passed**. These are static evidence checks. Boot067 independently
demonstrates construction/thread creation and subsequent main-thread progress;
it does not prove event delivery, profile transitions or normal worker shutdown.
Document frozen with no runtime, source, test, configuration or original-file edits.
