# Guard-only probe 035

Reach in-game remains incomplete. A temporary experiment admitted effect
`8202AD78` in `SimpsonsNativeSceneDispatch`; no other guards were changed.
The pinned AOT regeneration and focused application build succeeded.
NativeRigidVertexDecode, NativeRigidShaderWARP and NativeRigidShaderHardware
passed (3/3). The retail dualtextured instruction inventory rejected all 107
semantic mutations; its current --verify flag does not compare the HLSL source.

Replay evidence: `K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-guard-probe-035\game.log`.
The replay verified the main menu and delivered Continue Game/movie skip,
then the application failed with `Original rigid recording-build packet differs`.
The replay helper's success only describes input-sequence completion, not gameplay.
No dualtextured draw, presented gameplay frame, or character control was verified.

The rejection is at `SimpsonsNativeSceneRecordingBuild` in
`K:\SimpsonsNativeCopy\runtime\engine_driver.cpp`, line 1694. Its source allowlist
also excludes `8202AD78`, so the condition short-circuits before packet-field
checks. This experiment establishes neither a malformed packet nor an allocation
defect. Broadening this second gate alone would still leave the missing rigid
profile and native shader/input integration.

Diagnostic replay 036, under
`K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-diagnostic-036`,
confirmed source=8202AD78, phase=reflected=1, vtable=820616C0,
technique=0003FFFC, wrapper=expected_wrapper=E1AAC9A8, meta24=0,
bucket=1, enabled=1, context=00900001, caller=82740A60. All checked
non-source fields matched; the source allowlist alone caused this rejection.

The temporary admission, diagnostic logging and unfinished vertex-layout edit
were reverted. The restored application was regenerated and rebuilt. Clean
replay 037, under
`K:\SimpsonsNativeCopy\build\automatic-startup\reach-game-clean-037`,
verified the main menu at 22.4666s and movie skip at 23.7096s, then reproduced
the original 82740680/caller8273B4E0 failure at 24.065s. No gameplay was reached.
No runtime implementation change remains from these experiments.

Clean capture inspection confirms typed identity00500024/vtable820616C0,
opaque technique at A8=0003FFFC and separate AC=0007FFFC. The diagnostic's
technique_AC is not the field checked by the rigid opaque guard. The declaration
has stride36: position offset0, normal12, color16, UV0 float2 at20 and UV1 float2
at28, followed by the expected terminator. Generated hooks remain at original
entries82740680 and82740420. Final AOT verification passes311 files with zero
semantic diagnostics; the three focused vertex/shader tests pass again.
The complete162-test suite was not rerun during these experiments.
The next implementation still requires the dualtextured profile, UV1 input,
exact shader binding and direct/recorded ownership qualification described in
`K:\SimpsonsNativeCopy\docs\session-checkpoint-2026-09-13-live034.md`.
