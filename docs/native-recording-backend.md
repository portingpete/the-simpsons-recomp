# Native recording-context ownership — build102

The backend can create, validate and release a real D3D11 deferred context.
This document records the isolated ownership capability introduced in build102.
Build106 connects it to the original engine constructor and paired destruction
through `runtime/engine_recording.cpp`; see `native-recording-owner-design.md`.
No recording, finish, replay or draw readiness is implemented by these methods.
The common SDK-creation fallback and unsupported consumer guards remain active.

## Implementation and lifetime

`renderer/native_recording.cpp` implements three `NativeBackend` methods:
`createRecordingContext`, `validateRecordingContext` and
`releaseRecordingContext`. `NativeRecordingContext` is opaque outside that
implementation. It owns actual COM references to the creating device and its
distinct deferred context, plus the backend/thread identity. There is no guest
SDK layout, console packet buffer or guest pointer in the object.

Creation calls `ID3D11Device::CreateDeferredContext(0, ...)` and accepts only
`S_OK`. Zero is the required reserved flags value; D3D11 provides actual native
recording capability through the resulting deferred context.
[Microsoft's API contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdeferredcontext)
defines this behavior. Allocation and validation failures retain no published
successful owner.

Validation first checks the live immediate backend owner, then the recording
owner, thread, device, actual deferred-context type and zero context flags.
The actual deferred context must differ from the immediate context. Explicit
release validates before dropping COM references; all aliases then reject use
or repeated release. Creating and releasing the object never changes immediate
bindings, clears state, submits a draw, finishes a command list or executes one.

Final shared-owner destruction releases COM references without dereferencing
the stored backend address. A retained object can outlive its backend. Its
device reference prevents a replacement backend at the same C++ address from
matching the old device identity; the replacement rejects the stale owner.
This closes host ownership only. Original engine context aliases, recording
results, resource references and guest destruction still require their own
verified implementation.

## Verification

Build102 passes all **26 CTest suites**. `NativeRecordingOwnership` passes
**350 checks** on WARP, with another 350 on the actual hardware backend:

```powershell
.\tools\build.ps1 -SkipGenerate -Jobs 8
.\build\native\NativeRecordingTests.exe --hardware
python tools\run_native.py --timeout 20 --log build\boot-062.log
```

The AOT manifest verified unchanged generation inputs before this build; the
backend source and CMake changes were compiled normally. The new test queries
real device/context identity and type; checks simultaneous distinct contexts,
foreign backend/thread rejection, release aliases, repeated ownership cycles,
final-reference destruction on another thread, and exact-address backend
replacement. Native Get* snapshots, color/depth readbacks and submission
counters verify that these ownership operations leave the immediate renderer
unchanged. Fixture geometry only seeds existing bindings; it is not game art.

Full logs: `build/hundred-second-build.log`,
`build/native-recording-warp-062.log`,
`build/native-recording-hardware-062.log` and
`build/native-driver-recording-062.log`.

The actual executable's boot062 still completes two front copies and fails
explicitly at engine constructor **826F4988**, caller **823B748C**, before
publication or SDK allocation. Original geometry draws remain zero. The latest
inspected desktop capture remains `build/captures/native-present-060.jpg`, which
shows the black clear only. These tests do not establish original rendering,
menus, gameplay or recording semantics.
