# Native execution identity from the original loaded header

Boot212 reached XamGetExecutionId at82433C1C in original82433BF0. Its output
pointer was0203F110 and requested title45410809. The original function permits
title0, otherwise compares the high16 publisher bits with the execution
record's title at+12. A mismatch returns1627; this original check is retained.

Runtime::load already hashes the actual derived simpsons.unencrypted.xex and
copies its header into owned native image memory at01010000. The new import
uses the existing optional-header parser for key00040006 and writes its actual
record pointer010119C0 to the caller's four-byte output. There is no allocation,
replacement execution structure or change to the original title identity.
The source record at file offset19C0 is exactly:

    056310420000000100000001454108090000010100000000

The import preflights output alignment/writability and the complete loaded
header span, rejects header/output aliasing and missing or truncated metadata,
and checks runtime cancellation. Successful execution changes only r3 to0;
other CPU state, host floating-point controls and host last-error are preserved.

Local primary ABI reference:
K:/Simpsons/RexGlueCurrent/src/kernel/xam/xam_info.cpp, XamGetExecutionId_entry,
SHA256 cd24f41f6b1b83d10c2cf9cd96b5d116df2f8fbbdab047b135edcf1b1a4c054b.
It corroborates pointer-to-original-optional-header return semantics. No
reference runtime is linked. Original82433BF0/68-byte span SHA256
c6946de4e437052d6d90f46b61523f2ae0fcac496e7ce195c9f6a2a2fe431e41 in the
verified flat original image.

OriginalExecutionIdentity independently reads and checks the original source
record, confirms pointer identity with the existing RtlImageXexHeaderField,
executes original82433BF0 with zero/current/same-publisher/foreign-publisher
inputs, and checks five FP modes, complete CPU state, four-byte output bounds,
unchanged header and native ownership, cancellation and malformed requests.
One initial test fixture mistyped the version bytes; it was corrected from
the actual source before tests. Production code always used original bytes.

AOT regeneration:249 explicit unsupported imports,311 files,0 diagnostics.
Focused build succeeded. OriginalExecutionIdentity, NativeMemoryContract and
NativeConfiguration passed3/3 in0.25s, build/native-execution-identity-tests.log.
The previous full suite passed126/126 before storage-data and this addition.
Boot213 accepted the query at log line2882615: header01010000, identity010119C0,
title45410809. Original publisher validation completed and the game requested
XamUserReadProfileSettings at82C71D14. That next import was unimplemented. The
run exited1 naturally. Main-menu acceptance is not yet verified.

The subsequent full integration build passes128/128 in235.51s:
build/native-profile-storage-integration-build.log.
