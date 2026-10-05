# Native yellow input prompts

The default theme imports the user's revised hand-drawn lettering sheet at
`../ChatGPT Image Sep 26, 2026, 10_10_55 PM.png` as supplied. Its 27 icons preserve
the glossy yellow/orange shapes and original colors while supplying the keyboard
and labeled mouse artwork. No recoloring is applied.
`yellow-supplement.png` adds Up, Left, Down, Right, mouse movement, G, and `?`.
It was revised October 5, 2026 against the original sheet to use uneven handwritten
marker strokes instead of smooth, heavy lettering. The source sheet stays intact.

The complete set covers every key on a standard 104-key US ANSI keyboard:

- `yellow-alphanumeric.png`: the remaining letters and top-row digits.
- `yellow-function-keys.png`: F1 through F12.
- `yellow-special-keys.png`: Backspace, Caps Lock, left/right Shift, Ctrl, Alt and
  Windows keys, Menu, navigation keys, Print Screen, Scroll Lock and Pause.
- `yellow-punctuation-numpad.png`: all eleven punctuation keys, Num Lock and
  distinct numpad digits, decimal, Enter and arithmetic keys.

The numpad keys carry `NUM` labels; modifier keys carry `L`/`R` labels so a future
rebinding screen can identify the physical key. Generic Shift, Ctrl and question
mark prompts, the four mouse prompts and movement groups remain available.
This expands the artwork library; the current runtime still uses its fixed bindings.

All five built-in image_gen requests are saved in `generation-prompt.txt`.
The yellow artwork is not attributed to Xelu or labeled CC0.
See the [complete compiled preview](yellow-full-keyboard-preview.png).

`manifest.json` records source PNG hashes, measured component bounds, extracted
RGBA hashes, and the WASD/directional group compositions. Import detects substantial
four-connected shapes at alpha 32/255, then retains a four-pixel soft-edge margin
from the unmodified source. This separates the faint export noise between F,
Space, and Shift without removing their opaque outlines. Extracted RGBA pixels
are copied directly from the sheets; resizing uses premultiplied alpha.

All 111 single icons and two groups are embedded as 64×64 RGBA8 images in
`renderer/native_prompt_icons.generated.h`. All existing 36 enum values remain
stable; the 77 new key icons follow them. The runtime does not load PNGs or
require Pillow.

From the project root:

```powershell
python -B tools/compile_prompt_icons.py --preview assets/native-input-prompts/yellow-full-keyboard-preview.png
python -B tools/compile_prompt_icons.py --check
python -B tests/test_prompt_icons.py
```

`--theme xelu` compiles the original 30-icon CC0 reference theme into the same
header and `NativePromptIcons` interface. Run the compiler without that option
to restore yellow. The historical `renderer/xelu_light.generated.h` is retained
as a reference and is not modified by theme switching.
