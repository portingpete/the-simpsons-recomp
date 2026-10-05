#include "runtime.h"
#include <algorithm>

// Retail CRT type-zero jump buffer: f14..31, SP, r13..31, CR, LR,
// discriminator. No VMX/FPSCR/XER/CTR save is present in this original routine.
struct PPCNonlocalFrame::State {
    struct Record {
        uint32_t buffer,continuation;
        uint64_t stack;
        std::array<uint8_t,0x13C> bytes;
    };
    PPCNonlocalFrame* owner;
    PPCContext& ctx;
    uint8_t* base;
    State* previous;
    std::vector<Record> records;
    static thread_local State* top;
    static std::pair<State*,Record*> find(PPCContext& ctx,uint8_t* base,uint32_t buffer) {
        for(auto* s=top;s;s=s->previous) {
            if(&s->ctx!=&ctx || s->base!=base)continue;
            for(auto& r:s->records)if(r.buffer==buffer)return {s,&r};
        }
        throw Simpsons::Failure("Nonlocal jump has no live save in this native thread and CPU context");
    }
    static void unchanged(const Record& r,uint8_t* base) {
        if(std::memcmp(PPCGuestPointer(base,r.buffer,0x13C,false),r.bytes.data(),r.bytes.size()))
            throw Simpsons::Failure("Nonlocal jump buffer changed after the original save");
    }
};
thread_local PPCNonlocalFrame::State* PPCNonlocalFrame::State::top{};

PPCNonlocalFrame::PPCNonlocalFrame(PPCContext& ctx,uint8_t* base):state(nullptr) {
    if(!Simpsons::active || Simpsons::active->base!=base)
        throw Simpsons::Failure("Nonlocal frame belongs to an invalid runtime");
    state=new State{this,ctx,base,State::top,{}};State::top=state;
}
PPCNonlocalFrame::~PPCNonlocalFrame() {
    // Every path uses native C++ unwinding, including transfer past a nested
    // registered frame. A native longjmp would bypass these destructors.
    if(State::top!=state)std::terminate();
    State::top=state->previous;delete state;
}
void PPCNonlocalFrame::save(uint32_t continuation) {
    auto& ctx=state->ctx;auto* base=state->base;const uint32_t buffer=ctx.r3.u32;
    if(continuation<4)
        throw Simpsons::Failure("Unsupported nonlocal save continuation wraps below zero");
    const uint32_t call=continuation-4;
    const uint32_t expected=0x48000001u|((0x82A43E50u-call)&0x03FFFFFCu);
    if(State::top!=state || ctx.lr!=continuation || PPC_LOAD_U32(call)!=expected ||
       (buffer&7) || PPC_LOAD_U32(0x82E3E26C)!=0)
        throw Simpsons::Failure("Unsupported nonlocal save call, alignment, or extended CRT mode");
    PPCGuestPointer(base,buffer,0x13C,true);
    const auto stack=ctx.r1.u64;
    PPCSafeIndirect(ctx,base,0x82A43E50);
    if(ctx.r3.u64 || ctx.r1.u64!=stack || ctx.lr!=continuation ||
       PPC_LOAD_U64(buffer+0x90)!=stack || PPC_LOAD_U32(buffer+0x134)!=continuation || PPC_LOAD_U32(buffer+0x138))
        throw Simpsons::Failure("Original nonlocal save did not produce its verified type-zero buffer");
    State::Record record{buffer,continuation,stack,{}};
    std::memcpy(record.bytes.data(),PPCGuestPointer(base,buffer,0x13C,false),record.bytes.size());
    auto it=std::find_if(state->records.begin(),state->records.end(),[&](const auto& r){return r.buffer==buffer;});
    if(it==state->records.end())state->records.push_back(record);else *it=record;
}
void PPCNonlocalFrame::validateJump(PPCContext& ctx,uint8_t* base) {
    auto [s,r]=State::find(ctx,base,ctx.r3.u32);(void)s;
    State::unchanged(*r,base);
    if(PPC_LOAD_U32(r->buffer+0x138))throw Simpsons::Failure("Extended nonlocal restore is unsupported");
}
[[noreturn]] void PPCNonlocalFrame::transfer(PPCContext& ctx,uint8_t* base) {
    auto [s,r]=State::find(ctx,base,ctx.r7.u32);State::unchanged(*r,base);
    if(r->buffer>UINT32_MAX-0x138)
        throw Simpsons::Failure("Nonlocal jump buffer address would wrap");
    if(ctx.r1.u64!=r->stack || ctx.lr!=r->continuation || !ctx.r3.u32)
        throw Simpsons::Failure("Original nonlocal restore produced an invalid continuation");
    const std::array gpr{&ctx.r13,&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,
        &ctx.r21,&ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
    const std::array fpr{&ctx.f14,&ctx.f15,&ctx.f16,&ctx.f17,&ctx.f18,&ctx.f19,&ctx.f20,&ctx.f21,
        &ctx.f22,&ctx.f23,&ctx.f24,&ctx.f25,&ctx.f26,&ctx.f27,&ctx.f28,&ctx.f29,&ctx.f30,&ctx.f31};
    for(uint32_t i=0;i<gpr.size();++i)if(gpr[i]->u64!=PPC_LOAD_U64(r->buffer+0x98+8*i))
        throw Simpsons::Failure("Original nonlocal GPR restoration differs");
    for(uint32_t i=0;i<fpr.size();++i)if(fpr[i]->u64!=PPC_LOAD_U64(r->buffer+8*i))
        throw Simpsons::Failure("Original nonlocal FPR restoration differs");
    const std::array cr{ctx.cr0,ctx.cr1,ctx.cr2,ctx.cr3,ctx.cr4,ctx.cr5,ctx.cr6,ctx.cr7};
    uint32_t packed=0;for(const auto& c:cr)packed=(packed<<4)|(c.lt<<3)|(c.gt<<2)|(c.eq<<1)|c.so;
    if(packed!=PPC_LOAD_U32(r->buffer+0x130))throw Simpsons::Failure("Original nonlocal CR restoration differs");
    throw PPCNonlocalTransfer{s->owner,r->buffer,r->continuation};
}
uint32_t PPCNonlocalFrame::resume(const PPCNonlocalTransfer& transfer) {
    if(transfer.frame!=this)throw transfer;
    auto [s,r]=State::find(state->ctx,state->base,transfer.buffer);
    if(s!=state || State::top!=state || transfer.continuation!=r->continuation ||
        state->ctx.lr!=r->continuation || state->ctx.r1.u64!=r->stack)
        throw Simpsons::Failure("Nonlocal transfer reached a mismatched native frame");
    state->ctx.fpscr.setcsr(state->ctx.fpscr.csr);
    return r->continuation;
}
void SimpsonsNonlocalJumpValidate(PPCContext& ctx,uint8_t* base) { PPCNonlocalFrame::validateJump(ctx,base); }
void SimpsonsNonlocalJumpTransfer(PPCContext& ctx,uint8_t* base) { PPCNonlocalFrame::transfer(ctx,base); }
