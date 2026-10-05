# Original built-in image decoding and nonlocal returns

Historical build162 record. Build163 adds native ownership and original mip
filtering; see [native built-in textures](native-builtin-textures.md).

Build162 adds a verified native control-flow correction and qualifies the
original CPU image decoder. The engine memory-image loader remains guarded at
82B84838, caller826FF1D8. These checks establish decoded CPU pixels and resource
requirements; they do not establish native texture upload, mip filtering,
sampling, original game images or gameplay.

## Original execution

The original parent826FF0F8 requests three embedded images through82B84838:
white82CED9A8/102C bytes, black82CEECF0/102C, gray82CEE9D8/312.
Their original immutable data hashes and headers are pinned in
build/builtin-textures/evidence.json. White/black are32x32 uncompressed32-bit
TGA images; gray is16x16 uncompressed24-bit TGA. The unused gray color-map
entry-size field is24 even though no color map is present.

The wrapper82B84838 and extended wrapper82B84550 select default dimensions,
mip levels and format, usage0, pool1, filter-1, mip filter-1, color key0,
no info/palette output and texture kind3. Their original generalized loader
82B83D20 resolves the filter to00080004 and mip filter to5. The
[Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxcreatetexturefromfileinmemoryex)
corroborates triangle/dither and box defaults; original Xbox instructions and
execution, rather than PC documentation alone, establish this profile.

Original CPU image constructor82BBD720 initializes only7 of21 words. The
decoder82BC2FF8 tries file types in order0,5,4,1,3,8,7,2,6. JPEG type1 calls
82BC0470; TGA type2 calls82BC0A60. The initial probe failed after JPEG rejection:
the restored guest LR pointed to82BC04C4, but the native call stack returned to
82BBD7EC, whose original instruction is an illegal zero word. That trap was
correctly retained and exposed the missing nonlocal transfer.

## Native control flow

The new generator setting `nonlocal_setjmp_address=0x82A43E50` emits a native
scope and exception handler around each compiled function containing a direct
save call. Continuations are static labels at the original return addresses.
All11 unique save sites are emitted; overlapping recovered function bodies
contain15 occurrences in14 scopes. There is no runtime instruction decoder,
native jmp_buf stored in guest memory, or native longjmp across C++ objects.

The original82A43E50 save routine executes. Its316-byte type-zero buffer contains
f14..31 at00..88, SP at90, r13..31 at98..128, CR at130, LR at134 and the zero
discriminator at138. VMX, FPSCR, XER and CTR are not saved by this routine.
The optional extended handler global82E3E26C must be zero; other modes fail
explicitly before saving. Runtime registration is tied to the live native
scope, the exact CPU context and thread, and the captured original buffer.

The hook at82A43D30 validates the live buffer before original mutation.
The original restore routine runs, including82A51C80 bookkeeping and zero-to-one
return-value normalization. Its bookkeeping argument reads the high BE32 word
of the saved64-bit SP, as the original instruction specifies. The hook replacing
only its terminal blr at82A43E18 verifies the restored registers and throws the
native transfer. C++ unwinding releases intervening native scopes, and the
matching generated caller resumes its static label. Nested nonmatching scopes
propagate the transfer. Registration expires when a scope exits.

Changed buffers, expired registrations, another CPU context/thread, mismatched
continuations, misalignment and extended save mode reject explicitly. Repeated
saves replace the previous capture; repeated jumps to a still-live save work.
Indirect saves without a registered static continuation cannot later jump.

## Decoded profile and resource requirements

The original decoder executes after muted original startup has initialized the
CRT and allocator. Four initial metadata poisons qualify each image. Every
converted texel, all21 image words and all7 file-info words are checked.

| Image | Decoded dimensions | Original format | Decoded BE32 pixel | Row bytes | Source levels | Selected levels |
|---|---|---|---|---|---|---|
| White |32x32x1|18280086|FFFFFFFF|128|1|6|
| Black |32x32x1|18280086|FF000000|128|1|6|
| Gray |16x16x1|28280086|FF808080|64|1|5|

The decoder creates an owned pixel allocation, no palette, no linked mip/face
images, resource type3 and file type2. Original destructor82BBE1F8 frees owned
data through8238EB00 but leaves every CPU image field unchanged, including its
retired pointer. The fixture observes later allocator reuse and never reads
pixels after their destruction. Source blobs and surrounding metadata canaries
remain unchanged. Null-source/zero-length requests return original8876086C
without changing the image object.

Original82B7F950 selects the levels shown above with actual live native context
identity, dimensions from decoding, default requested mip countFFFFFFFF,
usage0, decoded format, pool1 and kind3. This CPU helper never dereferences the
device argument:82B7DE10 ignores it,824B7408 is an original blr, and8244E5E0
tail-copies the immutable304-byte table at8206AA30. That table contains texture
caps0001EC45 and maximum width/height8192. The original format descriptor table
821A2D60..821A3A30 is also pinned. No capability values are invented or derived
from the Windows device.

## Verification and next boundary

OriginalNonlocalJump passes699 checks over18 original restores, including four
return values, three buffer poisons, full saved integer/floating registers,
non-saved state, repeated saves, nested unwinding, native destructors and invalid
registrations. OriginalBuiltinImageDecode passes10,365 checks over54 direct AOT
calls and12 decoded images, plus rejected null/empty requests. Generated
continuation handling executes in the full original decoder sequence.

Evidence validation pins17 original spans/1,964 instruction words, all11 save
sites,28 direct/tail jump sites, original image/capability/format data hashes,
the three relevant hooks and all177 configured hook byte ranges. The production
loader guard is unchanged. Next implement and verify native image ownership,
the selected formats and complete mip chains, upload/filter equivalence and
subsequent consumers before replacing that guard. The original loader reads SDK
texture headers after allocation; simply returning an unmapped native identity
from its low-level factory would be incorrect.

Build and actual-run results are frozen in
build/builtin-textures/build162-summary.json, build162-tests.log,
aot-manifest162.json and build/boot-105.log. Earlier reflection lifecycle,
camera, cube-layout and effect verification remains applicable.
