# Executed original empty audio reader lifecycle

Frozen fixture, 2026-09-10. **Standalone actual-AOT run passed: 556 checks, two groups, eight managers, 34 actual allocations and matching frees, followed by original audio-root teardown and OS worker join.** The run is muted. Source: [test_audio_reader_lifecycle.cpp](K:/SimpsonsNativeCopy/tests/test_audio_reader_lifecycle.cpp). No production, runtime, configuration, CMake, generated, original, or reference files were changed.

## Correction to the earlier fixture prescription

This supersedes the Q48/default-lock prescription in the fixture section of [native-audio-reader-lifetime.md](K:/SimpsonsNativeCopy/docs/native-audio-reader-lifetime.md). **Do not acquire Q48 while the Dac worker is running.** Original `823460EC: 80790048` / `823460F0: 4BFE3631` acquires it for the worker's entire lifetime. The first fixture attempt consequently blocked; the bounded failed run is preserved in `build/audio-reader-lifecycle/q48-timeout.log`. No group had been created in that attempt.

Using Q4C directly would also be wrong for this actual startup. `82816318: 4BB306B9 ->823469D0` installs **Q40=823469B0 and Q44=823469C0** at `823469F4/F8`. These use the original mutant in **82E31BC8**: acquire tails `824337A8(handle,-1)`; release tails `82B76660(handle)`, which supplies reserved r4=0 to NtReleaseMutant. The running fixture observed real owned mutant handle **0000014C**. Default Q4C exists but is bypassed by the installed callbacks.

The fixture uses actual root wrappers **823392C8(Q)** / **823392F0(Q)**, verifies the installed callback pair and real Mutant handle, and checks their observed success returns 0/1. It neither resets guest counters nor steals another thread's lock. It never holds the callback lock while waiting for cleanup or joining the worker.

Retirement now uses a stronger path than the proposed mapped eight-byte command: **actual original `8233D950(G)` enqueues into Q's real command buffer**. `8233D95C/960` loads Q+D0 and Q+20; `8233D96C/970/974` advances by eight and writes `{8233D980,G}`. The fixture preflights against **Q+CC**, the original allocated command-buffer extent saved at `82338E54`, and verifies the resulting command words. No synthetic command storage is needed.

The real Dac worker's command executor calls `8233D980` at **823395B8**, LR **823395BC**, under the installed Q40/Q44 lock. A fixture-only forwarding observer checks return r3=8, actual deferred callback fields G+C/G+10, and Q+F0 increment before that same worker can destroy G. Phase-one deferred execution at **82339640/648 ->82348140(Q+60,1)** is also inside the same callback lock, released through Q44 at `8233967C`. Reacquiring that lock after the final-free event therefore also waits for the original callback epilogue; no Q48 acquisition is needed.

## What was actually executed and checked

Setup follows [test_dac_lifecycle.cpp](K:/SimpsonsNativeCopy/tests/test_dac_lifecycle.cpp): `Runtime.load`, initialized saved entry context, full original startup with the real native muted source, and diagnostic observation **828166FC** after the original root unlock. A fresh `EngineCpuCalls` uses the saved entry; the exception-unwound startup context is never resumed. No production caller LR is fabricated.

Each cycle invokes original **8233D5F8(2EA8FB98,4,64000,A,Q)**, with actual Q14 allocator in r8 and the original unused r9=1000. Original allocator arguments, addresses, calls and returns remain unchanged. The fixture dynamically reads both actual allocator vtables and intercepts only their indirect dispatch entries plus the original retirement command. All intercepted bodies execute normally, with no audit mutex held across an AOT call. Direct constructor calls remain AOT.

Observed allocator identities were distinct:

- G adapter **E1A66710**, vtable **8215CA2C**.
- Reader adapter **E1A503A8**, vtable **8215094C**, obtained from the already initialized **82E36B94** service.

Both cycles returned **G=E40780A0**, extent **1900B0**. The four ring slices were **E4078150..E40DC150**, **E40DC150..E4140150**, **E4140150..E41A4150**, and **E41A4150..E4208150**. All are slices of the single G allocation. Actual managers were **E2B59700, E2B5A980, E2B5BC00, E2B5CE80**, also reused in cycle two. Each M has actual **218-byte** storage, **D entries of 138 bytes**, real **14-byte H** and **10-byte filter**, default chunk **11000**, original ring/cursor initialization, empty lists, and no file/job/source state. G's ownership fields, record references/activity and original global list linkage are checked. The test does not poison or initialize these allocations or ring bytes.

Generation receipts are bounded fixture metadata: 1..34. Group generations advanced **1 ->18**, manager generations **2/6/10/14 ->19/23/27/31**. Old receipt inspection is rejected both after free and during actual address reuse. This is an observation oracle; it is **not** a native production reader registry, claim lease or copy gate. It does not touch a freed pointer to demonstrate rejection.

Every actual free is observed after the original allocator returns and checked for matching allocator, exact original caller LR and real Dac worker thread. Filter/H/entry frees precede their M free; all four M frees precede G free. The final G-free observer signals a real native event. The test then checks only host receipts and still-live Q/global fields, never freed G/M/H bytes. Both cycles restore the prior G-list head and Q+F0 count.

Finally the fixture observes real original CPU/DSP/downstream progress, calls original **82338FA0(Q)**, and verifies normal Dac worker exit code0, completed OS join, cleared **82E31BCC**, EXm0 factory stopped, and native Dac identity retired. In the recorded run Dac released on worker50872 with seven consumed/submitted/downstream-retired blocks. The exact count/thread ID is diagnostic, not an assertion.

## Reproduction and integration

Main integration is complete in **build142**: CTest
`OriginalAudioReaderLifecycle` passes all556 checks, and the full47-suite run
passes in63.22s. See `build/native-graphics-cpu-142.log` and the matching
`build/native/Testing/Temporary/LastTest.log`. The standalone provenance below
remains the earlier isolated run against build141 libraries.

```powershell
python -B build/audio-reader-lifecycle/run_probe.py
python -B build/audio-reader-lifecycle/verify_evidence.py --verify
```

The standalone script imports `tests/test_host_fp.py`'s installed toolchain helper, compiles only this test with **clang-cl /std:c++20 /fp:strict /W4 /WX**, and links existing `SimpsonsRuntime`, `SimpsonsPPC`, `SimpsonsAudio`, `SimpsonsGraphics`, and `SimpsonsAudioOutput` libraries plus their normal Windows/owned codec dependencies. It prepends the owned codec install's bin directory to PATH. It performs no parent build or generation. Input hashes before/after were identical.

Main integration: executable **AudioReaderLifecycleTests**, source **tests/test_audio_reader_lifecycle.cpp**, link **SimpsonsRuntime bcrypt**, compile **/fp:strict**, CTest **OriginalAudioReaderLifecycle** with argument **analysis/simpsons.pe** and **TIMEOUT 60**. Each deferred-free wait is bounded to eight seconds; the complete standalone child process is bounded to sixty seconds. No CMake edits were made here.

Evidence in [build/audio-reader-lifecycle](K:/SimpsonsNativeCopy/build/audio-reader-lifecycle): `command.json`, empty successful `compile.log`, successful `run.log`, executable/input hashes in `result.json`, and **47 additional original lock/command/free pins** in `evidence.json`, reproducible by `verify_evidence.py`. Frozen test SHA-256: **ad7e5e5ab4e37cf404a57d3623faf9252fae26f4bf74b366a8ad6b1c6cfc22d1**. Original image SHA-256 is recorded and verified in both reports. Parent CTest integration was not run here.

This proves the **successful original empty-reader-manager lifecycle**. It does not test asynchronous file work, actual claimed source nodes, reset/copy races, partial allocation recovery, EXm0 admission, or production generation gates. Other startup subsystems use the existing fixture's terminal Runtime shutdown; graphics terminal-cleanup notices are expected and do not constitute full application shutdown proof. No original-derived audio or other media was created. Only this document, the new test, and `build/audio-reader-lifecycle/*` were written.
