# Recovered switch selectors use their original word width

Actual build139 boots081/082 reached original8284BD30 with r3=FFFFFFFF.
At8284BD0C the original function loads that word, adds1 using the full GPR,
compares the low word with127 using CMPLWI, computes a word-scaled table
address using RLWINM, loads the branch target and sets CTR. The full selector
is100000000 while its bounded word index is0. Original table8284BD34[0]
contains8284C520, whose original return value is287. No profile state change
or fabricated parser token is needed to reach that case.

The offline generator previously emitted a C++ switch over `.u64`, rejecting
this valid state after the original low-word bound had accepted it. The fix in
`third_party/XenonRecomp/XenonRecomp/recompiler.cpp` selects `.u32`. Full original
GPR arithmetic, the bound, table load, CTR assignment and case bodies remain
generated and executed. Unexpected low-word selectors still fail explicitly.

This width follows the existing recovery contract: `XenonAnalyse/main.cpp`
`ScanTable` obtains the dense selector only from CMPLWI. The three authored
sparse overrides select byte/halfword offsets already loaded into r0; their
configured values are uint32. This change does not reinterpret arbitrary
unrecovered indirect branches or make an unknown table executable.

The single-instruction emitter regression in `tests/test_generator_memory.py`
compiles and executes dense/sparse switches in context-register and local-register
modes with zero, one, DEADBEEF and FFFFFFFF upper words. Valid low-word cases
must branch without changing the complete selector, and invalid low words must
still trap. The focused regression passed after correcting its local-register
fixture declaration; the initial fixture compile error is retained in the
build log and is not claimed as an executed pre-fix test.

`tests/test_native_word_switch.cpp` pins the ten original dispatch instructions,
the actual table entry and return body, then invokes original8284BCF8 through
the real AOT mapping with its caller-owned FFFFFFFF input. It checks return287,
retained r11=100000000, actual CTR=8284C520, original frame/nonvolatile restoration
and unchanged input/output bytes. This is an executable regression for the
observed failure, not an interpreter or replacement of the original parser.

Full build and actual boot results are recorded in `STATUS.md` after execution.
No original game/reference files or generated translation files were edited.
