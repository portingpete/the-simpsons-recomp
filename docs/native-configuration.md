# Native Windows language query at the verified original wrapper

## Save timestamp time zone (2026-09-13)

Replay009 first returned the real save file's directory metadata, then exposed
the platform timezone query while displaying the save date. Original82CB7A60
calls82432A10, uses its0/1/2 classification to select Bias, StandardBias or
DaylightBias, and converts the signed minute offset into100ns units. Original
82432A10 delegates to824349F8, which reads console configuration3/settings1..7
and12 and fills the172-byte TIME_ZONE_INFORMATION layout.

The native port now adapts the public82432A10 boundary to Windows
[GetTimeZoneInformation](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation).
Actual host names, bias, transition dates and current classification are packed
into the verified original big-endian layout. The original bias/timestamp and
game-side formatting consumers remain AOT. The generic ExGetXConfigSetting
import remains unsupported; no console configuration store is created.

NativeConfiguration now checks every field against a fresh native query, exact
output extent, full CPU/FP/host-error preservation, invalid pointers/runtime,
and the original82CB7A60 signed bias consumer. It passes in0.11 seconds after
the normal strict build,311-file/zero-diagnostic regeneration. Evidence logs:
build/native-save-timezone-regenerate.log, build/native-save-timezone-build.log,
build/native-save-timezone-tests.log. Live replay010 passed the timezone query
and reached original824393C8's calendar conversion, exposing the separately
missing RtlTimeToTimeFields import at824393FC.

That import now calls the real native Ntdll RtlTimeToTimeFields, translating the
absolute100ns input and eight16-bit output fields without changing the CPU
context or host FP/last-error state. The original824393C8 still rearranges the
fields into its SYSTEMTIME result. Its tests cover1601,1970,2000,2026,2100 and
2400, century leap-day behavior, weekday, millisecond truncation, exact output
extent and invalid buffers. NativeConfiguration passes0.09 seconds after strict
regeneration/build (239 explicit unsupported imports, zero diagnostics).
Logs: build/native-save-calendar-{regenerate,build,tests}.log. The full build
including both date additions passes133/133 tests in94.69 seconds, recorded in
build/native-save-calendar-full-build-tests.log. Live replay011 checks loading.
Reference: [RtlTimeToTimeFields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtltimetotimefields).

**Setting category3/setting9 is the original language query.** The bounded
native adaptation replaces **82432D10** with a real Windows user-UI-language
query and a byte-verified original-language enumeration. It does not implement
`ExGetXConfigSetting`, manufacture a console configuration store, supply region
or country settings, or write console configuration globals. Original game
locale selection and missing-translation fallback remain AOT.

Production, test and evidence files are frozen after **build136: all39 CTest
suites passed in53.09 seconds**. NativeConfiguration passed **65,681 checks**;
actual boot079 passed language selection and reached the separate
`XamUserGetSigninState` boundary. No profile implementation is included here.

## Integration and ABI

Add **runtime/native_configuration.cpp** to the Runtime library. The entry
hook is **82432D10**, `return=true`, name **SimpsonsNativeGetLanguage**. Its
exact original 48-byte pin is:

```
7d8802a69181fff89421ffa03960000038e1005038c0000438a100543880000938600003b16100504888fbad2c030000
```

The plain C++ declaration is
`void SimpsonsNativeGetLanguage(PPCContext& ctx, uint8_t* base)`.
Do not use `PPC_FUNC`/a restricted-reference declaration for this midasm hook;
MSVC decorates that reference differently. There are no input arguments,
borrowed pointers, result buffers, allocations, or guest-memory accesses.
Only **r3 changes**, receiving a zero-extended language ID in 1..12. Guest
SP/LR/registers/FPSCR are retained, and host MXCSR controls/status are restored
around the Windows call. The helper does not change Windows UI language,
locale, keyboard settings, or process/thread locale.

The test target is **NativeConfigurationTests**, source
**tests/test_native_configuration.cpp**, linked with Runtime, `/fp:strict`.
Invoke with the original flat image path as its sole argument; the exact USA
locale table is read beside that image's project root. Main owns CMake/config
integration. No runtime header or existing runtime implementation was edited.

## Original wrapper and reached consumer

Authority: original flat `analysis/simpsons.pe`, base **82000000**, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Original `.pdata` gives **82432D10 size90**. Boot078/build135 reached its
**82432D38 BL**, returning to **82432D3C**, with:

- r3=3, r4=9;
- r5=0203F6B4, a four-byte value output; r6=4;
- r7=0203F6B0, the size output whose original stack initialization is **sth**,
  two bytes, at **82432D34**.

The import thunk **82CC28E4** identifies xboxkrnl ordinal **10**,
`ExGetXConfigSetting`. Negative status makes the wrapper use language0;
otherwise **82432D50** loads the returned BE32 value. Values **1..12** return
unchanged. Zero or values above12 cause the original **XGetGameRegion** query
through **82CC2714**: region0101 returns language2; other region01xx returns7;
all other regions return1. This native wrapper replaces that console-specific
configuration/region fallback with the explicit Windows policy below. No
unverified raw-import buffer/status contract is exposed as a native service.

Actual startup caller **8282AEFC**, return **8282AF00**, lies in **8282AEE8**.
It is called by the locale-owner constructor at **8282B224**, returning to
**8282B228**, consistent with the boot backchain to **823B7800**. This is a
language selection call, not an audio gain, time-zone or country request.

The checked direct-branch scan of original `.text` finds five calls to this
wrapper:

- **822587EC**: original language-to-name function **822587E0**.
- **822696B4**: passes the result as r4 to **8226A750**, then retains its
  original subsequent request path. No extra query is implemented there.
- **827ADDC8** and **8286DBB4**: original switches select embedded localized
  branches for IDs3..6, with their existing default paths for other IDs.
- **8282AEFC**: the reached locale selector described below.

This is a direct-call inventory, not an exhaustive proof against indirect
aliases. The hook returns the original enumeration to every caller and leaves
their own control flow intact.

## Original enum proof and Windows policy

**822587E0**, `.pdata` size11C, subtracts1, bounds the result to0..11, reads
the byte table at **821D3C20**, then dispatches to literal-name branches based
at **82258824**. The table bytes are
`00102030405060709080a0b0`. Original literal bytes prove:

- 1 English, 2 Japanese, 3 German, 4 French, 5 Spanish, 6 Italian;
- 7 Korean, 8 T.Chinese, **9 Portuguese, 10 S.Chinese**, 11 Polish, 12 Russian.

In particular, simplified Chinese is **10 for this original image**. A later
console SDK or a reference project's different enum is not authoritative.
The verifier checks every branch's address-forming instructions and every
NUL-terminated name against the original image.

The native query uses **GetUserDefaultUILanguage()**, which returns the current
user's UI-language identifier and has Windows-defined fallback to the system's
preferred/install UI language. This selects display-language preference rather
than regional formatting or keyboard layout. Microsoft notes that a language
interface pack using a supplemental locale may return a custom UI identifier.
[Microsoft query contract](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getuserdefaultuilanguage).

The mapping uses the installed official Microsoft SDK **winnt.h**, version
**10.0.26100.0**, checked read-only and hashed in the evidence report. It maps
the primary identifiers for English/Japanese/German/French/Spanish/Italian/
Korean/Portuguese/Polish/Russian to the corresponding original IDs, regardless
of regional variant. For Chinese, the SDK's five concrete regional variants
map as follows:

- zh-TW 0404, zh-HK 0C04, zh-MO 1404 → original T.Chinese8.
- zh-CN 0804, zh-SG 1004 → original S.Chinese10.

The SDK macros use the low ten bits for `PRIMARYLANGID` and the upper six for
`SUBLANGID`. Both macros and the sixteen used primary/sublanguage constants
are independently checked by the verifier. Microsoft documents primary and
sublanguage identifiers as the components of a Windows LANGID.
[Microsoft language identifiers](https://learn.microsoft.com/en-us/windows/win32/intl/language-identifiers).

**Explicit PC adaptation:** unrepresented languages and custom/unknown IDs,
including unqualified Chinese neutral/custom variants, return original
English1. There is no fabricated Japanese/Korean region query, no claim that
Windows country equals the original disc's region, and no OS settings change.
The mapping helper's output is bounded even for arbitrary 16-bit input; this
does not classify every arbitrary input as a valid Windows locale. A richer
language-name/LIP preference policy may be added separately if needed.

## Actual original assets and retained fallback

The original **text/localetable.txt** is exactly **77 bytes**, SHA256
`87c577f2c11f86e985696e5f422c19ea7c680cc2a410788d4d2b834be2c97557`.
Its first `5` is a **format version**, not a five-language inventory.
**8282C870** accepts versions4/5; version5 uses five header tokens, then
five-token records. The actual version5 sample contains these two records:

```
en  en  English    1  (none)
ss  en  StringIDs  0  (none)
```

Header tokens are `5`, `EA205701A0X11`, `ntsc_en`, `0`, `0`. The original
constructor computes the record count from token count/header size, creates
20-byte records, and copies each first-token locale code to record+0. The
source is read without changing newline bytes or creating translations.

**8282AEE8** maps only original IDs **1→en, 3→de, 4→fr, 5→es, 6→it** into
an original **8282C750** lookup. ID2 and IDs outside1..6 leave the current
locale unchanged. The locale owner initializes its selection **O+34=0 at
8282B0A8** before this query.

**8282C750** compares each record's first string exactly, advances by20 bytes,
and returns its index on a match. On no match it explicitly returns **0 at
8282C7AC**. Thus this exact USA table falls back to **record0, English**, for
de/fr/es/it requests; other original IDs retain the initial index0. The
application method **8282ADD8** rejects out-of-range indices and does nothing
when the chosen index already equals O+34. Both this lookup and application
of the selection remain original AOT. The native language helper does not
claim missing French/German/etc. assets exist or force a game locale field.

Returning the real mapped Windows preference is useful to other original
consumers with embedded language strings, while the shipped asset table still
determines actual available game localization.

## Reproducible evidence and test scope

Run `python -B build/native-configuration/verify.py`. It verifies **11 spans,
570 original words, 34 explicit pins, twelve language names, five direct
callers, two actual locale records, sixteen Microsoft SDK constants**, and
**eight malformed-input rejections**. Outputs are deterministic
**build/native-configuration/evidence.json** and **disassembly.txt**. The JSON
also includes all144 bytes of the original wrapper, the48-byte hook pin,
original/ref/header hashes and evidence qualifiers.

The C++ test covers:

- 29 explicit Windows LANGID examples and five additional custom/neutral
  cases; all65,536 possible16-bit inputs remain within the original1..12 bound.
- Real `GetUserDefaultUILanguage` versus the native result; whole-context
  equality except r3, inaccessible/null guest base, and five MXCSR profiles.
- Actual generated AOT entry **82432D10**, original language-name consumer
  **822587E0**, and original **8282C750** search over an exact two-code fixture
  grounded in the read-only77-byte original table. The lookup tests missing
  codes, English index0 and StringIDs index1; it does not replace the parser or
  claim full locale-owner construction was executed.
- Continued explicit failure of direct **ExGetXConfigSetting**, including
  category3/setting9, with unchanged mapped value/length sentinels. No general
  configuration import becomes available as a side effect of this hook.

The production files changed in this scope are only new
**runtime/native_configuration.cpp**, **tests/test_native_configuration.cpp**,
this document, and evidence under **build/native-configuration**. Main owns
the source-list, hook and CTest additions. Original images/assets, references,
other runtime files, generated files, and existing tests are untouched here.

## Final verification

ClangCL syntax checks with `/fp:strict /W4 /WX` passed for both sources before
freeze. Main build136 compiled/linked the actual generated hook and passed
**all39 suites in53.09 seconds**. **NativeConfiguration:65,681 checks passed**,
including the native Windows query, original AOT consumers and preserved raw
configuration rejection. The test is read-only with respect to the image and
locale file; mapped RAM records are explicitly fixtures.

Actual **boot079** progressed past language selection to
`XamUserGetSigninState` at **PC/LR823A1254**, wrapper **82431880**, r3=0/r4=0.
Verified main executable SHA256:
`7db75bf6088c70f664307f5fa54cf70454b9a7b8f55e7902b58a115cc2076e9f`.
That next profile boundary remains separate and unsupported; successful
language selection does not imply a signed-in user or working profiles/saves.
