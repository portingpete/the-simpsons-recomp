# Native save store and original file bridge

The native backend owns ordinary Windows files for the actual active native
profile and title45410809. The game supplies the content name, display name and
payload bytes. It does not create game progress or use an emulated console
content container. Live acceptance is pending after Boot215 reached
XamContentCreateEx at82432508, flags12 and root rmcsave.

## Persistence

New writes live in
`<content>/save-data/<profile GUID>/<title>/<name>/<random generation>/`.
Completed saves are published by a checksummed UTF8 version2 index at
`<content>/save-index/<profile GUID>/<title>/<name>.save`. The index records the
full identity, display name and generation. Publication flushes actual payload
files, writes and flushes a unique staging file, then atomically renames its
owned handle over the index and flushes again. A failed rename preserves the
previous index. Old payload generations are retained; garbage collection is
not implemented. Abandoned new generations remain unpublished.

An exclusive native lock file serializes each profile/title/name across store
instances. Held directory and file handles reject reparse points, hard links,
unexpected types and path movement. Flat names are bounded and reject traversal
and Windows reserved names. A catalog snapshot holds the index and actual data
files against write/delete until released. Empty containers and sessions with
outstanding file owners cannot publish. An existing session forks actual bytes
into a new generation before writing. Version1 native directory saves remain
readable and migrate through copy-on-write without replacing their old data.

## Original ABI and file ownership

`runtime/save_bridge.cpp` connects synchronous XamContentCreateEx,
XamContentGetCreator, XamContentFlush and XamContentClose to the real store.
Only the observed rmcsave alias, local device1, save type1, zero cache/size and
the original bounded creation modes are qualified. Creator queries use the
actual native GUID owner and native equality key. It is not an Xbox credential.
Original wrappers are824327D0/824324B8(create),82432550(creator),
82432538(flush) and82432530(close).82432528 is Install, not Flush.

Original-image SHA256 spans (address/byte count):

-824324B8/68: a708b1e4b0fdc0220ee1f932e2f5a0899fa875831fd4a35792b04bc10c55d5d0
-824327D0/48: 078159abe8432cf7fed413f4c84e377a302afc303306e644bf08b4a4afec67a7
-82432530/28: a24d42ecf0cffaef5bcba65cf724cbde255433989131d308b07114bc0b16f69c
-82B75030/1F4: 016eaf923bceaf7da6c58ea939e7eb0f13f91e10af3c2b7a70a9de50501ba7fb
-82B74E50/124: a8800436adb3be85ea9414d52e27033d97df30c149fc38844bede2b2d8e91e1c

`runtime/filesystem.cpp` routes only the mounted rmcsave path to writable native
files. The save object owns its real handle through the kernel handle table;
NtClose releases it once. NtWriteFile, NtFlushBuffersFile and writable save
allocation/end-of-file updates call Windows on that object. Asset opens retain
their read-only access and creation rules. Unsupported asynchronous requests
remain explicit failures. Native file errors retain their corresponding status.

## Verification to date

`build/native-save-store-verified-tests.log` passes NativeSaveStore and
OriginalContentEnumeration (2/2,5.35s). Coverage includes actual create/read/
update/flush/close, alias and identity ownership, unclosed-file rejection,
failed replacement retaining old data, catalog leases, checksums, reopening
after a new store instance and version1 migration. Payloads are temporary test
fixtures only. The live profile and content folder were not changed by tests.

The bridge build completed, and `build/native-save-bridge-tests.log` passes
4/4 in5.56s: OriginalNativeSaveBridge, NativeSaveStore,
OriginalContentEnumeration and OriginalAssetFilesystem. The bridge fixture
executes the actual original create/creator/flush/close wrappers, writes and
reopens binary bytes through the native file imports, checks creator CPU/FP/
last-error preservation and verifies handle/stack/profile ownership. Regeneration
in `build/native-save-bridge-regenerate.log` reports242 explicit unsupported
imports,311 generated files and zero semantic diagnostics. The last full suite
before this save addition was128/128 in235.51s. The new full integration build
passes130/130 in239.93s: `build/native-save-integration-build.log`.
Boot216 accepted the bridge live. The actual game requested SIMPSONS_SLOT1,
display `0:00.00 (0%) The Land of Chocolate`, flags12. It wrote28,8 and114764
bytes through file handle20C, then original close published the index and
released rmcsave. The actual payload is114800 bytes, SHA256
4cb60f9428e5d5de6959f0565c67c79bbed260e81c71834062bf4dd8082bcf01,
under generation4185aac0de3dbaacbf717c968013ae08. The224-byte published index
has SHA256939bb381e59f7e587296d459d5e0f1820c46ffe1d70e2cfd8de035e83bed3473.
This is original game output, not a test fixture or injected progress. The
opening level movie is visible in
`build/captures/native-loading-216-after-save/native-frame-1067980.png`.
The run subsequently stopped on the unchanged first character-shadow activation
guard at826B5FC0/caller8270715C,technique0007FFFC. It exited1 naturally. The
completed save is preserved; Boot217 is checking its normal existing-save route.
Live reopening and main-menu arrival remain unverified.
