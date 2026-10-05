# Skippable videos

Press **Enter** in the game window or **Start** on a controller to skip a video.
The automatic launcher sends Start for each launch video and the opening
cutscene. A fresh press is required after a neutral input poll; holding Start
cannot skip successive videos or press Start in the next menu.

The shared original player is the object stored at `82D09750`. Three
signature-checked hooks in `config/simpsons.toml` provide the native skip policy:

- `826B926C`: successful movie start (state 2) begins an input session.
- `826B92F0`: before the original frame update, consume a pending Start and
  invoke original stop `826B9290`, then completion `826B8AD8`.
- `8282D998`: original decoder shutdown ends the input session on every stop,
  including natural completion and failed startup cleanup.

The skip follows the same stop-and-complete pair as original `826B9790–826B9798`.
It waits for the real worker exit through `82373738 → 82375C88`, destroys the
decoder normally, and dispatches the original completion event. It neither
fabricates end-of-file/thread status nor modifies saved progress. Explicit stop
also avoids restarting a movie whose natural-completion policy requests a loop.

`NativeControllers::state` observes the ordinary game polls without sampling
the input source a second time. It retains quick taps for the movie update,
consumes Start while leaving other buttons intact, and requires release before
Start can act on the next screen. Native storage dialogs own their own inputs;
opening one clears pending movie input. The shared controller tests cover
keyboard taps, recorded commands, all four physical slots, held inputs across
movies, wrong-owner requests, ordinary menu input, and modal input isolation.

Final live verification in `build/automatic-startup/movie-skip-002` skipped
all three launch videos and the opening cutscene. Each of the four decoder
threads logged a real zero exit status between its Start acceptance and
completion message. The launch Start receipts were at 1.63, 1.96 and 2.28 seconds;
the main menu appeared at 22.76 seconds. The opening movie began at 24.02 seconds
and its skip completed at 24.20 seconds. The input recording is `inputs.jsonl`.
Automatic startup waits for an explicit neutral-input event so the first
Start cannot arrive before the movie is ready to accept it.

The final normal build passed all 133 tests in 94.77 seconds; results are in
`build/native-movie-skip-full-build-tests.log`. AOT verification accepted all
311 generated files with zero semantic diagnostics. Profile, save data, save
index, achievement data and the original image retain their previous hashes.

After the opening skip, the run reached the previously observed character
rendering failure (`0090077D`). That is a separate gameplay rendering issue;
skipping a video does not resolve it.
