# Native local-player state bridge

The native runtime owns four session slots backed by real immutable local
profile files. A normal launch begins with every slot empty. This is an
explicit native application policy, independent of the original game's
no-selection value4 and its two game-player associations. Creation and loading
do not activate a slot. Active local profiles report1; this service never
reports Xbox Live state2 or manufactures Xbox credentials.

`runtime/local_players.cpp` implements the qualified `XamUserGetSigninState`
import. Original82431880 is the unchanged four-byte tail `48890C94` to82CC2514.
The only input is r3.u32; the result replaces r3 with a zero-extended state.
Incidental r4 is ignored, and the adapter preserves the remainder of the CPU
context and caller host floating-point controls. It validates runtime ownership
and cancellation. Out-of-range indices return0 as a native no-such-slot policy;
4/6/7/FFFFFFFF cannot alias an active slot. Mutation APIs instead reject invalid
slots. Original consumer evidence is in `native-player-entry.md`.

The portable default store is `userdata/local-profiles` beside the game's
read-only data directory. An explicit root can be configured before the service
is first used. Each runtime has a fresh slot registry; files persist between
runs. The native store's bounded record, identity, name and filesystem policies
are documented in `native-local-players.md`. Its GUID is an opaque native
profile ID, not an exposed Xbox XUID. Identity/name/settings/SigninInfo and
content imports remain unsupported until their complete adapters are qualified.

Runtime activation/sign-out serialize transitions and publish notificationA
only after a real change commits. Idempotent or rejected operations produce no
event. The original828616A8 handler ignores its payload and re-queries identity
and state; this native invalidation uses parameter0, with no claim that a
changed-slot mask has been recovered. NotificationA does not select a game
player. A broadcast exception after commit stops the runtime explicitly: it
cannot roll back a state change or notifications already seen by another
listener. Runtime clients must use the runtime transition methods so that these
notifications and ordering are preserved; direct store mutation is for a
standalone owner and isolated fixtures.

The native executable exposes genuine profile management before the original
chooser is connected:

```powershell
.\build\native\SimpsonsNative.exe --profile-store K:\SimpsonsNativeCopy\userdata\local-profiles --create-local-profile Player
.\build\native\SimpsonsNative.exe --profile-store K:\SimpsonsNativeCopy\userdata\local-profiles --list-local-profiles
```

Creation prints the new native ID and name. To explicitly activate that stored
profile in slot0 for a launch, pass `--local-profile 0:<printed-ID>` with the
ordinary `--image` option. `--profile-store` can override the default launch
root. This is a native session operation; original game-player association is
unchanged, and a reached unimplemented dependency still fails. No command
silently creates a profile, selects a player, edits original caches, or supplies
save data. Management operations do not load the game or start its window.

Focused integration tests use actual profile files and executable processes.
`OriginalLocalPlayerQuery` exercises the original823A1228 query loop with empty
slots and with an active unassociated local profile, checks unchanged option
defaults and original selection caches, invokes the real82431880 tail, and
verifies the import's full context/FP contract, state transitions, notification
delivery, durable reopen and cancellation. `NativeLocalProfileCommandLine`
creates/lists profiles across separate executable processes and verifies that
invalid option combinations reject before opening a store. Core ownership and
malformed-file tests remain in `NativeLocalPlayerOwnership`.

These fixtures support service correctness. They do not establish an original
profile chooser, controller-to-player association, save/load, or gameplay.
Integrated build and actual-boot results are recorded in `STATUS.md` only after
they have run.
