# Screen material: original shader and state evidence

This supplements the frozen `render-boundary.md` / `analysis/render-boundary.json`. It is limited to the four original Screen_Xenon records selected by `0x82756480`, their input/output behavior, and state changed by that helper. It does not define a general renderer or GPU command interpreter.

Original image SHA256: `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`; flat mapping is VA minus `0x82000000`.

## Original material identities

The quad reads shader handles from guest globals. Each handle's adjacent `+4` word identifies its original compiled shader record:

- Textured VS: handle storage `0x82CF2340`; record `0x82152880`; original debug path ends `Screen_Xenon_VSTextured.updb`.
- Textured PS: handle storage `0x82CF2334`; record `0x82152708`; path ends `Screen_Xenon_PSTextured.updb`.
- Flat VS: handle storage `0x82CF231C`; record `0x821525E8`; path ends `Screen_Xenon_VSFlat.updb`.
- Flat PS: handle storage `0x82CF2310`; record `0x821524C8`; path ends `Screen_Xenon_PSFlat.updb`.

These are compiled shader records with debugging/reflection information, not original HLSL text. The frozen boundary analyzer verifies the adjacent pointers and debug identities. The completed original instruction proof is in [screen-shaders.md](screen-shaders.md): **both vertex shaders export Z=0/W=1; PSFlat exports c0 RGBA; PSTextured exports sampled RGBA multiplied by c0 RGBA, including alpha.** All nine executed fetch/ALU instructions and their complete straight-line schedules are verified. This closes the arithmetic uncertainty retained in the frozen boundary JSON.

The next native target can create these four exact shader objects offline, using the original quad's already transformed X/Y as input and its float4 color as PS c0. The textured shader uses normalized UVs, a 2D fetch, computed LOD and sampler settings from fetch constant 0. No additional shader premultiplication, alpha replacement, saturation or depth export is present. Shader creation need not wait for inherited state; draw submission must remain gated until the selected target/viewport, effective sampler/texture view and output-merger state are supplied. Original fixed-function alpha testing belongs to that later draw-state implementation, not the four shader identities.

## Exact meaning of `0x8243B3B0`

This setter controls an expanded blending representation for **color target 0**. It is not an additional blend-factor selection, nor a gamma-enable switch.

Original instructions `[0x8243B3B0,0x8243B454)` establish:

1. `target = BE32[device+0x3090]`; store requested Boolean to `device+0x2EF4` even if there is no target.
2. If target is zero, return. Otherwise read its descriptor at `target+0x1C` and extract `(descriptor >> 16) & 15` (`0x8243B3C4`). Only formats **2, 3, 10, 12** can change.
3. Extract the current expanded bit `(descriptor >> 19) & 1` (`0x8243B3E8`). If it already equals the requested Boolean, return.
4. Request 1 maps **2→10**, **3→12**. Request 0 maps **10→2**, **12→3**. These transformations follow the arithmetic at `0x8243B3F8..B428`: `(format+3)*2` for enable, `format/2-3` for disable, after the earlier no-change checks.
5. Replace only descriptor bits 16..19, write the target descriptor at `0x8243B430`, and replace the corresponding bits of `device+0x2884` at `0x8243B440`. Set dirty bit `1<<56` in the 64-bit word at `device+0x10` (`0x8243B444..B44C`).

Read-only `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h`, lines 298–315, names those exact format pairs `2_10_10_10` / `2_10_10_10_AS_10_10_10_10` and `2_10_10_10_FLOAT` / `2_10_10_10_FLOAT_AS_16_16_16_16`. The second pair expands blending precision without changing the stored packing's meaning. The exact rounding behavior of a native replacement is not established by an enum name.

The quad brackets selectors other than 3 with request 1 at `0x827565C0`, then request 0 at `0x82756848`. Other formats are unaffected except for the remembered request. It does not restore the previous request or target format snapshot. A native target with a different format cannot silently use these switches as a reason to change pixel packing.

## State the quad establishes and state it inherits

The helper submits a four-vertex strip, primitive 6, TL/TR/BL/BR. Input color is BE float32 RGBA at r8 and is copied to device constants `+0x1780..178C`; rectangle and UV endpoints arrive in f1..f8. The separate shader proof establishes output Z/W, color arithmetic, and texture-fetch semantics independently of that geometry construction. The native vertex shader must not reapply the CPU coordinate transform.

The original blend words are `00010106`, `00010706`, `00010186`, `00010001` for selectors 0,1,2,3. Using the verified reference field positions, their RGB equations respectively use source-alpha/add/one, source-alpha/add/one-minus-source-alpha, source-alpha/reverse-subtract/one, and one/add/zero. Alpha factors are one/add/zero in all four. The original loading spinner uses selector 0; the background uses selector 3. Values outside the observed selectors must not be invented as additional named modes.

The helper requests depth-test off, culling off, alpha-test on, comparison GREATER, reference float32(1/255), then after submission replacement blending, alpha-test off, and depth-test on. In `0x8243A6C8`, depth-on is conditional on a nonzero depth surface at device `+0x30A0`; the requested state is retained at `+0x2E5C`. The setter only changes bit 1 of effective depth control at `+0x2934`; it does not change depth-write bit 2 or stencil bit 0.

Sampler stage 0 receives min/mag filter request 1 through `0x8243BA40/0x8243BBD0`; reference declarations identify filter 1 as linear. The helper clears U/V addressing fields to repeat. Those setters also read inherited anisotropy/override data (including per-stage bytes around `device+0x2E8C` and `+0x2EDA`) and manipulate other sampler fields. Thus “linear min/mag requested” is not proof that every effective sampler field is fixed by this helper. Mip filtering, LOD constraints/bias, anisotropy, border/sign/gamma behavior, color masks and stencil remain inputs to be recovered or explicitly represented by the native engine state service. A native implementation must not silently choose defaults for them.

Reproduction of the state proof uses the existing offline disassembler, for example:

```powershell
python -B tools/disassemble.py 0x8243b3b0 --count 41
python -B tools/disassemble.py 0x82756480 --count 247
```

Both complete functions and the relevant filter/depth/alpha/blend setters are already byte-checked in the frozen boundary JSON. No new runtime code is needed to inspect them.
