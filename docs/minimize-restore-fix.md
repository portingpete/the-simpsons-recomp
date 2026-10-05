# Minimize/restore crash fix (2026-09-20)

Minimizing the game triggered `Native front presentation window state or
client extent changed`. Windows reports an empty client area while minimized,
and can briefly retain that empty area after clearing the minimized flag
during restoration. The renderer incorrectly treated those states as resizing
its fixed 1280x720 render targets.

`renderer/presentation_window.h` now shares the window check between swapchain
attachment and frame presentation. It samples minimized state around the
client query and allows up to one second for an empty restored client to
recover. Nonempty incorrect sizes, persistently empty restored windows,
invalid windows, and foreign process ownership still fail validation.
Swapchain dimensions, resource formats, copy completion, and display status
checks are unchanged.

Verification:

- The new regression reproduced the original exception before the fix.
- `NativePresentationContract` passes on WARP; direct `--hardware` testing
  also passes. Each tests three minimize/restore cycles with normal and
  already-minimized attachment, exact pixel transfers, copy retirement,
  preserved bindings/depth, and invalid size/closed-window rejection.
- `OriginalDriverLifecycle` passes. The game rebuild and AOT verification
  pass (311 generated files, zero semantic diagnostics).
- The live main-menu check completes three sustained and twenty rapid
  minimize/restore cycles, with completed presentations continuing in each
  state and the restored client returning to 1280x720.

Evidence is under `build/minimize-fix-318`: `regression-before.log`,
`tests.log`, `hardware.log`, `build.log`, and `live-result.json`.
The live harness explicitly closes its own game afterward; the existing
runtime reports this cancellation as `Native window closed` with exit 1.
This does not claim full-suite or gameplay verification.

The updated executable is `build/native/SimpsonsNative.exe`, used by the
existing launcher and Steam shortcut.
