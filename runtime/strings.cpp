#include "runtime.h"

// Counted ANSI descriptor: BE16 Length, BE16 MaximumLength, BE32 borrowed
// Buffer. Original 82B750BC calls this import, then lhz reads Length at SP+68.
// Windows RtlInitAnsiString defines saturation at 65534/65535 and no return.
PPC_FUNC(__imp__RtlInitAnsiString) {
    if(!Simpsons::active || base!=Simpsons::active->base)
        throw Simpsons::Failure("Invalid ANSI string initialization runtime");
    Simpsons::active->checkRunning();
    const uint32_t destination=ctx.r3.u32,source=ctx.r4.u32;
    if(!destination) throw Simpsons::Failure("ANSI string descriptor is null");
    PPCGuestPointer(base,destination,8,true);
    uint32_t length=0;
    if(source) {
        // Once the representable length saturates, later source bytes cannot
        // change the descriptor. Never wrap the original 32-bit address space.
        while(length<65534) {
            const uint64_t address=uint64_t(source)+length;
            if(address>UINT32_MAX) throw Simpsons::Failure("ANSI source address overflow");
            if(!PPC_LOAD_U8(uint32_t(address))) break;
            ++length;
        }
    }
    // Complete preflight/counting before publication; source is borrowed and
    // is neither copied, transcoded nor freed. Void ABI preserves incoming r3.
    PPC_STORE_U16(destination,uint16_t(length));
    PPC_STORE_U16(destination+2,uint16_t(source?length+1:0));
    PPC_STORE_U32(destination+4,source);
}
