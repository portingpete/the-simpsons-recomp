# Native declaration ownership contract

Status: bounded implementation complete and focused tests passed. Scope is immutable
CPU declaration ownership only. Input layouts, shader matching, binding, buffer
creation, guest hooks and engine readiness are outside this component.

## Original evidence

Flat image `analysis/simpsons.pe`, VA minus `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Read with existing `build/generator-ninja/SimpsonsDisasm.exe`; no regeneration.
Addresses and multi-byte element fields below are big-endian guest values.

`823EF838(r3=elements,r4=outputWord)` returns 1 iff the output resource is nonzero.
At `823EF84C..86C` it scans 12-byte records until **the word at +4 is FFFFFFFF**,
including that record in the count. At `823EF89C..8CC` it compares every word of
all records, including the terminator and byte +11. Cache globals are count
`82D0CB28`, capacity `82D0CB2C`, array `82D0CB30`; entries are 12 bytes:
`{resource, countIncludingTerminator, ownedCopiedRecords}`. Capacity grows by 16
through E+108/E+110. Creation `823EF94C -> 824458E0` precedes copying 12*count
bytes at `823EF9B0`. An active hit calls AddRef `82441690` at `823EF9E0`; an
inactive cached descriptor is recreated at `823EF9EC`. A cache entry itself
does not keep an extra active resource reference.

`823EFA18(r3=resource)` searches by resource, calls Release `82441708` at
`823EFA74`, and zeros only entry.resource when the result is zero. The copied
records stay cached. An unknown resource does nothing in the original; r3 is
not a reliable Boolean/status return on all paths. Native misuse may fail
explicitly instead. AddRef/Release update SDK object+4 atomically; the native
service needs logical references, not a reproduction of that SDK object.

The SDK creator `824458FC..591C` instead scans **BE16[element+0] == 00FF**.
Both sentinels must agree for a bounded safe native input. This is not the
desktop 8-byte D3DVERTEXELEMENT9 layout or a FFFF stream terminator.

The observed 12-byte structure is:

- +0: BE16 stream (0 in every declaration in this scope).
- +2: BE16 byte offset.
- +4: BE32 packed type descriptor; FFFFFFFF terminates the engine scan.
- +8/+9/+A: one byte each, method/usage/usage index; method and index are 0 here.
- +B: opaque trailing byte. Static screen arrays contain 0; stack constructors
  do **not** write this byte. Preserve it for the original engine's exact dedup.

These role names do not establish HLSL semantic names or native input layout
fields. SDK comparison `82445ACC..AEC` compares only the first eleven bytes;
its hash `82445B24..88` reads +0/+2/+4/+8/+9/+A. This corroborates that +B is
outside the meaningful element fields. Engine dedup nevertheless compares it.
SDK initializer `82445868/884` stores initial refcount 1; `824458AC..D0` copies
all three words per data element, excluding the terminal record from SDK storage.
The engine cache includes the terminal record; our owned bytes do likewise.

## Static screen arrays: exact original bytes

`82151724` (textured, 36 bytes):

```text
00000000 002C23A5 00000000
00000008 002C23A5 00050000
00FF0000 FFFFFFFF 00000000
```

`82151748` (flat, 24 bytes):

```text
00000000 002C23A5 00000000
00FF0000 FFFFFFFF 00000000
```

These are raw BE words, not serialized native structs. The stream/offset pairs
are (0,0)/(0,8); usage bytes are 0 and 5. Their type low six bits are 37, matching
`k_32_32_FLOAT` in read-only
`K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:595`.

## Stack-built declarations and owner fields

Notation below: `(stream,offset,type,method,usage,index,opaqueByte)`; `??` is
unwritten stack data, not a license to substitute zero in runtime input.
Every terminal record is `(00FF,0,FFFFFFFF,0,0,0,??)`.

- Scratch `82409A90`, call `82409BB8`, r3=r1+50, r4=`82D101D8`:
  `(0,0,001A23A6,0,0,0,??)`, `(0,10,00182886,0,A,0,??)`,
  `(0,14,002C23A5,0,5,0,??)`, terminator. Offsets are hexadecimal.
  Stores `82409B20..BB4` prove all meaningful bytes. Resource creation is
  conditional on the scratch index buffer succeeding. The function returns 1
  after this call regardless of declaration success: a native port must report
  creation failure explicitly, not reproduce accidental false readiness.
- Pipeline `82416C58`, first call `82416D6C`, output `82D507F4`:
  `(0,0,002A23B9,0,0,0,??)`, `(0,C,002A23B9,0,3,0,??)`,
  `(0,18,00182886,0,A,0,??)`, `(0,1C,002C23A5,0,5,0,??)`, terminator.
- Second call `82416DDC`, output `82D507F8`:
  `(0,0,002A23B9,0,0,0,??)`, `(0,C,00182886,0,A,0,??)`,
  `(0,10,002C23A5,0,5,0,??)`, terminator.
- Third call `82416E30`, output `82D507FC`:
  `(0,0,002A23B9,0,0,0,??)`, `(0,C,00182886,0,A,0,??)`, terminator.

Pipeline stores are `82416CB0..D68`, `82416D70..DD8`, `82416DE0..E2C`.
They reuse the same stack array: untouched +B bytes remain the same per slot.
The pipeline function also ignores declaration failures after its index buffer
succeeds. Native ownership must not adopt this failure-handling defect.

Scratch cleanup `82408E30` releases declaration `B+14` through `823EFA18` and
zeros it, then releases optional vertex and index resources via SDK Release.
Pipeline cleanup `82416BC8` releases its index buffer, then declarations in
FC,F8,F4 order, clearing each. Replace these mixed cleanups before native IDs
can reach them. No native declaration token may be passed to an SDK service.

Scratch field ledger, B=`82D101C4`: initialization always stores B+00=0 and
B+0C=0. Only when caps+1C bit 10000 is clear does it attempt a 40000-byte vertex
buffer, storing B+08; successful allocation then stores B+00=1 and B+04=0.
On the observed capability path the optional allocation and B+04/+08 writes
are skipped. Index buffer is 4E20 bytes at B+10, declaration output B+14.
Thus preserving B+04/+08 differs from blindly clearing the entire block.
Pipeline initialization stores `82D507EC=0` and owns the separate 1FFFE-byte
index at `82D507F0`; that buffer is outside the previously delivered startup
bundle. The declaration service does not allocate or initialize either buffer.

The screen arrays take a different ownership route: `827521C4/21D8` call SDK
creator `824458E0` directly, storing outputs at S+110/+114, where S=`82DFEA20`
(`82752114/21C` constructs this base). `827522EC/22FC` call SDK Release directly
and `827522F8/2314` clear those fields. Replacing only `823EF838/823EFA18` does
not cover screen declarations. A future screen owner must replace these paired
operations at its engine boundary; this component supplies validated data,
not a global SDK hook or fake object.

## Packed descriptors and supported scope

Only these exact four original type words are accepted:

- `001A23A6`: low six bits 38, `k_32_32_32_32_FLOAT`, source extent 16 bytes.
- `002A23B9`: low six bits 57, `k_32_32_32_FLOAT`, source extent 12 bytes.
- `002C23A5`: low six bits 37, `k_32_32_FLOAT`, source extent 8 bytes.
- `00182886`: low six bits 6, `k_8_8_8_8`, source extent 4 bytes.

Reference: read-only format enum at
`K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:595`, SHA256
`7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227`.
No renderer, fetch implementation, packet processor or emulator code was copied.
Original builders' following offsets corroborate these source extents. Other
descriptor bits are retained verbatim, not interpreted as DXGI, normalization,
endian or swizzle policy. The byte color field alone does not establish native
RGBA/BGRA handling. Full descriptor interpretation and shader-specific layout
creation remain explicit work before binding. CPU ownership needs neither.

Validation accepts stream=0, method=0, usageIndex=0 and usage bytes 0/3/5/A,
as observed here, without duplicate usage/index. Unknown full type words are
unsupported even if their low six bits match. Source offsets must be 4-byte
aligned and offset+extent <=10000 hex. No extra restrictions on gaps, order or
overlap are inferred. Terminal stream/type must be 00FF/FFFFFFFF together,
terminal offset/method/usage/index zero, and its opaque byte may have any value.
An exact span includes one final terminal record; empty declarations, trailing
bytes, early/mismatched terminators and partial records fail explicitly.

The maximum of 64 data elements, 1024 cached records by default, aligned source
range and supported field subset are native safety/support policies, not
claims of the original SDK's complete limits. The four distinct supported usage
values currently further constrain a valid declaration to at most four data
elements. Other original builders exist outside this scope; they must receive
explicit unsupported failures if they use other forms. This is not complete
game declaration or vertex semantic support.

`minimumStreamBytes()` is the largest offset+extent, not a recovered stride.
For scratch, pipeline F4/F8/FC and screen flat/textured, these are respectively
28, 36/24/16, and 8/16 bytes (decimal). No vertex-data conversion/upload occurs.

## Native API and integration contract

`renderer/declaration_resources.h/.cpp` expose
`Simpsons::Graphics::DeclarationRegistry`:

```cpp
DeclarationRegistry declarations;
DeclarationId id = declarations.create(exactOriginalByteSpan);
auto immutable = declarations.record(id); // shared_ptr<const DeclarationRecord>
declarations.retain(id);
declarations.release(id);
declarations.release(id); // ID now invalid; descriptor remains cached.
declarations.reset();     // Drop cache and invalidate all remaining IDs.
```

`DeclarationId {uint64_t generation; uint32_t slot;}` is host-only. Do not truncate
it into a guest word, treat the slot as a pointer, or send it to original SDK
calls. A parent-owned bridge must hold complete IDs beside engine owner fields
and preserve guest output/return behavior only after all consumers are handled.
This change does not write `CB28/CB2C/CB30`, scratch, pipeline or screen fields.
Guest allocator/pool accounting and hook frames stay with runtime integration;
the original SDK cache/stop loop must not process native IDs.

Exact bytes deduplicate against active and inactive cache entries, including all
opaque bytes and the terminal record. Active hits retain the same ID. Final
release invalidates the ID and leaves immutable copied metadata cached, as the
original retains its copied records. Recreation reuses those bytes with a new
generation and one logical reference. Generations are process-wide across
registry instances, never reset/reused, and fail explicitly before overflow.
Foreign, zero, stale and double-released IDs fail. Native refcount limit is
configurable and checked before increments. Unknown release is stricter than
the original no-op; surface misuse as an integration error, not guest success.

Record snapshots can survive logical final release, reset or registry destruction.
They do not keep an engine reference or make an old ID valid. Deferred work
requiring an active resource must retain its ID separately. Records expose const
spans and cannot be copied/mutated through the API. A supplied
`std::pmr::memory_resource` must outlive the registry and all snapshots; the
default allocator has process lifetime.

Calls are serialized by the owner. Atomic generation issuance prevents collisions
between separate registries; it does not make one registry concurrent. Input
bytes must stay stable throughout create. Validation and new-record allocations
precede publication, so exceptions leave prior IDs/refcounts/cache unchanged.
No device creation or D3D-context thread affinity is required.

Parent's proposed first-start integration preserves original AOT `82409A90`,
including stack +B, and replaces these exact mixed call sites:

- `82409B0C`, word `48037EFD`, BL `82441A08`: real owned scratch index buffer;
  r3 receives the bridge's native owner ID, continuation `82409B10` retains
  the original store to B+10.
- Engine create/release `823EF838/823EFA18`: checked reads of whole 12-byte
  records, bounded by MaxElements+1, both sentinels validated, complete native
  DeclarationId mapped by checked monotonic 32-bit engine IDs below mapped
  memory. The bridge token is not the truncated DeclarationId or an SDK object.
- `82408E90`, word `48038879`, BL `82441708`: release only the mapped native
  scratch index owner, continuation `82408E94`; original cleanup clears B+10.
  Original `82408E54 -> 823EFA18` releases declaration, then clears B+14.

This is compatible with the reviewed first-start/capability path provided native
IDs never reach SDK consumers and the mapping covers deduplicated logical
references. An active dedup hit must return the same bridge ID, preserving
original resource identity, and increment its logical ownership. Native create
failures must throw: original scratch returns 1 after
the declaration call regardless of r3. If ID-map publication or guest output
write fails after registry.create, release that newly acquired reference. Use
the actual checked callback frame and guest bytes; no guessed terminator/padding.
For rollback, keep owner mappings valid until original cleanup has consumed
them, then invalidate/reset the registry. After final release a recreate must
receive a fresh bridge ID. Unsupported optional-VB and later render paths remain
explicitly guarded. No changes to CAF8 or driver readiness are authorized by
declaration creation. Parent owns/testing these hooks; this component does not.

## Tests and parent integration

Focused standalone ClangCL C++20 compilation used `/W4 /WX /EHsc /MD`, with no
warnings. Only the two new C++ files were compiled, with output under TEMP.
`DeclarationResourcesTests K:/SimpsonsNativeCopy/analysis/simpsons.pe` passed.
Tests require no GPU, generated AOT, game execution, shader headers or runtime.
The parent build/generation was not run.

Tests cover all six layouts; exact BE decoding and copied input lifetime;
byte-for-byte dedup including +B/terminal bytes; logical retain/release and
inactive cache recreation; foreign/stale/reset IDs; 1000 sequential lifetimes;
snapshots surviving reset/destruction; reference/cache limits; malformed and
unsupported records; exact source-range boundary; and real PMR allocation
failures during first creation and insertion beside an existing record, with
unchanged prior state, leak accounting and successful retries. PMR injection
covers data/cache allocations, not global operator-new allocation of the shared
record/control block; those use standard exception-safe C++ ownership.

With the image argument, tests compare both raw screen arrays and the SDK
terminal constant at `8206A310`, reconstruct original scratch/three pipeline
stack arrays from fixed straight-line constant/store windows, prove +B unwritten,
and check exact ABI evidence words. This evaluator is test-only static evidence:
no guest branches, calls or loads execute. Image length and selected evidence
bytes are checked; complete image SHA256 is independently pinned above and
remains the loader/parent's responsibility. Omitting the image runs ownership
tests only and explicitly reports that original evidence was not requested.

Suggested parent CMake additions (not applied here):

```cmake
target_sources(SimpsonsGraphics PRIVATE renderer/declaration_resources.cpp)
add_executable(DeclarationResourcesTests tests/test_declaration_resources.cpp)
target_link_libraries(DeclarationResourcesTests PRIVATE SimpsonsGraphics)
add_test(NAME NativeDeclarationOwnership COMMAND DeclarationResourcesTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
```

No runtime, old bundle, CMake, material-owner, generated or reference-project
files were edited. These four assigned files are the entire change; frozen.
