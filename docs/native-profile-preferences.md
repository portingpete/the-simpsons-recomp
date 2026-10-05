# Native profile controller preference query

Boot213 reached XamUserReadProfileSettings at82C71D14 after the loaded execution
identity passed original publisher validation. Original827B2CE0 requests
10040002 (Y-axis inversion) and10040003 (controller vibration). Its wrapper
82C71CB8 supplies title45410809,user0,XUIDcount0,XUIDpointer0,count2,IDs pointer,
size pointer,result pointer and a null overlapped at caller SP+54. The first
query requests capacity; the second uses the original allocator's result.

The real immutable SIMPSONS-LOCAL-PROFILE 1 schema contains only the full GUID
and display name, with a checked hash. It has no controller preference overrides.
The adapter resolves the actual active file-owned profile before reporting an
empty preference result. It does not claim global controller values were saved,
create settings sidecars, write game progress, or borrow dummy platform defaults.
The native v1 schema's absence is the source of this result; a future schema
with preference fields must extend this query rather than suppress those fields.

The qualified import accepts synchronous local-slot queries for one or both
of those distinct integer setting IDs. Capacity is8+40*requested-count bytes,
matching the original maximum record layout. A size-only request writes the
required size and returns122; a nonzero short capacity returns122 unchanged.
On successful fill the header count is0 and pointer is buffer+8. Reserved
capacity is zeroed, with no serialized setting records. The source reference
also omits absent keys and decrements returned count. Signed-out slots fail1627
without publishing a result. Original wrapper publisher validation remains.

Original827B2CE0 starts with both availability flags false. It only reads two
setting records when the returned count equals2; the real empty result preserves
those false flags and the caller's value outputs. It frees the original result
allocation. Original823A1228 consequently leaves the game's own controller
options unchanged. Neither original routine is patched or replaced.

CPU state outside r3, host FP controls and host last-error are preserved. The
adapter checks stack argument bounds, pointer alignment and writability,
size/result/ID aliasing, cancellation, qualified title/slot/IDs and synchronous
mode before publication. Other setting IDs, XUID queries, asynchronous reads,
preference writes and future profile versions are not newly qualified.

Local primary ABI sources:

- K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_user.cpp SHA256
  6ca9296c6410e96cbdf5307a74f297d893abc0ba1a0bdaa6b8562205212625f7.
- K:/Simpsons/RexGlueCurrent/include/rex/system/xam/user_profile.h SHA256
  9e2405b248aa50b6e1449dd8d9c994311c54746174237b15b526c8a70c83b281.

Only layout, argument and missing-setting behavior were used. The reference's
emulated profile, hard-coded preferences and online identities are not linked
or used. Original flat-image source spans:

- 82C71CB8/64 SHA256 0c3774bb2a37a111aef5ce376fac3dcae43dca895c3fcdbc72148cef31857328.
- 827B2CE0/13C SHA256 eb76b51151f0ea330e0b321a9c25171a798f8a4b88cebc53a407a66e0da0e37a.
- 823A1228/138 SHA256 a8a2bc6907f33d559c8b36f36f94c137c0fad3c919c98ef40f5108bbef5b0c44.

OriginalProfilePreferences checks exact two-phase output bytes, five FP modes,
complete CPU/host state, absent/signed-out profile behavior, short and malformed
requests, unchanged actual profile files and native ownership. It executes the
actual startup through the existing post-audio observation to establish the
real original allocator, then original827B2CE0 and active-player823A1228. The
test confirms false availability flags, unchanged game option bytes and paired
original allocation/free. It creates only temporary test profiles, no saves.

Test-only corrections were needed for a mixed auto declaration, the startup
fixture's current-context ownership, and preparing a cancellation request before
stopping the runtime. Production checks were not relaxed. The final target
passes in4.55s (build/native-profile-preferences-tests-verified.log). Execution
identity and original local-player regressions passed in the first run. AOT
regeneration reports248 unsupported imports,311 files and0 semantic diagnostics.
Boot214 is the live retry with the same actual profile and content folder. The
last full suite is126/126 before storage metadata, execution identity and this
addition. Boot214 accepted the real profile's empty preference result at log
line4662390, then displayed Saved Games with four New Game entries (viewed
native-frame-1328689.png). Normal A selected a new-game entry; a second
preference read succeeded at6403361. The next missing service was the selected
storage device name. Boot214 exited1 naturally. Main-menu arrival remains
unverified; Saved Games is an intermediate selection screen.

The subsequent full integration build passes128/128 in235.51s, including this
target and the related identity, profile, controller, storage and filesystem
regressions: build/native-profile-storage-integration-build.log.
