# Repository setup

This is a development source snapshot. A fresh clone does not yet reproduce the
entire existing workstation build without local game inputs and evidence.

## Requirements

- Windows x64, Python 3.11+, CMake 3.24+, and Ninja on PATH.
- Visual Studio C++ tools, Windows SDK (`rc.exe` and `fxc.exe`), and LLVM at
  `C:\Program Files\LLVM`.
- XexTool 6.3. `prepare_image.py` defaults to
  `K:\XexTool_v6.3\xextool.exe`; `--xextool` accepts another location.
- Native audio build: MSYS2 at `C:\msys64` with MINGW64 GCC, binutils,
  winpthreads, and GNU make; Git at `C:\Program Files\Git`.

## Local game inputs

Place your own extracted US Xbox 360 retail data tree in
`Simpsons Game, The (USA)` beneath the project root. The supported title is
`45410809`; the supported `default.xex` SHA-256 is
`71d99dad06be1b512fc3058123b84fdad71339205a7e9249058ac5e34a82a231`.

Keep the original game tree, derived executable images and shader evidence,
generated translation, build outputs, and private save/replay data out of Git.

## Available regeneration commands

From the project root in PowerShell:

```powershell
.\tools\build.ps1 -GeneratorOnly -Jobs 6
python -B tools\prepare_image.py --xextool 'K:\XexTool_v6.3\xextool.exe'
python -B tools\analyze_effect_catalog.py
python -B tools\analyze_post_effect_catalog.py
python -B tools\inspect_assets.py --root 'Simpsons Game, The (USA)' --decode-str --image analysis\simpsons.pe --image-layout flat --output analysis\assets.json
python -B tools\generate_audio_catalog.py
python -B tools\generate_resident_audio_catalog.py
python -B tools\generate_amx_audio_catalog.py
python -B tools\build_native_audio_codec.py --jobs 4
python -B tools\recompile.py --generator build\generator-ninja\XenonRecomp\XenonRecomp.exe --analyser build\generator-ninja\XenonAnalyse\XenonAnalyse.exe
.\tools\build.ps1 -SkipGenerate -Jobs 6
```

These tools can regenerate the principal analysis inputs and offline translation
from the local game. The audio builder uses a pinned upstream FFmpeg archive,
downloading it when a verified reusable archive is unavailable, and applies its
embedded patch. The full command sequence has not been validated as a complete
clean-clone build.

## Current fresh-clone limitations

Some shader qualification tools require hash-pinned reference files under
`K:\Simpsons\RexGlueCurrent`, including Xenos/ucode declarations and the depth
format reference in `texture_cache.cpp`. Those workstation references are not
included. Direct original-derived shader dumps and translated shader output also
remain local; keep the available conversion tools with the player's game inputs.

The full test build references evidence helpers in excluded output directories:
`build/effect-reflection-all49/verify.py` and `evidence.json`, and
`build/effect-finalizers/verify.py`. These need to be recovered or moved into
maintained source before all test targets can be rebuilt from a fresh clone.

Status reports and historical evidence documents describe the local tested
workspace. Captures, compiled binaries, original images, replay archives,
shortcuts, and analysis exports referenced by those documents are not supplied.
Change generators and source configuration rather than generated translation.
