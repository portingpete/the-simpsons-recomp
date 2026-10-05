# Native ANSI string descriptor initialization

Build135 passes38/38 CTest suites in53.12 seconds. Actual muted boot078 passes
the reached `RtlInitAnsiString` call and continues until a distinct configuration
query: `ExGetXConfigSetting`, category3/setting9, caller82432D3C. Successful
external game-file reads are not established by this run.

`runtime/strings.cpp` implements the actual counted-string operation, with an
eight-byte original descriptor: BE16 Length, BE16 MaximumLength, BE32 borrowed
source pointer. It creates no host string allocation or NT handle. A null source
produces an empty null descriptor; a nonnull source counts bytes to NUL and
retains its pointer. It performs no encoding conversion, source copy or free.
The void ABI preserves the incoming CPU context.

The native policy follows Windows' documented `RtlInitAnsiString` count and
saturation behavior: Length excludes NUL, MaximumLength includes it, and lengths
at or above65534 saturate to65534/65535. The source remains caller-owned.
[Microsoft's contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlinitansistring).
The implementation stops counting at the saturation threshold, because later
bytes cannot change the descriptor. It does not claim Windows' exact fault
behavior for invalid, unterminated sources beyond that prefix. Unmapped input
encountered before saturation remains an explicit failure.

The full destination is preflighted before reading/counting source bytes. Count
and address checks finish before descriptor publication. Checked memory accesses
retain runtime cancellation and data-import guards. Source address arithmetic
cannot wrap the32-bit guest address space. Invalid descriptor/source preflight
does not partially initialize the descriptor; concurrent terminal cancellation
is not a rollback guarantee.

## Original boundary

Boot077 failed on worker92284 at LR82B750C0: r3=022BF888, r4=022BF930.
Original82B750B4 passes the saved source in r4; B8 passes SP+68 in r3; BC calls
thunk82CC2924. At C0 the original body reads the descriptor's BE16 length, then
uses the source plus length to inspect its last byte. At82B75128/138 it publishes
the descriptor pointer into the original object attributes for file creation.
`python -B tools/verify_ansi_contract.py` verifies eight original instruction
words and the exact branch against the pinned original image; the report is
`analysis/native-ansi-contract.json`. The import's original name is established
by the executable import mapping and reached failure log.

## Verification

`NativeAnsiStringTests` runs85 checks using the actual Windows ntdll
`RtlInitAnsiString` as the length/pointer oracle. Ten byte lengths cover empty,
short, page boundaries,65533/65534/65535 and65536. Other cases verify null input,
high-bit bytes, embedded NUL, terminator at the last mapped byte, exact descriptor
footprint, borrowed-storage mutation, readonly/unmapped/overflowing inputs,
invalid-input output preservation, whole CPU context and host FP state.
Original pointers remain32-bit even though the host oracle has a64-bit pointer.

Logs: `build/hundred-thirty-fifth-build.log`,
`build/native-ansi-verification-135.log`, `build/boot-078.log`.
Executable SHA256:
`e1dfe69e53765474d8b208a2eb725e8290e08a29235081500c47652859b8756c`.
The existing native filesystem suite also passes; full asynchronous asset
streaming, all file-open modes and reached configuration remain separate work.
