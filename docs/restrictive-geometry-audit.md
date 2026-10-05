# Restrictive geometry checks — October 2, 2026

The native skin decoder admitted only 48- and 56-byte records; its morph and
auxiliary stream guards repeated that whitelist. The sky decoder admitted only
28-byte records and the original captured row order and offsets. These were
observed layouts, rather than restrictions enforced by their original producers.

The original non-auxiliary setup at `826FF31C..826FF340` and auxiliary skin
setup at `826FF394..826FF3B4` read the active geometry's stride from `G+4` and
bind its descriptor at `G+38`. SDK `8243C5D8` retains incoming `r7` in `r26`;
`8243C6A8..B4` shifts that stride by two and stores it as one byte. Nonzero
aligned strides that round-trip through this encoding range from 4 to 1,020
bytes, inclusive. Each selected attribute must also fit within the record.

Fetch association `8245EE64..94` matches semantic and usage index and scans
twelve-byte declaration rows. `8245EF98/8245EFA0` loads the selected stream and
byte offset. It does not require the capture's row order or attribute offsets.
The read-only verifier `tools/analyze_vertex_stride_contract.py` pins these
instructions to the original flat image SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0` and rejects
mutations of every pinned byte.

Skin and sky now accept that representable stride range while retaining exact
typed inputs, unique semantic/index rows, bounds, finite consumed attributes,
source identity, index limits, and all original manager/resource checks. Skin's
64-bit byte occupancy mask has become a bounded bitset spanning the complete
original record range; relocated fields above byte 63 no longer shift beyond
the mask. The existing declaration shape restrictions remain explicit: skin
requires its six original morph declarations, and sky requires its three
consumed inputs plus a terminator. This work does not establish support for
every original declaration type or extra unused semantic.

CPU decoder cases exhaust every padded stride from each input's minimum
attribute extent through 1,020 bytes, relocate those attributes to the end of
the record, and preserve the input bytes. Sky reverses row order as well.
Malformed alignment, overflow, truncated data, wrong formats, duplicate or
overlapping inputs, invalid flags and nonfinite consumed values still fail.

Whole original skin fixtures independently run base, textured and dual effects
with an 80-byte stride and attributes relocated by 16 bytes. Whole original sky
runs a 64-byte stride with attributes relocated by 36 bytes and reordered rows.
Each case constructs its declaration using original `82701BD8`, enters the
original scene dispatcher and material/bone callbacks, exercises opaque/alpha
passes and Boolean flags, checks actual output pixels and ABI, ends its manager
and camera, then invokes original `82700A78` to release declarations and verifies
the allocator owners are retired. The padded fixtures additionally run paired
original `82701118` cleanup for both effect tables, reject all 49 retired FX
identities/source/shader records and CPU cache allocator owners, restore the
original table publications, verify released quad declarations and shadow
textures, and destroy the manager through `826B7600`. The original root pool
and borrowed driver depth remain owned by their respective parents. These are
synthesized original-valid input regressions, not newly encountered mission
assets or gameplay route evidence.

Native immutable skin/sky mesh upload caches belong to the backend rather than
an individual FX record. Original effect cleanup has no per-effect cache
retirement API. Tests therefore emit `backend_mesh_cache=owner_resident` and
`full_gpu_retirement=unproven`; they do not equate logical FX/declaration/cache
retirement with complete GPU-resource or driver teardown. The preexisting
normal shadows cleanup also retains its camera rasters and CPU extensions.
The parent coordinated build passed all four padded cases with the added paired
effect cleanup as part of the 48-case focused suite. Exact JUnit/source/executable
identity receipts are exported by the catalog audit; synthetic layouts are kept
separate from live encounters.

The static shadow decoder also rejected packed `tangent0` even though the
Z-prepass already accepts the exact `002A2187` declaration from live stride-40
geometry. Original shadow fetch metadata `820C3DA8..3DB4` selects only usages
0/5/1/2 at index zero. Generic SDK association matches each fetched usage/index
against the supplied twelve-byte rows, so the tangent row is unselected. The
Boolean-false shader path consumes only position0/UV0. The decoder patch admits
only that exact typed tangent row under the existing stream, format, bounds,
alignment and uniqueness checks. It does not qualify arbitrary unused rows or
the existing unsupported nonempty textured alpha-caster queue.

`tools/analyze_shadow_tangent_contract.py` pins those original association bytes
and the complete shadow shader record; its 80 mutation checks pass. New CPU
cases verify dead packed payload invariance and malformed tangent rows.
`OriginalShadowTangentPass` constructs declarations through original `82701BD8`,
uses the entire original static entry `82707678`, changes every unused tangent
payload and checks identical depth, retains the existing original queue/range
negatives, then releases declarations through `82700A78` and all 25 actual
FX/cache owners plus manager through the original first-table cleanup. The
coordinated native source wave passed `OriginalShadowTangentPass`. Physical
backend mesh cache retirement remains separately qualified. The material shader lists remain implementation
coverage limits rather than claims that every catalog combination is qualified.

All six mesh backends formerly required `baseVertex == 0` and rejected any
raw non-restart index at or above the total vertex count while uploading.
The source patch now qualifies each selected index range against its owned
vertices after applying the signed base. The separate 65,535 total-vertex cap
has also been removed using the owned-byte producer proof below: an R16 index
does not by itself limit the size of an owner addressed using a base offset.

The read-only `tools/analyze_submesh_base_contract.py` independently pins six
original producers: immediate rigid/sky `827013A0..B0`, recorded rigid/sky
`827015B0..C0`, skinned `827018B0..C0`, character shadow `82706444..54`,
static Z-prepass/mono `826FF574..84`, and skinned mono `82700460..70`.
Each loads submesh `+10` into `r5`, separately from primitive `+C`, start
index `+14`, and index count `+18`. SDK `8244D374` retains `r5` in `r15` and
`8244D5EC` writes its unchanged low 32-bit word. The nearby comparison against
65,535 at `8244D5E0` checks index count in `r17`; the subsequent branch splits
large draws. It does not establish a 65,535 vertex-count limit. The verifier
checks 48 producer/offset dataflow cases and rejects all 188 pinned-byte
mutations. Xenia's primary implementation documentation describes the hardware
register as 24-bit, with restart checked before offsetting and clamping, and
explains how D3D9 writes the signed 32-bit base word. This corroborates safe
negative offsets such as raw indices 2 through 5 plus base -2, without
qualifying arbitrary wrap/clamping or upper-byte aliases. See
[Xenia register documentation](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/registers.h#L314-L355).

A focused lifecycle regression has been added for the existing original sky
fixture: prepend two owned records, keep its four original vertices at slots
2 through 5, use submesh base 2, and select seven R16 words starting at index 1:
`0,1,2,FFFF,2,1,3`. The expected effective indices are
`2,3,4,restart,4,3,5`, preserving the original quad and output. Unselected
prefix/suffix words may be 65,534: they are not referenced by that draw.
Construct and retire the declaration through `82701BD8`/`82700A78`, traverse
the original scene dispatcher and all eight Boolean passes, compare output
pixels and ABI, and verify immutable geometry and retired allocator ownership.
Independent additional cases use base -2 with selected raw indices 2 through
5, and base zero with out-of-range unselected prefix/suffix words. All three
execute the complete original scene dispatcher, declarations, draw/callback
paths and paired FX/cache cleanup. The coordinated native source wave passed
all three `OriginalSkyBase_positive`, `OriginalSkyBase_negative` and
`OriginalSkyBase_selected` cases; the Python producer verifier also passes.
Its modeled ownership checks remain distinct from native renderer evidence.

The renderer repair validates the selected effective range before every
immediate draw and recorded-draw admission. Checking all raw R16 indices while
uploading cannot express this contract: unselected indices are irrelevant,
valid negative offsets can lower an index, and positive offsets can turn a
raw in-range index into an out-of-range access. Keep restart `FFFF` separate
before applying the offset, use a widened signed sum, and require every
addressed vertex to belong to the uploaded owner. Malformed underflow,
overrun, selected index extents, and extreme signed offsets need their own
cases. The shared predicate uses a widened signed sum and immutable index
snapshots retained by the mesh owner. Updated skin/sky backend cases exercise
positive, negative and selected-only ranges through immediate and recorded
create/use/release, pixel comparisons and stale replay rejection. All six
mesh suites now validate malformed selected indices at draw admission;
uploading opaque index words alone is permitted. Those native mesh cases passed
the coordinated source wave. Original opaque/alpha auxiliary tests for gloss,
flipbook and dual-UV still treated aligned stride64 as malformed. Their test-only
repair retains zero, misaligned65, overflow1024 and mismatched-stride rejection,
then repacks all owned records to valid stride64 and invokes the whole original
auxiliary producer for both passes and bit1 flags. The corrected three cases
passed, with exact baseline pixels and eight original auxiliary lookups each.

The Mob Rules ability route encountered a separate textured skin rejection
for source `8200FB98`, opaque VS `8201146C` / PS `82013900`. Original fallback
`827401CC` skips blend writes for the opaque alpha-byte=0 branch. Alpha setup
`827401D4..82740210` publishes expanded=1, enabled=1, source=6, destination=7;
cleanup `827402B8..C4` resets only expanded blending to zero. The following
opaque skinned branch changes depth/cull and invokes `82701638`, retaining the
same enabled `07060706` blend equation. The old guard incorrectly tied that
equation to the selected alpha shader and to expanded=1.

Both immediate and recorded skin guards now permit that exact source-alpha
tuple with canonical expanded=0/1 independent of shader selection. Original
shader identity, texture ownership, all other pipeline state and unsupported
blend equations still fail closed. `tools/analyze_skin_blend_contract.py` pins
the producer, opaque branch and cleanup bytes against the same original image
hash and passes 232 independent mutation rejections. The new
`OriginalSkinTexturedInheritedPass` executes original alpha then opaque without
the isolated fixture's blend reset, checks retained state and distinct pixels,
and completes original declaration/FX/cache/manager retirement. The backend
suite separately verifies fractional opaque RGB/alpha blending, direct/recorded
parity and malformed tuples without changing pixels, bindings or receipts.
The coordinated source wave passed `OriginalSkinTexturedInheritedPass` and the
fractional backend cases. The rerun Mob Rules ability route also passed. The
old failing live receipt did not include the exact blend tuple, so the original
source/sequence regression establishes the valid admitted contract, while the
route rerun establishes that the previously crashing route now completes.

The same original shared fallback branches to `82701220` at
`82740298..827402AC` for zero-bone rigid/sky work. Its alpha cleanup retains the
same enabled `07060706` equation with expanded=0. Rigid previously admitted that
inheritance only for VFX; sky tied it to the alpha pixel shader and expanded=1.
The renderer changes admit only that exact canonical equation for both
complete pairs, preserving all other state, ownership and shader checks.
`tools/analyze_fallback_blend_contract.py` extends the original instruction pins
and rejects 256 independent byte mutations. New independent original cases
exercise all six established rigid families and sky alpha-to-opaque sequences,
selected original pairs, pixels, original declaration/FX/cache release and stale
identity rejection. Fractional opaque RGB/alpha tests cover immediate and
recorded paths, expanded0/1 parity and five malformed tuples without changing
bindings, pixels or receipts. The coordinated 206-case wave passed all six
whole-original rigid inheritance cases and `OriginalSkyInheritedPass`. The
new rigid backend fixture initially disabled both original shadow branches via
c31.x=0 while expecting the copied-depth alpha result. Its test-only correction
sets c31.x=1 before the direct commit and live-bank update. The intended original
PS result is alpha .4; over destination alpha1, source-alpha blending yields
.4*.4 + 1*(1-.4) = .76, quantized to A2 code2. The focused rigid WARP/hardware
rerun passed in `producer-ranges-frontier-tests.xml`; the production blend
implementation was not changed for that fixture error.

GPU cache lifetime is an explicit native adaptation, rather than a recovered
original mesh-cache policy. Original `8273B760..8273B7E8` initializes embedded
vertex/index headers at geometry+38/+58 and publishes its guest data pointer
through `82C20EE8`; the resource-type1 path `82C20F30..44` updates that header's
address while preserving its low resource bits. The native backend copies
immutable content into device-owned meshes and caches full snapshots under
512-entry/64MiB bounds per cache. Those snapshots do not retain an original guest
allocation or FX identity; their own lifetime can legitimately extend beyond
original FX/declaration retirement.

A test-only observation now captures one diagnostic COM reference to the
actual bound vertex/index buffers in each of the six existing real mesh-draw
suites. After the complete original-shader drawing fixtures, recorded owners and
backend scopes end normally, it requires the mesh weak owner to expire and the
observation's final buffer `Release()` calls to return zero. Success emits
`AUDIT_GPU_MESH_RETIREMENT` with scope `backend_owned_immutable_buffers` and
`original_asset_retirement=separate`. This proves native VB/IB retirement
after backend destruction; it does not prove mission-exit eviction, all shaders,
all GPU objects or complete original driver teardown. The coordinated runs passed
all six families on WARP and hardware with those markers: the corrected rigid
fixtures passed in `producer-ranges-frontier-tests.xml`, and corrected shadow
and extended mono fixtures passed in `index-mono-frontier-tests.xml`. Original
lifecycle markers preserve the narrower limitation.

A further source audit found a vertex-count whitelist independent of R16 draw
ownership. The previous CPU decoders and all six native mesh uploads rejected
owners over 65,535 vertices. Original `8273B760` passes G+0's owned byte extent to
`82C1FAB0`; instruction `82C1FB10` masks aligned bits6..29 (`03FFFFFC`) into the
resource size field. The original asset callback `826FED80` checks type
`3C43A23D` and forwards loaded metadata. Neither that callback nor the geometry
setup divides the byte extent by G+4's independently bound stride or tests a
remainder. An integral record-count requirement is therefore unproved. An unused
partial tail may remain in the byte owner, and the last fetched record need only
contain its consumed attributes rather than all unused stride padding.

The source repair distinguishes the representable aligned byte
owner from consumed fetch bounds. For highest consumed attribute end E and
stride S, record i requires `i*S+E <= ownedBytes`; safe fetched-record count is
`1+(ownedBytes-E)/S` when the first consumed record fits. Fields declared but not
selected by the original shader need their own adapter-specific treatment.
The read-only verifier `tools/analyze_geometry_extent_contract.py` pins the asset
callback, geometry/resource constructors, publication and paired allocator calls,
checks 255 representable strides with 65,536 owned records, and rejects 424
instruction mutations. No wrapped byte-header or 24-bit vertex-index alias is
qualified by this proof.

Original morph constructor `8270DAE8` iterates geometry/target tables.
`8270DB24..8270DB74` multiplies each independent stored vertex count by12,
calls the same `82C1FAB0` constructor, and publishes its float3 data through
`82C20EE8`. The native morph owner maximum therefore also uses `03FFFFFC`;
the old parsed field mask `0FFFFFFC` cannot qualify bits26/27 that this original
constructor clears. Aligned unused tails remain allowed, and skin validates
the complete consumed float3 prefix before publishing any decoded delta.

The passed `OriginalSkyLargeOwnerPass` constructs a real 65,536-record owner via
`8269BF70` and whole original `8273B760`, draws its last four records through all
eight original Boolean fallback combinations using signed base65,532 and R16
indices0..3/restart, then retires the original declaration, allocated vertex
owner and all49 FX/cache owners through their paired original paths. A separate
partial-tail case appends12 unused bytes to the source owner. Malformed source
alignment, selected effective ranges and stale allocation generations are
separate cases. The fresh `producer-ranges-frontier-tests.xml` verifies both
whole original owner cases, `NativeGeometryExtent`, `NativeLargeGeometryDecode`
and the corrected rigid WARP/hardware suites. Independent CPU
decoder cases cover rigid, mono, sky, skin, character shadow, static shadow and
zprepass consumed spans, including 65,536-record owners, inert partial tails,
missing final padding and selected-boundary rejection. A large morph stream
also checks inert tail bytes and atomic rejection of a missing consumed delta.
Each of the six existing GPU suites now includes a 65,536-vertex owner drawn
with signed base65,532; rigid/skin/sky compare immediate and recorded pixels,
release their payloads and reject stale execution. All six retain observations
of their actual large vertex/index buffers for the separate normal backend
destruction checks. The separate six-backend native run passed both WARP and
hardware for rigid, mono, zprepass, skin and sky. Shadow's large selected draw
and malformed effective-range checks initially passed, then its suite stopped
because the empty-upload negative expected the old error substring. The
test-only diagnostic correction passed both shadow suites in
`index-mono-frontier-tests.xml`, including normal backend buffer retirement.
Those bounded native retirement results remain separate from original asset
retirement.

Actual native upload bounds must include unsigned `D3D11_BUFFER_DESC::ByteWidth`
and the D3D11 buffer element limit, rather than the R16 index word's maximum.
The arithmetic predicate guards multiplication before the UINT cast;
`CreateBuffer` still enforces actual device allocation availability and size
limits before an upload/cache receipt is published. Microsoft's
[buffer descriptor](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_buffer_desc)
and [resource limits](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-limits)
define those native bounds. Vertex captures previously capped character, mono
and zprepass at16MiB; they now use the original representable `03FFFFFC` byte
extent, while sky/rigid/skin's existing64MiB captures feed the same original
extent validation in their decoders. Header and declaration captures retain
their separate existing bounds.

The index byte owner is an independent full32-bit loaded word. Original
`8273B7C8..DC` passes format from `G+18`, bytes from `G+14` and inline header
`G+58` to `82C1FB48`; the complete constructor stores those bytes unchanged at
header+1C. Its conditional branches inspect usage flags, rather than byte
parity. Original `8243C7EC` binds that header, and the count7 R16 packet path
`8244D5D8..D6C0` adds `2*startIndex` to header.data. Its65,535 comparison
controls draw-count splitting; it is not a byte-owner limit. The independent
verifier `tools/analyze_index_extent_contract.py` pins the complete constructor,
binding and selected packet path, rejects392 instruction mutations and checks
six byte boundaries through `FFFFFFFF` without qualifying wrapped addresses
or larger draw chunk semantics.

The index repair removes the16MiB snapshot/decoder ceiling and parity
rejection. Original snapshots and exact source caches retain every owned byte;
the decoded R16 view has `floor(ownerBytes/2)` complete words, leaving an odd
last byte inert. Selected draws still reject missing complete words, overflow,
negative effective indices and effective vertices outside their owned span.
All six native uploads replace the unrelated `02000000` index-count cap with
the same actual D3D11 element/UINT byte arithmetic predicate used for vertices.
The installed Windows SDK defines
`D3D11_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP` as27; actual `CreateBuffer`
success is still required before publishing native ownership.

Three independent original index-owner regressions passed in
`tests/test_sky_index_owner_pass.cpp`:16MiB+2 complete storage,16MiB+1 with
an odd unused tail, and64MiB+2 crossing the former native upload/capture cap.
Each allocates the index owner through `8269BF70`, constructs original inline
headers/declaration, selects the same seven-word prefix through all eight real
Boolean passes and verifies pixels, ABI, retained complete bytes, truncated
selected source rejection, original declaration/vertex/index/FX cleanup and
stale allocation rejection. `index-mono-frontier-tests.xml` records all three
successful native cases. Full32-bit original source admission does not establish native whole-buffer
support beyond D3D11 capacity or device availability. Backend GPU retirement
and original CPU owner retirement remain separate receipts.

The mono renderer adds actual deferred static opaque drawing for
the catalog agent's traced `82740420 -> 82701448` producer. Recording admits
only VS`82120C04`/PS`82122BD4` and a zero skin Boolean bank. It captures all244
material float4 rows, keeps each draw's constant buffer distinct and merges
the first56 live VS rows with the original MSB-first four-register mask at
replay. Recording binds only its deferred context; released or unready live
owners reject before command execution. The existing WARP/hardware mono suite
now includes original reflected-mask capture, caller-poison isolation,
independent half-width matrix pixels, immediate/deferred equivalence,
malformed range/state/constants, stale owner/payload rejection and paired
payload/live release. Actual input VB/IB references are separately observed
through normal backend destruction. Both extended mono GPU suites passed in
`index-mono-frontier-tests.xml`, including separate buffer retirement markers.
Whole original mono recording caller lifecycle remains a separate frontier.

The original small draw-count producer does not reject selected counts0,1,2.
Whole SDK body `8244D360..8244D7B8` still requires the bound index header even
for count0, transports the full count to its packet, and exits on the zero
remaining count. `tools/analyze_short_strip_contract.py` pins that complete body
and the original submesh loads, rejecting1,136 independent byte mutations.
The shared native predicate now admits these no-triangle draws over nonempty
owned vertex/index buffers. An empty selected interval at the owner end performs
no fetch; a start beyond the owner, missing buffers, truncated selected words,
and any actually fetched effective vertex outside its owner still reject.

Independent `tests/test_sky_short_strip_pass.cpp` cases allocate real owners,
run whole original setup and all eight Boolean passes, require unchanged full
color/depth/stencil output, then retire declarations, vertex/index owners and
all49 FX/cache owners through paired original paths and reject stale use.
All three original counts, the shared R16 predicate and the source verifier
passed in `short-strip-recording-tests.log`. Its two mono recording cases still
failed at their separate state frontier; the five short-strip/source cases
passed. No zero-size source owner has been qualified.

Large selected counts exposed a separate rendering defect. For primitive6,
SDK splitting uses chunks65,534 with a65,532-word start advance and two-word
overlap. `8244D5B0` keeps only the primitive's low6 bits; `8244D618/D624`
add count<<16. Its R16 packets leave initiator bit12 (`not_eop`) zero. Native
drawing previously submitted one `DrawIndexed`, which changes the winding
after a restart near the original packet boundary. For count65,536, a reset
at65,530 followed by0,1,2,3,4 produces single-strip triangles ABC,CBD,CDE;
the two original packets produce ABC,BCD,DCE. Cull2/6 distinguishes them.
Xenia's primary [register definition](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/registers.h#L293-L310)
and [per-packet draw submission](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/d3d12/d3d12_command_processor.cc#L2196-L2294)
corroborate the packet interpretation; the game-specific instruction/table
proof comes from the immutable original image.

The six direct and four supported deferred mesh paths now use
`renderer/r16_strip_chunks.h` after validating the complete selected owner
range. It preserves the exact chunk/overlap sequence, count0 packet behavior,
and one logical receipt per original draw. `tools/analyze_strip_chunk_contract.py`
pins the full SDK body, primitive table and original sky cull transport,
rejects1,424 mutations, and checks
count boundaries through full DWORD transport without qualifying wrapped starts
or unowned fetches. The six GPU suites include independent triangle coverage,
literal original-packet comparisons, cull2/6, malformed whole-range rejection
before any prefix draw, and separate actual buffer retirement observations.
Rigid/skin/sky/mono also check deferred parity, no immediate recording work,
paired payload retirement and stale replay rejection; shadow/zprepass have no
qualified deferred API. A new whole-original sky fixture independently allocates
both owners, constructs inline headers/declaration, traverses all eight Boolean
combinations with requested cull2/6 and material-byte3 profiles0/1, compares the full draw with independent
original packet calls, and runs paired declaration/vertex/index/49FX/cache
cleanup. Public dispatcher `827408A8..8274090C` derives logical alpha from
metadata and the request, then clears packet+12 for the fixture's metadata-bit0
profile. Logical alpha still selects the alpha technique, while static draw r8
is0 for both shader pairs. The original material branch temporarily sets cull0
for byte3=0 or cull2 for byte3=1 and restores requested entry cull2/6. The fixture
asserts those actual arguments, both technique choices and original setter
sequences; it uses actual draw-time cull0/2 in its independent pixel oracle.
Actual cull6 remains separate backend evidence. The corrected original case
passed 73,729,465 checks in
`build/restrictive-audit/chunk-oracle-corrected-original-tests.xml`, including
the material-cull2 winding discriminator and original packet comparison. Its
LastTest log and source/executable hashes are preserved alongside that receipt.
Strict compilation and all1,424 source mutations also pass. Larger byte-owner admission and arbitrary
signed-base wrap/clamping aliases remain separate contracts.

The whole original mono recording producer now reaches draw caller`82740624`
with halfpixel1, MSAA request1 and sample mask`0000FFFF`, as preserved in
`recording-state-tests.xml`. Original application force`827246C8` selects that
mask; SDK setter`8243AC40` stores only its low16 bits, equal to the SDK default
`FFFFFFFF`. `tools/analyze_application_scalars.py --self-test` passes all13
original-byte/default/force/setter checks. The mono direct and deferred guards
now admit those two canonical full masks on verified single-sample attachments.
Halfpixel0 remains outside the proved mono draw profile. The new GPU regression
compares direct/deferred color/depth/stencil for both masks, preserves recording
without immediate work and paired payload/live retirement, and checks rejected
partial-mask profiles plus halfpixel0/2 and MSAA0 without state or output
changes. Partial masks are valid original SDK scalar values but their rendering
is not qualified by this full-coverage repair. Both mono WARP/hardware suites
passed with `AUDIT_GPU_MONO_SAMPLE_MASK` in `storage-mono-audit-tests.xml` and
`retired-graph-storage-observer-tests.xml`, including the separate normal
backend buffer retirement marker. Those are bounded backend receipts; the
whole original recording manager's history/payload teardown frontier remains
the catalog audit's independent lifecycle work.

Other reviewed fixed sizes are justified at their current original producers.
Reflection setup loads literal 256 at `826FF140` and literal 16 at `826FF18C`
before calling `8273C2B8`. Shadow creation loads literal 1,024 for both surfaces
at `827065F0/82706600` and `82706630/34`, and literal 32 for the border texture
at `82706664/68`. Their current guards preserve these original call profiles;
the geometry findings do not justify relaxing unrelated resource sizes.
