# Native application scalar-state bridge

`82723D80` now executes its original AOT instructions for cache suppression,
register save/restore and dirty-stack publication. The native bridge replaces
only the SDK setter lookup/call at `82723DC4..82723DE8`, then resumes at
`82723DEC`. Its native context identity stays unmapped. No SDK object, console
device state structure or GPU command-stream processor is created.

Boot047 passes all 82 registered scalar requests and all twenty sampler
categories at sixteen application stages. It stops at the explicit original
camera-clear guard **`823EE940`, caller `823F1BA8`**. This is renderer-state
progress, not an original frame or gameplay milestone. Boot044's earlier
`168` stop was identified as disabled hi-stencil enable.

## Original contract and checked boundaries

Authority is the pinned original image and generated AOT code, with static
Ghidra/independent-decoder evidence in
`analysis/reagent/ppc64-32addr-inline-02/report.json`, the original SDK state
ledger `analysis/native-driver-state.json`, and the full initializer/body pins
in the application-sampler evidence. Ghidra's corrected pseudocode is useful
navigation, not the execution implementation.

- Entry arguments are application owner in r3, selector in r4, raw value in r5,
  and force in the **low byte** of r6. The dispatcher has no Boolean result.
- `82150580 + 4*selector` maps selectors 1..86 to SDK byte offset
  `4*(selector+9)`. Selector zero is a sentinel. The original initializer
  `82724840` fills 87 map entries, then registers 82 application scalar rows.
- `82D6D498[selector]` supplies the compact cache index; the inverse list is at
  `82D6D6A0` and registered count at `82D6D7E8`. The native preflight checks
  bounds and agreement in both directions, rather than hardcoding the current
  scalar's compact index.
- Original SDK initializer `82466828..82466870` installs each 12-byte
  `{getter,setter,default}` row from `82CD28B8` at context `+40+id`.
  The bridge checks that original row against the supported native field's
  setter address. It never reads a method pointer from the opaque native ID.
- The applied application cache is owner `+694+4*index`. An unchanged value
  with a zero force byte skips the actual setter. Forced calls are retained:
  blend enable/separate-alpha can broadcast effective blend words even when
  the requested scalar is unchanged.
- Positive signed depth at owner `+D28` selects saved frame
  `owner + depth*694 + 698`. The original body compares the saved value and
  sets or clears exactly bit `index&31` in word `192+(index>>5)` of that frame.
  Original push `82723978` copies the applied record and clears dirty words.
  Eight frames fit before constructor flags at `+41CC`; invalid depth rejects.

Static constructor `82CB9580` passes literal address `82D5DB78` to `82725848`;
the driver currently accepts only that original process-lifetime object and
preflights its full `41D4`-byte extent. Alternate object allocations remain
unsupported. The standalone effective-state test uses an explicitly mapped
fixture object through the lower-level service, not the driver entry hook.

The entry hook validates state ownership, mappings, native value support and
all later guest publication addresses before the original prologue runs. The
callsite checks again, applies a temporary native state copy, then publishes it.
The original remaining instructions perform guest cache/dirty writes. This
does not claim recovery from cancellation or arbitrary concurrent modification
of the same guest object after preflight.

Application direct state and the separate RenderWare pending/applied queues
are distinct. This bridge changes neither `82D0F3B0` pending scalar entries nor
`82E3D580` applied cache entries. The original application code did not update
those queues either. The native effective owner is shared by both paths.

## Newly recovered fields

The effective owner now supports 82 identified scalar IDs. Unsupported IDs and
unsupported values still fail explicitly before application cache publication.

**Blend constant, offset `44`, setter `8243A4D0`.** The original word is ARGB.
The setter extracts R/G/B/A bytes and produces four float32 values at original
offsets `28E0/28E4/28E8/28EC`, using a float32 multiply by exact bits `3B808081`
at `821DD204`. The native owner retains the raw word and supplies the same RGBA
floats in its immutable screen-state snapshot. Existing four screen blend
equations do not consume constant factors; this adds no new blend equation.

**Alpha-to-mask, offsets `150/154`, setters `8243B7A8/8243B7D8`.** The first
sets bit 4 of the original color-control word (`293C`); the second replaces its
top byte with four two-bit coverage offsets. The adjacent alpha comparison and
alpha-test bits are unchanged. Requests 0/1 and offset bytes 0..255 are retained.
Screen drawing explicitly rejects enabled alpha-to-mask. Disabled coverage
retains its offset byte without affecting the pass. The checked field names
agree with read-only `RB_COLORCONTROL` in
`K:/Simpsons/RexGlueCurrent/include/rex/graphics/registers.h`.

**Two-sided and back stencil, offsets `70`, `90..A8`.** The original setters
publish a two-sided flag, back fail/depth-fail/pass operations, comparison,
reference, read mask and write mask. The native owner retains these separately
from front stencil. Operations/comparison remain limited to the verified
startup values; the two-sided request is a Boolean. Both front and back
references accept canonical bytes. Read/write masks accept canonical bytes
plus the SDK default `FFFFFFFF`, whose effective byte is `FF`. That distinction
was reached by boot042: the application supplies `FF` where the SDK default is
`FFFFFFFF`. Enabled stencil drawing remains outside the screen contract.

No pixel parity, expanded-blend arithmetic, enabled coverage, stencil rendering,
camera pass, presentation, application shutdown or gameplay is claimed here.

## Complete registered startup pass

[native-application-scalars.md](native-application-scalars.md) recovers all 82
original registration names, their order and the 37 fields added after boot044.
Their native owner retains only the observed SDK/application baseline requests;
unregistered selectors and broader values still reject. SDK and application
defaults are distinct: for example guardband 2.0 becomes 1.0, half-pixel mode
0 becomes 1, multisample mask `FFFFFFFF` becomes `FFFF`, and present interval
0 becomes 1 when the actual original forced pass runs.

The three hi-stencil requests accept only zero. User clip planes, point sprites,
coordinate wrapping and enabled scissor remain outside the accepted subset.
Viewport enable is fixed at one. Point/tessellation sizes, multisample state,
primitive restart and interval requests are retained for their later consumers;
they do not establish native point/tessellated/indexed rendering or presentation.
Tessellation mode zero means discrete, not disabled.

The current screen-state gate explicitly rejects the application's changed
half-pixel and guardband policies pending full raster-convention proof. It also
rejects separate volume filtering. Other resource/topology/presentation checks
remain required at the currently guarded native draw and present boundaries.

The native disabled-scissor state never calls original `8243D0E8`: that routine
tail-calls a command-emitting helper. The remaining 36 added setters/getters
are exercised on isolated CPU fixture bytes, including float-bit retention and
the original low-16-bit multisample getter. No console SDK device is instantiated.

## Validation and reproduction

`tests/test_engine_state_bridge.cpp` executes the **actual original AOT setters**
on isolated test memory and compares their results with the native owner:

- Every 0..255 byte value in every blend-color channel, with bit-exact floats.
- Every coverage-offset byte, both enable values, and unchanged adjacent bits.
- Original back-stencil setters/getters and all 256 canonical values for each
  of six front/back reference and mask fields.

The isolated fixture memory is not used as a live graphics device. None of
these original SDK setter bodies are called by the shipped application bridge.

`tests/test_engine_driver.cpp` runs actual original startup and then the actual
hooked dispatcher. It verifies save/restore and stack ABI, force-byte masking,
equal-value suppression/reapply, all eight original pushed frames, exact dirty
bit set/clear, corrupt mapping and invalid-depth/value rejection, unchanged
RenderWare caches, and the pre-existing real texture/camera lifetimes.

The focused state tests also retain the explicit enabled-coverage draw rejection
and snapshot fields. The ordinary strict build and muted run commands remain:

```powershell
.\tools\build.ps1 -Jobs 8
python tools\run_native.py --timeout 20 --log build\boot-next.log
```

Build076 passed all 23 CTest suites with the complete scalar pass. The subsequent
full sampler bridge is described in
[native-application-sampler-bridge.md](native-application-sampler-bridge.md).
Current native run: `build/boot-047.log`. Latest aggregate validation and exact
counts are kept in `STATUS.md`. Next implementation boundary: camera clear
`823EE940`, using the existing pinned camera-pass evidence and native targets.
