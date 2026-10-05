# Native selected-folder storage details

Boot211 reached82432580/XamContentGetDeviceData at LR8285CE58 after the real
storage selector accepted native folder1. The request supplied device1 and
an80-byte output at02303B60. Original8285CDF8 reads total bytes at+8 and
available bytes at+10, shifts right12 and publishes64-bit4KiB counts to its
optional outputs. It retains all caller state transitions and size comparisons.

queryNativeStorage now owns the actual configured folder/ancestors, obtains
caller-available, total and free bytes using GetDiskFreeSpaceExW, and reads the
real volume label using GetVolumeInformationByHandleW on the pinned directory.
It creates no files. inspectNativeStorage builds on this query and adds the
existing exclusive write/flush/delete probe only for interactive eligibility.
Capacity observations do not reserve disk space or implement game saves.

The import serializes the original80-byte ABI in big-endian order: native
identifier1, fixed-storage class1, total64-bit bytes, caller-available64-bit
bytes, and28 UTF16 name units. It copies at most27 units, avoiding a split
surrogate pair and retaining a zero terminator. An empty Windows volume label
uses its actual drive-root path. There is no dummy HDD size/name, console drive
object, profile fabrication, save creation or success for an inaccessible folder.
Other device identifiers and invalid outputs reject explicitly; unavailable
device Win32/last-error handling is not newly qualified by this milestone.

Only r3 changes on successful import return. CPU state, host FP controls and
host last-error remain intact. Output writes occur only after real native
metadata has been obtained. Output extent is exactly80 bytes and no native
directory handle survives the query's owner scope.

The ABI/layout cross-check is local primary
K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_content_device.cpp, SHA256
72beb8299ce1861c83867eb857282efc51a9673a524dd8c10aa936cd9585a546;
fixed-storage enum reference include/rex/system/xam/content_device.h SHA256
fffe156f34df189a318c8efa24c2f4aeeaa83fe7c0f6d11e97863d7c501e42e4.
Its dummy devices and emulated runtime are not used or linked. Original image
SHA2566df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0;
original8285CDF8/E0-byte .pdata span SHA256
17361deaad6684d0601b027aebe6a2f7ca8b345791a7014446fe17bbce3b134f.

Native storage/content tests verify actual volume totals/label, five FP modes,
full CPU/host state, exact byte bounds, original8285CDF8 capacity conversion
with all outputs and with the live optional-output pattern, invalid buffers/
devices, released handles and unchanged storage contents. Conversion expectations
read the exact original call's released stack record, avoiding races between
separate free-space observations. Selector regressions exercise the refactored
native query through actual visible UI and file commands.

AOT regeneration reports250 explicit unsupported imports,311 files and zero
semantic diagnostics. The focused build completes; both affected test targets
pass in5.54s:build/native-storage-data-tests.log. The previous full suite passed
126/126 before this addition. Boot212 accepted device1 metadata at log line
2501795: total1706177851392, available526175166464, actual volume label Codex.
The original autosave notice was viewed, dismissed with A, and the selected
device's empty saved-game catalog was scanned. It then reached the next missing
import, XamGetExecutionId. Arrival at the main menu remains unverified.

## Selected storage display name

Boot214 accepted the new-game slot selection and reached XamContentGetDeviceName
through original tail82432588 at LR82C9ED0C. Arguments were device1,
output82D01BB4, capacity1C UTF16 code units. Original82C9ECB8 divides its38-byte
buffer size by2, obtains the name, measures its UTF16 length and may apply its
own UI ellipsis. Its E0-byte original span SHA256 is
5d5df31c24193c0ee8b0b2d692bd3c2cb8c4bcdb33dc2421c6f8dc2979c8841d.

The native name query uses the same actual fixed-folder volume metadata. It
returns the real volume label, or the actual drive root if the label is empty.
Capacity counts UTF16 units including the terminator. An insufficient capacity
returns122 without writes; success copies the complete big-endian UTF16 name
and terminator, leaving the rest untouched. Invalid/unqualified device IDs and
buffers reject. No dummy device object, renamed volume or saved data is created.

The local primary xam_content_device.cpp reference above establishes the
device/name/capacity ABI and short-buffer status. Its copy_and_swap_truncating
helper writes only source characters plus the terminator. The reference dummy
device names are not used. Tests compare real Windows volume data, five FP
modes and entire CPU state, exact/short/oversized UTF16 capacities, two-byte
alignment, untouched tails, invalid buffers, original82432588 and original
UTF16 length function82C9E1B0. The full82C9ECB8 UI lock/ellipsis wrapper is not
newly isolated by these tests; its live acceptance is pending.

The affected OriginalContentEnumeration target passes in5.09s, including the
existing content/catalog/capacity regressions. Build and AOT regeneration pass:
247 unsupported imports,311 files,0 diagnostics. Evidence logs are
build/native-storage-name-build.log and build/native-storage-name-tests.log.
Boot215 retries the same actual profile/folder with a fresh input stream.

Boot215 accepted the name at log line9335093: device1,nameCodex,capacity28. The
original UI name consumer completed. The next worker request was
XamContentCreateEx at LR82432508 with root rmcsave and flags12. It remained
unimplemented and Boot215 exited1 naturally. The last sampled frame predates
that save transition and still shows Saved Games; no save was created.

The full integration build after storage details, execution identity, profile
preference absence and device name passes128/128 in235.51s:
build/native-profile-storage-integration-build.log.

## Save-loading storage status

The updated goal is Reach in-game. Automatic startup replay-002 delivered Start
and five A holds, including the viewed existing-save confirmation, by36.0715s
after launch. It then failed on XamContentGetDeviceState at original8285B538,
through82432570, with device1 and overlapped0. This was a real game failure,
not a missed or delayed controller command.

Original82432570 is the unchanged branch48890134 to82CC26A4. The original
8285B518..8285B554 consumer loads a device ID through r4, passes overlapped0,
and maps a zero status to0 and an unavailable/zero device to2. The primary Xenia
source establishes the two-argument ABI and synchronous0/1167 status distinction:
https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/kernel/xam/xam_content_device.cc
Its dummy device implementation is not used.

The native service now checks registered folder1 using a fresh pinned ancestor/
directory walk and actual Windows volume/capacity query. Missing folders return
disconnected; unregistered IDs return1167. Invalid, aliased or inaccessible
storage still fails explicitly, and nonzero overlapped pointers remain guarded.
No files or guest outputs are written. Existing queryNativeStorage preserves its
prior missing-folder rejection through a shared optional internal query.

New tests execute both original functions with actual present and removed empty
fixture directories. They cover foreign IDs, five FP modes, complete CPU state,
host last-error, untouched guest bytes, unsupported asynchronous calls, invalid
base and cancellation. OriginalContentEnumeration passes1.08s and the separate
OriginalNativeStorageSelector regression passes0.55s. Logs:
native-device-state-focused-tests.log and native-device-state-selector-tests.log.
Regeneration passes311 files/zero diagnostics with241 remaining explicit imports;
the focused normal game/content/selector build completed. Live replay-003 is the
next check. Full integration after this change is still pending.

Replay003 accepted the real device status and remained live, displaying the
original damaged-save notice. The payload and index hashes remained unchanged.
Replay005/006 verified the header read succeeds (28/28,status0); all three checksums
calculated from the image's original CRC table match the persisted header/data.
The read-only82C9C3D8 callback trace in replay006 shows event20/state4 with the
correctMC02 header/114800 size, but object+FC=0. Original82C9C640 compares that
zero length against header+4 and rejects before reading the payload. This is
the save-loading failure found before the directory lookup fix below.

### Existing-save file metadata (2026-09-13)

Replay008 traced the metadata request to original8285BF78/82B75D20 and
82434388. The latter splits `rmcsave:\\SIMPSONS_SLOT1`, opens the directory
with access00100001/share3/options4021, then queries the filename. The bridge
previously treated the empty relative directory name as a regular file and
returnedC000000D. The original worker consequently returned12, omitted event28
(which supplies object+FC), and the loader rejected its otherwise valid header.
Event27 is a prompt, not the file-size event. Slowing the buttons in replay007
did not change this failure.

NativeSaveSession now reopens its pinned real generation directory with an
independent NT search cursor and read-only access. The directory remains owned
by the session and blocks close or generation changes until released. Original
NtQueryDirectoryFile is implemented for these mounted save directories: real
Windows directory information is translated into the original big-endian,
ASCII record. File length, allocation, timestamps, attributes, missing-file and
end-of-search status come from the native filesystem. Reparse/directory entries,
foreign handles, unsupported asynchronous requests and invalid guest buffers
remain rejected. Original asset write restrictions are unchanged.

The regression executes original82434388 and82434500. It failedC000000D before
the fix and now reads the real fixture payload name/length, reaches native end
of search, restarts, and returns the native missing-file status without leaking
handles. Additional checks cover CPU/FP/host-error preservation, exact output
extent, short/invalid buffers, read-only ownership and unchanged saved indexes.
NativeSaveStore, OriginalNativeSaveBridge and OriginalAssetFilesystem pass.
Logs: build/native-save-directory-baseline-tests.log (expected failure) and
build/native-save-directory-tests.log (3/3 pass). Regeneration:311 files,
zero semantic diagnostics,240 explicit unsupported imports. The full build and
133/133 tests pass in96.63 seconds (build/native-save-directory-full-build-tests.log).
Live replay009 read the actual114800-byte entry twice and closed both original
search/container handles successfully. It then reached a previously unvisited
timezone query while formatting the save date (ExGetXConfigSetting3/1,
caller82434A3C). It did not reach the load confirmation in that run.

After the timezone and calendar services were added, replay011 returned metadata
result11 and emitted original event28 with114800 bytes. Original loader states4,
5 and6 read28,8 and114764 bytes respectively, all with status0, then completed.
The real Main Menu was captured and viewed at
build/automatic-startup/replay-011-review/native-frame-2026877.png. Replay012
independently repeated the entire automatic sequence through Main Menu and
Continue Game. All profile, payload, index and achievement hashes remain equal
to the established pre-run values. Gameplay then hits the existing character
shadow activation guard; save loading is no longer the blocker.

Windows contract reference:
[NtQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile).
