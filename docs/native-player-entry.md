# Native player entry: original consumers and a real local-profile service

**A host-owned four-slot registry with durable app-local profiles is the smallest
coherent service.** Start with all slots empty as an explicit native application
session policy; `GetSigninState` reads that registry. Real create/load/activate/
sign-out operations must exist behind it. Activation is a later native UI action,
not automatic Xbox sign-in or a successful constant-zero query masquerading as
profile support. The original selection path accepts an offline nonzero state;
it does not require the separate state-2 predicate.

This pass changes only this document and `build/native-player-entry/*` evidence.
No production, generated, configuration, original or reference files changed.
Addresses, offsets and setting IDs below are hexadecimal; byte counts and slot
counts stated in prose are decimal. Original authority: `analysis/simpsons.pe`,
base **82000000**, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Existing [profile startup](native-profile-startup.md) and
[notification ownership](native-system-notifications.md) remain frozen.

## Reached ABI and the first nonzero branch

Boot079 independently confirms `823A1254`, wrapper **82431880**, r3=0/r4=0,
with saved LRs **823A1494 -> 823A1594 -> 823B7A04**. Main's build136 log
reports 39/39 suites passed; this evidence task did not rerun them.

**82431880 is exactly `48890C94`, an unconditional tail to 82CC2514**, original
XAM ordinal **210**, `XamUserGetSigninState`. Its caller **823A1228** sets only
r3 to each index **0..3** at **823A124C/50** and tests the returned low word
against zero at **823A1254**. There is no flags argument established by this
call. Incoming r4 is saved to r26 by the *outer CPU method* and controls a
first-game-player settings refresh. Its observed zero comes from **823A1468**.
The single-u32-index signature is also corroborated by
[Xenia's own XAM implementation](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xam/xam_user.cc)
and read-only `K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_user.cpp`.
Those implementations are ABI research, not authority for inventing this
title's accounts, startup events or online state.

A future adapter can replace that four-byte wrapper with a returning native
query, or implement its qualified import. Input is r3.u32; output is the
zero-extended state in r3. No pointer output or guest structure is required.
Do not interpret incidental r4 as flags or write original profile caches here.
Preserve all other context and host FP state under the existing hook convention.
Retain the original caller/constructor bodies.

The containing constructor **823A1548** publishes owner O at **82D08D90**, calls
**823A13B0**, then copies its original option state into its snapshot. Before
the reached query, **823A1488/8C** writes **O+A0=1, O+A4=1**. Other original
defaults include several 100-valued scalar words, a locale-dependent word and
five original option bytes. All four query results zero skip to **823A134C**:
no option-word updates, profile settings calls or player-association calls occur
in this loop. That preserves original defaults; it is not a global claim that
all future startup code tolerates every missing platform service.

For each nonzero result, the loop obtains F from **82D08B9C** and asks
**8285D7D0(F,j)** for j=0 then j=1. This leaf reads the association byte at
**F+18+18*j**, returns it only if <4, otherwise returns **6**. Thus there are
**two game-player associations mapped onto four platform slots**, not two
platform profiles. Only an associated slot enters **827B2CE0** below. Its two
validity outputs control bits **2 and 0** respectively in **O+A0+4*j**;
**823A1348** also retains the original `823A1038(O,j,0)` side effect.

## Four distinct concepts; preserve the original selection state machine

- Platform sign-in state: this title checks **0 versus nonzero**, and separately
  **exactly 2** in **827B2720** (`state-2; cntlzw; extract`). The conventional
  labels 0=not signed in, 1=local/offline, 2=Live are research corroboration;
  the original instructions independently prove the distinctions used here.
  Native active local profiles should report **1**, never pretend to be Live2.
- Game selection: **827B25C0** returns **BE32[P+10] only when P[14]!=0**,
  otherwise **4**. P is the original singleton **BE32[82D08D68]**. Sentinel4
  is not a platform sign-in state and does not prove Windows or platform users
  are signed out. P+48 is the cached identity; P+28 is a separate title/content
  value established as **45410809**, not an XUID.
- Pending selection: **827B25B8(P,candidate)** stores P+30. Initial **6**
  bypasses platform lookup in **827B2788** and reinstates the original empty
  selection/name/identity. Do not redirect this sentinel to slot0.
- An additional original candidate **7** is passed to that setter at
  **8285DDAC/B4**. Selection bypasses only6, so a future lookup may receive7.
  A registry query should treat an out-of-range index as no slot, returning0,
  consistent with the research signature's implementation. This is a stated
  native compatibility policy, not proof of every console invalid-index case.
  Never treat4/6/7 as an alias for an active native profile.

The successful selection body is concrete: **827B27B0** asks state(candidate),
then **827B27C4** asks identity if state is nonzero. On identity success it
writes **P+10=candidate, P+C=0, P+48=returned64, P+14=1**, retains original
**82C73428**, and refreshes the name through **827B25E8**. Failure retains the
original invalid-selection path and **82C73400** CPU dirty-state helper.

A verified game-side candidate-to-selection bridge exists at **8239CCC4**
(`827B25B8`) then **8239CCCC** (`827B2788`) inside **8239CC48**. Player
association paths also call the setter at **8285E760**, and selection followed
by settings at **8285EBE0/EBFC**. The evidence enumerates 11 direct callsites
to these two helpers. It does not claim a complete indirect graph or identify
the final Press Start/controller/native UI interaction from these instructions.
**Publishing notification A does not select a game player by itself.**

## Cohesive activation dependencies: implement these before exposing an active slot

1. **Identity:** `82431F08(index,out64)` sets r5=out64, r4=**7**, then calls
   **82CC25A4**, XAM **20A XamUserGetXUID**. The output is eight guest BE bytes.
   The wrapper translates nonnegative HRESULT to0, Win32-facility failures to
   their low16 code, other failures to **65B**. A durable local compatibility
   identity can represent an actual app-local profile; it must not claim to be
   an Xbox account or credential. It must remain stable across load/restart and
   distinct from a recyclable slot number. Full XUID type-mask/prefix rules
   beyond this requested mask7 remain unqualified here.
2. **Name:** **827B2638 ->82431878 ->82CC2504**, XAM **20E XamUserGetName**,
   uses r3=index, r4=P+34, r5=**16 bytes**. The original caller subsequently
   zeros P+44 and does not check the name status. Supply a real bounded name
   from that same profile; define an explicit native encoding/truncation policy.
   Console encoding, every error output write and arbitrary capacities are not
   recovered by this caller.
3. **Options/settings:** **827B2CE0(P,flagA,valueA,flagB,valueB,index)** uses
   r3..r8 in that order. Flags are byte pointers; values are word pointers.
   It first clears both flags. Indices >=4 fall back to P's selected index only
   if P[14] is true. IDs are exactly **10040002 and 10040003**; this pass does
   not assign unproved UI names or invented default values to them.
   **827B2D68** requests size with sizeWord=0/output=null, then allocates the
   returned size through original **8269BE40**; **827B2D98** requests data.
   Second status0 and result count2 are required. Result+4 is a BE record
   pointer, stride **28**, ID at record+10, scalar at record+20. It copies only
   those fields into the flagged outputs and frees through **8269BEB0**.
   **82C71CB8 ->82CC33E4**, XAM **219 XamUserReadProfileSettings**, receives
   r3=original P+28 value, r4=index, r5/r6=0, r7=2, r8=ID array,
   r9=size pointer, r10=data pointer, and ninth argument0 at callee SP+54.
   Preserve the original two-call allocation/error contract; a successful empty
   or arbitrarily zeroed result is not a settings implementation. Remaining
   record tags/header fields need qualification before producing whole records.
4. **Sign-out/revalidation:** **827B28BC ->82B75DB0 ->82CC3434**, XAM **227
   XamUserGetSigninInfo**, uses r3=index, r4=**4**, r5=40-byte output. This
   original caller prezeros the buffer but consumes only the translated status.
   **525** clears cached selection, identity and name. Raw Win32-facility
   failure **80070525** therefore has a verified path through the wrapper;
   arbitrary error/output behavior and the full SigninInfo success schema are
   not qualified by this consumer. Do not report success for a signed-out slot.

The earliest query can therefore be backed by a real registry now, but enabling
an original game slot needs this coherent set of adapters, or an explicit gate
at its first unimplemented dependency. Do not implement activation by manually
patching P+10/P+14/P+48 or bypassing the original allocation/selection helpers.

## Native object, storage and notification lifetime proposal

This is a native product contract, not a recovered Xbox storage schema:

- Runtime owns the slot registry and an app-local profile store. Real
  `createProfile`, `loadProfile`, `activate(slot,profile)` and `signOut(slot)`
  operate on durable profile objects. Creation persists identity, name and
  versioned settings before activation can succeed. Loading validates them;
  failed writes, corrupt files or failed activation leave the prior slot intact.
  Existing profiles may load without being automatically activated.
- Use Windows-user-owned app storage, obtainable via
  [SHGetKnownFolderPath](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath)
  for LocalAppData, and an explicit native profile chooser. Windows account
  identity is the storage/security context, not automatic Xbox identity. A
  persisted locally allocated ID is a compatibility ID backed by real data;
  its generation/collision rules must be explicit. No external Xbox credentials
  are necessary for this proposed local-only service.
- Each slot holds a profile lease and revision. Read operations return a
  coherent snapshot; asynchronous work holds the actual profile lifetime, not
  just a slot index that can be reassigned. Serialize state transitions, finish
  durable work before acknowledging it, and publish system notification **A**
  only after an actual visible transition has committed. No initial synthetic A.
  Do not call game CPU code while holding a registry lock or from native UI/
  notification producer callbacks.
- Original **82861880/A4** re-query XUID and SigninInfo for an already selected
  user. The A branch ignores its payload; changed-slot payload encoding is not
  proved by this consumer. A must reflect real transitions, but cannot itself
  carry a selected-user command. These queries may invoke settings refresh,
  sign-out dialogs or `iMsgExitGameDueToSignInChange`; retain their original AOT.
- Detach/sign out does not delete the stored profile. Quiesce UI, queued saves
  and workers before releasing native ownership; don't invalidate leases while
  those operations still use them. Existing original profile workers and CPU
  subscriptions remain their original owners. Terminal cancellation has prior
  runtime evidence; normal original profile-worker join/teardown remains
  unverified, so this report does not authorize a full normal-restart claim.

The original sign-in UI request is **82861180 ->82432DA0 ->82CC2724**,
XAM **2BC XamShowSigninUI(1,0)**. A real native chooser can implement this
local-only product operation. It needs actual open/close lifecycle and system
UI notifications9; fake success can strand original busy state. If that operation
is not ready, **82861098** is the known earlier engine guard before busy-byte
and queue mutation. Its broader queue also launches marketplace/achievements;
do not silently declare those implemented by providing a local profile chooser.

## Save/content dependencies and the next bounded implementation slice

The selected/offline path does not inherently require Live2. It does require
real content services to claim saving: **827B29A8** queries state then XUID for
either the selected slot or each slot0..3, and calls **827B2A60 ->82432560 ->
82CC2684**, XAM **25C XamContentCreateEnumerator**. Original arguments are
index, incoming device selector, type1, flags1000, count1, output-size pointer,
output-handle pointer. It allocates 0x134 bytes and calls **827B2A88 ->82432C90
->82CC26F4**, XAM **250 XamEnumerate**; wrapper r4 becomes0 and subsequent
arguments shift one register. No successful native container, enumeration or
save format has been implemented or proved by this analysis.

The existing content worker additionally waits for UI/storage notifications
9/B and can call **827AF0B8 ->82432570 ->82CC26A4 XamContentGetDeviceState**.
Content create/flush/close/delete, device enumeration/selector UI and profile
settings writes remain separate unsupported capabilities. Their presence is
not proof that all are the next calls from the reached constructor. A truthful
native store will need actual per-profile files, path containment, real
enumeration, retained operation ownership and durable save completion; complete
original container/save-data schemas and the final input-to-gameplay path are
the remaining bounded follow-ups, not blockers requiring Xbox credentials.

Recommended next slice: implement/test the **durable local store plus four-slot
registry**, including restart-stable identity, explicit activation/sign-out,
retained leases, failure atomicity and real notification delivery. Wire only
the qualified SigninState query initially; exercise real registry mutations in
native tests while original activation dependencies remain explicit gates.
Then qualify and wire identity/name/settings/SigninInfo together, and connect
the actual chooser/game-player association path. This creates a useful native
service without claiming that an empty session alone provides selection/saves.

## Reproducible evidence and freeze

Run `python -B build/native-player-entry/verify.py` from the workspace. It
validates the pinned original image, 12 original `.pdata` extents, 8 reviewed
leaves, 6 instruction windows, 58 word pins, 37 call pins, 9 import thunks/names
and the 11 direct callsites to the two selection helpers. Every one of the
**654 disassembled words** is checked against original bytes. Seven malformed
image/range/decoder/thunk fixtures reject. Outputs are deterministic
`build/native-player-entry/evidence.json` and `disassembly.txt`.

Validation passed. These are static byte checks and a check of the existing
boot079 log, not an executed native profile/selection/save test. Scope frozen;
main owns the production implementation decision.
