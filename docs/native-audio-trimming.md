# Original EXm0 trimming and BE16 conversion

The CPU conversion boundary is independently usable: **8233F250** reads signed
BE16 PCM and writes planar floats equal to `sample / 32768`, with no XMA imports,
allocation or codec-object access. Its **stereo destination clear touches sparse
32-byte chunks beyond one plane's requested samples**. The codec's 384-sample
skip and 512-sample carry are separate operations upstream of this converter. Stock FFmpeg's already
trimmed output cannot be fed through those operations a second time without a
proved frame-origin mapping.

Scope: original instructions and local primary source, read-only. This report
adds no production converter, decoder, SDK context or test executable. Addresses
and unprefixed field offsets below are hexadecimal; sample counts and equations
are decimal unless prefixed with `0x`. The original flat image is
`analysis/simpsons.pe`, base `82000000`, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The reproduction block checks complete function bytes and `.pdata` extents.
Earlier pool/creation evidence remains in [native-audio-boundary.md](native-audio-boundary.md).

## Immediate converter ABI and storage contract

Call **8233F250** with these registers (the actual call is **8233FD78**, returning
to **8233FD7C**):

```text
r3  unused incoming value; original caller passes V, isolated fixture may use 0
r4  output plane 0 address
r5  output plane 1 address; unused in mono
r6  BE16 PCM ring base
r7  starting BYTE offset in ring
r8  layer channels: 1 = mono; original valid alternative = 2, interleaved stereo
r9  tail count: sample FRAMES starting at ring + offset
r10 head count: sample FRAMES starting at ring base, appended after tail
```

The function treats every `r8 != 1` as the stereo branch, without validation.
This is not support for arbitrary channel counts. It rounds `r7` down to an even
byte address at **8233F26C/270**; valid original stereo offsets are whole frames,
thus multiples of four. Negative counts, arithmetic overflow, aliasing input
with outputs, and invalid memory are outside the established calling domain.
The return register is scratch, **not a produced-count result**. Use normal
`EngineCpuCalls` stack/linkage setup: this function saves registers and uses
scratch below SP and at SP+2C even though it has no `stwu` frame of its own.

Let `C` be 1 or 2, `T=r9`, `H=r10`, `N=T+H`, and `O=r7`. The sample mapping is:

```text
source(j,c) = ring + O + 2*(C*j+c)          if j < T
              ring + 2*(C*(j-T)+c)         otherwise
out_c[j]   = float(signed_BE16(source(j,c))) * 2^-15
```

For the original layer ring, require `0 <= O < 0x1800`, `O % (2*C) == 0`,
`O + 2*C*T <= 0x1800`, and `2*C*H <= 0x1800`. The converter itself does not
enforce the ring capacity or decide when to wrap; its caller computes both
counts. The caller normalizes an exactly exhausted cursor back to zero at
**8233FDBC..FDD0**. Source must be readable for the executed scalar/vector loads;
use a mapped, padded ring fixture. Outputs must be float-aligned and writable
over both conversion spans **and the following clear footprint**.

### Exact `dcbz` footprint — important fixture correction

If `outLeft % 128 != 0`, neither branch runs its initial `dcbz` loop. Otherwise:

```text
mono:   K = floor(N/32)
stereo: K = floor(N/16)
for k in [0,K): clear [outLeft + 128*k, outLeft + 128*k + 32)
```

Mono tests `N >= 32` and shifts by five at **8233F2C0..F2E8**. Stereo tests
`N >= 16` and shifts by four at **8233F50C..F534**. Both advance the clear address
by 128 per iteration. **The instruction is plain DCBZ (32 bytes), not DCBZL
(128 bytes).** Original words `7C055FEC` and `7C045FEC`, after removing the RA/RB
fields, both equal `7C0007EC`; the `00200000` extended-size bit is clear. The
read-only Xenia opcode table distinguishes `7C0007EC` from `7C2007EC`, and
`ppc_emit_memory.cc:1108..1130` implements 32 versus 128 bytes respectively.
The current generated original body likewise uses 32 bytes. No generator
change is justified. The earlier draft conflated the 128-byte loop stride with
the clear width; **that contiguous-clear claim is withdrawn**.

**Stereo clears only from outLeft, not separately from outRight.** It
subsequently writes `4*N` converted bytes at each actual output pointer.
Therefore the total write set is the union of these sparse clear chunks and
the conversion intervals. Clear-only bytes must become zero; bytes outside
the union must remain unchanged.

For stereo `T=256,H=0`, there are 16 clears of 32 bytes, starting at offsets
`0,80,...,780`; the last ends at `7A0`. A sentinel at `outLeft+400` is cleared,
but the gap `outLeft+420..47F` must stay unchanged unless an actual conversion
span covers it. No doubled sample count or changed input ABI is required to
explain that write. For `N=17`, there is one 32-byte clear, then 68 converted
bytes per plane. Clear-only bytes are not additional decoded samples.

The original caller supplies contiguous channel planes. At **8233FD24..FD58**:

```text
B = BE32[output_descriptor + 4]
S = BE16[output_descriptor + E]    # plane stride in float elements
layer i left  = B + 8*S*i          # channel 2*i
layer i right = B + 8*S*i + 4*S    # channel 2*i+1, only if stereo
```

Require requested output per plane `N <= S`. A stereo pair then owns `8*S`
contiguous bytes and every clear chunk fits within that pair. The sparse
chunks can touch the gap between a partial left result and the right plane.
A separate-pointer diagnostic fixture instead needs writable left-side storage
through `128*(K-1)+32` for K>0, in addition to both actual conversion spans.
The converter neither allocates nor releases outputs. The generic constructor's
optional internal output allocation (**823405D0..82340648**) requests
`4 * channels * descriptor_stride` bytes, alignment 128; stores the allocation
at V+10, and its pointer/stride in the output descriptor. This corroborates
capacity and layout, not permission to free it in the converter. EXm0's
descriptor D+18 is zero; do not assume this optional allocation branch runs for
EXm0 or gives every externally supplied output descriptor the same owner.

### Exact numeric result, not hardware quantization proof

**821DD2AC = 38000000** is float `2^-15`. Scalar `lha` / signed integer-to-float /
multiply and vector signed-halfword unpack / `vcfsx ...,15` express the same
exact values. Mono uses 16-sample vector chunks; stereo uses 32-frame chunks,
with scalar remainders in each tail/head span. Endpoints as guest float words:

```text
signed16   -32768       -1          0          1          32767
float bits BF800000     B8000000    00000000   38000000   3F7FFE00
```

Every signed16 value and its multiplication by a power of two are exactly
representable here. This proves **BE16 -> float**, including positive zero.
It does not prove how XMA hardware quantizes its internal decoded samples to
BE16: rounding direction, saturation, dithering, and transform differences from
FFmpeg remain unproved. A proposed `round(float*32768)` or truncation-and-clamp
step must remain a candidate, not a bit-exact hardware implementation claim.

## Segment skip, remaining samples and carry

The codec callback **8233FAF8(V, output_descriptor, request)** takes r3/r4/r5 and
returns its accumulated produced count in r3. Request zero returns zero.
Relevant existing CPU fields, all words BE unless stated otherwise:

- V+24: relative offset of segment array; each segment is `0x14` bytes.
- V+31: current output segment index (byte); V+32: capacity (byte, constructor
  initializes `0x14`). V+30 is the separate compressed-input feed index.
- Segment+0: borrowed compressed-layer data pointer; segment+C: sample extent
  `Nseg`; segment+10: byte `F`, tested as **zero versus any nonzero value**.
- V+38: current segment pointer; V+48: remaining samples of that segment;
  V+4C: carry; V+54: reset/error-output flag (byte).
- V+34: layer array; V+44: layer count. Each `0x18`-byte layer has PCM cursor
  at +8, channels byte at +C, and outstanding skip count at +10.

The segment byte is separate from the low two bits of a compressed layer's
size word, used for rate selection by **8233F0A4..F0B0**. This pass proves the
byte's consuming behavior, **not its producer or a name such as final-block,
loop, seek or continuation flag**. Nonzero means continuity-preserving in the
equations below; this is a behavioral description.

### Beginning a segment

When V+48 is zero, **8233FB3C..FBC4** selects
`segment = V + V[24] + 0x14*V.byte[31]`. Its +C must be nonzero: the empty-slot
branch leaves r6=0 and the next load reads byte `[r6+10]`. Thus this callback
requires a queued output segment; calling it against an empty slot is not a
safe input-availability query.

```text
if F == 0:
    for every layer: layer.skip += carry + 384
    carry = 0
# if F != 0, neither outstanding skip nor carry is changed here
V.current_segment = segment
V.error = 0
V.remaining = Nseg
quota = min(request_remaining, V.remaining)
```

These additions and count operations are original 32-bit arithmetic. A future
native preflight must reject overflow and invalid extents instead of silently
wrapping or accepting negative counts. The intended nonnegative finite domain
is what the following mathematical notation represents.

### Consuming skip before emitting output

Availability `A` is in bytes, derived from the hardware write cursor and input/
output-valid state. The original can distinguish equal cursors using validity;
do not replace that with an unconditional empty-ring assumption.
**8233FC6C..FCCC**, for positive availability and outstanding skip `K`, performs:

```text
skip_bytes = min(A, 2*C*K)
cursor = cursor + skip_bytes; if cursor >= 6144: cursor -= 6144
K -= floor(skip_bytes / (2*C))
A -= skip_bytes
```

Frame-aligned finite inputs make that division exact. A single subtraction
wraps the original cursor; an arbitrary oversized availability is not supported.
Only once `A >= 2*C*quota` does the layer convert the quota. Tail/head are split
at the 6144-byte ring boundary. Skip can span several refills; it is not applied
again merely because the caller requests a small amount or input temporarily
runs out. The hardware read-offset publication granularity is a different
quantity from this exact CPU cursor/skip accounting.

### Completing a segment — zero remainder retains prior carry

After conversion, request remaining and V+48 decrease by quota. When V+48
becomes zero and quota was nonzero, **8233FF28..FF5C** performs exactly:

```text
t = Nseg + (384 if F == 0 else 0)
r = t & 511
if r != 0:
    carry = 512 - r
# if r == 0, NO STORE: retain current carry
if V.error != 0:
    carry = 0
```

The often tempting replacement `carry = (-t) % 512` is wrong when `F != 0`,
`t` is aligned and a prior carry exists. With F=0 the start branch has already
cleared carry, so the aligned result is normally zero. Examples:

```text
F=0, Nseg=128: start skip += old_carry+384; end carry=0
F=0, Nseg=129: start skip += old_carry+384; end carry=511
F=1, Nseg=512, old_carry=127: no start change; end retains127
F=1, Nseg=513: no start change; end stores511
```

Constructor **8233E9C8** initializes carry and layer skips to zero. Reset
**8233FF80** marks V+54, reinitializes hardware and clears layer cursor/skip/feed
state. It does not itself unconditionally clear V+4C; the segment-completion
error branch above does. The original error path can fill output with zero;
that is not authorization to translate a native decoder failure into successful
silent output.

### Partial requests, segment boundaries and source lifetime

Within one active segment, partial requests retain V+48, the exact layer cursor,
and outstanding skip. Finishing a PCM-ring tail and reading its head is fully
specified by the converter and does not itself mean a new compressed segment.

There is a **caller-contract limit** for a single decode call that would cross
an entire segment. 8233FAF8's final branch can loop, but it does not advance V+31;
its normal conversion plane addresses are rebuilt from the descriptor base
without adding accumulated-produced r20. The error-fill branch does add r20.
This pass has not proved the higher caller's segmentation/descriptor advancement.
Do not fabricate an all-segments append rule or invoke this callback past the
current segment in a purported equivalence fixture. First qualify partial
requests bounded by the current remaining extent, then retain/verify the
original outer queue advancement before testing transitions between segments.

**8233F000** feeds compressed inputs, including copying at most 2048 bytes at a
time into the original input slots. When its input queue helper **8233E108**
returns null, **8233F090 -> F244** returns without declaring EOF, clearing carry,
or manufacturing PCM. The decode callback may retry/re-enable and enter its
original timeout/error path if it cannot satisfy a quota; this does not make
starvation equivalent to successful end-of-stream. A native asynchronous codec
must keep pending packet/frame/skip state, and expose actual failure or pending
work through a separately proved engine contract.

The source bytes are not freed by the converter or feeder. A bounded owner
consumer, **823417B8..82341894**, examines 20 request records and considers their
segments consumed using segment+C minus V+1C for the current V+31, or segment+8
for another index. It then clears the request status and can free the tracked
source allocation through **8234187C -> 8238D640**. Thus copying/accepting an
input packet alone is not proof that the engine may release the original
segment owner. Full outer queue and normal end/seek ownership remain outside
this bounded proof; preserve them until qualified, or own a real packet copy
without accelerating guest completion.

## Stock FFmpeg work already performed

Primary source was read directly **inside** the read-only archive
`K:/DarkRecomp/build_native/deps/ffmpeg-darkxma-upstream.tar.gz`, prefix
`FFmpeg-1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c/`. No archive files were
extracted. This pins source behavior; the installed successful probe is
`n8.1.1-7-g3728de467d-20260519`, so the archive is not asserted to be that binary's
exact source revision.

In the archive's `libavcodec/wmaprodec.c`:

- Lines 427 and 1518..1523 initialize and consume `skip_frame`, suppressing the
  first decoded frame. The XMA frames at this boundary are 512 samples.
- Lines 1477..1484 read bitstream trim_start/trim_end. Lines 1840/1845 additionally
  establish 64 skipped samples. `libavcodec/decode.c:358..385` removes samples
  from returned audio; `AV_CODEC_FLAG2_SKIP_MANUAL` changes that latter removal
  into metadata, but does **not** restore the already suppressed first frame.
- Lines 1919..1920 hold up to 4096 samples in the non-EOF FIFO. This affects
  availability as well as sample count.
- At EOF, lines 1926..1928 remove
  `clip(trim_end + trim_start - 128 - 64, 0, available_samples)`. The inner
  decoder's zero-byte packet path at 1628..1649 also emits retained overlap.
  Flush resets the initial frame-skip state at line 2055.

The stock initial 512+64 treatment is **not the engine's 384 rule**. Do not
blindly subtract another 384 or claim a universal 192-sample correction: the
relative origin of hardware PCM and FFmpeg's raw transform frames still needs
proof, including final overlap and segment transitions. Exact declared output
counts in the two finite mono probes establish a useful decoder capability,
not sample identity or streaming timing.

The read-only DarkRecomp patch supplies a *custom* raw-frame mode that bypasses
the stock first-frame/FIFO/64-sample/tail processing and emits one mono/stereo
stream's raw 512-sample frames. It is a candidate way to make trim ownership
explicit, not a stock FFmpeg option or a Simpsons-verified integration. No
hardware-context model is needed to study these codec/layout operations.

## Differential fixture and next qualification

The parent owns [test_audio_pcm.cpp](../tests/test_audio_pcm.cpp) and original
AOT execution. **Build113 passed all 29 tests**; its build log independently
confirms `OriginalAudioPcmConversion` passed. The parent reports **664 cases and
225,264 exact float samples**, covering the full signed16 mono/stereo domain,
wrap and tails, sparse 32-byte DCBZ chunks with preserved gap guards, and
SP/LR/nonvolatile GPR preservation. The earlier stereo sentinel failure was
resolved by correcting the expected sparse clear footprint, without a
production or generator change. These results qualify the original CPU
converter; they do not establish raw-codec frame alignment, hardware
quantization, or a complete game audio callback. This bounded report is frozen
with that distinction; the separate native raw-codec work adds no claim here.

The meaningful converter fixture executes original 8233F250 and compares the
entire mapped source/output/sentinel arena against independently computed
BE-float words, applying each 32-byte clear before conversion writes and
preserving the 96-byte gaps except where conversion writes overlap. Cover
all signed16 values on both asymmetric stereo channels; tail/head splits around
15/16/17 and 31/32/33, wrap and no wrap, zero counts, aligned and float-aligned
unaligned output bases, separate plane spacing, and SP/LR/nonvolatile GPR
preservation. Count result floats separately from clear-only capacity. No
hardware output or invented audio is required for these mathematical fixtures.

After that, the minimum remaining trim qualification is an engine-owned stream
fixture with a proved raw frame origin. Preserve residual compressed bits,
decoded frames, per-layer skip and carry across arbitrary input chunk splits.
Compare one-shot versus partial consumption **within each current segment**;
include F=0/nonzero, aligned remainder with a prior nonzero carry, and skip that
crosses several refills. Feed no EOF at temporary input exhaustion. Qualify
outer segment advancement and final drain separately. A failure/reset fixture
must distinguish the original explicit error-fill branch from real decoded
samples. Hardware-equivalent float-to-BE16 quantization remains an independent
unresolved requirement even if every CPU conversion fixture passes.

## Reproducible read-only byte and equation checks

Validation passed on the original image: **9 function/leaf hashes, 29 selected
instruction/constant words, 65,536 signed16 values, 32,768 carry cases, 6,145
sparse-clear capacity cases, and four reference source hashes**. This is the
completed evidence validation for this document; the parent's separate AOT
fixture results are not included in those totals.

Run the following Python block from `K:/SimpsonsNativeCopy` with `python -B`.
It writes nothing. These are byte/provenance and mathematical checks, **not an
execution of the original CPU decoder**. Whole-function hashes cover additional
instructions beyond the selected semantic words. Two small leaves have manually
reviewed `blr` extents; other extents must agree with original `.pdata`.

```python
import hashlib
from pathlib import Path
import struct
import sys
import tarfile

sys.path.insert(0, 'tools')
import analyze_poststart_integration as a
b = Path('analysis/simpsons.pe').read_bytes()
a.validate_identity(b)
_, pdata = a.layout(b)
FUNCTIONS = {
    0x8233F250: (0x89C, '9eb1c0cca32c571a144bab02c3af27311281ee9638e3082f1597731bbd79e516'),
    0x8233FAF8: (0x484, 'feae5c3a4877b181a3101fc3969699c88d67883d1ec92951b4447043a1004276'),
    0x8233FF80: (0xB8, '452e533af99aadc160a98dc92dfbcddda168de42877ea1a56bb9f81d8097338a'),
    0x8233F000: (0x24C, '574a68fbfc334602c29df3d71c754d1b54ab63720e4c0e143efa9533fae02e5c'),
    0x8233E9C8: (0x228, 'ce0b448956bac299b2d91dd95db7ad48effa5f0d91273b857d9c16d9aa312eb2'),
    0x823404A8: (0x1E4, '51b11d9f5f257c69fab285b67bfc1b4a963ad524b1c99b09b341101bcb084599'),
    0x823417B8: (0xE0, 'fed4b7941a662ce9703042b680bf989e2173a221f11432003d87eaaad3a24c9c'),
    0x8233E108: (0x58, '32416150b5862ff66014c66f5f4642e0e70244268999456d3fce51941979abf6'),
    0x8233E0E0: (0x18, 'ac1c2f6e248fdc2a085d3b73c49b506975a307c11206f84bd60e1a26b107362a'),
}
for va, (size, digest) in FUNCTIONS.items():
    if va not in (0x8233E108, 0x8233E0E0):
        assert pdata[va][0] == size, hex(va)
    else:
        assert a.word(b, va + size - 4) == 0x4E800020
    assert a.sha(a.span(b, va, size)) == digest, hex(va)
WORDS = {
    0x8233F258: 0x7C8B2378, 0x8233F268: 0x2F080001,
    0x8233F26C: 0x7CE80E70, 0x8233F270: 0x5508083C,
    0x8233F2CC: 0x2F070020, 0x8233F2D8: 0x54E7D97E,
    0x8233F2DC: 0x7C055FEC, 0x8233F518: 0x2F050010,
    0x8233F524: 0x54A5E13E, 0x8233F528: 0x7C045FEC,
    0x8233F3C0: 0xA9280000, 0x8233F3DC: 0xEDAD0032,
    0x8233F830: 0xA8A70000, 0x8233F854: 0xA8A90000,
    0x8233FD24: 0xA176000E, 0x8233FD38: 0x54C61838,
    0x8233FD3C: 0x5564103E, 0x8233FD78: 0x4BFFF4D9,
    0x8233FB64: 0x89660010, 0x8233FB6C: 0x4082004C,
    0x8233FBA0: 0x39080180, 0x8233FBB4: 0x92BC004C,
    0x8233FF3C: 0x396B0180, 0x8233FF40: 0x556B05FF,
    0x8233FF44: 0x4182000C, 0x8233FF4C: 0x917C004C,
    0x8233FF5C: 0x92BC004C, 0x8233F090: 0x418201B4,
    0x821DD2AC: 0x38000000,
}
for va, expected in WORDS.items():
    assert a.word(b, va) == expected, hex(va)

for sample in range(-32768, 32768):
    value = struct.unpack('>f', struct.pack('>f', sample / 32768))[0]
    assert value * 32768 == sample
assert [struct.pack('>f', s / 32768).hex() for s in (-32768,-1,0,1,32767)] == [
    'bf800000', 'b8000000', '00000000', '38000000', '3f7ffe00']

def carry_after(n, flag, old, error=False):
    current = 0 if flag == 0 else old
    remainder = (n + (384 if flag == 0 else 0)) & 511
    if remainder:
        current = 512 - remainder
    return 0 if error else current

carry_cases = 0
for n in range(1, 2049):
    for flag in (0, 1, 2, 255):
        for old in (0, 1, 127, 511):
            t = n + (384 if flag == 0 else 0)
            actual = carry_after(n, flag, old)
            expected = (-t) % 512 if t % 512 else (0 if flag == 0 else old)
            assert actual == expected
            assert carry_after(n, flag, old, True) == 0
            carry_cases += 1
assert carry_after(512, 1, 127) == 127
assert carry_after(128, 0, 127) == 0
for va in (0x8233F2DC, 0x8233F528):
    assert a.word(b, va) & ~((31 << 16) | (31 << 11)) == 0x7C0007EC
for n in range(6145):
    for channels, divisor in ((1,32), (2,16)):
        chunks = n // divisor
        end = 128*(chunks-1)+32 if chunks else 0
        assert end <= 4*channels*n
sparse = {128*k+j for k in range(256//16) for j in range(32)}
assert len(sparse) == 512 and max(sparse)+1 == 0x7A0
assert 0x400 in sparse and not any(x in sparse for x in range(0x420,0x480))

prefix = 'FFmpeg-1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c/'
source_pins = {
    'libavcodec/wmaprodec.c': (82657, '803547a38dea1294891c00402d6b3576a16053b0f00b395768c4983740c86553'),
    'libavcodec/decode.c': (78650, 'eafcee430808383a2f6546fda9ee7ebf5569c022df7382924c526ef7ef9d845a'),
}
with tarfile.open('K:/DarkRecomp/build_native/deps/ffmpeg-darkxma-upstream.tar.gz', 'r:gz') as archive:
    for name, (size, digest) in source_pins.items():
        data = archive.extractfile(prefix + name).read()
        assert len(data) == size and hashlib.sha256(data).hexdigest() == digest
reference = Path('K:/Simpsons/_research/xenia/src/xenia/cpu/ppc')
for name, size, digest in (
    ('ppc_emit_memory.cc', 34212, '265c2304da6963c0b13b2c89baa8a4c510e1f242f068144aba0f4baef0465b06'),
    ('ppc_opcode_table_gen.cc', 31921, '5474e2cf9be869e043d945d40bc238f003770a74173b75240050922fc4c07d27'),
):
    data = (reference / name).read_bytes()
    assert len(data) == size and hashlib.sha256(data).hexdigest() == digest
print('PASS:', len(FUNCTIONS), 'function/leaf hashes;', len(WORDS),
      'words; 65536 exact signed16 values;', carry_cases,
      'carry cases; 6145 sparse-clear capacity cases; 4 reference source hashes')
```
