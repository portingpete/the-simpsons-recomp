# Native local-player store and session ownership

`Simpsons::Platform::NativeLocalPlayers` owns a real Windows file-backed local
profile store and four initially empty session slots. It implements create,
load, list, activation, replacement and sign-out. It does not create Xbox
credentials, expose a GUID as an Xbox XUID, write guest caches, publish events,
implement game settings/saves, or choose a profile implicitly. Main owns the
Runtime/import/UI/CLI and notification integration.

The later achievement extension is documented in native-achievements.md. It
reuses the native profile owner and directory protections while storing unlock
records separately; the immutable profile format and original selection logic
remain unchanged. See native-local-identity.md for the current identity/name ABI.

Owned files are `runtime/native_local_players.h`,
`runtime/native_local_players.cpp`, `tests/test_native_local_players.cpp`, and
this document, plus the specifically requested standalone fix diagnostics under
`build/native-local-players`. Existing notification and original/reference files are unchanged.
The original boundary evidence remains in [native-player-entry.md](native-player-entry.md).

## Public contract

Namespace **Simpsons::Platform**; construction takes an explicit
`std::filesystem::path root`. The owner is noncopyable and nonmovable. Destruction
requires callers to have stopped invoking its methods, as with any ordinary C++
owner; it retires active file leases before closing pinned directory handles.

- `LocalProfile { std::string id; std::string name; }` is an independent value
  snapshot. IDs are canonical lowercase version-4 GUID strings generated with
  actual system randomness. They identify native stored profiles, not Xbox
  accounts or console handles. Names are **1..15 printable ASCII bytes**, without
  leading/trailing spaces. This is explicit native compatibility policy; duplicate
  display names are allowed and never used as file paths or identity keys.
- `list() const -> vector<LocalProfile>` validates every listed profile and sorts
  by ID. `load(id) const -> LocalProfile` reads and validates that exact record.
  Neither activates a profile. Modifying returned strings changes no owner state.
- `create(name) -> LocalProfile` publishes a new immutable record after successful
  Windows writes, flushes and exclusive final naming. It never replaces an
  existing profile, including on an actual random-ID collision; such failure is
  explicit. No deterministic fallback ID or silent repair is used.
- `activate(slot,id) -> bool` prepares and validates the entire file lease before
  replacing the old slot. Same slot/same identity returns false. A profile already
  active in another slot rejects, leaving both slots unchanged. Replacing a slot
  with a different inactive profile returns true. A second owner has its own
  independent session; the duplicate-slot rule is per owner.
- `signOut(slot) -> bool` returns true only when an active lease was removed.
  Repeated sign-out returns false. It never deletes the durable profile.
- `state(slot) const -> uint32_t` reads the owner: **0 empty, 1 active local**.
  `profile(slot) const -> optional<LocalProfile>` returns the matching snapshot.
  Every slot method rejects values outside **0..3**, including4,7,FFFFFFFF.
  Main's qualified import may map invalid indices to0 as its separate explicit
  no-such-slot policy; the core never aliases an invalid index.

Errors from validation and Windows/CNG operations throw `LocalPlayerError`
(derived from `std::runtime_error`), with Win32/NTSTATUS values for API failures.
Standard allocation/filesystem-path construction exceptions can also propagate.
There is no success-with-empty-data fallback. A single mutex serializes all
registry/store operations within an owner. Core methods never call guest code,
UI callbacks or a notification broker while holding it.

The **private** `createWithId` shares the production publication path and is
accessible to the friend fixture solely to force a deterministic existing-ID
collision. It is not a public import/restore facility and supplies no Xbox
identity semantics. Public creation always obtains a random GUID.

## Storage format, publication and containment

The supported root is a dedicated directory on a **fixed local NTFS volume**.
Ordinary relative paths resolve once against the current working directory;
drive-relative paths, traversal components, UNC/device namespaces, invalid or
ambiguous components and volume-root stores reject. The constructor creates
missing directories one component at a time; a failed constructor can leave
new empty directories. It does not roll back or delete pre-existing directories.

Each directory from the drive root downward is opened with
`FILE_FLAG_OPEN_REPARSE_POINT`, checked as a non-reparse disk directory, and held
with read sharing only. Those lifetime pins deny write/delete opens needed for
ancestor replacement. Store operations recheck the pinned objects. Opened files
must remain under the pinned root's resolved path, be non-reparse disk files,
and have exactly one hard link. IDs are validated before constructing any file
path; no alternate streams, separators or path fragments are accepted.
These checks use the actual opened objects, not merely a string-prefix test.
The relevant Windows sharing and reparse-open behavior is documented by
[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

The native version-1 format is a small ASCII record named `<guid>.profile`:

```
SIMPSONS-LOCAL-PROFILE 1\n
<canonical lowercase GUID>\n
<display name>\n
SHA256:<64 lowercase hex digits>\n
```

The notation above shows line terminators explicitly; actual files contain LF
bytes, not backslash escapes. SHA-256 covers the first three complete lines,
including their LF bytes. Reads are bounded to **256 bytes** before allocation;
framing, version, filename/record identity, name, checksum and exact ending must
all agree. The checksum detects accidental corruption, not an authenticated
account or a defense against a Windows user deliberately authoring valid files.
Records are immutable through this API; there is no edit/delete/repair method.

Creation opens the **final `<guid>.profile` filename with CREATE_NEW**, share
mode **0**, and write-through enabled. All record allocations precede creation.
The sequence is WriteFile with exact byte count, **FlushFileBuffers**, then close
before returning success. No existing record is opened or overwritten, and no
rename or directory sharing relaxation is used. See
[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).

This is **readable publication**, not atomic filename visibility: enumeration
can see the name while creation is in progress, but share-zero ownership denies
opening the contents until the complete write/flush/close. Another owner can
therefore receive an explicit sharing failure during concurrent enumeration.
There is no successful partial-profile result. A crash can leave a partial final
file; ordinary strict framing/checksum validation rejects it without deleting,
repairing, or treating it as a successfully loaded profile. A fully valid record
that survived an interrupted caller can be listed; that does not retroactively
acknowledge the interrupted create request or activate a session slot.

Failure cleanup marks only the newly created, still-exclusive file for deletion
using its handle. It never deletes by a path that may have been replaced, and
CREATE_NEW failure cannot delete the pre-existing destination. Cleanup API
failure is logged and the original operation still throws. Legacy `.pending-*`
files are also explicitly rejected by enumeration, never automatically repaired.
Durability means the actual Windows write/flush completed before close and
success; this is not a claim of tested power-loss recovery or a guarantee about
a storage device's dishonest write-cache behavior.

Enumeration permits at most **4096 non-dot entries** and rejects unexpected
files/directories, malformed records and leftover pending files. Different
owners/processes can read the same immutable profiles; exclusive creation and
no-overwrite creation protect publication independently of the in-process mutex. Concurrent
cross-owner enumeration may encounter an exclusively held record and
fail explicitly. There is no claimed multi-process list snapshot or automatic
retry/recovery policy.

An active slot retains a read handle denying write/delete access for its whole
activation lifetime. Cached name/identity therefore remain paired with the
actual protected file. Sign-out/replacement retires that lease; a subsequent
load rereads the record. External deletion while inactive makes a later
activation fail, preserving an existing slot. An externally changed invalid
record is never silently recreated or replaced.

System randomness uses
[BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom);
record integrity uses
[BCryptHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash).
The latter's algorithm pseudo-handle requires Windows10+, matching this native
backend's qualification. Link **bcrypt.lib**; no COM or additional Runtime
dependency is required by the core.

## Integration and validation

Main serializes actual activation/sign-out with subsequent notification
publication. Only a true changed-state return warrants system notificationA.
The agreed parameter0 is native ID-only invalidation because the recovered
consumer ignores the payload; this core neither publishes nor claims a console
changed-slot bitmask. A main-side publication failure after commit must follow
its terminal-failure policy, not pretend the slot update rolled back.

The no-argument executable `tests/test_native_local_players.cpp` exercises real
Windows storage and ownership: persisted reopen and empty new sessions, immutable
value copies, four-slot replacement/sign-out, duplicate-slot rejection, all
specified invalid indices, actual fixed-ID collision with unchanged old bytes,
real sharing-violation failures, deleted stale identities, corruption/checksum/
size/format rejection, and preservation of an old active slot on failures.
It creates actual NTFS junction/hard-link fixtures, verifies no outside write,
and checks live ancestor pins and their release. Eight threads perform 32 real
publications, followed by concurrent slot transitions and one-winner admission
of the same profile. The fixture uses its own temporary tree and removes known
reparse objects before any recursive cleanup. It does not access game assets,
credentials, audio, native UI, or the notification broker.

Both production and test CPP passed **clang-cl C++20 `/W4 /WX /Zs`**. Main's
build138 then exposed a real NTFS sharing violation at the former final rename
in all three new integration tests. A focused native probe reproduced:

- Win32 absolute rename with parent read-only sharing: **Win32 32**.
- Win32 relative RootDirectory rename on this installed Windows version:
  **Win32 87**, even though the current Microsoft structure documentation
  describes that form.
- Native NtSetInformationFile relative rename: **C0000043** with read-only
  parent sharing, and success only when write sharing was allowed. Adding
  FILE_ADD_FILE to the retained handle did not resolve the conflict.

The accepted minimal fix removes rename entirely and keeps **every directory
pin's original FILE_SHARE_READ policy**, including denial of delete sharing.
No native NT API probe code was added to production. The final exclusive-create
policy above avoids the failing parent reopen while retaining the existing
containment and file-lease guarantees.

After that fix the standalone executable built with **C++20 /O2 /MD /fp:strict
/W4 /WX** and actual `bcrypt.lib`, and passed **931 checks** across every owner
fixture group. Logs are `build/native-local-players/compile.log` and `test.log`.
This includes the formerly failing first creation, actual collision preservation,
and real NTFS junction/hard-link/ancestor-lifetime tests. No full build was run
by this task.

Main's **build139 passed all 43 suites in 55.31 seconds**. Independently read
`build/hundred-thirty-ninth-build.log` and
`build/native/Testing/Temporary/LastTest.log` confirm:

- **NativeLocalPlayerOwnership:** all **931 checks** passed in the integrated
  target, including the complete storage/containment/concurrency fixture.
- **OriginalLocalPlayerQuery:** **199 checks** passed for the durable profile,
  real transitions, and original **empty and active-but-unassociated** query
  loops. This does not claim original game-player selection or saving.
- **NativeLocalProfileCommandLine:** actual create/list/reopen processes passed,
  including stable distinct IDs, invalid names and parse-before-write behavior.

Actual muted **boot081** (empty session) and **boot082** (a real temporary local
profile explicitly activated in slot0) both advanced past SigninState. Boot082
records activation without changing original player association. Both then stop
at the later AOT switch-bound failure **8284BD30**, function **8283CC78**, LR
**8284C0C8**. That separate issue remains main-owned; neither run establishes
gameplay or save support. Checkpoint executable SHA256:
`e13cf2adebdaa12273ade06772f41c7adbfddcd4420c4e2a0bf12fc48e83c110`.

Core/header/test remain unchanged and frozen. This final update changed only
the document to record the integrated build and actual boot evidence.
