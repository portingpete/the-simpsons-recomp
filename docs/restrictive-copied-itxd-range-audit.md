# Copied ITXD range audit

The full valid output range of the original offline ITXD serializer remains
unresolved. The copied-reader trace proves transport and ownership, but does
not establish the native decoder's minimum dimensions, power-of-two rule,
upper dimensions, pitch or packed-mip restrictions as original producer
bounds. No production decoder change is justified by this trace alone.

The existing independent [producer report tool](../tools/audit_texture_producer_ranges.py)
pins the immutable 15,466,496-byte image, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`,
complete source spans and annotated instruction words. Its ordinary SDK
requirements path `82B7F950` accepts compatible 2D formats within device caps
of 8,192 and applies its own normalization. Its descriptor constructor
`8243F928` / dimension reader `8243DED8` carry 13-bit dimensions. Those
functions establish an SDK contract; copied ITXD records do not pass through
them.

Normal named loading `826F26B0` forwards the declared resource payload and
sets copy mode zero before calling `826F24D8`. On an index miss, that walker
calls copy `82736F58`, relocation `82736EA8`, extension reset `82736D38`,
index publication `826F8AE8` and owner attachment `826F7618`. Copy allocates
exactly `100` hex bytes of metadata and copies them at `82736FF4`. It reads
the independent serialized pixel extent from extension `+10` at `82736FA4`,
passes that extent to allocator slot `+20` at `8273703C`, and copies precisely
those bytes at `8273704C`. It does not construct or normalize dimensions.

Relocation `82736EA8` adds the load base to pointer fields and passes the
descriptor to `82C20E80`. That routine changes the descriptor's base address
and, when the actual level count exceeds one, its mip address. Its call
`82C20EB0 -> 8243FE90` extracts the existing four-bit mip maximum and adds
one. Neither this relocation nor that reader validates an authored mip
domain. Field representability, including 1..16 encoded levels, is not proof
that all represented level/dimension/storage combinations are valid.

The current native decoder profiles remain explicit:

| Family | Native qualification | Producer authority still missing |
|---|---|---|
| L8 | Power-of-two 64..2,048; exact RRR1 tiled descriptor and mip allocation | Authored minimum, upper size, non-power-of-two layouts and packed storage domain |
| BC1/2/3 | Power-of-two 4..2,048; pitch multiple 128 and at most 2,048; exact mip allocation | Serialized partial block extents, padded pitches, upper sizes and mip combinations |
| RGBA8 | Power-of-two 32..1,024 plus the verified 16×16 base; exact pitch; levels through the first packed 16-texel level | Other small/upper extents, padded pitch and deeper shared tails |

The fresh `final-source-alias-chunk-textures-20261002.json` census contains
7,318 admitted metadata occurrences across 863 dictionary occurrences, with
709 unique dictionaries. It reports no shipped metadata counterexample to
these profiles. L8 stock dimensions are 64..512, RGBA8 16..1,024, and BC stock
dimensions fit the current profile. These are observations from shipped
assets, not producer maxima, pixel-decoding proof, native draw coverage or
complete owner retirement.

The original runtime's copy and relocation code is a reader, not the offline
serialization producer. No serializer output-domain proof was located in the
reviewed source paths. Qualifying a wider decoder requires genuine producer
output or the serializer itself, then independent original copy/publication,
binding, selected-level pixel sampling, unbinding and final allocator release,
with malformed descriptor and consumed-span cases. SDK caps alone cannot
substitute for that missing serialization proof.

The independent [loader/lifetime verifier](../tools/analyze_copied_itxd_lifetime_contract.py)
now pins 15 complete original function spans and 51 annotated instructions,
including both loader branches, group creation/owner attachment, last-owner
removal, group release, copied and borrowed destructors, copy/relocation and
descriptor address patching. Its source-only report at
`build/restrictive-check-audit/copied-itxd-lifetime-contract-20261002.json`
passes 2,056 independent span-byte mutation rejections and records 768 mode
cases. These checks establish immutable source semantics; they grant no
native texture or asset-lifetime credit.

Generic loader `826F24D8` has a real alternate branch. `826F24EC` masks the
mode to its low eight bits. Every nonzero low byte selects no-op destructor
`82736DF0` at `826F24FC`, then directly relocates the caller's in-place record
at `826F255C`, resets its extension, publishes it and attaches a group owner.
This branch makes neither the metadata copy nor the physical payload
allocation. Publication `826F8AE8` applies the same low-byte test and sets
texture-plugin bit0 for borrowed mode. The zero-byte mode clears that bit;
on an index miss it executes the copied record path. Named loader `826F26B0`
supplies literal r6=0 at `826F26DC`. No reviewed stock caller supplying the
alternate mode was established.

Borrowed mode still owns bookkeeping. Group creation `826F75B8` allocates
12 bytes and retains the chosen destructor at group+8. Attachment
`826F7618` allocates owner-list nodes and increments the texture's reference
count. Group release `826F8168` decrements that count and removes its owner;
with no other owner it calls `826F80C0`, which removes the indices, invokes
the retained callbacks, then invokes the group's destructor at `826F8158`.
For borrowed mode that final callback immediately returns without freeing
metadata or payload. Supporting it requires the original resource envelope
to keep both allocations alive through sampling and final group removal.
Relaxing the native normal-loader guard while retaining copied allocation
and destruction would not implement this branch. Standalone relocate/reset
wrapper `826F2270` also has no copy or owner-transfer operation; it does not
by itself establish complete borrowed ownership.

The native auxiliary-zero release guard is a qualification of its current
profile, rather than an original copied-destructor rule. Relocation
`82736F08..82736F18` explicitly handles a nonzero raster-extension word+4.
For the currently qualified extension offset/layout this is record T+B0.
Complete copied destructor `82736D50..82736DEC` never reads that word: it
recovers the SDK level-zero payload, frees it through allocator slot+24,
then frees the 256-byte metadata record through slot+4. This proves the
original destructor has no auxiliary-zero assertion. It does not prove
valid serialized auxiliary contents, consumers, byte extent or separately
owned storage. Those remain unresolved, so no auxiliary admission repair
or native regression is claimed.

The original also contains syntactic null handling. Copy
`82736F8C..82736F9C` preserves a zero payload offset as a null source, but
still allocates and calls the byte-copy routine at `8273704C` with the
independently serialized extent. Relocation preserves a zero pixel offset
at `82736F30` before patching descriptor addresses. A positive extent cannot
be justified by these null branches; a valid zero-extent authored texture,
allocator policy and GPU sampling contract were not established. The native
null-payload rejection therefore remains an explicit unresolved qualification.

Run the additional source verifier with:

```powershell
python -B tools/analyze_copied_itxd_lifetime_contract.py --output build/restrictive-check-audit/copied-itxd-lifetime-contract-20261002.json
```
