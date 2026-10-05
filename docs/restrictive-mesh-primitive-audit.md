# Mesh primitive and count audit

No shipped alternative to primitive6 has been established. The initial
complete packaged census reports 25,990 geometry records and 29,319 submesh
records across 5,533 geometry and 8,770 VFX entries; every interpreted submesh
primitive is6. These are asset-field observations, not original render or
native lifetime receipts. The full authored primitive domain remains
unresolved because the reviewed runtime paths are deserializers and consumers,
rather than the offline serializer's output specification. The native mesh
primitive checks currently admit6; shader, raster and ownership combinations
have additional qualifications. Primitive6 is not a literal original producer
rule proved by this census.

The [independent source verifier](../tools/analyze_mesh_primitive_contract.py)
pins the original flat image at base82000000, 15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
It verifies 15 complete function/table-prefix spans, annotated instruction
words, 4,964 independent span-byte mutation rejections and 140 primitive/count
arithmetic cases. Its source-only report is
`build/restrictive-check-audit/mesh-primitive-contract-20261002.json`.
The packaged parser and its separate evidence are owned by
[the mesh/VFX field audit](packaged-mesh-vfx-field-audit.md).

Original packaged extension reader8282F618, relocation82831280 and
visitor828311D0 expose the type3C43A23D record. Dispatch826FED80 passes that
record to setup8273B760, which validates metadata00030002 and creates vertex,
index and declaration headers. Setup uses the independently owned G+0 vertex
and G+14 index byte extents. It neither normalizes submesh primitives nor
checks them against6.

Six complete original draw consumers independently load the same 36-byte
submesh row:

| Field | Offset in hex | Offset in decimal | Meaning at SDK entry |
|---|---:|---:|---|
| Primitive | C | 12 | Unsigned primitive DWORD |
| Base vertex | 10 | 16 | Signed base offset |
| Start index | 14 | 20 | Index elements from the bound index payload |
| Count | 18 | 24 | Raw index elements, not triangles |

The primitive/count loads and paired SDK calls are826FF580/826FF574/826FF584,
8270046C/82700460/82700470,827013AC/827013A0/827013B0,
827015BC/827015B0/827015C0,827018BC/827018B0/827018C0 and
82706450/82706444/82706454. They cover static, skinned,
immediate, recorded and character-shadow consumers. Their full functions
are pinned; none forces a primitive6 literal or multiplies the count by a
triangle factor. The native six mesh backends currently require primitive6
and restartFFFF independently of these raw original transports. A new
source-valid asset combination would need its own qualified native case.

SDK8244D360 retains r4/r6/r7 in r16/r19/r17. At8244D5DC, the emitted count
initially equals r17. If that raw count is at most65,535, the SDK emits it
unchanged. At8244D618/624 it inserts that count into the draw packet alongside
the primitive's low six bits. For a larger count only,8244D5F4..610 selects a
factor from821D3D30 and computes:

```text
emitted = ((65535 / factor) rounded down to an even integer) * factor
remaining = previous_count - emitted
next_count = remaining + overlap
next_start = previous_start + emitted - overlap
```

The recurrence at8244D788..7A8 uses the independently stored overlap. Factors
preserve packet grouping; they do not convert a triangle count to an index
count. For example, primitive4 count7 produces a7-index packet, rather than
21 indices. With count65,536, the inspected common factors yield:

| Xenos code and identity | Factor | Overlap | First packet | Next count | Start advance |
|---|---:|---:|---:|---:|---:|
| 1 point list | 1 | 0 | 65,534 | 2 | 65,534 |
| 2 line list | 2 | 0 | 65,532 | 4 | 65,532 |
| 3 line strip | 1 | 1 | 65,534 | 3 | 65,533 |
| 4 triangle list | 3 | 0 | 65,532 | 4 | 65,532 |
| 5 triangle fan | 1 | 2 | 65,534 | 4 | 65,532 |
| 6 triangle strip | 1 | 2 | 65,534 | 4 | 65,532 |
| 8 rectangle list | 3 | 0 | 65,532 | 4 | 65,532 |
| 13 quad list | 4 | 0 | 65,528 | 8 | 65,528 |

The names are Xenos identities, not desktop Direct3D enum values; they agree
with Xenia's version-pinned [Xenos enum](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/gpu/xenos.h#L38-L84).
These factors do not qualify the raster, shader or serializer domain for
the alternatives. Only the0..13 table prefix is part of this source report.
Some GPU identities have a zero factor in that prefix; a large-count call
traps on zero at8244D608, while the small-count path never reads the factor.
Thus a nonzero table entry is neither a complete primitive whitelist nor
proof that zero entries forbid every small-count use. Additional factor
entries and tessellation/explicit-major-mode combinations remain unqualified.

The selected count still measures source index elements.8244D620/63C adds
2*start to the R16 payload address; the R32 branch8244D678 uses4*start and
sets the packet format bit. A selected R16 interval therefore needs the
owned byte span `[2*start, 2*(start+count))`, regardless of primitive factor.
An unused odd owner byte is distinct from the selected complete words.
Signed effective vertex fetch bounds and wraparound remain separate checks.
The primitive packet masks to six bits, but large-count factor indexing uses
the original unmasked primitive; arbitrary high-bit aliases cannot inherit
the literal primitive6 qualification.

Restart interpretation must be proved per primitive. Original reset setters
8243B750/8243B780 are independent of topology. SDK8244D360 does not scan or
removeFFFF; it transports the selected words and the current reset registers
to hardware. Current native strip handling skipsFFFF as a cut and resets
parity, as established by the separate original strip regressions. Applying
that rule to a newly admitted list would not follow from this source trace.

Xenia's [primitive processor](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/gpu/primitive_processor.cc#L604-L630)
enables emulated restart for selected strip/fan/loop identities and filters
list topologies, explicitly noting an assumption about guests not using list
restart. This is an emulator implementation caveat, not proof of original
Xenos list behavior. Both external sources are pinned to commit
`95a5c3ee250f80c3b9d139658649d9ffb6db3eec`; complete snapshots and SHA256
hashes are preserved under
`build/restrictive-check-audit/external-xenia-95a5c3ee` and in the source report.

Any future stock alternative should first preserve archive/entry identity,
relocated field hashes, selected index words includingFFFF, original material,
shader pair, caller and effective state. Its regression must invoke original
setup, selected use, paired CPU owner release and qualified backend retirement.
Small/partial counts, large packet boundaries, signed base offsets, truncated
selected spans, stale owners and per-primitive restart inputs need independent
coverage. This audit grants no new native, shader, gameplay or GPU-retirement
credit and makes no production repair.

```powershell
python -B tools/analyze_mesh_primitive_contract.py --output build/restrictive-check-audit/mesh-primitive-contract-20261002.json
```
