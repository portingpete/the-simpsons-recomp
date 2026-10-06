# Native control settings

Open **Options → Controls** from the frontend or pause menu. The screen uses the
game's original Apt movie, text, pulsing buttons and Accept/Cancel footers.

The first two pages list sixteen keyboard/mouse actions, with two bindings per
action. Select the page row to advance through the three pages; Left/Right on
that row moves backward/forward. On a binding row, Left selects the primary slot
and Right selects the secondary slot. The slot number appears in each label.
Press Enter/controller A or click a binding row, then press the desired key or
mouse button. The row displays the capture prompt until the input is released.
Escape cancels a capture; Delete or Backspace clears the selected slot.

A key already assigned to another action swaps with the displaced binding.
F6, F8, F9 and Windows keys remain reserved. A reserved key leaves capture active
and displays a prompt to choose another. Holding a selection or captured key
cannot navigate the following menu; focus loss cancels capture and releases input.
Menu navigation retains its fixed WASD/arrows, Enter and Escape controls so
rebinding movement or pause cannot strand the player in the settings screen.

The third page contains mouse sensitivity (25–300% in 25% steps), separate
mouse X/Y inversion switches, and **Reset keyboard and mouse defaults**. These settings
apply live. It also retains the original controller invert Y, invert X and
vibration selectors, which use the existing gamepad preference save/rollback.
Mouse inversion switches reverse the direction selected by those original camera
preferences; leaving them Off preserves the existing camera direction.

Select **Accept changes** or click the original Accept footer to save all native
bindings and mouse options. Escape/controller B or the original Cancel footer
restores the native values from when Controls opened. While capturing a binding,
Cancel stops capture and leaves the screen open.

Native preferences are stored in the profile store's sibling `.controls.cfg`
file, preserving the original profile validation. Missing or malformed files use
the default bindings and 100% mouse sensitivity without mouse inversion.

`tools/build_native_control_menu.py` extends each owned retail Options resource
after the native video patch and before Xbox constant relocation canonicalization.
It creates separate derived packages and leaves retail resources unchanged.
The keyboard pages contain ten visible rows; the mouse/controller page contains
nine. Original Audio, Credits, controls selector callbacks, screen navigation and
controller input remain authored game behavior.

The page header sits below the original Controls underline. Keyboard rows use a
26-unit pitch; the mouse page uses a 24-unit pitch above the original controller
caption/selector pairs, which retain their 38-unit pitch. The bottom row stays
clear of the original Accept/Cancel footers.

The checked callback sites are `823A54F0` (PopulateControlsSettings), `823A5518`
(SaveControlsSettings) and `823A5A00` (the Controls event branch). At the last site,
`r31` is the movie and `r30` is the original UI event. Binding capture and local
row edits replace event 6 with neutral event 0 so the original branch keeps the
screen open. Accept retains event 6, and Cancel retains event 7, allowing the
original dispatcher to save/restore gamepad settings and transition normally.
Labels use the original movie-target getter `827F20E0`, string-argument Apt bridge
`827BF8F8`, and the existing safe-string numeric export `823A4628`. Both temporary
strings lie beyond the bridge's outgoing argument spills and below saved LR.
