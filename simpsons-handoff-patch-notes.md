# Handoff continuation — 2026-09-18

## Status

**No new Windows game run was performed. No first gameplay frame is verified.**
The most recent supplied run remains reach-game-212. This patch is a candidate
packet-owner correction plus diagnostics and a receipt-based gameplay gate,
not evidence that packet 82D6EAF0 now completes.

The supplied archive contains 33 entries: selected source files, tests, tools,
and three logs. It does not contain the full runtime, analysis/simpsons.pe,
generated AOT bodies, native executable, startup profile/save configuration,
or the Windows toolchain. The available execution environment is Linux.

## Findings from the supplied source and logs

1. The fallback diagnostic's `typed=00000000` was read from
   `peek(peek(packet)+0x1C)`, i.e. metadata+0x1C. The execution path actually
   obtains the typed pointer from packet+0x18. The old diagnostic cannot
   establish that the second packet's typed pointer is null.
2. The untested fallback in the handoff allowed chocolate's r4/r5 values when
   wrapper-chain lookup failed and the *previously selected* effect was
   chocolate. This is not proof of the new packet's effect identity.
3. engine_driver.cpp's presentation log/capture total counted screen, im2d,
   movie, edge, AA, shadow and z-prepass draws, but omitted rigid, skin and sky.
   The cumulative total already reaches 1,689,696 before the supplied crash;
   that number alone is not a gameplay receipt.
4. auto_start_native.py always wrote gameplay_verified=false, yet an alive
   process after the observation delay could still produce success=true for
   --until game. Input delivery is not a first-presented-gameplay-frame proof.
5. skinImmediateOperation documents a zero packet+0x14 and obtains its context
   from the live effect record. Generic packet-owner validation must retain
   this skin-specific rule rather than requiring rigid's packet context.

## Changes

### Current-packet effect ownership

runtime/rigid_packet_owner.h resolves packet+0x18, reads typed+0x1C, and checks
that live registry record against typed manager/wrapper addresses, source,
vtable and context. Rigid/sky/chocolate require the packet context to match;
skin retains its observed zero packet context and uses the creation context.
The previous selection is not an input to source resolution.

Both fallback qualification and beginRigid use this resolver. The original
wrapper/manager/cache checks before GPU work remain in place. The resolver
does not repair guest pointers, substitute an effect, or bypass GPU-owner
checks. A readable typed identity does not prove that all later wrapper
fields will be valid.

Failures identify the exact checked-read/lookup/association step and carry
packet, typed, manager, wrapper and context evidence. Nonzero r4/r5 entries
print the actual packet-derived effect source. The fallback `typed` diagnostic
now prints the real typed pointer.

The new header is explicitly included in tools/recompile.py's AOT input list.
Recompilation is required before the native build.

### Material texture evidence — not a speculative texture fix

Chocolate type-1 rows now log the handle, leaf, default-word offset, both
source value words, and all six binding words. Existing texture bindings,
shader profiles, blend behavior and sampler assumptions are unchanged.
The known missing chocolate material texture binding remains open; exact
resource/stage qualification still needs live row evidence.

### Gameplay receipt and launcher behavior

Presentation/capture totals now include rigid, skin and sky draw counters.
Metadata additionally includes their individual cumulative counters,
scene_geometry_draws, and frame_scene_geometry_draws. The frame delta is
updated for every completed presentation, not just when a capture is saved.

After the recorded Continue Game and opening-movie skip sequence, the launcher
polls captures and requires all of the following:

- a completed 1280x720 front readback in R10G10B10A2_UNORM_LE;
- display_accepted=true and a presentation newer than the verified main menu;
- consistent scene counters and positive scene draws in that presentation;
- at least one nonzero RGB pixel, excluding the two alpha bits;
- no matching known startup/error screen and no observed native failure.

The accepted result stores the capture path, presentation, counters, nonzero
RGB count and pixel SHA-256. Old capture metadata without scene counters fails
closed. A timeout fails --until game rather than reporting input delivery as
success. Status 3 is reported as a native SEH exception. [TERMINATE] now stops
automatic replay alongside the existing failure markers.

This is a receipt-level rendering milestone, not proof of visual correctness,
complete gameplay, correct chocolate textures, or long-term stability.

## Validation actually performed

- Clang C++20, -Wall -Wextra -Werror -pedantic, AddressSanitizer and
  UndefinedBehaviorSanitizer: **146 synthetic assertions passed**.
- GCC C++20, -Wall -Wextra -Werror -pedantic: the same **146 assertions passed**.
- Python: **26 tests passed**, including rejection of alpha-only black pixels,
  missing/incorrect counters, cumulative-only geometry, old presentations,
  occluded presents, known menus, damaged-save screens, status 3 and failures
  observed while a capture completes. The actual supplied reach-game-212 log
  is rejected with its original fallback failure.
- git diff --check: passed.
- Native CMake configuration: **blocked**, with the project's explicit
  `Native x64 Windows ClangCL is required` error.
- Existing chocolate shader inventory tests: **blocked**, because the archive
  lacks analyze_screen_shaders.py; the required game image is also absent.
- No FXC, D3D11 execution, full native compilation, AOT regeneration, or live
  packet-82D6EAF0 replay was performed here.

The synthetic positive test capture is an in-memory fixture. It is not an
actual game frame and is not distributed as gameplay evidence.

## Next Windows run

Apply the patch to the exact uploaded baseline using the package's
apply_patch.py. It checks hashes and backs up modified files instead of
silently overwriting newer work. Then, from K:\SimpsonsNativeCopy:

```powershell
powershell -NoProfile -File .\tools\resume_handoff.ps1 -RunName reach-game-213
```

The script runs the receipt tests, regenerates AOT, builds, and launches the
recorded startup sequence. It refuses an existing run directory and stops
on the first nonzero exit. Use another run name if 213 already exists.

Inspect [NATIVE FALLBACK OWNER], [NATIVE PACKET OWNER], and
[NATIVE CHOCOLATE MATROW] in the resulting game.log. If the second packet is
not actually chocolate, its own source is now reported rather than inferred
from the previous packet. If ownership resolves but another guard fails,
continue from that exact failure. Do not change the gameplay flag manually.
