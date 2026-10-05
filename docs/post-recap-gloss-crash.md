# Crash after the mission recap loading screen

The manual completion run `Completion-20261001-025225-143Z-24408-0000` reaches the recap and then rejects an original gloss material at `827400F8`. Its owner is typed `E1AAD2D0`, source `82019988`, identity `00500025`, with the original Boolean flags `1/1`. The preceding ordinary dual-textured failure has already been passed.

This failure revealed missing alpha rendering for three existing opaque material families. Original metadata pins these separate passes:

| Family | Source | Alpha VS / PS | Context |
| --- | --- | --- | --- |
| Gloss | `82019988` | `8201A430` / `8201B0BC` | `2DF0` |
| Multitone | `820547E8` | `82055440` / `820560B8` | `3040` |
| Normalmap | `82057E08` | `820589EC` / `82059880` | `3450` |

All three sample the base texture at stage 0 with linear filtering and wrap addressing. Their alpha maps omit shared shadows and the opaque noise/normal texture. Gloss maps specular exponent to PS50 and scale to PS47; normalmap maps exponent to PS50 and parameters to PS47. All use custom lines PS49, object PS40 and eye PS4. Multitone has no vertex noise material upload in this pass.

The repair adds the original shaders and selects the correct pass-specific inputs, material register maps, texture and sampler checks. The gloss alpha vertex executable matches the already-qualified textured alpha vertex shader; the other two share a distinct five-input vertex program. Normalmap alpha does not consume a tangent. Live packet ownership, original pass metadata, finite constants, dirty-bank semantics and ABI guards remain checked.

The existing **Play First Mission - Completion.lnk** still launches immediately into the completion route without a startup window. Each completion launch retains its private profile/content/video copy and logs. The primary stores are unchanged. No game assets are edited.

Before-source snapshots and the actual manual crash log are preserved under `build/post-recap-crash/before`.

Both native and native-release game, recorder and direct-launcher builds are current. All 22 focused CTest groups pass in each build (14.81 and 13.87 seconds); see `build/post-recap-crash/final-native-tests.log` and `final-release-tests.log`. The new whole-original dispatcher fixtures pass 328 gloss, 331 multitone and 414 normalmap checks, including all four Boolean pairs and a normalmap alpha draw with its unused tangent declaration removed. Both WARP and hardware pass 72 independent original-instruction numerical shader cases each. Seven offline tests cover record/control/fetch/linkage mutations, original split scalar45 operands, zero inputs and generated-source identity. Material projection tests pin the original selected maps and dirty accumulation; artifact inventory verifies 74 compiled records, 182 unsupported and no owner leaks. Existing UV/ordinary dual, recorded/direct mesh, launcher, completion and profile CLI regressions also pass.

Final AOT verification covers 311 files with zero semantic diagnostics. Primary profile, save-index and video hashes match the baseline; readback of the existing shortcuts confirms the direct native launcher and completion flag. No gameplay or shortcuts were launched. The full recap-to-next-area route remains for the user's manual retest; no gameplay performance gain is claimed from these bounded fixtures.
