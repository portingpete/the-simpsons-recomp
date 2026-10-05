# Packaged mesh and VFX field audit

`tools/audit_packaged_mesh_vfx.py` inventories source-proven fields from each
named packaged occurrence. It checks the original flat image SHA256,
**18 complete original function spans**, archive SHA256, decoded entry SHA256
and named payload SHA256. No original asset or guest memory is modified.
The report is offline evidence. Its rows grant no native implementation,
setup, draw, retirement or gameplay credit.

The asset catalog records **5,533 EARS_MESH** and **8,770 VFX** occurrences.
The earlier catalog support report left their parameters opaque. The new
report retains archive path/hash, entry, decoded payload offset, embedded
name, payload hash and authored source path, then adds fields only where the
original loaders establish their offsets. A repeated payload can share parsed
fields; each packaged occurrence keeps its own identity.

```powershell
python -B tools/audit_packaged_mesh_vfx.py --output build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json
python -B -m unittest discover -s tests -p 'test_packaged_mesh_vfx_audit.py' -v
```

The optional `--packaged-fields` argument to
`tools/audit_mission_asset_support.py` joins these records by the complete
occurrence identity. It preserves every support and coverage field. Conflicting
identity, byte extent or attempted native credit is rejected.

The complete October2 source report is
[`packaged-mesh-vfx-source-complete-v2-20261002.json`](../build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json),
SHA256 `13e81e770f5d96048067efcf77339fdf2fd0c56a9f20bcb7f5c7e6cfef166291`.
All **14,303 packaged occurrences** passed archive, decoded-entry and named
payload identity checks, with **zero archive or field-qualification failures**.
Its **25,990 native geometry records** contain **29,319 submeshes**. Every raw
submesh primitive is6 and every selected R16 span contains the literal word
FFFF. Those are observations of packaged fields, not a producer limit, a
primitive-restart contract or native draw evidence.

| Vertex stride | Native geometry records |
| --- | ---: |
| 24 | 52 |
| 28 | 8,270 |
| 36 | 2,456 |
| 40 | 40 |
| 48 | 13,656 |
| 56 | 1,516 |

All recovered typed geometry metadata words are00030002. Material EA13
pipeline selectors are DFLT (`44464C54`,23,455 material records), MULP
(`4D554C50`,5,863 records) and zero (one record). Their continuation to an
actual effect/shader still requires separate proof. The original
geometry-list path also admits **36 empty
clumps** with zero geometries/atomics; these are retained as valid empty
source structures rather than rejected for lacking a native pool.

VFX row counts range from0 through26, including19 zero-row resources. The
referenced prefixes contain16 distinct module keys: `.ais`, `.bem`, `.dst`,
`.efe`, `.flc`, `.flr`, `.fnc`, `.lbm`, `.ltg`, `.mot`, `.prt`, `.scr`, `.shp`,
`.snd`, `.trl` and `.vfe`. Their revision and flag fields are recorded, while
module-specific vertex/declaration/material/shader semantics remain unknown.

## Original mesh stream path

The registered `EARS_MESH` callback **826F2710** opens the loaded named bytes
and calls **8270F3C8 → 8270F108**. The latter dispatches chunk16 to the original
clump reader **823CE380**. Chunk26's geometry list reader **823CC388** reaches
the geometry reader **823D0980**. Header reader **823F7DD0** reads and swaps
three 32-bit words from each little-endian twelve-byte chunk header. The
geometry reader separately reads/swaps its first sixteen struct bytes into
format flags, triangle count, vertex count and morph count. These are recorded
as serialized fields, without deriving a native vertex owner from the count.

The geometry plugin registration **8282F970** registers tag **EA33** with
reader **8282F618**. That reader reads/swaps a twelve-byte prefix containing
allocation alignment and two owned byte extents, copies the two opaque pools,
then invokes **82831280**. The opaque pools are big-endian data. The relocation
function reads a count at first-pool+20 and eight-byte rows from +28. Each row
contains the target field offset and a base selector: zero adds the first
pool's base; any nonzero selector adds the second pool's base.

Visitor **828311D0** checks the exact `BFBFBFBF01000000` prefix, reads the
record count at +24, and traverses twelve-byte `{type, bytes, dataOffset}` rows
immediately after the relocation table. It passes the original type and
pointed record to the visitor unchanged. The offline audit resolves relative
values only through those original relocation entries; it never assumes
runtime geometry G starts at the beginning of a packaged payload.

Callback **826FED80** selects record type **3C43A23D**. Setup **8273B760**
selects metadata word **00030002**, obtains G from metadata+12, and consumes:

| Runtime field after original relocation | Offline field |
| --- | --- |
| G+00, G+04 | Vertex owner bytes, byte stride |
| G+08, G+0C | Declaration count, relocated declaration data |
| G+10 | Relocated vertex bytes |
| G+14, G+18, G+1C | Index owner bytes, raw format word, relocated index bytes |
| Metadata+10, Metadata+14 | Submesh count and relocated submesh rows |

Declaration constructor **82701BD8** hashes exactly `12*count` bytes and
creates the original declaration. The audit preserves every twelve-byte
record as BE16 stream/offset, BE32 type, and method/usage/index/opaque bytes.
The terminator remains a record; unused auxiliary streams remain visible.

Draw consumer **82701220** advances **36 bytes** per submesh and loads
material index+00, lookup key+04, primitive+0C, signed base vertex+10,
start index+14 and index count+18. The audit records these raw values and
preserves the other row words as opaque. It does not turn a stock primitive
count into proof that other original primitive values are invalid.
The +04 word is passed unchanged to the geometry-owned lookup
`82831C18`, distinct from the material EA13 pipeline selector. The JSON's
`material_selector` submesh field retains that raw +04 word; no FX or named
material identity is inferred from its label.

Material extension reader **827277F0**, tag EA13, selects LE16 versions1/2
and their sixteen-/thirty-six-byte headers, plus a24-byte tail when byte7
bit0 is set. The report records flags, original material bytes2/3, the
serialized opaque byte6/flag byte7, field4 and the pipeline selector. Virtual
continuations for DFLT, MULP, REFL and other selectors are still separate
producer/consumer contracts; these fields alone do not identify an FX pass.

## Original VFX stream path

The registered VFX callback **82C71F90** copies the named payload through
**8271AC80** and calls **82750580 → 82758D70** before publication. The latter
uses an unsigned byte at **payload+28** as its row count, advances **28 bytes**
per row from **payload+48**, and treats each row's first word as a relative
reference to the same owned payload. It resolves the referenced first word
through **8275AC40**, compares the module's +16 revision against the reference's
+4, then replaces relative pointers with live module identities. The report
reads the prepublication relative reference, referenced module key/revision/
flags, row flags and outer flags at +20 without modifying them.

The prepublication word at +16 is retained by that name: **82750580** replaces
it with the resource identity. Its original serialized meaning is not inferred.
The module prefix and row fields do not establish the full particle, sound,
decal, light, screen-effect or mesh-particle payload layout. Stride, material
and selected shader remain explicitly null in VFX rows.

## Qualification failures and remaining work

Each payload and each native record/VFX row is processed independently.
Malformed or unqualified pool magic, missing relocation, out-of-owner field,
unknown metadata/material version and archive/decode/hash failures retain
their exact named identity and reason. Later cases continue. Top-level broken
chunk bounds can make subsequent sibling offsets unreadable; that failure
remains explicit rather than searching arbitrary bytes for plausible records.

The ten Python regressions cover signed bases, declaration preservation,
per-record continuation after bad pool/reference, owner bounds, absence of
guessed relocations, unknown metadata, the valid original empty geometry-list
path, exact occurrence joins and mutation of both ends of every pinned
original function span. The repeated-entry provenance regression also changes
decoded/payload hash labels and named offsets independently, then requires a
later valid occurrence to succeed. The cache stores actual content hashes
and checks every occurrence's catalog labels, including reused parsed fields;
new named ranges are decoded/hashed independently. Any field-qualification
failure makes the CLI return nonzero. These tests verify offline parsing and attribution, not
native resource lifetimes. The complete report supersedes the preserved
initial report's36 empty-clump qualification failures; the original producer
does not require every clump to contain geometry.
The earlier complete report is also preserved. The v2 report repeats the full
original inventory with the cache provenance repair and unchanged field counts.

The following remain unproven: serialized material continuation to the full
native FX/shader selection, module-specific VFX payloads beyond the referenced
prefix, selected vertex fetch validity for every packaged declaration,
runtime payload-hash association at geometry/emitter setup, and independent
original create/use/release for every packaged mesh/VFX combination. The
source inventory narrows those gaps without changing their coverage status.
