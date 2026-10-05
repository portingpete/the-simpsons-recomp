# Native system notification service

The reached system subscription (64-bit mask1, version2) now has a native FIFO
queue and manual-reset Windows event. The original UI constructor, subscriptions,
one-poll-per-tick handler and destructor remain AOT. This is a platform service
implementation; it neither decodes console commands nor uses an emulator runtime.
Build108 passes all 27 CTest suites. Actual boot067 creates system listeners
00000140 and 00000144, then starts original worker827AEFB0. The next explicit
failure is NtCreateMutant at LR82B76618. No initial notifications were fabricated,
and no original geometry or artwork was drawn.

## Ownership and delivery

`runtime/native_notifications.cpp` owns host values only. Each listener owns one
event and its synchronized queue. A Runtime owns a broker whose listener entries
are weak references. The guest handle registry owns a KernelHandle with a strong
listener reference and an owned duplicate of the same event. Existing native
wait and NtClose paths therefore retain their normal checked handle leases.
Closing a handle cannot invalidate a concurrent poll or wait that already owns
its reference. Once the final listener reference expires, later publication
prunes that subscription. No guest stack or CPU owner pointer escapes a poll.

Publication is serialized across sources, then queued under each listener lock.
The identifier's category and version determine delivery. FIFO order and repeated
identifiers are preserved. An exact-match read consumes the first matching item;
a filter miss leaves both other entries and the event unchanged. The event resets
only when the final pending item is consumed. Allocation/wakeup/reset failures
are exceptions, not successful delivery. A broadcast allocation failure can
follow delivery to earlier listeners; no cross-listener rollback is claimed.

`runtime/notifications.cpp` validates the active Runtime, guest memory base,
owned handle and writable outputs before consuming a notification. It writes
32-bit big-endian outputs and returns a boolean. A nullable parameter output is
supported. Empty polls write zero outputs and return false. This empty-output
policy and exact filtering are reference-correlated native behavior; the current
original caller uses filter0, two nonnull outputs and ignores outputs on false.
See [the original evidence](native-system-notifications.md) for those limits.
Other subscription profiles remain explicit failures until their producers and
original usage are qualified.

## Initial state and remaining producers

Creation starts with an unsignalled, empty queue because no native platform
transition has occurred. The original constructor performs no initial query or
poll, and the following profile constructor establishes its own no-selected-user
defaults. No required startup sequence was proved. There is no fabricated UI
open/close pulse or signed-in account. ID9 would execute original pause/resume
and queued UI behavior; IDA would re-query profile services. These effects are
substantial and remain tied to actual state transitions.

The broker exposes publication for future native UI/profile/input producers.
Those producers are not implemented by this change. Their still-unimplemented
platform imports continue to fail explicitly if reached. Window focus alone is
not treated as Xbox system UI visibility. An initially empty queue does not
establish a complete notification platform, working sign-in, actual notification
delivery during game execution, or complete CPU subscription teardown.

## Verification

`tests/test_notifications.cpp` links the real runtime and calls the imported
entry points. It passes native wait wakeup/reset, FIFO/duplicate/exact filtering,
category/version field edges, independent subscriptions, concurrent publication
and close, checked big-endian outputs, stale/wrong-type handles and cancellation
already set at entry. A retained native handle lease survives registry close;
final release expires the broker subscription. These publications are synthetic
test fixtures, not delivered original UI/profile transitions.

Logs: `build/hundred-eighth-build.log`, `build/native-notification-108.log`,
`build/boot-067.log`. Build108 executable SHA256:
`d3f7f144c82384d4fa3e792ca8e3eb5204607b8385e3f2e458f27b129cd6d496`.
Full suite time was 39.15 seconds. The failed build107 attempt stopped in CMake
while the new test source was still being authored; its AOT generation passed.
