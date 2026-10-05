# Original edge parameter storage and activation boundaries

This implementation is limited to the registered `simpsons_edge` effect
(`8202DF98`), typed vtable `820614E4`, and the original caller `823CA568`.
It retains the original typed finalizer `823C8F68`, which resolves the wrapper,
reflects it, and publishes technique/parameter handles at `82D09928..9954`.
The CPU private values live in native-owned mapped storage initialized from the
current owned defaults. There is no fabricated SDK FX header behind the effect
identity. The allocation is freed with the effect's original wrapper lifetime.

The original scalar setter `823C90D0` contains inline SDK descriptor and dirty
mask access. The following narrow cuts replace that access with checked private
parameter addresses. The original value loads and `stfsx`/`stwx` writes continue.

| Cut | Resume | Parameter / preserved work |
|---|---|---|
| `823C9100` | `823C9178` | LineWidth, handle `00340016`, slot368 |
| `823C91A8` | `823C9220` | DepthFadeControl, `003C001A`, slot400 |
| `823C9264` | `823C92E0` | TargetWidth, `0044001E`, slot432; original `lfd/fcfid/frsp/stfsx` |
| `823C92F4` | `823C92F8` | skip one SDK load, retain original height sign extension |
| `823C92FC` | `823C9314` | skip SDK loads, retain original integer scratch store |
| `823C9318` | `823C9358` | skip SDK resolution, retain original `lfd/fcfid/frsp` |
| `823C9364` | `823C9384` | TargetHeight, `00480020`, slot448; original final float store |
| `823C93A4` | `823C941C` | optional palette texture, `004C0022`, slot464 |
| `823C9448` | `823C94AC` | scene color texture, `002C0012`, slot240 |
| `823C94C0` | `823C9538` | depth texture, `00300014`, slot304 |

Offsets and slot sizes in this table are decimal; addresses and handles are
hexadecimal. The original video-mode queries and optional palette lookup remain
actual original CPU calls. Their returned resource words are stored unchanged;
a stored word is not permission to sample that resource.

The separate original boolean setter `823C8EB0` is admitted only at return sites
`823C9190` and `823C9238`, for DepthFade (`00380018`) and DebugEdges (`0040001C`).
It writes float zero/one according to the low byte of r5, matching the original
`clrlwi24/cntlzw/xori/vcfux/stvewx` sequence. Each operation changes only the first
word of its float4 slot and the corresponding native private dirty bit.

Each continuation checks its actual original 176-byte stack frame, r30's mask
table identity, current context/thread, typed vtable, wrapper/cache publication,
registered source, one completed typed reflection and live shared-pool owner.
`lastFunction` is only a trace of the last entered callee, not a call stack; the
original boolean and video helpers legitimately change it within this function.

The original setter reaches `826B3980` after these writes. The qualified edge
commit now uploads the active pass's numeric inputs and copied scene image.
The original-CPU fixture exercises three numeric/boolean profiles, compares the
complete private value block, checks untouched lanes and immutable original
bytes, and rejects a boolean write to a numeric handle. It also checks that
high bits outside the original boolean byte have no effect.

Activation at `826B5FC0` admits only `(manager,edgeWrapper,0003FFFC)` from
`823CA5B4`, with the original typed object and selected camera. It validates
the complete CPU cache row before selection, lazily compiles the two pinned
native shaders, preserves the original manager stores and calls actual
`826B35D8` to save/apply five scalar and six sampler requests. The pass's direct
SDK literal operations separately update effective native state; these do not
rewrite RenderWare caches. Only the first original 16-byte private dirty block
is filled. Real D3D11 VS/PS objects are bound and checked against the immediate
context. A valid repeated wrapper/technique pair returns without resaving.

Native end replaces `826B4B18` and manager end `826B4628`. Active ownership must
match the manager's wrapper/effect/technique. It runs actual `826B37B8` to restore
the saved application requests and clears the original manager selection.
The shaders and other pipeline bindings remain, matching original end; there
is no complete pipeline push/pop. An inactive manager's unspecified +8 word is
not read as an SDK object. Releasing an active edge effect is rejected.

Shader source/loop/parameter-binding evidence is in `native-edge-shaders.md`.
Original code is from the pinned flat image identified there. This boundary
does not admit another effect, shader, parameter setter or successful game frame.

## Commit and original rectangle integration

`826B3980`, from `823C9544`, requires the active typed edge owner, its genuine
176-byte setter frame, camera, wrapper/manager cache, native shader pair and
private storage. Exact immutable pass metadata associates color slot240 with
PS texture0, kernel slots528..640 with c20..27, width432 with c49, height448
with c48 and line width368 with c50. Only lanes read by the proven shader are
repacked into the 144-byte native constant buffer. The eight kernel float4s
must retain their authored defaults. Other parameters remain original CPU data.

The first original scene-copy owner must have received a real GPU copy from
the same active camera. The immediate context orders that transfer before
sampling. A native immutable commit lease retains the constant buffer, sampler
and sampled backing. Allocation, an unrelated texture ID, the second scene
copy, another camera, another device and an output alias do not grant sampling.
Actual PS constant/sampler/SRV bindings are checked before reporting commit.

At original `82C1DBAC`, r21 becomes E; `82C1DBE0` saves E at stack+68.
At `82C1ED00` a dcbzl clears E's complete aligned 128-byte private dirty line;
`82C1ED04..0C` load E+100 (the shared pool P) and clear its 128-byte dirty line.
The bridge clears the native private mask and genuine CPU pool dirty line only
after real commit succeeds. Neither immutable parameter masks nor values clear.

Three pinned cuts retain the original `823CA448` rectangle computation:

| Original SDK call | Continuation | Native operation |
|---|---|---|
| `823CA46C` | `823CA470` | validate original cached declaration and bind native float2/float2 layout |
| `823CA480` | `823CA484` | allocate owned 48-byte storage for primitive8, count3, stride16 |
| `823CA4F0` | `823CA4F4` | snapshot original vertices, submit native rectangle, release CPU staging |

Actual `8269D428` computes UV endpoints. Original loads/stfs write all twelve
vertex words. The consumer admits only clip corners (-1,+1), (+1,+1), (-1,-1)
and UV (0,0), (1,0), (0,1), deriving the fourth native strip corner from those
endpoints. It requires the same 128-byte original frame throughout, exact
declaration publication, unchanged active owner/camera and real commit bindings.
The original epilogue and paired effect end continue normally.

The bounded state is full-viewport, half-integer pixels, guardband1, solid/no
cull, RGBA replace, depth/write/stencil/alpha test/scissor/expanded blending off,
all sample-mask bits on, no tessellation, point/wrap min/mag and one texture
level. Line widths are the tested 0,0.5,1,1.5,2,2.5 profile. Real native
predication, additional programmable stages and stream-output bindings reject.
Output RGB is exactly0/1 and alpha1, so direct RGB10A2 quantization is exact;
no blend intermediate or clear is introduced. Original CPU state restoration
remains separate. GPU bindings and physical resource references can survive
effect end as in the original; stale native identities cannot authorize reuse.

The camera owner retains the original logical depth endpoints1->0. Camera
selection itself does not create a native viewport. At the qualified rectangle
draw the bridge establishes a full-size native0->1 viewport; this effect has no
depth reads/exports and both depth/write and stencil are disabled. Logical
endpoints and all original vertex words remain unchanged. This does not admit
a later depth-enabled consumer through that normalization.
