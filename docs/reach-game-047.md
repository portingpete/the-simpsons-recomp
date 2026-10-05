# Replay047 / native gloss shader checkpoint (2026-09-16)

## Outcome

**Getting in-game remains incomplete.** Replay047 reaches the main menu,
Continue Game and movie skip, then rejects the unsupported graphics boundary
82740680 from8273B4E0. No gameplay frame or character control is verified.
The gloss source82019988 remains excluded by both engine dispatch guards and
rigidProfile. No alias to dualtextured, runtime interpreter, or guard bypass
was installed.

## Implemented

- K:\SimpsonsNativeCopy\tools\analyze_rigid_gloss_shader.py now emits the fixed
  VS8201A07C / PS8201A6EC HLSL from retail-image/hash-pinned records. It checks
  complete CF, slot coverage, semantic associations, literals and record hashes.
  The three false-predicate jump targets derive nested branches7->22,9->14,
  16->21. Generated source verification is registered as
  OriginalRigidGlossShaderEvidence in CTest.
- K:\SimpsonsNativeCopy\renderer\rigid_gloss_shader.hlsl contains the native
  shader pair and test-only observers. It preserves UV0/UV1, world position,
  both shadow projections, normal/color, and the20-word PS literal bank.
  Its PS bank is51 float4 registers (816 bytes), covering c50. Existing runtime
  RigidPixelConstants remains50 registers: this is NOT yet a runtime binding.
- CMake compiles all four gloss entry points offline using FXC /Ges /Gis /O3.
- K:\SimpsonsNativeCopy\tests\test_rigid_shader.cpp runs1296 actual gloss PS
  draws per device (4 normals x3 exponents x3 scales x3 base-alpha values x2
  UV1 values x6 shadow cases). Final RGBA is compared with an independent
  geometric/packing reference. WARP and hardware pass. Shadow cases include
  disabled/nonreceiver with unbound resources and independently lit/dark maps.
  These uniform shadow tests do not yet qualify every nonuniform tap/weight.
  Gloss VS compilation is verified; its exports have no GPU oracle yet.

## Boundary mismatch investigation

The original oblique input(.8660254f,.5f,0), exponent1, scale.25, alpha.5
initially failed packed green: actual.31867057085, reference.286412511952.
Build-only intermediate observers and a hardcoded arithmetic-only shader showed
red/green reaching exactly6/10 before floor on both WARP and hardware; the
unrounded double oracle put both just below those integers. Rounding color to
float32 before floor fixes the reference. The original failing normal is
retained along with an off-boundary(.8f,.6f,0) case. Production floor/log/exp
arithmetic was not changed to fit the test. This validates the tested native
inputs, not bit-exact original-console GPU transcendental behavior.

The build-only constant-color control also passed on both devices. Diagnostics
under K:\SimpsonsNativeCopy\build\gloss-boundary and
K:\SimpsonsNativeCopy\build\gloss-constant-control are not runtime shaders.

## Validation and evidence

- SimpsonsNative and RigidShaderTests build successfully.
- K:\SimpsonsNativeCopy\build\gloss-checkpoint-final-ctest.log:166/166 pass,
  95.67s. Earlier165/166 run failed during NativeHostFloatingPoint toolchain
  setup (vcvars64 exit255), before executing that test. Repeated VS setup had
  expanded PATH to88 entries with25 unique entries. Process-local deduplication
  of PATH/INCLUDE/EXTERNAL_INCLUDE/LIB/LIBPATH restored the complete suite;
  no persistent environment or host-FP source change was needed.
- AOT verification:311 files,0 semantic diagnostics.
- K:\SimpsonsNativeCopy\build\gloss-final-rgba-ctest.log: both GPU tests pass
  with the restored boundary case.
- K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-047\result.json:
  success=false, main_menu_verified=true, input_sequence_completed=true,
  gameplay_verified=false. The run directory preserves captures and input logs.
- Replay045/046 evidence is untouched.

## Remaining implementation

1. Finalize gloss selected-context2B00 private/shared maps. Private storage is
   22x16+192=544 bytes/136 words, with exponent leaf13/word76, scale leaf19/
   word124, shadow leaf20/word128, rim leaf21/word132. Confirm selected-register
   ownership against the original maps before adding material commits.
2. Extend native constant ownership/upload/replay snapshots to cover c50;
   preserve existing materials' mappings. Gloss uses exponent c50, scale c47,
   shadow c46, rim c45 (unlike existing rigid shadow/rim c47/c46).
3. Qualify gloss VS exports, native artifact/pair selection, depth/color draw
   adapter and both immediate/recorded paths. Keep base texture ownership and
   original sampler/UV semantics; do not reuse the dualtextured pixel layout.
4. Only after those paths are implemented and tested, admit source82019988,
   rebuild, and run a fresh replay048. Report rendered gameplay and controllable
   character verification separately.
