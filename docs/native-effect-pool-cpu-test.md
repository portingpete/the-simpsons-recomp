# Original CPU effect-pool first initialization

The standalone fixture passed **437 checks** against the frozen **build146**
libraries, compiled with ClangCL `/W4 /WX /fp:strict`. It used real Runtime
startup, the actual muted Dac source and OS worker, and the established
`828166FC` startup observer. It then invoked the original AOT entry
`82C181E8(r3=820D573C, r4=E1A50400)` exactly once. The result was HRESULT zero.
No native FX record, wrapper, typed FX object, registration, console device,
or shader was created by this probe.

This qualifies **first initialization of this genuine empty CPU pool from the
original littextured serialized body**, plus the original shared-name lookup.
Production can retain the original CPU metadata storage and lookup API within
this boundary. It does not need an invented SDK FX object or a replacement
host pool merger for this operation. Native x64 AOT and the existing native
D3D11 engine boundary remain the execution model.

The freeze was released to main after the successful run and final input-hash
comparison. Main may rebuild the parent libraries. Subsequent source/library
changes do not expand this recorded build146 result; the runner deliberately
rejects other library identities instead of silently qualifying a new build.

## Reproduction and recorded evidence

From `K:\SimpsonsNativeCopy`:

```powershell
python -B build/effect-pool-cpu/verify_original.py --test
python -B build/effect-pool-cpu/run_probe.py
```

The first command is offline and read-only. It regenerates its comparison in
memory, compares saved evidence, and runs **22 tests**: a valid baseline,
20 targeted in-memory instruction/data mutations, and truncated-image
rejection. The second command strictly compiles and runs the fixture only
when the recorded build146 libraries remain available. It does not invoke
CMake, regenerate AOT code, build the runtime, or execute the original game
executable. `--write --test` on the verifier refreshes evidence solely inside
`build/effect-pool-cpu/`.

- Fixture: `tests/test_effect_pool_cpu.cpp`.
- Build/run receipt: `build/effect-pool-cpu/result.json`; all five parent
  libraries, copied production headers, source image, fixture and linked audio
  codec inputs remained unchanged across compile/run.
- Exact header copies and identities: `build/effect-pool-cpu/include/` and
  `frozen-inputs.json`. These are unchanged production headers, not shims.
- Commands and diagnostics: `compile-command.json`, empty successful
  `compile.log`, `run.log`, and `runner.log` in that directory.
- Original bytes: `original-evidence.json`, `original-disassembly.txt`,
  `original-checks.log`, and `verify_original.py` in that directory.

The current build146 Runtime library SHA256 is
`76444646f5eeffdc78bf2618df72bb8f02a032c7359a3a07260065dc1a7bb84b`;
Graphics is
`8861bcba7cb63a151fc9623d6cefb64ebe9157c638f1a9554debcf77d1b73ca5`.
The earlier first-effect lifecycle receipt predates build146's last rebuild
and has different hashes for these two libraries; it was used as a fixture
pattern, not as the library-identity authority. Full identities are recorded
in the new receipt. Its `returncode=0` and `inputs_unchanged=true` are the
execution result, independent of any later parent build.

## Original byte and call-boundary checks

The flat original PE is `analysis/simpsons.pe`, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Littextured blob B=`820D5730`, F=B+12=`820D573C`, body size `47A4`, total
blob size `47B0`; blob SHA256 is
`38972d41a498c52988afc803454fb6830086e3ec8a3cac966ad2a3705ee63fd9`.
Addresses and unqualified offset/size notation below are hexadecimal;
explicit byte/count descriptions are decimal.

The verifier checks **8 spans, 1,235 original words, 52 semantic word pins,
and 20 external direct edges** across the bounded helper/allocator closure.
It parses original `.pdata` extents, verifies complete span hashes, invokes
`SimpsonsDisasm`, and compares every emitted address/word to the original
image. Memcpy and name lookup use reviewed complete code extents because they
do not have their own `.pdata` records. The allocator's inline jump tables
are data; their disassembler labels are not interpreted as instructions.

The full `82C181E8` entry is `348` bytes, SHA256
`078e1c130b8ee1b9c3a39689c1f2937152b0a2224fc8bfe54452193a70a299f9`.
It has exactly these external calls and no indirect call:

- `82C1824C ->82C16D90`: one aligned CPU allocation.
- `82C18384 ->82A3CD80`: descriptor copy.
- `82C183D4 ->82A3CD80`: default-storage copy.
- `82C18424 ->82A3CD80`: name-block copy.
- `82C18478 ->82A3CD80`: per-descriptor name-pointer copy.

`82C16D90` adds alignment `10` to the requested size, calls the existing
`8238E880` allocator with flags `24870000`, aligns the result, and stores the
raw allocation pointer at aligned-4. The recorded fixture also verifies the
live dispatch: `[82D57244]=82D5724C`, allocator vtable=`820B60B8`, slot0=
`8268DDA0`. These flags select the CPU allocation branch at `8238E9B4`, with
option descriptor `{2,10,0}`. The small aligned allocation reaches the existing
heap service at `8268DEF0 ->8285A190`, with the allocator's other fallback
edges retained in the evidence. Raw request size is `2B4` (692 bytes).

This closure is explicitly bounded at the existing heap services
`8285A190`, `82859930`, `82857038` and ABI save/restore helpers. Heap internals,
failure/fallback execution, all possible allocator instances, and the whole
startup call graph were not requalified. Full memcpy `82A3CD80..82A3D1D8`
and lookup `826B2528..826B2664` have no external call. No effect application,
shader, device, or console command-stream consumer is in the initializer's
direct helper closure.

The SDK constructor is static **caller context only**. At `82C1D0D4/D8`,
r27 receives B and r28 receives P; `82C1D0E8` makes r30=B+12. The original
first-initialization branch tests the shared count and P+100. At
`82C1D304/308`, r4=r28 and r3=r30, followed by `82C1D30C ->82C181E8`.
Thus the initializer receives the original serialized body, not a relocated
SDK FX body. The fixture never invokes the enclosing SDK constructor.
`82C1D2F8` routes a nonempty pool away from this initializer. The fixture
requires every metadata field and backing pointer empty before its only call.

The mutation tests bypass the whole-image hash gate to exercise semantic
checks: swapped arguments, relocated-body substitution, nonempty-branch
change, allocation target/link/alignment changes, an unexpected indirect
edge, pointer alignment, dirty-mask loop step, sentinel arithmetic, raw
backpointer, shared-handle bit, 208-byte name-size substitution, missing
cell indirection, shifted source, descriptor/default corruption, sentinel
and name association corruption, and a changed allocator vtable slot.
They mutate only offline bytearray copies.

## Exact source and owned layout

Serialized shared pointer fields use **two relative additions**:
`cell=F+U32(F+field)` and `data=F+U32(cell)`. Actual row1 values are:

- F+10C=`380`: cell `820D5ABC`, descriptors `820D9C3C`, 12 slots / 96 bytes.
- F+12C=`384`: cell `820D5AC0`, defaults `820D9C9C`, 320 bytes.
- F+290=`388`: cell `820D5AC4`, names `820D9DDC`, **209 bytes**.
- F+29C=`38C`: cell `820D5AC8`, name words `820D9EB0`, 12 words / 48 bytes.
- F+114=11 top names, F+11C=12 descriptor slots, F+124=1 bookkeeping word,
  F+134=11 leaves, F+13C=320 bytes, F+294=209 bytes.

The eleven NUL-terminated names consume 208 bytes. The source field includes
one additional zero byte, making the **copied block 209 bytes (`D1`)**.
Allocation is
`align4(align16(8*12)+320+209)+4*12 = 2A4` (676 bytes), not `2A0`.

The successful run recorded P=`E1A50400`, Q=`E1A66760`, raw=`E1A66750`:

- P+100=Q descriptors; P+104=12 slots.
- P+108=Q+60=`E1A667C0` default storage; P+10C=11 leaves; P+110=320 bytes.
- P+114=1 bookkeeping word; P+118=11 top names.
- P+11C=Q+1A0=`E1A66900` names; P+120=209 bytes.
- P+124=Q+274=`E1A669D4` per-descriptor names.
- P+180=Q; P+184=`2A4`; **P+188 remains 1**.

The fixture compares all 96 descriptor bytes, all 320 default bytes, and all
209 name bytes directly to the immutable serialized source. It validates all
12 relocated name words against actual source words, not a catalog fixture.
Descriptor zero's source word is `FFFFFFFF`. The original loop decodes that
to a zero source pointer but still subtracts the source name base and adds
the owned base. Therefore the stored word is **P.namesBase-source.namesBase
modulo 2^32**, not null. The fixture checks that arithmetic and never
dereferences the sentinel. The three alignment bytes Q+271..273 have no
specified initial value and are not assigned one.

P+000..00F becomes FF; P+010..07F remains its original zero; P+080..0FF
remains FF. The fixture snapshots the complete 200-byte-hex pool and compares
the entire expected post-call footprint. Unknown padding P+128..17F and
P+18C..1FF must remain byte-for-byte unchanged. It neither poisons padding
nor assumes it was initially zero.

## Original lookup and raw readback

The original call-free API `826B2528(P,namePointer)` reads P+118, P+100,
and P+11C. Its shared-handle encoding is
`(descriptorIndex<<18)|(precedingLeafCount<<1)|1`. Both original source-name
pointers and relocated owned-name pointers were passed through that actual
AOT entry; all eleven pairs returned the same expected results:

- `g_ViewProjection`: `00040001`.
- `g_WorldEyePosition`: `00080003`.
- `g_UTransform`: `000C0005`.
- `g_VTransform`: `00100007`.
- `kWorldToViewPortTfmLight`: `00140009`.
- `kWorldToViewPortTfmCharLight`: `0018000B`.
- `kShadowDepthSampler`: `001C000D`.
- `kShadowCharDepthSampler`: `0020000F`.
- `kShadowEdgeSampler`: `00240011`.
- `kShadowAmt`: `00280013`.
- `kIsShadowReceiver`: `002C0015`.

Actual original row0 private name `g_Weights` and wrapper name `fourtapblend`
both returned zero. The former is reached through row0's **direct-relative**
F+288, unlike row1's shared double-indirect field.

`run.log` also records each descriptor's first raw 16-byte storage slot.
For example `kShadowAmt` at storage+120 contains
`3EB33333,3E99999A,3F99999A,3F800000`; `kIsShadowReceiver` at +130 contains
`3F800000,00000000,00000000,3F800000`. These are raw readbacks backed by a
complete source-byte comparison. No vector/matrix/sampler setter semantics
or texture bindings are inferred from those values or names.

## Runtime and lifetime limits

The fresh `EngineCpuCalls` uses the saved initialized entry context, not the
exception-unwound startup context. Original calls preserve SP, LR and all
nonvolatile GPRs. No forged LR, return flags, callback substitution, production
table edit, synthetic FX object, or manually allocated pool is used.

After initialization and all queries, the complete row1 source and all 25
registration rows are unchanged. Pool root/global and original allocation
provenance remain valid, `Runtime.effectPoolThread` remains the real owner
thread, pool population `82E2D968` remains 1, and P+188 remains 1. Native
context `00900001` is unchanged and accepted by `requireContext`; FX count,
FX manager publication and console device global remain zero. The actual Dac
source was muted throughout.

Backing remains owned by the real root until terminal Runtime teardown.
The log explicitly reports terminal audio/driver cleanup with original graph
cleanup incomplete. **This is not normal CRT cleanup, a CF final-release
test, or leak proof.** Original CRT raw free does not imply pool-backing
cleanup. The fixture does not fake free, alter that protocol, exercise the
known nonfinal retirement-hook issue, or call initialization on a populated
pool. Nonempty merge, native FX creation/reflection with this pool, host leases,
normal root destruction, shader application, constants upload, rendering and
gameplay remain separate integration work.

Only the new fixture, this document, and `build/effect-pool-cpu/*` were written.
The previous caller sidecar, production sources, global registration data,
CMake/configuration, shaders, and original/reference files were not edited.
