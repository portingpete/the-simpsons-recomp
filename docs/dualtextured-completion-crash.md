# Ordinary dual-texture fallback crash after mission completion

The manual completion run `Completion-20261001-015123-814Z-70244-0000`
reached original `EpisodeComplete` and rendered the completion movie, then
stopped with `Unqualified original opaque rigid fallback entry` at `827400F8`.
The failing packet was `82D6EE08`, typed owner `E1AACAF0`, with incoming
`r4/r5=1/1`. The same run's reflection and packet-owner diagnostics identify
that owner as `simpsons_rigid_dualtextured`, source `8202AD78`, identity
`00500024`. This is the ordinary material; the animated UV material repaired
earlier has source `820465E8`.

The native source-specific argument guard already admitted the ordinary
dual-texture alpha pass with `r4=1`, but required its incoming `r5` to be zero.
The corrected guard admits the original Boolean values for both registers for
this already-supported source. Other sources and downstream packet ownership,
material, texture, geometry, pass and cleanup checks retain their qualification.

## Original instruction evidence

The actual dispatcher `82740680` loads metadata `+8` at `827408B4` and
extracts bit1 into `r21` at `827408B8` (`81690008`, `5575FFFE`). That flag can
select alpha `r23=1`; packet byte `+12=0` can instead retain opaque `r23=0`.
The call at `82740B14..24` forwards `r6=r22`, `r5=r21`, `r4=r23` and the packet
to `827400F8`, returning at `82740B28`. These are Boolean selectors, including
the previously rejected `1/1` and `0/1` combinations.

The original fallback saves the packet and incoming `r4/r6`, but never reads or
saves incoming `r5`. At `82740120`, instruction `80BF0004` replaces it with
the packet's object pointer before its first use. Admitting this flag does not
change the original selected technique or drawing behavior. Image-verified
disassembly is retained in `build/mission-completion-crash/original-fallback.txt`
and `original-fallback-caller.txt`; the original manual log is copied there as
`manual-crash.log`.

The existing dual-alpha profile already selects VS `8202B884`, PS `8202C3BC`,
context `2C30`, the original base texture at stage0 and six sampler rows. Its
executable shader bodies match the independently qualified textured-alpha pair
VS `8201739C` / PS `82017E4C`. The repair changes the argument guard rather than
adding a shader or changing original assets.

## Direct launcher

Normal and completion launches now dispatch the game immediately. The launcher
has no startup window, Play button or polling loop. It keeps the same process
creation, inherited output log, private completion profile/content/video copy,
source reparse rejection and long-path handling. It closes its own handles and
exits after starting the game; errors still show a message with the log path.

The completion route still loads Land of Chocolate and calls the original
completion handler. The game retains its outro/results flow. Main campaign
progress and preferences remain separate from the private completion run.

Use **Play First Mission - Completion.lnk** for direct completion startup and
**Play The Simpsons Game.lnk** for the ordinary route. These native Windows
shortcuts also avoid the brief console flash of the retained `.cmd` wrappers.

## Verification

Both native and native-release game, recorder and direct launcher builds pass.
All 11 focused CTest groups pass in both builds. Native results are in
`build/mission-completion-crash/native-existing-tests.log`,
`shared-alpha-tests.log` and `final-native-dual-tests.log`; the release results
are in `final-release-tests.log` (10.57 seconds).

`OriginalRigidDualPass` passes 230 checks. It constructs the actual original
catalog's ordinary dual material, loads unchanged original ITXD textures, and
executes `8273B4D0 -> 82740680 -> 827400F8`. Only packet metadata and its alpha
eligibility byte vary; the fixture never supplies fallback argument registers.
All four Boolean pairs complete. Alpha `1/0` and the crashing `1/1` produce
identical pixels, as do opaque `0/1` and `0/0`. The fixture verifies material
callbacks, actual texture/register mappings, dirty clearing, immutable shader
and geometry bytes, bounds and complete nonvolatile integer/floating-point ABI.
Each bounded draw starts with a neutral outer-pass state; the fixture does not
claim to reproduce the complete scene scheduler.

The 210 packet-owner checks cover the ordinary source's Boolean admission,
rejection of non-Boolean values, canonical owner and exact alpha profile.
Additional checks cover original UV dispatch, WARP/hardware rigid mesh and alpha
shader numerics, original alpha shader identity, original completion dispatch,
direct launcher file/command/private-copy behavior and profile CLI rejection.
AOT verification passes 311 files with zero semantic diagnostics. Primary
profile, save index and video settings match their pre-change hashes.

No automated gameplay was performed. The live transition still needs the user's
manual retest using the rebuilt shortcut.
