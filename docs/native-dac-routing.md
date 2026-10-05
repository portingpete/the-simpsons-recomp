# Dac0 six-component routing and category-volume source

Bounded result: **original component ordering and category update semantics are
verified; the original physical speaker labels and stereo downmix are still not
verified.** This evidence alone does not qualify a recovered console audible
route. The current source guard at `8234591C` preserves that fidelity limit;
it is not an approval requirement. The authorized native port may implement an
explicit PC routing policy, identified as an adaptation. Six Windows channels do not
justify assigning either `0x3F` or `0x60F` to the original stream.

All original addresses/offsets are hexadecimal. Evidence uses the unchanged flat
image `analysis/simpsons.pe`, base `82000000`, SHA-256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The scratch collector validates the image, PE/.pdata framing and each decoded
word against the original bytes. Only this document and `build/dac-routing/*`
belong to this evidence task. The separately requested build125 resolution was
appended to `docs/native-audio-output-review.md`; no production/original/reference
file was modified.

## What the speaker query actually does

`8225879C:48A6A0E9` is the call **inside function `82258778`**, not a separate
function entry. The .pdata extent is `64` bytes. It calls original import
`82CC2884`, `XAudioGetSpeakerConfig`, with `r3=SP+50`, and treats the return as
a status. Its full observable selection is:

- Nonzero status: return string pointer `821D1EB0`, `"Ana.Enabled"`.
- Status zero and output word exactly `00010000`: return `821D1EBC`,
  `"Dig.Enabled"`.
- Status zero and any other output word: return `821D1EC8`, `"Ana.Enabled"`.

The equality is exact, not a mask-bit test: `822587AC:3D600001` loads `10000` and
`822587B4:7F0A5840` compares the entire returned word. This does not prove that
`10000` is a six-channel flag or that other values identify a particular speaker
layout.

Its direct caller at `82258480` is inside `82258368`. That caller passes the
selected string in r6 to an object method with tags `HDWR` (`48445752`) and
`AUDI` (`41554449`). The surrounding code similarly reports screen/video/language
strings. This supports a hardware-summary role; there is no write to Dac0's
source description or send matrix here. The bounded reference scan found that
one direct caller and an aligned literal pointer to the function at `821E46B8`;
it is not a proof of every indirect caller.

## Numeric route proved by original CPU and SDK bytes

The six-component branch of original CPU helper `823544D0` receives the mixer
description in r4 and channel count in r5. It reads planar base `P=[r4+4]` and
plane stride `N=BE16[r4+E]`, then writes 256 frames to `82E32000` with a 24-byte
interleaved stride:

```
output[6*j + component] = original_float(P + 4*(j + plane[component]*N))
plane = [0, 2, 1, 5, 3, 4],  j = 0..255
```

This is pinned by the address formation at `823544F4..82354528` and six original
load/store pairs at `82354538..82354580`; it is not inferred from an asset's
channel count. The helper ends at `82354674:4E800020`. The earlier original CPU
and transport differential tests remain separate evidence; this task does not
rerun them or rename their planes.

At the SDK default-send boundary, leaf `82C40D28` uses the low eight bits of the
source/destination channel counts. Equal nonzero counts through six return a
mapping count and the table at `821B2718`. Its six records are eight bytes each:
source byte, destination byte, zero halfword, BE float gain. They are exactly
`0->0`, `1->1`, `2->2`, `3->3`, `4->4`, `5->5`, all gain bits `3F800000`.
This independently corroborates the numeric route in `native-dac-sdk-contract.md`:
do not add another permutation after the original CPU interleave.

**The helper has no six-to-two branch.** For `(source,destination)=(6,2)`, it
reaches `82C40E3C:38600000` and returns null. Its four-to-six table routes the
last two quad components to destination indices4/5. That reinforces the index
relationships but still does not attach names to them. Neither the identity
table nor the null six-to-two case supplies final console stereo-downmix
coefficients. This is not a claim that an Xbox cannot produce stereo; that
conversion, if required, belongs elsewhere in the original output/platform path.

The image also contains the mode string `"5.1"` at `821C83D8` and a pointer to it
at `82D069A4`. A mode name alone does not distinguish back versus side speakers
or prove a source-channel mask.

## Original category source and exact update behavior

Use `M` for the real original SDK singleton and `V` for the original SDK wrapper
voice only in this explanation; native code must not create these SDK layouts.

1. The singleton constructor `82C49C30` loads bits `3F800000` at
   `82C49CA0:3CE03F80` and stores them to `M+84` and `M+88` at
   `82C49CE0:90FF0084` / `82C49CE4:90FF0088`. Thus both tracked category
   multipliers initially equal1. These are original initialization instructions,
   not values supplied by an emulator.
2. Processing function `82C49148` performs the refresh when the low byte of its
   r4 argument is nonzero. At `82C49184:4807A6C1` it calls import `82CC3844`
   (`XAudioGetVoiceCategoryVolumeChangeMask`), with **r3 equal to the original
   client value `BE32(BE32(BE32(M+40)+44)+18)`**, and **r4=M+8C**, the output
   mask address. It checks the returned status for negativity.
3. For each set mask bit i=0,1, `82C491B4:4807A681` calls import `82CC3834`
   (`XAudioGetVoiceCategoryVolume`) with **r3=i**, **r4=M+84+4*i**. The platform
   therefore writes the current float directly into its retained multiplier
   slot. The loop checks the previous call's signed result before proceeding
   to the next category. Do not describe this as an unconditional get of both
   values or as a value returned in f1. The examined code does not validate,
   clamp, square or convert those returned floats from decibels.
4. `82C4C450` tests the wrapper's category bit in `M+8C` and dispatches its gain
   method at `82C4C494` when changed. The processing function clears `M+8C` at
   `82C49270` after processing. Changes are inputs to gain reapplication, not
   evidence of a buffer completion or original engine enabled-state mutation.
5. Gain leaf `82C4C540` loads V.byte90. For categories0/1 it uses `M+84+4*i`;
   other categories select the unity constant at `82000BB0`. At
   `82C4C574:EC2D0032`, original `fmuls` computes
   **effectiveGain = V.float8C * categoryMultiplier** and tail-dispatches to the
   underlying voice. This does not replace the separately proved gain ramp.
   The observed zero category in Dac0's original source record is category0.

The source of later values is consequently **the original platform service**,
not the game image's constants or mixer planes. Its kernel implementation is
not present in the supplied image. This task has no proof of the numeric range
or meaning of all later values, a semantic name for category0/1 in this SDK
version, a required first refresh mask, or which dashboard/profile/media events
would change it. In particular, category0 has not been equated to Windows
`AudioCategory_GameEffects` merely because the native mastering voice uses that
Windows category.

## Primary/reference evidence and limits

Read-only local searches found no original XAudio1 documentation/header that
binds these six SDK indices to physical speaker labels. The inspected local
RexGlue/Xenia-derived category implementation returns fixed unity and no changes;
that is its implementation policy, not independent proof of original kernel
behavior. DarkRecomp's native masks likewise express its own route choice.
Neither was copied into a proposed original contract.

The installed official Windows SDK `x3daudio.h` (10.0.26100.0) explicitly defines
back bits4/5 versus side bits9/10, and both six-speaker masks. Microsoft documents
XAudio2's implicit six-channel positions and a one-to-one compatibility rule for
the two standard back/side configurations. This qualifies a **Windows routing
policy**, not this older SDK's component meanings.
[XAudio2 channel mapping](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping)

Microsoft's `X3DAudioInitialize` page requires `SPEAKER_XBOX` on Xbox360, and its
DSP-settings example names six output rows including rear-left/right. Those are
primary sources for that API. No bounded original callsite or descriptor here
was identified as that API contract, so they cannot close the missing link by
themselves. A modern header, third-party `SPEAKER_XBOX` macro or ordinary Windows
six-channel example is insufficient to relabel the original Dac0 stream.
[X3DAudioInitialize](https://learn.microsoft.com/en-us/windows/win32/api/x3daudio/nf-x3daudio-x3daudioinitialize),
[X3DAudio DSP settings](https://learn.microsoft.com/en-us/windows/win32/api/x3daudio/ns-x3daudio-x3daudio_dsp_settings)

## Narrow native policy supported now

- Retain exactly six **numbered components**, the original CPU interleave and
  scalar/ramp operations already proved. No second channel swap is justified.
  A six-component data owner or offline capture may preserve these bytes without
  claiming speaker labels. The generic backend's explicit routing API remains
  useful, but this evidence does not supply its production speaker mask/matrix.
- A native category-state owner may initialize its two logical multipliers to
  original unity and support explicitly supplied later values/change tracking.
  The native source of those changes must be defined separately. A constant1
  import stub, a never-changing mask, or treating the global test mute as that
  category state would overclaim original behavior. Any selected native-PC
  category policy must be labeled as such, not as recovered Xbox policy.
- A binding claimed to reproduce the console route needs the original
  index-to-speaker link and target-device conversion qualified. A stereo endpoint needs an
  explicit justified six-to-two policy; the default-send leaf recovered here
  cannot provide it. Muting an arbitrary guessed matrix does not prove that
  matrix. Conversely, a future deliberate native downmix can be a product policy
  if declared as an adaptation; it cannot be called the verified original one.

The smallest missing evidence is an original-version output channel definition
linked to this SDK format, or a controlled original-console component test,
plus the final platform downmix definition when targeting stereo. Category
closure requires the actual platform category policy or an explicit native
replacement policy and event producer; further PCM packet inventory will not
answer either question. No broader engine/backend investigation was started.

## Reproduction

`build/dac-routing/collect.py` accepts `--function VA` for a unique containing
.pdata function, `--span VA SIZE` for a bounded explicit code span, `--refs VA`
for direct branches/aligned literal pointers, and `--strings REGEX`. It writes
only adjacent scratch JSON/text. Explicit leaf spans used here are
`82C40D28/11C`, `82C4C540/4C`, `823544D0/1A8`, `82353BC8/80`.
They are distinguished from .pdata-derived extents. The disassembler is the
existing `build/generator-ninja/SimpsonsDisasm.exe`; every output word is checked
against the immutable original image.

Run `python -B build/dac-routing/verify.py` to validate the collected evidence,
route records, diagnostic strings and semantic instruction pins. The script also
rejects a modified image and a forged disassembly word. Its output is
`build/dac-routing/verification.json`; these are **static evidence-integrity
checks**, not original CPU differential execution or an audio playback test.

Final verification passed: **9 code spans, 535 original instruction words,
6 identity-send records, 3 diagnostic strings and 2 integrity-rejection
fixtures**. Routing deliverables are frozen; no production or reference changes
were made. Physical speaker labels and original stereo-downmix coefficients
remain explicitly unresolved.
