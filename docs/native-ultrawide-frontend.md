# Ultrawide frontend

On outputs wider than 16:9 (21:9 up to 32:9) the title clouds, the opening
living-room zoom, every TV-cabinet menu and the pause menu with its
transition fill the whole screen. Nothing is stretched: the Apt stage keeps
its 16:9 scale and position, and the screen edges show art that continues the
scene.

## Renderer: UI overscan

Apt draws are Im2D packets with `preserveAspect`, normally drawn into the
centered 16:9 content viewport. With `Im2DDraw::overscan` the packet uses the
full-width viewport instead, and its x positions are remapped so every vertex
lands on exactly the pixel it had in the 16:9 viewport
(`renderer/im2d_pipeline.cpp`). The stage renders identically; geometry past
the stage edges is no longer clipped. Only wider outputs change: on 16:9 and
narrower outputs the viewports are equal and the remap is skipped.

`EngineDriver::setUiDrawing` decides per UI pass (`runtime/engine_driver.cpp`):

- No world geometry was drawn this frame: the frontend, loading screens and
  other 2D screens.
- The previous frame's UI drew a full-screen menu backdrop
  (`isFullCanvasUiBackdrop`: one textured quad covering the whole canvas, such
  as a menu's TV cabinet). The world is hidden behind it.
- The game is paused, this frame or the last: the original gameplay update's
  pause query (`SimpsonsNativePhotoPause`, `EngineDriver::noteGameplayPaused`).
  The pause menu's transition plays over the paused game before any backdrop
  covers it.

The gameplay HUD keeps the 16:9 stage. Full-canvas solid fills (fades, GrayOut)
already cover the whole target and are unchanged. Flat bands across the whole
canvas width (`isFullWidthUiBand`, such as the pause menu's closing shutters)
span the output the same way on any UI pass: on outputs at least as wide as
16:9 both viewports map y identically, so only their flat color reaches the
sides. This also covers the first shutter frames, which play before the game
reports paused. Apt clips with depth: mask shapes write depth only, clipped art
draws with a `>=` depth test, and the masks are reset by depth-only triangles on
the canvas corners. Those resets (`isCanvasDepthClear`) cover the whole target
like the fills, otherwise clipped art such as the logo's glint would show past
the stage. A mask zoomed past the whole canvas looks the same as a reset, so
clipped art must not depend on it past the stage (see the pause TVs below).
Mouse hit testing uses stage coordinates, which do not move.

`[NATIVE UI OVERSCAN]` log lines name each change: full-screen menu backdrop
shown/hidden, gameplay paused/resumed, and full-width UI band spans the
output/ended.

## Art: `tools/build_native_ultrawide_art.py`

The retail art was authored past the 640x480 stage for 16:9, and most of it
continues further: the title sky, most clouds, and the living room are wide
enough for 32:9. The tool fills the rest while the derived frontend package is
built (`tools/build_native_video_menu.py`):

- **Living-room wall around the menus.** `SharedLibrary.BgndTV`, the TV cabinet
  of every frontend menu and popup, gains the wall the opening zoom ends on
  (the pause menu, set in a studio, uses its own; see below): the floor, pink wall and lavender window rectangles and
  the 6 pt trim lines of `simpsons_start` sprite 81, mapped into BgndTV's local
  space as solid shapes (new `rubi` rectangle geometry). They stay outside the
  cabinet, whose transparent screen shows the menu content drawn behind it. At
  16:9 the cabinet covers all of them.
- **Opening zoom keeps its own room, wider.** `simpsons_start` imports
  `BgndTV_intro`, an unchanged copy of the cabinet, because its living room
  already draws the wall. The room flies in as a box that covers the 16:9 view
  from frame 650; a copy of its floor, wall and horizontal trim lines widened
  to x -3400..4800 follows it one depth below from that frame until the final
  room replaces it at 694, so wider outputs never see its edges.
- **Painted cloud ends.** Four title cloud textures were cut where the 16:9
  stage ended (Cloud_03 left, Cloud_04 and Cloud_08 right, Cloud_09 ran into
  its texture edge). Each gets a billowed end: billows meeting the cloud's own
  top and bottom edges, the body mirrored from the cloud and remapped into the
  new outline, and the outline shading measured from the cloud's own edges.
  Only BC2 blocks past a block-aligned start column are re-encoded.
- **Pause menu transition** (`pause.swf`, in `simpsons_chars_global.str`). Its
  PauseMenu timeline closes shutters over the paused game, opens on a TV in a
  studio, zooms into the TV's screen and shows the menu on it (and reverses
  this when the menu closes):
  - The vertical shutters, a bar turned on its side, close the slit left
    between the horizontal ones from the 16:9 edges inward. Their outer ends
    move out to x +-1000 around the stage center (32:9 shows +-853); the inner
    ends keep their motion, so 16:9 shows the same bars.
  - The studio ends where 16:9 does, at the flat outer panels of its blue door
    frame. Two solid rectangles one depth below the studio art continue them
    out past 32:9. Their color and inner edges are read from the studio
    texture (`8_pause`), from the panels' opaque edge texels.
  - The studio's TV zooms `SharedLibrary.BgndTV` from full screen down into the
    studio and back. Zoomed out, the living-room wall would enter the 16:9 view,
    and zoomed in, its clip mask covers the whole canvas and spans the output
    like a reset. So `pause.swf` imports `BgndTV_intro` for these TVs, and the
    menu TV the transition ends on is a new sprite: that plain cabinet with the
    studio's wall color around it, laid out like BgndTV's wall. The scene stays
    in the studio from the first shutter frame to the menu.

Tests (`NativeUltrawideArt`, `NativeVideoMenuAssets`, `NativeUltrawide`) check
the wall against the opening scene and the cabinet texture, the patched Apt
structures, that painted cloud columns never enter the 16:9 view in any title
frame (16:9 stays pixel-identical), that the pause shutters cover the same 16:9
span and reach past 32:9, that the studio wall stays outside the 16:9 view,
that no pause TV places the walled cabinet, that every screen in both packages
draws the walled cabinet only at the menu framing (0.67 on the stage centre,
where it covers 16:9), that every ultrawide patch lands in
a built package, that the stage renders identically with and without overscan,
and that full-width bands span wide outputs row for row.

Verified in game at 16:9, 21:9 and 32:9 with burst captures of the pause menu
opening and closing (private saves, background window): at 16:9 the
transition matches a build without these art patches.
