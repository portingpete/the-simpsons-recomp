#pragma once
#include "runtime.h"

namespace Simpsons {
// Checked ABI frame for native-to-original CPU service calls. All memory effects
// use the original address space; call-clobbered registers stay in this context.
class EngineCpuCalls {
    PPCContext call;
    PPCContext* previous{};
    uint8_t* base;
public:
    EngineCpuCalls(const PPCContext& incoming,uint8_t* memory):call(incoming),base(memory) {
        if(!active || memory!=active->base) throw Failure("Invalid native engine callback runtime");
        uint32_t old=call.r1.u32;
        if(old<0x100 || (old&15)) throw Failure("Invalid native engine callback stack");
        active->pointer(old-0x100,0x100,true);
        call.r1.u32=old-0x100;
        PPC_STORE_U32(call.r1.u32,old);
        PPC_STORE_U32(old-8,uint32_t(incoming.lr));
        previous=currentContext;
        currentContext=&call;
    }
    ~EngineCpuCalls() {currentContext=previous;}
    EngineCpuCalls(const EngineCpuCalls&)=delete;
    EngineCpuCalls& operator=(const EngineCpuCalls&)=delete;
    PPCContext& registers() {return call;}
    uint32_t invoke(uint32_t address) {
        PPCGuestFloatingPointScope floatingPoint(call.fpscr);
        try {PPCSafeIndirect(call,base,address);}
        catch(...) {unwindAudioReaderCall(call);throw;}
        return call.r3.u32;
    }
    uint32_t invoke(uint32_t address,uint32_t a) {call.r3.u32=a;return invoke(address);}
    uint32_t invoke(uint32_t address,uint32_t a,uint32_t b) {call.r4.u32=b;return invoke(address,a);}
    uint32_t invoke(uint32_t address,uint32_t a,uint32_t b,uint32_t c) {call.r5.u32=c;return invoke(address,a,b);}
    uint32_t invoke(uint32_t address,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {call.r6.u32=d;return invoke(address,a,b,c);}
    uint32_t invoke(uint32_t address,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e) {call.r7.u32=e;return invoke(address,a,b,c,d);}
};
}
