# Original asset filesystem: bounded read-only service

Implemented in `runtime/filesystem.cpp` / `.h`. This service opens existing assets
under the parent's canonical `Runtime::gameRoot`. It does not mount saves or
emulate console storage devices. Originals and external reference projects were
not modified. All host writes in the tests are confined to a newly created temp
tree; implementation file handles never receive write/delete access.

## Verified guest ABI

Arguments are 32-bit guest values in r3 onward. Values in guest structures are
big-endian; native Windows structures are never overlaid onto guest memory.

- `NtCreateFile`: r3 handle-output pointer, r4 desired access, r5 object attributes,
  r6 IOSB, r7 optional allocation-size pointer (BE64), r8 file attributes,
  r9 share access, r10 disposition, **BE32 at r1+0x54** create options.
- `NtOpenFile`: r3 output, r4 access, r5 attributes, r6 IOSB, **r7 share access,
  r8 open options**. This is a six-argument ABI. The inspected RexGlue wrapper
  incorrectly declares only five arguments; it is not copied here.
- `NtReadFile`: r3 handle, r4 event, r5 APC routine, r6 APC context, r7 IOSB,
  r8 buffer, r9 byte count, r10 optional BE64 byte-offset pointer.
- `NtQueryInformationFile` / `NtSetInformationFile`: r3 handle, r4 IOSB,
  r5 information buffer, r6 length, r7 information class.
- `NtQueryVolumeInformationFile`: r3 handle, r4 IOSB, r5 volume-information
  buffer, r6 length, r7 filesystem-information class (a separate class namespace).
- Object attributes: 12 bytes, root handle at +0, PANSI_STRING at +4,
  attributes at +8. Accepted attributes are zero or OBJ_CASE_INSENSITIVE (0x40).
  The +0xC comment in Rex's struct is inconsistent with its actual layout.
- ANSI string: 8 bytes, BE16 length at +0, BE16 maximum length at +2, BE32 data
  pointer at +4. Read exactly Length bytes, without requiring a trailing NUL.
  Reject zero length, Length > MaximumLength, embedded NUL/control/non-ASCII
  bytes, and lengths above the explicit 4096-byte service bound.
- IOSB: 8 bytes, BE32 NTSTATUS at +0 and BE32 information at +4. It is optional,
  following the inspected wrappers. r3 returns the same NTSTATUS. Failed opens
  leave handle output unchanged; successful opens report FILE_OPENED (1).

Every dereferenced guest extent is validated with PPCGuestPointer before native
I/O or handle publication. Structure/stack offsets use 64-bit bounds arithmetic
so a wrapped r1+0x54 cannot become a valid low guest address. Invalid extents
return STATUS_ACCESS_VIOLATION; a valid IOSB receives that failure. Query buffers
validate/write only the documented result extent, leaving extra bytes untouched.
If PPCGuestPointer throws while Runtime::stopping is set (including window-close
shutdown), preserve the original Failure instead of converting it to a guest
access violation. This lets the parent's shutdown unwind escape guest retries.

Evidence, all inspected read-only:

- `K:\Simpsons\SimpsonsRexProject\generated\default\simpsonsgame_recomp.178.cpp`:
  original call at `0x82B74B84` sets r7=3 and r8=0x00800021 before NtOpenFile.
  Object attributes are constructed at stack +104/+108/+112. Call at
  `0x82B75F0C` stores the ninth NtCreateFile argument at stack +84 (0x54).
  `0x82B74C5C` queries class 14 with length 8; adjacent original wrappers set
  the same position class. `simpsonsgame_recomp.37.cpp`, call at `0x82432798`,
  independently stores NtCreateFile options at r1+84.
- `...\generated\default\simpsonsgame_init.cpp`: imports NtOpenFile
  `0x82CC2934`, NtCreateFile `0x82CC2944`, NtSetInformationFile `0x82CC2B04`,
  NtQueryInformationFile `0x82CC2B44`, NtQueryVolumeInformationFile `0x82CC2B54`,
  NtReadFile `0x82CC2B84`, NtWriteFile `0x82CC35E4`.
- `K:\Simpsons\RexGlueCurrent\include\rex\ppc\function.h:129`: stack argument
  extraction at r1+0x54 for argument index 8.
- `K:\Simpsons\RexGlueCurrent\include\rex\system\xio.h`: counted strings,
  IOSB and object-attribute layout. `include\rex\system\info\file.h`: query
  class numbers and exact result layouts.
- `K:\Simpsons\RexGlueCurrent\src\kernel\xboxkrnl\xboxkrnl_io.cpp:312` and
  `:407`: create/read parameter order; `xboxkrnl_io_info.cpp:153` and `:298`:
  query/set order, sizes and IOSB completion. `src\system\xfile.cpp:129`:
  -1 read offset uses current position.
- `K:\Simpsons\RexGlueCurrent\src\system\runtime.cpp:267`: original game root
  mounted at `\Device\Harddisk0\Partition1`, with `game:` and `d:` aliases.

## Path and original-preservation rules

Recognized aliases are case-insensitive `game:`, `d:`, and the exact device
prefix `\Device\Harddisk0\Partition1`, each followed by a separator or end of
name. `/` is accepted as a separator. Root handle 0 or `0xFFFFFFFD` (ObDosDevices)
allows these names or a bare path relative to gameRoot. A service-owned directory
handle permits a relative child path. Host working-directory resolution is never
used. Other aliases, including save:, update:, Cdrom0, `\??\`, host drives and
UNC names, are rejected and logged; none are guessed.

Reject `.`/`..` components, internal empty components, alternate streams/colons,
wildcards, trailing dots/spaces, and file/directory mismatches. Existing paths
are resolved case-insensitively by native NT OBJ_CASE_INSENSITIVE lookup.

Open the configured root with OPEN_REPARSE_POINT and compare its opened final
path to the configured canonical path. Walk one component per NtCreateFile,
relative to a held parent HANDLE, always with FILE_OPEN_REPARSE_POINT. Check
the opened object's type/reparse attributes before using it. **All reparse points
are denied**, even ones targeting another location inside the root. Final paths
must remain under the root with a separator boundary. Parent handles deny delete
sharing and remain pinned in the file's shared lifetime, blocking ancestor rename
races. File handles deny write/delete sharing; guest share bits allowing those
operations are deliberately narrowed by this read-only policy. This may produce
real sharing failures when a host writer already has an original open.

The runtime handle table remains authoritative. A weak ownership registry rejects
foreign Type::File objects. No new Runtime members are required. A shared derived
KernelHandle retains ancestor pins; close and in-flight shared references release
resources normally, without changes to the parent's close implementation.

Only FILE_OPEN (1) is accepted. Create, open-if, overwrite, supersede,
delete-on-close, write/append/EA-write/attribute-write/delete/security-write and
MAXIMUM_ALLOWED requests fail. FILE_OPEN does not apply FileAttributes or resize
to AllocationSize. Internal FILE_READ_ATTRIBUTES supports containment/type checks;
it never grants missing FILE_READ_DATA access. NtWriteFile and NtDeleteFile
explicitly fail. NtSetInformationFile permits only changing the handle position;
rename/link/disposition/size/allocation/basic-attribute/completion changes fail.

## Synchronous I/O and information contracts

Require FILE_SYNCHRONOUS_IO_NONALERT (0x20), plus SYNCHRONIZE or an accepted
generic read/execute mask. Supported optional create bits are DIRECTORY_FILE
(1), NON_DIRECTORY_FILE (0x40), SEQUENTIAL_ONLY (4), NO_INTERMEDIATE_BUFFERING (8), RANDOM_ACCESS (0x800), and
OPEN_FOR_FREE_SPACE_QUERY (0x00800000), as detailed below. Asynchronous/alertable
opens, backup intent and unknown options fail explicitly.

The original frontend loader at82B75030 submits options0x68 through the indirect
NtCreateFile call at82B75190. Bit8 passes unchanged to the actual native handle.
Windows enforces its volume's sector-multiple offsets/lengths and device buffer
alignment, including seek failures. Reads use checked guest memory directly;
there is no cached reopen or bounce buffer. Actual synchronous completion,
partial EOF bytes/count and NT errors are returned. Different host device
alignment from the console remains a portability constraint. The fixture tests
both open ABIs, queries the actual handle's mode/alignment independently, checks
bytes and native failures, and proves original content/write time unchanged.
See Microsoft's [NtCreateFile contract](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile)
and [NtReadFile contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile).

NtReadFile requires event, APC routine and APC context all zero. Native
nonalertable NtReadFile supplies actual bytes, partial completion, EOF status and
file-pointer advancement. NULL offset, Xbox -1, and NT -2 select current position;
other negative offsets fail. Explicit nonnegative offsets perform native atomic
seek-and-read. Zero-length reads are passed to the native operation. Never report
pending or queue a fictitious completion. Operations are serialized by the
filesystem mutex; cancellation of an in-progress native disk operation is not
implemented. Runtime stop/thread policy remains with the parent.

Queries supported:

- Class 5, 24 bytes: allocation size BE64 +0, EOF BE64 +8, link count BE32 +16,
  delete-pending byte +20, directory byte +21, zero reserved +22/+23.
- Class 14, 8 bytes: current byte position BE64. Setting this class also requires
  8 bytes, rejects negative positions, and reports information=8. Seeking past
  EOF is allowed and does not extend the original.
- Class 34, 56 bytes: four native timestamps as BE64 +0/+8/+16/+24, allocation
  BE64 +32, EOF BE64 +40, attributes BE32 +48, zero pad +52.

Short result buffers fail STATUS_INFO_LENGTH_MISMATCH. Unsupported query classes
fail STATUS_INVALID_INFO_CLASS and log their class. Native NT failures are
preserved. Other Win32 failures are mapped to explicit failure statuses, never
success. Directory enumeration, other volume-information classes, scatter reads,
asynchronous completion, save files and additional aliases remain unsupported.

Native API basis: [NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile)
documents relative RootDirectory opens, sharing and OPEN_REPARSE_POINT;
[NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile)
documents synchronous current-position and explicit-offset behavior. The Xbox
layout and alias decisions above come from the local executable/runtime evidence,
not from assuming that every Windows syscall has the Xbox signature.

## Free-space query: verified intent and native contract

`K:\Simpsons\SimpsonsRexProject\generated\default\simpsonsgame_recomp.178.cpp`,
function `sub_82B74B28` (original PPC instruction comments inspected read-only):

- `0x82B74B84` calls NtOpenFile with access `0x00100001` (SYNCHRONIZE and
  FILE_LIST_DIRECTORY), share=3, options=`0x00800021`, root=`0xFFFFFFFD`, and
  attributes=0x40. The options combine DIRECTORY_FILE, SYNCHRONOUS_IO_NONALERT
  and OPEN_FOR_FREE_SPACE_QUERY.
- On success, `0x82B74BB0..0x82B74BC4` sets r7=3, r6=24, r5=stack+128,
  r4=stack+96, r3=the opened handle, then calls NtQueryVolumeInformationFile.
  `0x82B74BD0` closes that handle. This wrapper never reads/writes file data.
- `0x82B74BE8..0x82B74C28` loads sectors/unit at stack+144 and bytes/sector at
  +148, multiplies with `mullw` (low 32 bits), then multiplies total units at +128
  and available units at +136 with `mulld`. Optional original r4 and r6 outputs
  both receive available bytes; original r5 receives total bytes. Query failure
  is mapped to a guest error rather than constructing space values.
- `...\simpsonsgame_recomp.41.cpp`, caller `0x8246C13C`, passes a path buffer
  trimmed through its last backslash and requests available bytes. These
  observations establish a directory/volume-space probe, not asset byte I/O;
  they do not establish an additional device alias or a console disk capacity.

The SDK header
`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\winternl.h:1028`
defines the flag as 0x00800000. Microsoft documents that it captures the opening
thread's user for subsequent quota-sensitive volume queries. The implementation
passes it unchanged to the final NtCreateFile, including an empty-name root open;
it is not discarded or substituted with backup intent. See
[NtCreateFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile).

Class 3 (FileFsSizeInformation) requires 24 bytes and returns exactly:

- +0 BE64 total allocation units available to the relevant native user.
- +8 BE64 free allocation units available to that user.
- +16 BE32 sectors per allocation unit.
- +20 BE32 bytes per sector.

The Xbox layout/class is independently present in
`K:\Simpsons\RexGlueCurrent\include\rex\system\info\volume.h:20,44` (size assert
24), and the five-argument wrapper in `src\kernel\xboxkrnl\xboxkrnl_io_info.cpp:435`.
The native call uses the service-owned handle directly. It queries the actual
Windows volume containing that opened file/directory; no raw volume/device is
opened and no path is re-resolved. Both counts may be quota-limited. They are
allocation units, not bytes, asset-folder size, or fabricated console capacity.
Sector geometry is copied unchanged, with no fixed 512-byte sector assumption.
Ordinary service file/directory handles may also query class 3 without the flag;
NT then selects the user according to its normal rules. See
[FILE_FS_SIZE_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_fs_size_information).

Success requires a complete native result: NTSTATUS=0, IOSB.Status=0 and
IOSB.Information=24. Guest IOSB receives status=0 and information=24. Native
failures retain their NTSTATUS, report information=0 and leave the result buffer
untouched. Unexpected pending or incomplete native success throws an actionable
runtime Failure; it never reports fabricated completion. Short guest buffers
fail STATUS_INFO_LENGTH_MISMATCH, other classes fail STATUS_INVALID_INFO_CLASS.
Only the 24-byte result extent is checked/written, even for larger buffers.
Pointer faults and shutdown follow the same checked-memory policy as file I/O.
See [NtQueryVolumeInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvolumeinformationfile).

All existing alias, traversal, reparse, ownership, access/disposition and sharing
restrictions still apply with this flag. `game:`, `d:` and the verified
`\Device\Harddisk0\Partition1` select the canonical asset root; their query data
describes its real host volume. No save/cache/other device is mapped by this work,
and reporting available host space does not grant permission to write originals.
Host quota/account semantics are delegated to NT; Xbox user/quota virtualization
and other volume classes are outside this bounded service.

## Tests and parent integration

`tests/test_filesystem.cpp` runs against production filesystem.cpp and the actual
runtime/context declarations with a small test-only checked-memory/handle-table
harness. It does not link the game or parent runtime. Tests use a fresh temp root
and prove: six/nine-argument ABI, endian layouts, counted strings without NUL,
partial reads/EOF/zero reads, 64-bit positions, boundary/wrap/protection failures,
case/alias handling, foreign/stale handles, every denied mutation category,
file/directory mismatches, actual ancestor pinning/release, directory junctions
inside/outside the root, configured-root junction escapes and concurrent readers.
Shutdown fixtures stop before IOSB validation and during the later buffer check;
both preserve the exception reason, guest outputs and native file position.
Volume fixtures exercise the exact original options through both open imports,
all recognized aliases and ordinary file/directory handles. Guest class-3 fields
are compared with independent native queries bracketing the call (up to 32
attempts for a stable free-space sample), including original-wrapper unit-to-byte
conversion. Tests cover 24-byte/end-of-guest-memory bounds, short/oversized/
protected buffers, unsupported classes, native/foreign/closed-handle failures,
cancellation, unchanged file position, and write/traversal/reparse rejection with
the free-space flag. Cross-user impersonation and quota configuration were not
changed or tested; the native API preserves that behavior.
File-symlink tests run when Windows permits creation; both tested runs here
included them without skipping. Fixture contents and last-write time are checked
unchanged. Cleanup explicitly unlinks reparse points before removing the verified
owned temp tree.

Passed on Windows with GCC 15.2 and with the parent's ClangCL/MSVC environment.
Compilation and execution used a temporary copy of ppc_context.template.h plus
a minimal image-constant ppc_config.h; no generated/build outputs were changed.

Parent reports registration and CTest integration complete. Integration contract
(performed by the parent, not this task):

1. Add `"runtime/filesystem.cpp"` to `config/native_sources.json`, then use the
   normal strict regeneration/build so the implemented imports are bound.
2. Add the standalone test target below. Do **not** link SimpsonsRuntime: the
   test deliberately defines only the Runtime/NativeWindow/GuestThread functions
   needed by its harness, avoiding a parent/game rebuild for filesystem tests.

```cmake
add_executable(FilesystemTests tests/test_filesystem.cpp runtime/filesystem.cpp)
target_compile_features(FilesystemTests PRIVATE cxx_std_20)
target_compile_definitions(FilesystemTests PRIVATE
  SIMPSONS_FILESYSTEM_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(FilesystemTests PRIVATE
  "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
add_test(NAME OriginalAssetFilesystem COMMAND FilesystemTests)
```

After registration, the parent can run only this target/test:

```powershell
cmake --build <parent-build-directory> --target FilesystemTests
ctest --test-dir <parent-build-directory> -R '^OriginalAssetFilesystem$' --output-on-failure
```

Eight import definitions are supplied: NtCreateFile, NtOpenFile, NtReadFile,
NtQueryInformationFile, NtQueryVolumeInformationFile (class 3), NtSetInformationFile,
NtWriteFile (denied), NtDeleteFile (denied). The filesystem source is already
registered; the parent must regenerate to bind the newly defined volume import.
No runtime.h, main, CMake, generator, native-source registry or reference
project files were edited. Successful service fixtures do not demonstrate that
the original game has reached asset reads or rendered any pixels.
