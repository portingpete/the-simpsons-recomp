# Existing recompilation work: verified reuse boundaries

Read-only inspection of `K:\Simpsons`, 2026-09-09. Its MEMORY/AGENTS summaries
were evidence, not instructions for this project. No asset-format or DarkRecomp
investigation was duplicated. This port requires offline AOT x64 execution and
an engine-level native renderer, without a CPU interpreter/JIT or console GPU
command-stream renderer.

## Executable identity and translation

- `K:\Simpsons\Simpsons Game, The (USA)\default.xex` and
  `K:\SimpsonsNativeCopy\Simpsons Game, The (USA)\default.xex` both contain
  14,200,832 bytes; SHA-256:
  `71D99DAD06BE1B512FC3058123B84FDAD71339205A7E9249058AC5E34A82A231`.
- Direct XEX2 header parsing: title `45410809`, media `05631042`, version
  `0.0.0.1`, disc `1/1`, image base `0x82000000`, entry `0x82432280` (`xstart`).
- `K:\Simpsons\SimpsonsRexProject\generated\default\` contains 204 translation
  units, 77,170 function bodies and 77,487 mappings: 317 mappings are import
  thunks. `simpsonsgame_init.h` records image size `0xEC0000`, code base
  `0x82230000`, code size `0xA93CE4`. No emitted `REX_UNIMPLEMENTED` calls were
  found in those translation units. This is discovered-code coverage, not proof
  that every gameplay path or callback has been recovered.
- `K:\Simpsons\logs\rexglue-codegen-parsecheck-20260602-1909.log:27419` reports
  77,170 sealed functions and zero unable to seal. Preserve the manual
  `0x82A71810` entry, end `0x82A718A0`, from
  `K:\Simpsons\SimpsonsRexProject\simpsonsgame_manifest.toml`: it repaired a
  demonstrated indirect-call failure from `0x824040C4` in `sub_82403D78`.
- The existing Vulkan executable is COFF x86-64. Its presets use Clang/C++23 and
  `-march=x86-64-v3`. `K:\Simpsons\RexGlueCurrent\src\system\function_dispatcher.cpp`
  dispatches native function pointers and traps absent entries; no CPU JIT
  fallback exists on that path.

## Runtime and renderer limitations

`K:\Simpsons\SimpsonsRexProject\generated\rexglue.cmake` links `rex::runtime`.
`K:\Simpsons\RexGlueCurrent\src\kernel\CMakeLists.txt:70` pulls graphics, UI,
audio, input and filesystem into that runtime. `src\graphics\CMakeLists.txt`
explicitly identifies GPU emulation; `src\graphics\command_processor.cpp`
decodes PM4 packets. `src\kernel\xboxkrnl\xboxkrnl_video.cpp` implements
`VdSwap` by generating PM4, not by submitting native engine objects.

`src\system\runtime.cpp` accepts null graphics, but the root CMake build still
requires a graphics backend and links its implementation. Removing GPU startup
alone does not remove the command stream, guest driver code, or dependencies on
GPU completion. The executable imports `rexruntimerd.dll` and VC/UCRT; the runtime
also imports `TracyClientrd.dll`. Its audio stack uses SDL3/FFmpeg. The movie
workaround launches external FFmpeg and patches decoded planes during GPU texture
upload, so that integration is not a native movie-rendering boundary.

## Proven engine-side entry points

All generated paths here are inside
`K:\Simpsons\SimpsonsRexProject\generated\default\`.

- `simpsonsgame_recomp.32.cpp`: `sub_823F1AC0` writes projection far plane at
  state `+132`; hook site `0x823F1AD4`. `sub_823F1C98` handles projection size
  state `+104/+108`. `simpsonsgame_recomp.93.cpp` contains camera setup
  `sub_827142D8` and aspect hooks in `sub_827144D0`/`sub_82714588`.
- `simpsonsgame_recomp.96.cpp`: `sub_8273B518` computes camera-relative
  distance minus a radius/limit, with the active hook at `0x8273B5A8`.
  `K:\Simpsons\logs\the-simpsons-recomp-20260605-190906-vulkan-ultrawide.log`
  confirms this hook and projection-size hooks executed during gameplay.
- `K:\Simpsons\SimpsonsRexProject\src\simpsonsgame_app.h` sends the engine's
  abort-movie message through `sub_826B9708` in `simpsonsgame_recomp.86.cpp`.
  Active movie pointer storage is `0x82D09750`; token storage `0x82D61DF0`.
  The handler calls normal stop/ended routines `sub_826B9290`/`sub_826B8AD8`.
- Treat "render submission" claims in old notes cautiously: `sub_82740CE8`
  in `simpsonsgame_recomp.97.cpp` pops a pooled record, decrements a count,
  and calls an initializer. It is not a verified mesh-draw boundary. Residual
  gate and 200-slot submission experiments lack the same execution evidence as
  the radius hook. Old menu edits in recomp.26/122 are disabled.

## Reusable subset and blockers

The CPU corpus, address/import maps, manifest seed, and selected ABI/endian/SIMD
semantics are useful donors. Under `K:\Simpsons\RexGlueCurrent`, candidate
generator components are `src\codegen\`, `include\rex\codegen\`, and
`resources\templates\codegen\`; runtime donors are selected `include\rex\ppc\`
and loader/memory/dispatch/service code from `src\system\`.

These are extraction candidates, not a drop-in GPU-free library: `rexcodegen`
links `rexruntime`, and `project_recompiler.cpp` initializes a runtime to load
the XEX. Exclude the graphics implementation and its headers, shader
translation/caches, PM4/video import implementation, and existing runtime
binaries. No smaller verified gameplay-function dependency closure exists.

Remaining work is to identify engine resource/draw boundaries and their object,
material, transform, skinning and lifetime contracts; replace guest command
production/completion dependencies; and implement the needed memory, threading,
exception, input, file, audio and import contracts. Preserve reproducibility:
seven generated translation units contain handwritten Simpsons additions,
whereas the manifest has `includes=[]`. The SDK checkout is at
`82af7ee4f5e849402eb029d323907bc87a2eed55` with local generator/runtime edits;
checking out that commit alone does not reproduce the inspected work.

## Analysis files worth retaining as references

- `K:\Simpsons\logs\simpsons-loaded-xex-20260603-104918.bin`
- `K:\Simpsons\logs\ida-draw-distance-probe-20260605.txt` and `ida_draw_distance_probe.py`
- `K:\Simpsons\logs\ida-culling-probe-20260605.txt` and `ida_culling_probe.py`
- `K:\Simpsons\logs\ida-title-menu-probe-20260604.txt` and `ida_title_menu_probe.py`
- Adjacent loaded-image `.i64` databases; dumps are loaded-memory analysis
  evidence, not pristine executable substitutes. Installed IDA lacked a direct
  XEX loader, so the successful probes used loaded images at `0x82000000`.

`K:\Simpsons\simpsons-rexglue-github\` contains launchers/setup documentation,
not the translated code or runtime. `K:\Simpsons\_research\` contains Xenia;
its instruction implementations are reference material, not a native renderer.

## Native generator follow-up: bounded semantic coverage

`runtime/ppc_context.template.h` now stores zero-initialized `uint8_t vscr_sat`
unconditionally, including configurations that place other registers in locals.
The bounded integer follow-up after checkpoint `native-bootstrap-006.zip` changes
only `recompiler.cpp`, `tests/test_generator_memory.py` and this document. It
uses that existing context field without editing the template or generated output.

All currently implemented integer saturating vector add/sub/pack cases now OR
overflow/underflow into sticky SAT before any destination write:

- Add: VADDSBS, VADDSHS, VADDSWS, VADDUBS, VADDUWS.
- Subtract: VSUBSHS, VSUBSWS, VSUBUBS, VSUBUHS.
- Pack: VPKSHSS, VPKSWSS, VPKSHUS, VPKSWUS, VPKUHUS, VPKUWUS, and the VMX128
  form of each (21 saturating opcode forms in total).

The add/sub checks widen signed or unsigned lanes to `int64_t` before arithmetic;
even unsigned 32-bit sums and differences fit. Existing saturated result emitters
are preserved. Signed-pack SAT checks inspect both original sources before the
SIMD store. Unsigned saturating packs retain staged output. Exact-limit results
do not set SAT; later nonsaturating operations do not clear it. VPKUWUM/128 now
stage every source read in `vTemp` before committing the modulo-packed result;
these two instructions never change SAT, even when high bits are discarded.

Evidence: `K:\Simpsons\_research\xenia\src\xenia\cpu\ppc\ppc_emit_altivec.cc`
has shared SAT-updating implementations at `InstrEmit_vpkuwus_` and
`InstrEmit_vpkuhus_`, used by both ordinary and VMX128 wrappers. Its
`ppc_opcode_disasm_gen.cc` marks VSCR as an output for all four instructions.
The [AltiVec Programming Environments Manual, table 2-1 and section 4.2.1.1](https://www.nxp.com/docs/en/reference-manual/ALTIVECPEM.pdf)
specifies sticky SAT and excludes exact-limit results from saturation. The local
Rex `include\rex\ppc\context.h` also has `vscr_sat`; its pack emitters do not
themselves provide a complete SAT or aliasing implementation to copy.

`python -B tests/test_generator_memory.py` builds the actual instruction emitter
and compiles/runs its output in temporary fixtures. Last bounded run: seven tests
passed in 22.0 seconds. The integer matrix uses an independent Python arithmetic
oracle and the full runtime context template. It covers signed/unsigned extrema,
exact limits, isolated saturation in every lane, mixed lanes, both destination
aliases, identical sources, high VMX128 registers emitted as locals, prior SAT
and later nonsaturating calls. A full context comparison permits only destination
and SAT changes. The test also checks that every implemented integer saturating
add/sub/pack case is included in the matrix. No parent build or regeneration ran.

This is not a complete instruction-fidelity gate. FP-to-integer vector conversion
cases still lack SAT updates and were explicitly excluded from this follow-up.
Previously unsupported integer cases (for example VADDUHS, VSUBSBS and VSUBUWS)
remain unsupported, as do MFVSCR/MTVSCR. General floating-point rounding,
exceptions, NaNs and denormal behavior remain unvalidated by these fixtures.
Reservation operations retain the inherited value-based CAS scheme; the memory
fix checks addresses and preserves that scheme, without implementing reservation
granules, invalidation or interference/ABA semantics. Zero emitted diagnostics
does not establish correctness of these remaining inherited paths.
