# Particle capacity source audit

The read-only packaged audit finds no shipped `.prt` request above the native
4,096-particle limit. It recovers **9,129 module occurrences in 3,191 named VFX
payloads**, with original requested capacities from 8 through 256. This is a
stock-input census, not a proof that 4,096 is the original producer maximum.
Actual activated capacity depends on pool availability and owner allocation;
complete original module create/use/release remains untested here.

The independent report is
[`packaged-prt-requested-fields-20261002.json`](../build/restrictive-check-audit/packaged-prt-requested-fields-20261002.json),
SHA256 `6624523ff84f5d5f408e0b491c23fa20499230fdb7fb89e148df323acb6aad7d`.
It pins 23 complete original function spans and the `.prt` module vtable against
the immutable original mapped image, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
All archive, decoded-entry, named-payload and recovered-field checks pass.
Repeated cached entries still check each catalog occurrence's identity. A bad
module or occurrence does not hide later independent cases.

## Original producer and lifetime

The module constructor `8275CE78` publishes key `2E707274`, revision 15 and
vtable `82152FBC`. The separate owner-contract trace establishes its member
literal 2,048 as an active-instance quota: creation compares module `+0x14`
with `+0x18`, then increments the former; removal decrements it. This quota
is distinct from an emitter's ring capacity. The VFX resource loader
`82758D70`, at `82758DE8`, calls the module's
`+0x14` callback with the reference address plus 12. That callback, `8275EE68`,
relocates block `+4` and `+8` relative to the block itself, producing the
definition and parameter pointers. It clamps the definition's **signed
16-bit** count at `+0xDA`: zero or negative values become one. It also clears
definition flags `+0xD0` bit `0x1000`.

| Boundary | Original behavior | Qualification limit |
|---|---|---|
| Preallocation `8275DFF0` | Requests `(count+7)/8` blocks, compares with `827527B0` pool availability, and selects the shared-pool or embedded-owner branch | IDA omitted the returned owner size; the exact instructions establish it in the separate owner-contract report below |
| Pool initialization `8275E290` → `827526F0` → `8270CA78` | Initializes the original shared block pool with 544-byte blocks and alignment16 | Shared pool availability can be smaller than an authored request |
| Activation `82763218` → `82762F08` → `82762980` | Binds definition/parameters; requests blocks through `827527A0` → `8270CB10`, or addresses embedded 544-byte blocks after the pointer table; publishes actual block count at owner `+0xC4` and eight times that count at `+0xC8` | Requested capacity is distinct from actual live capacity; source allocation lineage is known, but authentic native activation/lifetime remains unexecuted |
| Direct rendering `82760830` → `82772CA8` | Types0/3/5 use the direct producer. It reads live count `+0xF4`, reserves `64*count` bytes, and submits original SDK primitive13 with `4*count` vertices. It wraps the particle ring using the signed capacity at `+0xC8` | No literal4,096 cutoff occurs in this examined producer. Definition/state/texture/owner constraints still apply; signed arithmetic and field representability alone do not establish valid maximum |
| Module destruction `8275CED0` → `82760EB0` → `82760728` → `8275F090` | Unregisters texture owners, returns the actual shared blocks through `827527A8` → `8270CB78`, or releases the embedded allocation budget, then follows the original instance free branch | A complete authentic module setup/draw/destruction regression has not yet executed |

Pool destruction `8275E2E0` calls `82753018` to release its allocator-owned
storage. The entry `8275E2E0` is among the field report's 23 pinned spans;
the deeper helper `82753018` is separately pinned in the owner report below.
Alternate runtime types1/4/6 route through `8275FE78`; that draw/ownership path
is not qualified by the direct particle fixture.

The additive
[`particle-owner-contract-20261002.json`](../build/restrictive-check-audit/particle-owner-contract-20261002.json)
pins 31 complete spans, including the generic creator, allocator bridge,
activation context and normal remover. `8275DFF0` returns
`4*(blocks+76)` bytes for shared-pool ownership, or
`align16(4*(blocks+76))+544*blocks` bytes for embedded ownership.
Its output argument contains alignment16. `82774368` invokes this module
callback, rounds the returned owner size to16 and normalizes alignment to at
least16. `827743F0` passes sizes below `0x40000000` to `8274D530`, which calls
the allocator through `82DFE328`; successful allocation becomes the module
activation owner. No literal4,096 appears in this size producer.

The activation callback retrieves the actual selected instance row from the
generic setup structure, then its module block at row `+0x0C`. The base
constructor `82774AD0` publishes parent/index/matrix ownership before the
particle constructor consumes that module block. Normal removal `827749F0`
decrements the instance count, calls `8275A978`, dispatches the module's final
destructor and frees storage through `8274D588`. A separate retained-owner
sign-bit branch instead links the instance into the module's cache; immediate
free is not claimed for it.

For example, an authored request5,001 would request626 blocks and produce
2,816 aligned shared bytes or343,360 embedded bytes. This is source arithmetic,
not a native activated owner. All131,072 modeled encoded-count/availability
cases fit the examined generic size branch, but the largest signed encoded
count rounds to32,768, whose later signed capacity read needs independent
validity analysis. No full live range is certified from that arithmetic.

## Recovered stock fields

Each module row retains its exact archive hash, entry, decoded named-payload
offset/name/hash, relative reference, block/definition/parameter offsets,
revision, flags and consumed definition-prefix hash. Only the parameter
address is bounded: the full parameter-layout extent remains unknown.

| Field | Stock values |
|---|---|
| Authored signed count | -1 through256; two authored -1 values follow the original clamp to requested capacity8 |
| Requested rounded capacity | 8..256 across20 distinct values; zero requests above4,096 |
| Definition `+0x100` type | 0:6,170; 1:196; 4:2,154; 5:525; 6:84 |
| Definition `+0x104` flag profiles | 0:7,410; 2:1,617; 6:57; 64:5; 66:40 |
| Definition `+0x40` mode | 0:8,436; 1:693 |

The report also preserves alpha reference `+0x106`, serialized and
source-normalized `+0xD0`, and `+0xD4`. These are observed raw fields rather
than proof of a native shader/state combination. `live_capacity`, selected
shader and full parameters remain null. Setup/use/release/gameplay bits are
false. The stable packaged mesh/VFX v2 report and base matrix are unchanged.

## Native restriction and regression frontier

[`engine_particles.cpp:39`](../runtime/engine_particles.cpp#L39) admits
nonzero live count bounded by capacity, capacity at most4,096, a bounded ring
cursor and the original selected count. Its private CPU staging allocation
at line166 is `0x40000` bytes, exactly64 times4,096.
[`particle_draw.cpp:97`](../renderer/particle_draw.cpp#L97) separately restricts
the vertex array to16,384 vertices, also4,096 quads. These two related caps
must be reviewed together before any extension.

The existing whole original direct-draw fixture uses genuine `82772CA8`,
original shader selection and texture loading, but seeds its emitter,
definition, parameters and bucket. It therefore cannot prove the serialized
`.prt` module allocation/activation/retirement contract. The next meaningful
fixture must first invoke the original VFX/module loader, allocator/pool and
texture registration, then direct draw and original destruction with complete
shared/embedded owner release. Malformed extent, insufficient owner span,
invalid ring cursor, stale texture/module and use-after-retirement cases must
remain rejected. A valid activated capacity above4,096 would establish a
repair frontier; this census has not produced one.

The four Python tests cover immutable parsing, original signed clamp,
malformed-module continuation, per-occurrence cache identity and full-span/
vtable mutation rejection. A synthetic authored count5,001 rounds to5,008;
that arithmetic case grants no original allocation or native lifetime credit.
Run the source-only inventory with:

```powershell
python -B -m unittest discover -s tests -p 'test_packaged_particle_fields.py' -v
python -B tools/audit_packaged_particle_fields.py --packaged-report build/restrictive-check-audit/packaged-mesh-vfx-source-complete-v2-20261002.json --output build/restrictive-check-audit/packaged-prt-requested-fields-20261002.json
python -B -m unittest discover -s tests -p 'test_particle_owner_contract.py' -v
python -B tools/analyze_particle_owner_contract.py --particle-report build/restrictive-check-audit/packaged-prt-requested-fields-20261002.json --output build/restrictive-check-audit/particle-owner-contract-20261002.json
```

This new Python tool/test and its report were added after the frozen native
source manifest. Their separate hashes and passing tests are source-only
evidence; no native rebuild or broader runtime qualification is inferred.
