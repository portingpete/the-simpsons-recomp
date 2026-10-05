#pragma once
#include <cstdint>
// Forced into every generated chunk BEFORE ppc_context.h (see CMakeLists.txt,
// SIMPSONS_FAST_GUEST_MEMORY). The recompiled original code reaches guest memory
// through the out-of-line PPCGuestPointerFast (runtime/runtime.cpp), which relies
// on the page protection of the reserved guest window for mapping and permission
// faults instead of a per-access permission-table lookup. Keeping the access an
// out-of-line call matters: inlining the sequence at ~470,000 sites grows the
// executable from 85 MB to 125-160 MB and runs slower than the call.
// Native hooks and tests never see this definition and keep PPCGuestPointer.
#define PPCGuestPointer PPCGuestPointerFast
// Scalar accesses use direction-specific leaf functions (no write test, no frame).
uint8_t* PPCGuestPointerRead(uint8_t* base, uint32_t address, unsigned width);
uint8_t* PPCGuestPointerWrite(uint8_t* base, uint32_t address, unsigned width);
#define PPC_GUEST_READ(b, a, w) PPCGuestPointerRead((b), (a), (w))
#define PPC_GUEST_WRITE(b, a, w) PPCGuestPointerWrite((b), (a), (w))
