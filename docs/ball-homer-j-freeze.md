# Ball Homer J freeze — September 23, 2026

The manual run `build/render-tests/20260923-211109-968475/game.log`
stopped advancing at scene 5258 immediately after J at scene 5257. The earlier
texture-owner fix allowed its copied ITXD texture to bind. The next messages
were null-device calls from `82771CD4`, `82771CEC`, and `82771D08`, followed
by a read at `00004140`. Original sprite routine `82771A18` then entered the
console immediate allocator `8244C450` without a console device. Window/audio
activity continued until the window was closed.

The native sprite bridge keeps original position/alpha calculations, texture
binding, blend selection, animated UV calculations, four vertex stores and
register-restoring epilogue. Six byte-pinned hooks replace only the console
constant upload, input bindings, vertex allocation and draw submission. The
native renderer implements the original Distort sprite shaders and four blend
modes, including depth/stencil behavior and restoring retained host bindings.
Shader dataflow and limitations are documented in [the shader evidence](ball-effect-shaders.md).

The parent distortion phase also needs native scene/mask resolves, two filter
passes, encoding, scene restoration and final distortion composition. Those
continuations are included in the fix so completing the sprite cannot simply
expose the next console submission. Original CPU target-stack operations,
vertex generation and cleanup continue to execute. Viewport slots 2, 6 and 7
have checked native resolve storage; slot 7 shares the existing query-backup
owner instead of creating a second interpretation of the same texture.

Validation results are recorded under `build/ball-homer-freeze`. The focused
original-AOT fixture invokes the original sprite and parent phase with real
startup shader, declaration, texture and viewport resources. This isolates
rendering from nondeterministic gameplay routing.

Three baseline input replays did not reproduce the manual trigger: the hidden
latest-input route reached scene 5400 without the effect, a short Shift/J
sequence reached 1150 without it, and the visible full route ended at scene
2916 on an unrelated stale-shadow-texture failure. These attempts do not
establish that the exact gameplay route was replayed successfully.


Final validation (September 24 UTC): both `build/native/SimpsonsNative.exe`
and `build/native/SimpsonsInputRecorder.exe` rebuilt successfully. AOT generation
verified all 311 generated files with zero semantic diagnostics. Binary SHA256
identities are saved in `build/ball-homer-freeze/final-binaries.json`.

All 17 focused CTest cases pass; detailed results are in
`build/ball-homer-freeze/final-tests.log`. Coverage includes the sprite and
continuation on hardware and WARP, original shader evidence gates, existing
post-filter behavior, viewport snapshot ownership, all four ITXD lifecycle/cache
cases, and the complete original AOT screen bridge.

The screen bridge test executes the original parent phase twice, verifies its
five continuation draws and five resolves each time, compares preserved scene
and depth data, checks actual RGB displacement, and renders a regular screen
quad afterward. The second phase uses zero opacity and a different scene to
catch stale mask/snapshot reuse. Empty-list behavior and CPU target-stack/state
restoration are checked too. The original default-null clear pointer and retained
point-filter/linear-mip sampler combination are both supported.

A bounded production launch reached scene 900 in 22 seconds, with no failure or
null-device marker before the harness closed the window. Its artifacts are in
`build/ball-homer-freeze/20260924-014736-163184Z`. This is a launch and ordinary
rendering check; the route did not trigger the Ball effect.

These results establish the repaired rendering path under a controlled original
AOT fixture. They do not establish physical-console visual parity or replay of
the exact user-controlled scene that froze.
