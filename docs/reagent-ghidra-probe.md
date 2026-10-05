# ReAgent / Ghidra bounded trial

ReAgent 0.4.0 and ghidra-ai-bridge 0.2.0 work locally for Ghidra evidence queries,
target planning and packet export. This trial is useful for renderer analysis,
but it has **not** run or accepted an autonomous replacement C++ candidate.
At this initial trial, the native executable remained at boot038's explicit `82723D80` state boundary;
no new game pixels or gameplay are claimed.

Later fixed-function trial: `docs/native-im2d-shaders.md` records the bounded
ten-function export used after native loading artwork and Im2D vertex uploads
were implemented. The initial-trial limitations below are historical evidence.

## Installation and scope

- Isolated Python environment: `build/reagent-venv` (Python 3.14, PyGhidra 2.2.1,
  JPype 1.7.1). Installation log: `build/reagent-probe/install.log`.
- Existing Ghidra: `K:/Ghidra/build/dist/ghidra_12.2_DEV`, version 12.2,
  revision `8e0fc9bc643a924f90b1e5597f3315bf833f7777`.
- Existing Java: Eclipse Adoptium 21.0.10.7, under Program Files.
- Isolated project, cache, settings and temporary directories under
  `build/reagent-probe`. Original files, Ghidra installation and reference
  projects were not modified. Analysis databases and Python dependencies are
  development artifacts, not part of the shipped port.
- Ghidra language: `PowerPC:BE:64:64-32addr`, default compiler model, four-byte
  pointers. This is a generic PowerPC model, not a verified Xenon ABI.
- Input: the pinned 15,466,496-byte `analysis/simpsons.pe`, SHA256
  `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
  It is a **flat memory image**. BinaryLoader maps file offset zero at
  `82000000`; an ordinary disk-layout PE import would be incorrect.

The selected original `.pdata` bodies are `823EDF20`, `823EE1A8`, `823EE6C8`,
`82723C80`, `82723D80` and `82756480`. Whole-program autoanalysis is disabled.
Every one of their **763 instruction words** matched original bytes and the
project's independent offline decoder. All **75 direct BL targets** matched
Ghidra's branch destinations; **seven bctrl sites** remain indirect.
All six functions exported decompiled C, normalized high P-code and CFG data
without reported IR extraction errors. This does not prove semantic equivalence.

## A useful correction, and an evidence-coverage defect

The baseline is preserved at `analysis/reagent/ppc64-32addr-01`.
Its decompiler incorrectly makes the state owner the inferred return value of
`func_0x82a3c3c8()`. The original `82723D98` instruction copies incoming r3 to
r30. The helper at `82A3C3C8` only stores r28-r31 and r12, then returns; it
does not write r3. The baseline's invented return value is not a valid port.

The corrected experiment is `analysis/reagent/ppc64-32addr-inline-02`.
`tools/ghidra_probe.py` pins the helper's complete 24 bytes, defines its actual
Ghidra body, and marks it inline **in the analysis database**. No instruction
bytes are patched. Ghidra then correctly uses incoming `param_1` for the state
owner, preserving these observed operations:

1. Read the cache index from `82D6D498 + 4*selector` and compare the requested
   value with owner cache `(index + 1A5)*4`; the low byte of force overrides
   the equal-value fast path.
2. Perform the indirect setter at
   `BE32[82D6D890] + BE32[82150580 + 4*selector] + 40`.
3. For positive signed owner depth at `+D28`, update a dirty bit in the frame at
   `owner + depth*694 + 698`, according to its comparison with the saved value.
4. Publish the owner cache value after the setter.

The setter's **SDK/native rendering meaning is still unresolved**. The scalar
and sampler dispatchers cannot safely become cache-only success stubs.
Other helpers, tail exits, pointer signedness, argument widths and call effects
still need original-instruction review. Inlining this one helper does not
validate the other five pseudocode bodies or general VMX128 semantics.

The bridge exports cross-references only to functions defined in the bounded
Ghidra database. Its raw baseline reported zero callees for this state setter,
although the assembly contains a direct save-helper call and an indirect call.
ReAgent's baseline plan consequently reported **zero evidence gaps**. That
means no gaps were supplied, not that analysis was complete.

The probe now preserves raw `exports` and separately writes `annotated-exports`.
Annotations restore direct targets from already-verified BL words and describe
bounded caller coverage, unverified ABI, absent bodies, indirect calls and
external tail exits. No indirect target is guessed. The corrected one-function
ReAgent plan contains **six explicit gaps**, including selection limits. See
`export-audit.json`, `target-manifest.json`, and `packets/gaps.tsv`.

## Actual ReAgent checks

These commands ran successfully: Ghidra Bridge `decompile` and `context`,
ReAgent `plan`, `evidence`, and `reverse --dry-run`.

`doctor --address 0x82723D80` confirms the source directory, local CLI
executable, decompile capability and target evidence. It correctly reports
`ready: false` because candidate-specific trusted build/differential gates
have not been configured. The default verified-acceptance requirement remains
enabled. Running this project's existing tests without substituting and
exercising the generated candidate would not constitute such a gate.

The installed Codex CLI reports an existing ChatGPT login outside the sandbox.
The sandbox-only authentication check had reported no login. No credentials
were copied and no API key was requested. **No nested model calls were made**;
the configured model provider is not a tested ReAgent reversal run. Checker,
objective, candidate validation and parity results are therefore **not run**.
The profile caps a future experiment at one function, one round and two model
calls; candidate acceptance needs an actual consuming validation harness first.

## Reproduction

From `K:/SimpsonsNativeCopy` in PowerShell, with the existing isolated environment:

```powershell
& .\build\reagent-venv\Scripts\python.exe .\tools\ghidra_probe.py `
  --ghidra K:\Ghidra\build\dist\ghidra_12.2_DEV `
  --java 'C:\Program Files\Eclipse Adoptium\jdk-21.0.10.7-hotspot' `
  --tag ppc64-32addr-inline-02 --inline-save-helper

& .\build\reagent-venv\Scripts\ghidra-bridge.exe `
  --config config\ghidra-bridge-probe.yaml decompile 0x82723D80

& .\build\reagent-venv\Scripts\re-agent.exe `
  --config config\reagent-probe.yaml doctor --address 0x82723D80

& .\build\reagent-venv\Scripts\re-agent.exe `
  --config config\reagent-probe.yaml plan --address 0x82723D80 `
  --max-depth 0 --max-functions 1 `
  --output build\reagent-probe\target-manifest-recheck.json

& .\build\reagent-venv\Scripts\re-agent.exe `
  --config config\reagent-probe.yaml evidence `
  --manifest build\reagent-probe\target-manifest-recheck.json `
  --output build\reagent-probe\packets-recheck
```

The evidence output must be a new or empty directory. The doctor exit code is
currently 1 for the acceptance-policy finding described above. Preserve that
finding until real candidate validation exists. No native rebuild is needed
for this offline analysis-only change.

Upstream references: [ReAgent](https://github.com/Dryxio/reagent),
[Ghidra Bridge](https://github.com/Dryxio/ghidra-bridge). The installed source
and the local trial, rather than upstream capability claims alone, establish
the results above.
