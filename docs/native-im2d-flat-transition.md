# Flat Im2D after textured text

Actual162 reaches the original copyright screen after the startup movies.
The completed front readback at `build/captures/ui-162/native-frame-1111.png`
contains readable original copyright text. It is a renderer readback;
desktop presentation was occluded. This is not the main menu.

The next draw has a null raster. Original pixel builder `8240EAA8` emits
the flat diffuse-color result, but also declares and samples texture0.
The sampled value never contributes to either output RGB or output alpha.
The original texture argument remains selected after the preceding textured
draw even though the new stage operation selects only argument2 (diffuse).
The original vertex macro builder consequently enables texture coordinate0.

`qualifyIm2DProgram` now accepts this exact complete generated expression
for a null-raster draw and checks the corresponding original UV0 macro.
The existing native flat shader already implements its output calculation.
Texture-dependent expressions and other vertex options remain rejected.
Original argument state is preserved; the fix does not clear retained state
or substitute a generated program.

The original CPU shader fixture now executes flat, textured, then flat
schedules consecutively. The last schedule reproduces the retained unused
texture argument through the original queue, commit and source builders.
It verifies the complete source and all twenty macros, successful native
qualification, rejection with the wrong active texture profile, scratch
lifetime, and continued rejection of lighting, fog and texture transforms.
