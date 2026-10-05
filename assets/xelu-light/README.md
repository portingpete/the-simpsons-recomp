# Xelu Light input prompts

Original, unmodified PNGs from [Xelu's Free Controller & Key Prompts](https://thoseawesomeguys.com/prompts/), by Nicolae (Xelu) Berbece. Downloaded September 10, 2026. The bundled `LICENSE.txt` is the original pack readme: CC0, commercial use permitted, attribution optional.

This is the retained Xelu theme. The game now defaults to the user's yellow sheet in [native-input-prompts](../native-input-prompts/README.md). `tools/compile_prompt_icons.py --theme xelu` selects these 64px icons for the next build. It trims transparent export padding, fits each icon without stretching it, and resamples premultiplied alpha to keep its edges clean. WASD and arrow groups combine the original keycaps. The generated textures contain straight-alpha RGBA8 pixels with transparent borders; Space and other wide keys keep their authored proportions.

Run with Python and Pillow from the repository root:

```powershell
python tools/compile_prompt_icons.py --theme xelu
python tools/compile_prompt_icons.py --theme xelu --check --preview build/native-input-prompts/xelu-icons.png
```

The active generated header is `renderer/native_prompt_icons.generated.h`; the earlier `renderer/xelu_light.generated.h` remains as a reference. `--check` verifies exact header bytes without changing them; `--preview PATH.png` creates a contact sheet. Run the compiler without `--theme xelu` to return to the yellow theme, then rebuild `SimpsonsLauncher` to refresh both game executables. Normal builds do not require Pillow or load PNGs at runtime. Keep the original PNGs and `LICENSE.txt` unchanged.

Controller input continues to select the game's original controller artwork.
