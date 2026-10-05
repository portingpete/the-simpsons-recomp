# Native driver lifetime and original plugin execution

The native owner in `runtime/engine_driver.cpp` retains D3D11 backing through
the original engine's constructor/destructor walk. It replaces driver requests
2/3/8 at the verified `823F0630` boundary. Open, mode queries, mode-selection
refusal while started, callback installation and other CPU requests remain AOT.
Request 1 is allowed through only after the native owner has stopped.

Boot030 reached original post-start code at `82875D64`, after the original
35-entry plugin walk, gamma-table construction and request 17. The next
explicit failure is `823EE8F8`, the unported console-device integration entry.
No original frame, rendering, input, audio or gameplay is implied.

## Ownership and publication

The driver owns a hardware D3D11 backend, window swap chain, four RGB10A2 color
roles, two float-depth/stencil roles, scratch index, four dynamic vertex buffers,
engine state, native material/declaration registries and the separate pipeline
index. All original CPU pools and their linked records still use original
allocator callbacks. The shader plugins create immutable native material
records; sixteen shader translations remain unsupported at binding.

Target fields `CB00,CAFC,CF84,CF90,CF8C,CF88` receive six distinct monotonic
native IDs in `00F00001..00FFFFFF`, each checked against guest mappings. IDs
resolve to actual retained native resources through this driver only. Color
and depth lookup reject the other kind, stale IDs and another owner thread.
Front-color sampling retains the separate alpha-one policy; it is not applied
to storage alpha. No console SDK object layout is fabricated. `CAF8` stays zero.

The 124-byte original presentation record retains exact dimensions, format
words, flags and the conditional video-mode fields. Default color/depth are
bound and queried back from D3D11. Original binding/pipeline/raster helpers,
scratch initialization, native state commit and dynamic-buffer ownership all
complete before `CB08=1` and request-2 success. The original caller alone runs
plugins and advances engine state to 3.

Startup requires the original general allocator to have been initialized.
The actual original boot has `82D57244=82D5724C`, registration `82DFD8C4=82D5724C`
and its initialization bit set. SDK-backed allocation could lazily perform
this CPU initialization; an absent allocator is therefore an explicit
precondition failure, not a silently omitted side effect. The unsupported
single-buffer capability path also fails before publication.

## Stop, rollback and limits

The original caller executes reverse plugin destruction before request 3.
Native stop verifies that material and pipeline ownership has been released,
validates all six target fields, and rejects unported SDK cache ownership.
It frees the original mode table exactly once, releases the binding pool,
clears native bindings, calls original cache/raster cleanup, stops native
dynamic ownership through original callbacks, and executes original scratch
cleanup. All six native role fields are cleared and host backing is released.
The original caller advances state to 2, and original close advances to 1.

Partial start cleanup only unwinds entered stages and leaves the still-open
mode table owned by open. It never invokes the original full SDK stop.
A terminal exception after plugin construction releases host backing when the
runtime shuts down; it logs that original guest cleanup was incomplete. It
does not attempt callbacks using an unwound guest frame or present that fatal
path as a clean shutdown.

Fourteen byte-checked guards cover post-start device integration and unported
camera/raster/render callbacks. They fail with entry and caller addresses
before passing native IDs to an original SDK path. They implement no graphics
operation and will be replaced individually by verified native engine services.
Shader/declaration SDK bind guards remain separate.

## Actual original lifecycle test

`OriginalDriverLifecycle` runs the original program to the post-start guard,
then invokes original `823EC950` (plugin destructors and stop), `823ECAA8`
(close), `823ECF58` (reopen), and `823EC9E0` (start). It repeats stop/close.
The fixture uses a separate checked caller context for those API calls; it
does not resume the failed original call stack or add a production bypass.

Build052 passed this test with 166 checks and all nineteen CTest suites. Checks
cover original lifecycle values, real target ownership and sampling policy,
unmapped and distinct IDs, stale/wrong-kind/cross-thread rejection, premature
stop rejection while plugins own resources, original mode refusal, cleared
mode/pool/resource fields, and expiration of native backing after stop.
Fault injection changes only the fixture's mapped state constant, forcing a
failure after real target/pool/scratch creation. Original cleanup releases those
owners, preserves the open-mode table, and permits a successful retry. Missing
general-allocator initialization and the unsupported capability path also fail
without publishing a driver. Actual boot031 reaches the same post-start guard.

Build058/boot033 additionally connect post-start native backend registration.
The 207-check fixture now observes the first raster callback, after original
application CPU initialization, and checks process-lifetime storage retention
and native alias retirement. Full application cleanup/restart is not certified
by the engine stop/reopen checks. See native-submission-bridge.md.

Build066 / boot038 add original camera and embedded loading-texture lifetime
coverage. The fixture now passes 315 checks, including original dictionary and
camera destruction, native BC3 readback, independent native references, malformed
source rejection and failed device attachment rollback. Its final original
entry observation is the explicit 82723D80 application-state guard. This remains
a bounded driver/resource fixture, not full application shutdown or gameplay.
