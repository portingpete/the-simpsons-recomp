# Intro rendering repair — 2026-09-21

The target is the opening Land of Chocolate plaza: the shop sign/window and the chocolate river behind the pretzel fence.

## References

- The two user-provided screenshots are preserved at `build/renderer-fix-evidence/reference-shop.png` and `reference-pretzels.png`. They are the direct visual comparison targets.
- [MobyGames Xbox 360 screenshot: Homer dreams of chocolate bunnies](https://www.mobygames.com/game/xbox360/simpsons-game/screenshots/gameShotId%2C430967/).
- [Land of Chocolate gameplay reference](https://www.youtube.com/watch?v=v6wofAXb5J0).
- [Additional game screenshots and Land of Chocolate trailer](https://www.noiseland.co/nextgen.php).

The online references were found through search. Direct retrieval was throttled or blocked for some pages; no downloaded reference was substituted for game assets.

## Repairs

1. Ordinary textured rigid material `820168F8` has both opaque and alpha draws. Selecting alpha solely by material source put raw color into the opaque palette/post-processing path. Selection now honors the original requested technique, including transitions within the same effect.
2. The `168F8` alpha transcription mistook fetch serialization for predication. Its original texture fetch is unconditional. Predicate jumps, signed scalar subtraction, alpha discard with a zero destination mask, and SM3 zero-product normalization behavior are now preserved. Dual-textured alpha uses the same verified executable instructions.
3. Chocolate material values existed in original CPU storage but were omitted from the GPU material commit. The original alpha-pass map supplies PS c50/c44/c43/c42 and VS c47/c46/c45. The native vertex bank now includes c47. Dirty commits preserve prior values, and runtime binding checks pin each mapping to the original metadata.
4. Generated shader headers have explicit dependencies on the C++ sources that embed them. A single build now recompiles and links changed shader bytecode.

No replacement textures, material recoloring, geometry suppression, or post-processing bypass is used. The full-size color backing and EDGE → AA → EDGEAA chain are retained.

## Evidence

- Before river repair: `build/automatic-startup/20260921-intro-render-clean/plaza-frame/native-frame-1413056.png`.
- River draw identification: `build/automatic-startup/20260921-river-owner/river-frame-2/native-frame-1296941.png` (temporary orange shader diagnostic).
- Original material values versus zero GPU constants: `build/automatic-startup/20260921-river-material/captures/river-material-1.txt`.
- Correct mapped constants: `build/automatic-startup/20260921-river-fix/captures/river-material-1.txt`.
- Corrected shop: `build/automatic-startup/20260921-river-fix/shop-frame/native-frame-1292619.png`.
- Corrected river: `build/automatic-startup/20260921-river-fix/river-frame/native-frame-1422458.png`.
- Final clean executable, shop and rabbit: `build/automatic-startup/20260921-intro-render-verified/shop-frame/native-frame-986141.png`.
- Final clean executable, river and pretzels: `build/automatic-startup/20260921-intro-render-verified/plaza-frame/native-frame-1126080.png`.
- Source/executable hashes and final readback checks: `build/renderer-fix-evidence/clean-build-hashes.json` and `final-verification.json`.

The captures are completed native renderer readbacks at 1280×720. Their metadata records the completed front copy and active scene draws. The window was occluded, so display acceptance is not claimed. Camera positions and animation times differ from the supplied references.

## Validation and limits

Regression coverage checks original instruction bits, alpha-test boundaries on WARP and hardware, alpha texture fetches with testing disabled, zero-vector arithmetic, original chocolate parameter mappings, isolated dirty-leaf updates, GPU constant readback, and immediate/recorded rendering state and ownership.

Final result: **19/19 focused CTest entries passed** (`build/intro-render-final-tests.log`). The material artifact and constant-buffer test also passed separately on hardware (`build/intro-render-material-hardware.log`). A build dry run found no pending shader generation, C++ compilation, or link step. Temporary color overrides and river material dumps are removed from the shipped sources.

The two distant dark faces beneath the bridge were inspected in diagnostic views, but their intended appearance was not established by the supplied references. No speculative recoloring was applied to them.

Moving farther forward from the river view encountered a separate audio failure: `Unqualified EXm0 streamed format/start/seek or source ownership` at `823424D8`. This rendering repair does not claim completion of a full-level play-through. Evidence is retained in `build/automatic-startup/20260921-river-fix/game.log`.
