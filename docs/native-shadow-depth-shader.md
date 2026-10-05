# Original RenderShadowDepth vertex program

## Character parameters and native constant upload (2026-09-13)

Replay014 reached Main Menu at40.12 seconds and delivered the complete recorded
input sequence. At116.86 seconds, the real character completed shadow activation,
the skinning-flag setter, original826FE7C8 bone computation, and the native
826FDBE0 array setter. The live character uses **six bones**, not64. Its log
line195362 records the actual82D64080 source. The next failure was the old
edge-only826B3980 commit boundary. This is progress toward rendering, not gameplay.

The823C8EB0 entry now admits the exact shadow callers8270716C and827071CC with
the active reflected typed owner, native shader and constructor-owned camera.
kIsSkinned is handle00300014, descriptor00200008/00010010, private byte256.
It converts only the low input byte to float0/1 and preserves the other lanes.
The exact original character826FDBE0 caller827071A0 accepts kBoneMatrices
0040001C, descriptor00200102/03000041,64 children. Each child has descriptor
000005B0/000C0014+4*i, occupying64 bytes from private byte320. The original
mask lookup at826FDC74 extracts bits4..6 of the child descriptor, not its column
count. For000005B0 it selects82000EC0 (all ones), so the setter transposes all
four input vectors bit-for-bit. Count0 retains the original whole-array
convention;1..64 update only that prefix. Larger counts and unrelated handles,
owners, cameras or callers remain rejected. Bone computation remains original
CPU code. No SDK object is fabricated.

Original82C1DE7C and82C1E5E8 process category0 constant mappings. The exact
RenderShadowDepth context at body+51040 has no other active categories:

| Space / leaf | Handle | Mapping word | Actual upload |
| --- | --- | --- | --- |
| Shared0 | 00040001 | 00000C00 | g_ViewProjection, four vectors to c0..3 |
| Private2 | 000C0004 | 00000C0C | g_World, four vectors to c12..15 |
| Private10 | 00300014 | 00000028 | kIsSkinned, one vector to c40 |
| Private14..77 | (17+i)<<18 \| (14+i)<<1 | 00000834+3*i | First three complete vectors of bone i to c52+3*i |

The upload drops each transposed bone's fourth vector; it does not discard one lane from
each of four vectors. All81 private and11 shared mapping rows and all category
masks are checked against immutable original metadata. The native commit creates
and binds a real immutable3904-byte VS constant buffer. It validates the exact
shader and device, and only then clears the private and genuine shared128-byte
dirty lines, matching original82C1ED00/0C. No drawing occurs at commit.

OriginalShadowCameraPass covers both Boolean callers and low-byte cases, matrix
counts0,1,7,8,9,63,64, oversized rejection, asymmetric raw bits, neighboring
parameters and the original nonvolatile ABI. Actual GPU readback checks all976
constant words, all64 bone slots and repeated flag changes. An independent affine
check verifies that the transformed origin preserves the original position and
that unit axes preserve each basis vector. A rejected nonfinite
upload retains the preceding GPU buffer and dirty bits; successful uploads clear
the proper masks. Color and depth pixels remain unchanged throughout. Its
constant-upload focused run passes in0.61 seconds
(build/native-shadow-constants-tests.log). The preceding parameter-only complete
integration suite passes133/133 in93.53 seconds
(build/native-shadow-parameters-full-build-tests.log).
The first constant-upload integration build passed133/133 in93.96 seconds
(build/native-shadow-constants-full-build-tests.log), but both its setter and
test expectation misread the mask lookup as the column count. That passing run
does not qualify bone layout. The correction above transposes at the setter and
adds the independent affine geometry checks; fresh qualification is required.
The corrected focused test passes in0.66 seconds
(build/native-shadow-bone-transpose-tests.log).
The corrected complete integration build passes133/133 in92.73 seconds
(build/native-shadow-bone-transpose-full-build-tests.log).

The82706378 entry has a read-only diagnostic for the first real character mesh.
It checks the active constant owner and reports existing CPU metadata; original
mesh walking continues afterward. Mesh resource/declaration binding, native
viewport/depth adaptation and drawing remain unfinished.

Replay015 (before the matrix-layout correction) reached the actual mesh entry
at115.37 seconds: objectE1B524E0,metadataE8227340,geometryE8227378, six bones and
one submesh. Its read-only geometry header reports stride48, vertex data near
E822793C,11376 vertex bytes, and index dataE82274B8 with1154 bytes. These are
observed CPU fields, not a qualified native mesh resource. Original82706378
called8243C5C0, which attempted a console-context write at0090077D and correctly
failed the runtime memory guard. This older run is not bone-layout qualification.
The next implementation boundary is the original character vertex stream bind
(827063BC), followed by declaration82445798, index binding8243C768 and indexed
draw8244D360. No mesh draw or playable frame has been observed.

Replay016 used the corrected transpose build. Its actual six-bone update, native
constant commit and mesh entry appear at log lines85121..85125. The same console
stream-binding write failed at85126..85127. Its90-second helper observation
finished while the movie was still playing; result.json therefore describes
completed inputs with gameplay_verified=false, not a successful game transition.
The subsequent natural game failure is recorded in game.log. The inspected
automatic main-menu cue frame still showed profile loading, leading to the
Continue Game text cue correction documented in automatic-startup.md.
Replay018 then verified the corrected Continue Game cue visually and completed
the same live bone/constant update at log lines147004..147005. Its actual mesh
atE8229378 (one submesh,six bones,stride48) reached the same0090077D stream-bind
memory guard at147009..147010. The game exited naturally after the helper's
90-second observation had ended. No game process remains. All four existing
profile/save/index/achievement hashes were verified unchanged after this run.

## Character-pass activation (2026-09-13)

Automatic replay012 reached the verified Main Menu at38.23s, pressed Continue
Game and played the opening movie. At114.47s the original first character pass
reached the same826B5FC0/8270715C/0007FFFC boundary. Existing-save loading is
now working; the unchanged save, profile, index and achievement hashes were
checked after both successful menu runs.

The native activation now validates the real reflected shadows owner and its
constructor-owned1024 camera, selects original technique index1/RenderShadowDepth,
and checks its exact cache header, scalar rows and dispatch targets. The retained
826B35D8 routine saves and applies the real application states; the three literal
SDK states are also applied through the native owner. Only the exact compiled
VS820C2FA0 is bound, and the original null pixel shader is explicitly cleared.
The actual D3D11 bindings are read back and checked against the compiled owner
and device. Constants, mesh association and draws remain separate requirements.

Original repeated-pair activation retains its saved state. Original826B37B8
restores it on end, and the manager/cache selection is released. Active shadow
effects cannot be destroyed. No other shadow technique is admitted.

OriginalShadowCameraPass now runs the original82707138 character entry through
activation to its still-guarded skinning flag setter823C8EB0/8270716C. It verifies
one compiled shader, actual manager/cache selection, unchanged saved cache on a
repeat, dirty-constant extent, unchanged color/depth pixels, and original cleanup
restoring all three prior application states. The first fixture attempt reused
the wrong active CPU call scope for cleanup; that harness error was corrected.
The focused test passes0.61 seconds (build/native-shadow-depth-bind-tests-2.log).
The complete integration build passes133/133 tests in93.90 seconds in
build/native-shadow-depth-bind-full-build-tests.log. Live replay013 reached
Main Menu39.26s after launch and confirmed the actual shadow VS bind at log
line191493, id00500005,typedE4CEA400,cameraE2CA7550. The next line stopped at
the still-guarded boolean setter (original823C8EB0,caller8270716C), at115.05s
after launch. The next operation supplies kIsSkinned, private handle00300014,
descriptor00200008/00010010, storage slot16. Original82707138 then computes
bones with826FE7C8, uploads through826FDBE0, commits at826B3980, and draws via
82706378. Those parameters and mesh/draw operations remain unfinished.

A manual A hold during the movie was consumed at log line118644; the inspected
movie frame continued and the first world transition took the same approximate
75-second movie interval. It was not added to the automatic sequence.

Boot203 delivered a normal Start tap after the opening VideoDecodeThread exited
and the next edgeAA frame2432 completed. The game still attempted its shadow pass
before any visible pause/menu frame. The exact next boundary is826B5FC0,
LR8270715C,technique0007FFFC,typedE4CEA400,wrapperE1A9E3C0,managerE1A9C3C0.
This is original82707138's character path and RenderShadowDepth. Its vertex
program is820C2FA0; the original pass has no pixel shader.

## Static program and dataflow

The original image remains15466496 bytes,SHA256
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0.
The exact4396-byte shader record has SHA256
b22ecbda409e80ba9bbc5a8da237c8894c2f09c79b93646868e925c70ef058be.
Its708-byte program starts at record+3688. The preceding64 literal bytes encode
c254=(0,1,3,1000),c255=(60,0,0,0). The trailing words are
4E4A000A,01EA67DC,D36BD9F6; the trailer's low nibble is not assumed to be a stage.

tools/analyze_shadow_depth_shader.py pins every control-flow pair, all51 issue
slots, original literals and whole record. Slots7..10 are four vertex fetches;
slot11 is an ALU predicate write, not a fifth fetch. CF1 skips the20 skinning
slots when c40.x is zero. Otherwise four bone matrices blend in W,Z,Y,X order,
with original rounded address registers and three vectors per bone. The shader
transforms the source position with this blended matrix. It retains Y at/above
1000 and caps lower Y at60, then applies the world and view-projection matrices.
It exports clip position, source UV.xy and a second copy of clip position.

Original SUB_CONST_0 at slot33 reads -c255.x and -r0.w. Its temporary operand
index is encoded separately from the constant index. The source modifier applies
to both operands. Co-issued address-register updates read the old address for
the vector operation. The authored HLSL retains these operations and permutations.
Only static field definitions and operand parsing were read from the existing
pinned local reference; no reference interpreter or graphics backend is run.

## Native validation and remaining integration

renderer/shadow_depth_shader.hlsl compiles offline into a real D3D11 vertex
shader. Four decoded fetch payloads enter as TEXCOORD0..3; these names describe
the native input contract, not an already-qualified original mesh declaration.
The constant bank includes original c0..3(view-projection),c12..15(world),
c40.x(skin predicate),c52..243(bones). Finite inputs and integer bone indices0..63
are the current domain. Original vertex-format decoding, binding ownership and
native viewport/depth adaptation remain separate requirements.

tests/test_shadow_depth_shader.cpp observes the actual VS with a test-only
stream-output geometry stage. Its independent reference blends affine matrices
and transforms points geometrically; it does not duplicate the shader register
or swizzle schedule. Each device exercises7040 vertices in10 draws, including
all64 bones, zero/one-hot/fractional weights, both skin branches, negative
nonzero predicates, height threshold neighbors, non-diagonal world matrices,
projective W, UV preservation and both clip exports. Original console arithmetic
and raster precision are not claimed identical to native D3D11.

build/shadow-depth-shader-tests.log passes WARP, hardware and static evidence,
3/3 in0.56s. The native material compiler now admits this exact original record:
14 records have native artifacts and222 remain untranslated. This does not yet
activate the original shadow pass or claim a game shadow draw. The complete
integration build compiled successfully and passed122/123 tests in236.19s;
OriginalEngineResourceBridge still expected this shader to be unsupported.
Its allowlist was corrected, then the focused rebuild and test passed1/1 in0.14s
(build/shadow-depth-resource-bridge-build.log and
build/shadow-depth-resource-bridge-tests.log). These are a full run plus a focused
correction. The subsequent local-identity integration build includes this
correction and passed a complete123/123 suite in233.33s; see
build/local-identity-integration-build.log.
Main-menu acceptance remains outstanding.
