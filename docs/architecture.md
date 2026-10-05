# The Simpsons Game native Windows port

Target: the US Xbox 360 retail files in `Simpsons Game, The (USA)`. The original
folder and `K:\DarkRecomp` / `K:\Simpsons` are read-only references. No release or
playable gameplay has yet been verified in this new workspace.

## Verified starting evidence (2026-09-09)

- Original `default.xex`: SHA-256
  `71d99dad06be1b512fc3058123b84fdad71339205a7e9249058ac5e34a82a231`,
  14,200,832 bytes. XexTool 6.3 reports encrypted, uncompressed retail XEX2.
- Title ID `45410809`, media ID `05631042`, image `simpsons_f.exe`, timestamp
  2007-09-26 22:02:20 UTC, load address `0x82000000`, entry `0x82432280`,
  image size `0x00ec0000`, stack `0x40000`, TLS 64 slots / 12 bytes of raw data.
- Xbox imports: `xam.xex`, `xboxkrnl.exe`; statically linked D3D9/D3DX9,
  XGRAPHC, XAUD and platform libraries. These are **not** native host services.
- Loose Lua game flow and `.str` level/character/frontend streams survive.
  Resource structure and renderer boundary require independent investigation.

## Architecture decisions

1. Offline PPC-to-C++ translation, compiled to x64 with ClangCL. Retain guest
   registers and a checked, big-endian 32-bit address space for the original
   simulation. No instruction decoding or compilation in the shipped runtime.
2. Keep generator source in `third_party`, configuration in `config`, generated
   output in `build/generated`, handwritten services in `runtime`, renderer in
   `renderer`, and host entry in `app`. Fix source/configuration, never generated
   functions. Bind all derived output to the exact input and generator hashes.
3. Native Windows services for files, memory, threading, timers, input, saves,
   audio and video. Missing imports, invalid calls, invalid memory and unsupported
   translation must stop with address/name diagnostics. No pretend successes.
4. Use the verified original engine driver/resource boundaries with native
   D3D11 ownership. Current integration covers startup state, textures, targets,
   buffer/declaration/pipeline lifetimes and front copy/presentation. Original
   mesh/material draw contracts and visible game rendering remain unverified. No
   PM4/ring-buffer processor, Xenos register renderer or console GPU emulator.
   Cross-thread draw snapshots must own their referenced data until completion.
5. Preserve guest simulation timing; report newly rendered frames separately from
   repeated presentation. Automated executable launches remain muted by default.
6. Packaging must derive assets from the player's files and exclude proprietary
   binaries, generated game translation, reference emulators and game data from
   any public source release until packaging rights/strategy are addressed.

## Tool evidence and limitations

Primary-source checks on 2026-09-09:
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp/blob/main/README.md)
documents offline C++ generation, its lack of a runtime, incomplete instruction
cases, game-dependent jump-table analysis, and exception limitations.
[ReXGlue](https://github.com/rexglue/rexglue-sdk) supplies a broader runtime with
Xenia roots; a successful build using it alone would not establish the required
renderer architecture. Local tools and actual translation results take precedence
over generalized compatibility claims.

DarkRecomp's reproducible generation gate, strict FP build and engine-level owned
draw records are useful techniques. Its Starbreeze layouts and shader sources
are inapplicable without Simpsons evidence. In particular, its context template
has zero-return MMIO/low-address fallbacks: these must not be inherited here.

## Unresolved work

The original Dac0 CPU mixer, source buffers, event, worker and graph destruction
now connect to native Windows output through owned PCM/DSP queues. The actual
original worker drives CPU callbacks; private DSP and device callbacks never
access guest memory. Source consumption and downstream playback completion have
separate receipts. Windows speaker routing, volume and buffering are explicit
platform adaptations. See `native-dac-integration.md` and current `STATUS.md`.
The independent native XMA decoder is qualified separately; original EXm0 input,
trimming, full-quota decode staging and numerical conversion remain guarded.

Further reached imports, engine mesh/material draw contracts, remaining media
integration, native user interaction and complete gameplay/progression remain
unverified. The built AOT output passes its semantic/hash gate; this does not
prove all future original control-flow paths or platform consumers are covered.
No fundamental architectural impossibility has been demonstrated.
