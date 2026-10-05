# Historical platform boundary: system UI notifications

Build108/109 now implement the reached native listener, queue, wait and close
contracts. Boot067/068 creates both original system subscriptions. See
`native-notification-service.md`, `native-system-notifications.md` and
`native-profile-startup.md` for verified behavior and remaining state producers.
The notes below preserve the earlier boot064 investigation.

Boot064 passes native recording-owner construction and the original debug-name
request for `VfxCullStateManagerThread`. It then fails at unimplemented
`XamNotifyCreateListener`, LR **82860F14**, mask **1**, version **2**.
This was the executable boundary in boot064, before a native notification service.

Original constructor **82860EA0** owns the call at **82860F10**. It publishes
its CPU singleton at 82D08D64 and initializes CPU UI queue fields before calling
the import wrapper, then stores the returned handle at owner+C. The original
two-instruction wrapper **82432CC0** sets r4=2 and branches to import thunk
**82CC2704**; incoming r3 is the 64-bit subscription mask. It has no separate
`.pdata` record in the extracted table. Pin its actual eight bytes:
`388000024888fa40`.

Constructor size **124**, SHA256:
`90168e3e6cb619a0db8965081e1defe4a3a5c0b0744fa98891afabd2ab1adecc`.
The constructor retains original CPU registrations for the literal messages
`iMsgShowSignin`, `iMsgShowAchievements` and `iMsgShowMarketplace`, plus the
existing frame/message descriptors at 82D57390 and 82D573A0. These strings
identify the subsystem; they do not establish implemented native account or
store behavior.

Its message consumer **828616A8** polls at **82861814**, calling import thunk
**82CC24E4** (`XNotifyGetNext`) with:

```
r3 = owner+C listener handle
r4 = 0, any notification
r5 = stack+54, notification ID output
r6 = stack+58, 32-bit parameter output
```

Zero returned at 82861818 ends that poll path. The following instructions
distinguish notification ID **9** at 82861824 and **A** at 8286182C. Additional
branches and original user/UI calls need review before choosing native event
producers or initialization policy. Other game subsystems also poll this import;
the bounded dispatcher scan found 22 direct polling callsites, so a local
constant-return replacement would not establish the required platform service.

Primary open-source implementation references corroborate the mask/version
creation ABI and four-argument polling ABI in
[Xenia's notification entry points](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xam/xam_notify.cc).
Its [listener implementation](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xnotifylistener.cc)
uses a waitable event and a queue. These are comparative implementation evidence,
not proof of this game's exact original notification ordering or output-write
contract.

The reference's
[startup registration code](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/kernel_state.cc)
injects a fixed notification sequence based on observations of other games.
That sequence has not been verified for this executable and must not be adopted
as a substitute for native UI, local-profile and input state. No reference
notification code or synthetic startup events have been added to this port.

The next implementation should own native listener handles, wakeups, queue and
filtering lifetime, and establish the original poll/close contracts. Events
must reflect implemented native platform state. Original game CPU message
handling should remain AOT; unavailable external platform features need explicit
behavior at their own reached boundaries. Listener creation, polling, native
profiles and system UI are still unimplemented as of boot064.
