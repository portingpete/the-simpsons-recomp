# Muted Windows output capability

The installed XAudio2.9 engine processed two owned synthetic PCM buffers through
a real source/mastering graph. Both completion callbacks arrived in submission
order, with the original host context pointers; the queue then reported empty.
The mastering voice reported two channels at48kHz. Master and source volumes
were zero throughout processing. No sound was played to the user.

This probe establishes native output availability and buffer/callback lifetime,
not original audio decoding, console DSP/mixing equivalence or game integration.
The current game executable still fails explicitly at XMACreateContext in boot068.
The input is two4,800-frame stereo float32 synthetic buffers at48kHz. It is never
presented as original game audio. At the final end-of-stream marker, SamplesPlayed
was zero, consistent with the documented stream reset; the evidence of consumption
is the ordered OnBufferEnd callbacks and drained queue.

The later probe revision also accepts explicitly declared mono float32 48kHz PCM.
Both original clips decoded by `tools/probe_xma_codec.py` passed this native
submission/completion path: 8,064 frames and 54,901 frames. It splits each buffer
into two contiguous owned pieces without altering samples and verifies ordered
completion of both. Input format is explicit; no metadata is guessed from raw
bytes. Logs are xaudio2_short.log and xaudio2_documented.log in build/audio-probe.
The mastering graph's default mono routing is an output capability probe, not
the game's recovered panning or speaker matrix. No loop/seek/DSP equivalence or
audible original output has been established.

`tools/probe_native_audio_output.cpp` preserves the current authored source. The
first synthetic-only version remains at build/audio-probe/xaudio2_probe.cpp. Rebuild
from the workspace in PowerShell:

```powershell
.\tools\build.ps1 -GeneratorOnly -Jobs 8
& 'C:\Program Files\LLVM\bin\clang-cl.exe' /nologo /std:c++20 /EHsc /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 tools\probe_native_audio_output.cpp /Febuild\audio-probe\xaudio2_probe.exe /Fobuild\audio-probe\xaudio2_probe.obj /link xaudio2.lib ole32.lib
.\build\audio-probe\xaudio2_probe.exe
.\build\audio-probe\xaudio2_probe.exe --mono-f32-48000 build\audio-probe\short.f32le
.\build\audio-probe\xaudio2_probe.exe --mono-f32-48000 build\audio-probe\documented.f32le
```

Initial logs are build/audio-probe/xaudio2_compile.log and xaudio2_probe.log;
the revised build is recorded by xaudio2_pcm_compile.log and xaudio2_synthetic.log.
The Windows SDK supplies xaudio2.h and xaudio2.lib; the library resolves the
system xaudio2_9.dll on this Windows target. There is no new package installation.

Microsoft documents that submitted PCM must remain alive until
[OnBufferEnd](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-submitsourcebuffer).
This probe retains both vectors and callback contexts for the graph's lifetime.
It destroys the source voice before the mastering voice and engine.
[DestroyVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-destroyvoice)
quiesces data reads and callbacks before their storage is freed, including on
failure. The [voice-state contract](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/ns-xaudio2-xaudio2_voice_state)
explains the buffer count and sample counter. The completion wait is bounded
to two seconds; failure reports an error instead of claiming sound was consumed.
