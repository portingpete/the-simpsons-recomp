#include "runtime/engine_audio_output.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_audio_reader.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/threads.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* message) {++checks;if(!value) throw Failure(message);}
template<class F> void rejected(F&& action,const char* message) {
    bool failed=false;try{action();}catch(const Failure&){failed=true;}need(failed,message);
}
struct StartupObserved {};

// These words come from the original mapped image, independently of the
// production source. The stream stores its initial request token, and preserves
// it when it submits later preload requests. A claimed node carries the token
// of its OWN request. This fixture exercises that reader contract; it does not
// construct a fake stream owner or claim to execute the whole preload handler.
void originalEvidence(uint8_t* base) {
    constexpr std::pair<uint32_t,uint32_t> words[]={
        {0x82342190,0x4804AF71},{0x82342194,0x907F002C},
        {0x8234220C,0x4804AEF5},{0x82342210,0x817F002C},
        {0x82342214,0x2F0B0000},{0x82342218,0x409A0008},
        {0x8234221C,0x907F002C},
        {0x8238BC78,0x356B0100},{0x8238BC8C,0x815E0000},
        {0x8238BC90,0x554A063E},{0x8238BC94,0x7D4B5B78},
        {0x8238D5B8,0x817F0010},{0x8238D5C0,0x3BAB0004},
        {0x8238D5D8,0x813D0000},{0x8238D5E0,0x5529063E},
        {0x8238D5E4,0x1D290138},
    };
    for(const auto& [pc,word]:words)
        need(PPCLoadU32(base,pc)==word,"Original preload/request/claim instruction changed");
}

class RootLock {
    EngineCpuCalls& cpu;uint32_t q;bool held=true;
public:
    RootLock(EngineCpuCalls& calls,uint32_t root):cpu(calls),q(root) {
        need(cpu.invoke(0x823392C8,q)==0,"Original Q callback lock acquisition failed");
    }
    void release() {if(held){need(cpu.invoke(0x823392F0,q)==1,"Original Q callback lock release failed");held=false;}}
    ~RootLock() {if(held){try{release();}catch(...){active->requestStop("Reader claim fixture lock unwind failed");}}}
};

struct PreservedABI {
    uint64_t sp,lr;std::array<uint64_t,18> values{};
    static auto registers(PPCContext& ctx) {
        return std::array{&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                          &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
    }
    explicit PreservedABI(PPCContext& ctx):sp(ctx.r1.u64),lr(ctx.lr) {
        auto regs=registers(ctx);for(size_t i=0;i<regs.size();++i) values[i]=regs[i]->u64;
    }
    void verify(PPCContext& ctx) const {
        need(ctx.r1.u64==sp && ctx.lr==lr,"Original reader call changed SP/LR");
        auto regs=registers(ctx);for(size_t i=0;i<regs.size();++i)
            need(regs[i]->u64==values[i],"Original reader call changed a nonvolatile GPR");
    }
};

// Only fixture input is allocated here. The original constructor owns the
// real G/M/H, entry array, claim nodes, and rings throughout the test.
struct GuestSources {
    Runtime& rt;uint32_t address;
    explicit GuestSources(Runtime& runtime):rt(runtime),address(rt.allocatePhysical(0,4096,PAGE_READWRITE,0,
                                                                std::numeric_limits<uint32_t>::max(),4096)) {
        need(address!=0,"Fixture input allocation failed");
    }
    ~GuestSources() {
        rt.stopThreads(); // On failure, join readers before releasing fixture input.
        try{rt.freePhysical(address);}catch(...){std::fputs("Fixture input release failed\n",stderr);}
    }
};

struct RestoredWord {
    uint8_t* base;uint32_t address,old;
    RestoredWord(uint8_t* memory,uint32_t at,uint32_t value):base(memory),address(at),old(PPCLoadU32(base,address)) {
        PPC_STORE_U32(address,value);
    }
    ~RestoredWord() {PPC_STORE_U32(address,old);}
};

void cleanSnapshot(const std::shared_ptr<EngineAudioReader>& readers,size_t claims) {
    const auto s=readers->snapshot();
    need(s.groups==1 && s.managers==1 && s.claims==claims && !s.copies && !s.operations,
         "Reader copy/rejection stranded provenance or an operation gate");
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    try {
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        originalEvidence(rt.base);const auto entry=original;bool source=false,startup=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*) {
            const auto v=rt.engineAudioOutput->view();
            need(v.nativeEngine && v.muted && v.configured && v.identity,"Real muted native audio source missing");
            if(pc==0x82345920){need(!source && !v.active && !v.workerId,"Unexpected native source construction");source=true;return;}
            need(pc==0x828166FC && source && v.active && v.workerId && v.event,"Unexpected original startup boundary");
            throw StartupObserved{};
        };
        try{runOriginal(original,rt.base);}catch(const StartupObserved&){startup=true;}
        rt.audioBoundaryObserver={};need(startup,"Original startup did not release the real Q lock");
        const auto output=rt.engineAudioOutput->view();const uint32_t q=output.root;
        std::shared_ptr<KernelHandle> worker;
        {std::lock_guard lock(rt.threadMutex);for(const auto& t:rt.threads) if(t->id==output.workerId) worker=t->object;}
        need(worker && GetThreadId(worker->native)==output.workerId && output.workerId!=GetCurrentThreadId(),
             "Original Dac worker missing");
        need(q && PPCLoadU32(rt.base,0x82E31BCC)==q && PPCLoadU32(rt.base,q+0x40)==0x823469B0 &&
             PPCLoadU32(rt.base,q+0x44)==0x823469C0 && PPCLoadU32(rt.base,0x82E36B94),
             "Original reader root/allocator/callback prerequisites missing");
        GuestSources sources(rt);
        constexpr uint32_t bytes=64;
        std::array<std::array<uint8_t,bytes>,2> expected{};
        for(size_t j=0;j<expected.size();++j) {
            for(size_t i=4;i<bytes;++i) expected[j][i]=uint8_t(17*j+3*i);
            expected[j][3]=bytes; // Original callback's big-endian block length.
            std::copy(expected[j].begin(),expected[j].end(),rt.pointer(sources.address+uint32_t(j)*256,bytes,true));
        }
        const uint32_t owner=sources.address+512;
        EngineCpuCalls cpu(entry,rt.base);auto readers=audioReaders(rt,true);
        readers->permitFixtureGroup(cpu.registers(),true);
        std::array<EngineAudioReader::OwnedClaim,2> owned;
        uint32_t g=0,h=0,m=0;
        {
            RootLock lock(cpu,q);const PreservedABI abi(cpu.registers());
            cpu.registers().r8.u32=0;cpu.registers().r9.u32=0;
            g=cpu.invoke(0x8233D5F8,PPCLoadU32(rt.base,0x82D08ACC),1,0x55280,4,q);abi.verify(cpu.registers());
            need(g!=0,"Original stream-profile group constructor failed");
            h=PPCLoadU32(rt.base,g+0x44);m=PPCLoadU32(rt.base,h+4);
            need(PPCLoadU32(rt.base,m+0x4C)==h && PPCLoadU32(rt.base,m+0x3C)==7 &&
                 PPCLoadU32(rt.base,m+0x64)==g+0x50 && PPCLoadU32(rt.base,m+0x6C)==g+0x50+0x55280,
                 "Real original stream reader layout/profile differs");
            cleanSnapshot(readers,0);
            std::array<uint32_t,2> tokens{},nodes{};
            for(size_t j=0;j<tokens.size();++j) {
                tokens[j]=cpu.invoke(0x8238D228,h,sources.address+uint32_t(j)*256,bytes,0x823412C0,owner);
                abi.verify(cpu.registers());need(tokens[j]!=0,"Original memory reader request failed");
            }
            need(tokens[0]!=tokens[1] && (tokens[0]&255u)!=(tokens[1]&255u),
                 "Two original outstanding requests did not receive distinct tokens/entries");
            for(size_t j=0;j<nodes.size();++j) {
                nodes[j]=cpu.invoke(0x8238D568,h);abi.verify(cpu.registers());
                need(nodes[j] && PPCLoadU32(rt.base,nodes[j])==tokens[j],"Original claim did not return its request token");
                const uint32_t e=PPCLoadU32(rt.base,m+0x38)+0x138*(tokens[j]&255u);
                need(PPCLoadU32(rt.base,e)==tokens[j] && PPCLoadU32(rt.base,e+0x128)==0x823412C0 &&
                     PPCLoadU32(rt.base,e+0x12C)==owner,"Original request lost callback/owner association");
            }
            cleanSnapshot(readers,2);
            // This is the exact mismatch from the crash: same H and owner,
            // later actual B, but the retained token of the initial request.
            rejected([&]{readers->copy(h,nodes[1],owner,tokens[0]);},"Retained first token incorrectly admitted a later claim");
            for(size_t j=0;j<owned.size();++j) {
                owned[j]=readers->copy(h,nodes[j],owner,PPCLoadU32(rt.base,nodes[j]));
                need(owned[j].node==nodes[j] && owned[j].token==tokens[j] && owned[j].handle==h && owned[j].manager==m &&
                     owned[j].group==g && owned[j].owner==owner && owned[j].length==bytes && owned[j].bytes.size()==bytes,
                     "Actual claim token did not preserve original provenance");
                need(std::equal(owned[j].bytes.begin(),owned[j].bytes.end(),expected[j].begin()),
                     "Owned claim did not copy the original requested bytes");
                need(owned[j].groupGeneration && owned[j].managerGeneration && owned[j].epoch && owned[j].sequence,
                     "Owned claim has no original generation/sequence");
                readers->validate(owned[j]);
            }
            need(owned[0].sequence!=owned[1].sequence,"Distinct original claims reused a native sequence");
            const auto copyLater=[&]{readers->copy(h,nodes[1],owner,tokens[1]);};
            rejected([&]{readers->copy(m,nodes[1],owner,tokens[1]);},"Foreign handle admitted");
            rejected([&]{readers->copy(h,nodes[1],owner+4,tokens[1]);},"Foreign owner admitted");
            rejected([&]{readers->copy(h,nodes[1],owner,tokens[1]^0x100);},"Stale request generation admitted");
            // Mutations are confined to still-live original metadata, restored
            // before any original reader call. They verify the guard remains
            // strict after choosing the correct B token; no ring is rewritten.
            const uint32_t e=PPCLoadU32(rt.base,m+0x38)+0x138*(tokens[1]&255u);
            for(const auto& [at,value]:std::array<std::pair<uint32_t,uint32_t>,6>{{
                    {e+0x128,0x823412C4},{e+0x12C,owner+4},{e,tokens[1]^0x100},
                    {nodes[1],tokens[1]^0x100},{nodes[1]+8,owned[1].address+4},
                    {nodes[1]+0x10,PPCLoadU32(rt.base,h+8)+1}}}) {
                {RestoredWord altered(rt.base,at,value);rejected(copyLater,"Changed original reader metadata admitted");}
                readers->validate(owned[1]);cleanSnapshot(readers,2);
            }
            auto stale=owned[1];++stale.sequence;
            rejected([&]{readers->validate(stale);},"Foreign claim sequence admitted");
            stale=owned[1];++stale.managerGeneration;
            rejected([&]{readers->validate(stale);},"Foreign manager generation admitted");
            // The returned vector survives source changes and genuine release.
            rt.pointer(sources.address+256,bytes,true)[7]^=0xFF;
            need(std::equal(owned[1].bytes.begin(),owned[1].bytes.end(),expected[1].begin()),"Owned copy aliases fixture source");
            for(const auto& claim:owned) {
                readers->validate(claim,true);cpu.invoke(0x8238D640,h,claim.node);abi.verify(cpu.registers());
                readers->validateReleased(claim);
                rejected([&]{readers->validate(claim);},"Released original claim revived");
                rejected([&]{readers->copy(h,claim.node,owner,claim.token);},"Released B admitted to owned copy");
                need(std::equal(claim.bytes.begin(),claim.bytes.end(),expected[&claim-owned.data()].begin()),
                     "Owned compressed bytes changed after original release");
            }
            cleanSnapshot(readers,0);
            cpu.invoke(0x8238D480,h);abi.verify(cpu.registers());
            rejected([&]{readers->validateReleased(owned[1]);},"Old manager epoch survived original reset");
            const uint32_t used=PPCLoadU32(rt.base,q+0xD0),capacity=PPCLoadU32(rt.base,q+0xCC);
            need(uint64_t(used)+8<=capacity,"Original Q retirement command buffer is full");
            cpu.invoke(0x8233D950,g);abi.verify(cpu.registers());
            std::printf("Original later-claim token %08X differs from retained first token %08X; H=%08X owner=%08X; strict guard mutations rejected\n",
                        tokens[1],tokens[0],h,owner);
        }
        // Original Dac worker owns deferred retirement. Poll native metadata
        // only; never inspect G/M/H/B after their actual original frees.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        for(;;) {
            rt.checkRunning();const auto s=readers->snapshot();
            if(!s.groups && !s.managers && !s.claims && !s.operations) break;
            need(std::chrono::steady_clock::now()<deadline,"Original worker did not retire reader group");Sleep(10);
        }
        {RootLock lock(cpu,q);need(!PPCLoadU32(rt.base,0x82E37310),"Original group link survived retirement");}
        readers->permitFixtureGroup(cpu.registers(),false);
        cpu.invoke(0x82338FA0,q);DWORD result=~0u;
        need(WaitForSingleObject(worker->native,0)==WAIT_OBJECT_0 && GetExitCodeThread(worker->native,&result) && result==0,
             "Original root destructor did not join the Dac worker");
        need(!PPCLoadU32(rt.base,0x82E31BCC) && rt.engineAudio && !rt.engineAudio->ready(),"Original audio root teardown incomplete");
        readers->requireStopped();
        std::printf("PASS original audio reader claim contract: %zu checks; two real memory requests/claims/releases, distinct original tokens, actual reset/retirement/root join; ALL MUTED; no gameplay or fake stream owner\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original audio reader claim contract: %s\n",error.what());return 1;}
}
