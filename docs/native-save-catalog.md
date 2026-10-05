# Native profile-owned saved-game catalog

Current extension: the catalog also reads checksummed version2 indexes produced
by the native save store. Indexed generations take precedence over a version1
entry of the same name; both formats retain real file and directory leases.
See [native-save-store.md](native-save-store.md) for layout, publication,
original save/file adapters and current verification. The historical evidence
below describes the earlier read-only catalog milestones.

Boot209 reached original8285C418 after the real storage warning was dismissed.
At8285C46C it calls82432560 with selected user, owner+360 device, type1, flags0,
one308-byte record per page, stack size output and owner+32C handle output.
The original8285CC78 constructor sets device0 at8285CD50..64 from r10=0
(8285CC98). Device0 means enumerate available native stores; device1 selects
the installed native store. Saved-game requests for other devices remain
unsupported. No device is selected or UI state rewritten by this scanner.

XamContentCreateEnumerator now resolves the real active local profile and reads
`<content-store>/saves/<full lowercase profile GUID>/<8 uppercase hex title>`.
For this executable the title is45410809. No short equality key or Xbox identity
owns these directories. An inactive slot gets HRESULT_FROM_WIN32(NO_SUCH_USER),
with no output handle or metadata writes. A completed snapshot keeps its original
profile ownership across later session changes.

Each child is a native save directory whose ASCII name fits the original42-byte
filename field. It contains `metadata.txt` and a `data` directory with one or
more regular files. The metadata format is UTF8 without BOM:

```
SIMPSONS-NATIVE-SAVE 1
Display name
```

Both lines end in LF. The nonempty display name must decode to at most127 UTF16
code units and contain no ASCII control characters. Metadata is bounded to1024
bytes. Actual payload files are retained through read-only native handles.
They are not decoded or certified as valid game progress by this metadata scan.
Subdirectories within data, reparse points, hard-linked files, malformed names,
missing/empty data directories and invalid metadata reject the whole snapshot.
The title directory is bounded to4096 saves, each data directory to4096 files.

The scanner does not create directories, metadata, payload or campaign progress.
An absent catalog produces zero records from the real filesystem. A future save
writer must publish complete data before metadata, and must separately implement
the original game's payload access and durable commit behavior. No writer, load
service, mount, console storage object or STFS save conversion is supplied here.

Record fields are the existing original enumeration ABI: device1,type1,
big-endian UTF16 display name, ASCII directory name and zero padding. Native
Windows handles own the snapshot, and the existing paging/close path is reused.
Original8285C418 reads the first record;8285C530 reads subsequent records.
Both close an exhausted scan through original82432CC8 and set owner+32C to-1.
Original return12 denotes their exhausted scan path; populated records return11,
and an inactive selected player returns13. Those decisions remain original code.

The ABI cross-check is the local primary implementation
K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_content.cpp,288..369, plus original
instruction spans above. Its dummy devices/default account policy is not used.
The native directory format and device mapping are explicit portable policies.

Evidence SHA256: original flat image
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0;
local xam_content.cpp reference
dd3e2fa9b4b1a9ea9c4e46cf97884fca3ebbad3c69141853f68d960f6df877bf.
Original .pdata spans:8285CC78/124 bytes
faa027c6622451c58fd16f965b6046b1b33b81a34df9f0f7cfac05eac65fd5f1;
8285C418/118 bytes
804bd8fde2ac65557fc61d6ec7561501d393ed31ef7792ea8d877fcbe8f189b7;
8285C530/F0 bytes
b0f3f17c6964f22db1ed6cde7cd496e69d31598252abd455ef595d131ca133f1.

The existing ContentEnumerationTests target now covers
real empty/populated native save directories, UTF8/UTF16, owner/title/device
isolation, invalid metadata, missing data, real sharing/rename leases and hard
links. It executes the actual original save-owner constructor, request, next
record and paired close, including sign-out behavior. Populated files are test
fixtures only; no production save or successful load is claimed.

AOT regeneration reports252 unsupported imports,311 generated files and zero
semantic diagnostics. The focused build completed and OriginalContentEnumeration
passed in4.96s:build/native-save-catalog-tests.log. This includes original
marketplace regression coverage plus the new native save catalog and consumers.
The complete integration build passed125/125 tests in232.68s:
build/native-save-catalog-integration-build.log. Boot210 live acceptance used
the same real Player profile/content store. After START and the original storage
warning's A, the game requested device0/type1 for slot0. The actual folder was
absent; enumerator200 returned synchronous status18/count0 and the original
scan advanced to XamShowDeviceSelectorUI at8285CF64. No save was created and the
main menu was not reached. That selector remains explicitly unimplemented.

Native storage eligibility is the next backend component. inspectNativeStorage
owns the configured folder and its ancestors, requires an existing folder on a
fixed local volume, and reads actual caller-available, total and free bytes using
GetDiskFreeSpaceExW. Requested sizes are compared as64-bit values. No fabricated
capacity or console drive descriptor is involved. A random exclusive CREATE_NEW
temporary file proves actual write/flush access; FILE_FLAG_DELETE_ON_CLOSE
retires only that owned object, including on an exception. Existing files cannot
be replaced and no save or profile is created by this probe.

The returned native directory lease survives until its owner releases it. A UI
must recheck eligibility when accepting a choice; available-space observations
are snapshots and do not reserve disk capacity. The function does not select a
device, change any original state or report that payload saving is implemented.
Its focused ownership/capacity/probe-cleanup tests passed alongside the catalog
and original-consumer regressions in4.97s:
build/native-storage-eligibility-tests.log. The focused build completed in
build/native-storage-eligibility-build.log. This additional helper has not been
wired to any import and does not change Boot210's missing-selector boundary.
No second live launch was performed for a backend with no live caller.

The last full125/125 suite is the catalog integration build preceding this
eligibility-only addition. The current focused test additionally verifies actual
volume totals, full64-bit requested sizes, probe cleanup, unchanged existing
files, retained/released directory leases and rejection of absent folders,
files-as-roots and volume roots.
