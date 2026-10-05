# Native yellow input prompts

The default theme imports the user's revised hand-drawn lettering sheet at
`../ChatGPT Image Sep 26, 2026, 10_10_55 PM.png` as supplied. Its 27 icons preserve
the glossy yellow/orange shapes and original colors while supplying the keyboard
and labeled mouse artwork. No recoloring is applied.
`yellow-supplement.png` adds Up, Left, Down, Right, mouse movement, G, and `?`
in a matching style. The supplement's generation request is saved in
`generation-prompt.txt`. The yellow artwork is not attributed to Xelu or labeled CC0.

`manifest.json` records source PNG hashes, measured component bounds, extracted
RGBA hashes, and the WASD/directional group compositions. Import detects substantial
four-connected shapes at alpha 32/255, then retains a four-pixel soft-edge margin
from the unmodified source. This separates the faint export noise between F,
Space, and Shift without removing their opaque outlines. Extracted RGBA pixels
are copied directly from the sheets; resizing uses premultiplied alpha.

All 34 single icons and two groups are embedded as 64×64 RGBA8 images in
`renderer/native_prompt_icons.generated.h`. Existing enum values remain stable;
Z, X, C, V, Five, and Six follow the original 30 entries. The runtime does not
load PNGs or require Pillow.

From the project root:

```powershell
python -B tools/compile_prompt_icons.py --preview build/native-input-prompts/yellow-icons-preview.png
python -B tools/compile_prompt_icons.py --check
python -B tests/test_prompt_icons.py
```

`--theme xelu` compiles the original 30-icon CC0 reference theme into the same
header and `NativePromptIcons` interface. Run the compiler without that option
to restore yellow. The historical `renderer/xelu_light.generated.h` is retained
as a reference and is not modified by theme switching.
