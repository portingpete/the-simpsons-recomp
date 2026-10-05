# Bounded IDA evidence for the effect CPU pool

The existing offline IDA bridge was reused in `fx-pool-01` with IDA9.3 and the
PowerPC decompiler. All four selected extents came from the original `.pdata`:
`82C17DC0` (merge planning), `82C18530` (merge commit), `82C1D0C0` (FX constructor)
and `826B7218` (engine manager finalization).

The export completed and independently matched **1,468 instruction words,
51 direct calls and one indirect call** against the pinned original image.
Three functions decompiled. `82C1D0C0` failed with Hex-Rays error -12 at
`82C1D528` (call analysis failed); its full assembly export remains available.
The process exit code3 records partial decompilation, not a successful C recovery.
The call-free global pool lookup `826B2528` has no `.pdata` record and was
therefore excluded from this probe; its bounded original CPU evidence is
qualified separately by the pool fixture.

The pseudocode is navigation evidence. Observed incorrect save-helper return
types, excessive inferred arguments and pointer-width artifacts make it
unsuitable as a recovered ABI or replacement source. The native integration
retains the actual original CPU initializer `82C181E8`, whose assembly and
execution have separate checks. No guest instruction interpreter, runtime
recompiler, console GPU stream processing or synthetic SDK FX layout is added.

Reproduction:

```powershell
python -B tools/ida_fx_pool_probe.py --check-image
python -B tools/ida_fx_pool_probe.py --compare fx-pool-01
```

The checked exports are in `analysis/ida/fx-pool-01/`. The dedicated IDA database
is an offline workspace artifact under `build/ida-probe/fx-pool-01/`; original
and reference inputs remain unchanged. See `docs/native-effect-pool-cpu-test.md`
for the independently executed first-initialization boundary and lifetime limits.
