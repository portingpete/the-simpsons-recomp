# The Simpsons Game — native Windows port

A development port of the US Xbox 360 version of The Simpsons Game, title
`45410809`, for Windows x64. The project uses offline PowerPC-to-C++ translation
compiled ahead of time, handwritten native platform services, and a native
Direct3D 11 renderer.

This repository contains the current native port project: application, runtime,
renderer, audio, build tools, tests, configuration, documentation, custom input
art, and vendored translator dependencies.

## Current development state

The existing local build loads all levels. Full gameplay testing across those
levels is still incomplete. The port includes movement and jumping, native
mouse and keyboard input, native video settings, first-mission completion, and
rendering and crash fixes. The port is still in development; progression,
stability, visual fidelity, and performance need further testing and refinement.

Sound is enabled by default. Use `--mute-audio` for muted diagnostic runs.

## Getting started

Read [repository setup](docs/repository-setup.md) for prerequisites, local game
inputs, regeneration commands, and current fresh-clone limitations. This source
snapshot does not include a ready-to-play executable or the original game.

Supply your own supported retail game files in `Simpsons Game, The (USA)`.
Game data, executable images, generated game translation, local evidence,
compiled output, replay payloads, and private saves stay outside Git.

For an existing configured workstation build, use
[Play The Simpsons Game.cmd](Play%20The%20Simpsons%20Game.cmd).
See [launcher help](docs/launcher.md), [native input prompts](docs/native-input-prompts.md),
[native control settings](docs/native-control-settings.md),
[architecture](docs/architecture.md), [runtime stall profiling](docs/runtime-stall-profiler.md),
and [development checklist](docs/checklist.md).

## Dependencies and credits

The vendored [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) source includes
project-specific generator changes. Dependency license files remain with their
sources. The native audio builder derives an isolated codec from pinned FFmpeg
source and records its provenance locally.

Input art and provenance are documented in [native input prompts](assets/native-input-prompts/README.md)
and [Xelu's CC0 reference theme](assets/xelu-light/README.md). Development uses
AI-assisted implementation and review; the supplied artwork and generated
supplement are identified in their asset manifest.
