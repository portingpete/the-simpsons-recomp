# Native marketplace-content enumeration

The original startup request now uses a real Windows filesystem snapshot.
The default local store is `userdata/content` beside the game-data folder;
`--content-store <directory>` selects another installed-content root.
Both that root and the original game's `Content` directory are searched under
`0000000000000000/45410809/00000002`. Missing directories mean no installed
entries. The scanner creates no directories and writes no package data.
The title ID comes from this original XEX's execution header, verified as
`056310420000000100000001454108090000010100000000`.

The reached original owner `827B0590`, singleton `82D08B98`, calls
`827AFCD8` with user FE. That code requests device0, marketplace type2,
flags0, one record per page. Its original `82432C90` wrapper rearranges
five arguments and inserts flags0 before `XamEnumerate`. The original
record is 308 bytes: big-endian device/type, 128 UTF16 display-name units,
42 filename bytes and two padding bytes. Native device1 names installed
content; device2 names content supplied on disc. These are portable store
identifiers, not emulated console devices.

The scanner opens and pins every path component and each package. It rejects
reparse points, aliases, unsupported directories, malformed metadata, wrong
title/type/owner, truncated files and unqualified volume formats. File handles
deny modification and deletion until the enumeration snapshot is released.
Names are bounded and sorted; each root is limited to 4,096 packages. It reads
only the bounded STFS header of raw CON/LIVE/PIRS packages. Display names come
from original localized metadata, using the same Windows UI language mapping
as the native language service, with the package's English fallback.

This reads metadata only. It does not verify cryptographic signatures,
licenses, payload hashes or mounted file access. Those future services remain
explicitly unimplemented. No retail DLC was supplied; populated-package tests
use synthetic header fixtures, and do not establish that DLC loads or works.

Each enumeration owns its native handles and immutable metadata snapshot.
Synchronous requests return a record count, or `ERROR_NO_MORE_FILES` (18)
without changing record bytes. Short pages fail without advancing the cursor.
The reached overlapped request completes inline from the owned snapshot and
returns `ERROR_IO_PENDING` (997). Completion is already readable when it
returns; no worker or guest output pointer is retained. It resets and signals
an optional native event, preserves the caller's event/context fields and
rejects guest completion callbacks before changing outputs.

Empty overlapped completion is result1627 and extended HRESULT80070012.
The original result poll `82433670` and extended-error reader `82432C68`
observe those values. Original `827AFB78` then calls `827ADCE8`, which runs
the original CloseHandle wrapper `82432CC8`, clears the enumerator and state,
and marks the scan complete. The focused fixture runs the actual original
startup to its existing pre-FX observation point, then calls that real owner's
request and poll. Its native handle and snapshot expire through this original
paired close. Full owner worker/destructor teardown is outside this milestone.

Inline completion is an explicit native scheduling policy. Windows permits
asynchronous operations to complete synchronously; that does not establish
the original console's completion timing. The ABI status/record contract was
cross-checked against original consumers and primary reverse-engineering
implementations. Sources: [Windows synchronous and asynchronous I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o),
[Xenia content enumeration](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xam/xam_content.cc),
[original STFS layout research](https://github.com/xenia-project/xenia/blob/master/src/xenia/vfs/devices/stfs_xbox.h),
and [Xam overlapped event behavior](https://github.com/xenia-project/xenia/issues/1946).
The local reference files and original instruction spans are hash-pinned by
`build/content-enumeration/evidence.py`; none are included as a console runtime.

Actual muted startup now completes its filesystem scan and reaches the next
unimplemented service, `XamInputGetState`, caller return82321114.
No original screen, menu, world, gameplay, progression or save/load is verified.
