#include "runtime/engine_audio.h"
#include "runtime/engine_audio_output.h"
#include "runtime/engine_cpu_calls.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <string_view>
#include <thread>
#include <vector>

extern "C" void __imp__sub_828587F0(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* message){++checks;if(!value) throw Failure(message);}
template<class F> void rejects(F&& fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}need(caught,"Expected EXm0 operation rejection");}
struct ContextRestore {PPCContext& target;PPCContext saved;explicit ContextRestore(PPCContext& value):target(value),saved(value){}~ContextRestore(){target=saved;}};
constexpr uint32_t D=0x82D073AC;
struct OutputObserved{};
struct SourceObserved{};
PPCFunc* outputBody{};
void observeOutput(PPCContext& ctx,uint8_t* base) {
    need(ctx.lr==0x8233DC44 && ctx.r5.u32==0x82D069B4,"Dac0 fixture reached an unexpected original constructor call");
    const uint32_t s=ctx.r3.u32;auto* data=active->pointer(s,0x3108,false);
    std::vector<uint8_t> expected(data,data+0x3108);
    const std::array<uint32_t,2> sdk={PPC_LOAD_U32(0x82E2D9F0),PPC_LOAD_U32(0x82E2D9F4)};
    {
        // This SP passed the former 0x100-byte check but cannot hold the
        // original 0x120-byte frame. Invoke the actual entry and require the
        // hook to fail before the original save helper/backchain writes.
        ContextRestore restore(ctx);
        constexpr uint32_t stackBottom=0x02000000;
        need(!active->pageAccess[(stackBottom-1)>>12].load(),"Dac0 negative fixture needs an unmapped preceding page");
        const auto* stack=active->pointer(stackBottom,0x100,false);
        const std::vector<uint8_t> beforeStack(stack,stack+0x100);
        ctx.r1.u64=stackBottom+0x100;
        const uint64_t lr=ctx.lr;
        rejects([&]{outputBody(ctx,base);});
        need(ctx.r1.u64==stackBottom+0x100 && ctx.lr==lr &&
             !std::memcmp(stack,beforeStack.data(),beforeStack.size()) &&
             !std::memcmp(data,expected.data(),expected.size()),
             "Invalid Dac0 entry frame changed stack, caller ABI or owner storage");
        need(bool(active->engineAudioOutput),"Dac0 entry preflight did not reach its native owner");
        rejects([&]{active->engineAudioOutput->view();});
    }
    bool observed=false;
    try{outputBody(ctx,base);}catch(const SourceObserved&){observed=true;}
    need(observed && ctx.lr==0x82345920 && ctx.r31.u32==s && ctx.r4.u32==s+0x40 && active->engineAudioOutput,
         "Dac0 did not reach the real configured-source diagnostic boundary");
    const auto view=active->engineAudioOutput->view();
    need(view.owner==s && view.root==PPC_LOAD_U32(s+4) && view.mixer==PPC_LOAD_U32(s+0x24) &&
         view.nativeEngine && view.generation && view.muted && view.endpointChannels && view.endpointMask &&
         view.configured && view.identity && !view.active && !view.event && !view.workerId,
         "Dac0 native engine does not match its real original CPU ownership");
    // Six retained CPU words plus one real typed native source identity. The
    // diagnostic throw precedes original buffer/event/worker construction.
    for(auto [offset,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
        {0,0x821DCB90},{0xC,s+0x28},{0x24,view.mixer},{0x28,0x40400000},
        {0x30,0x473B8000},{0x38,0},{0x40,view.identity}})
        for(unsigned i=0;i<4;++i) expected[offset+i]=uint8_t(value>>(24-8*i));
    need(view.mixer && !(view.mixer&0x7F) && PPC_LOAD_U32(view.mixer+0x30008)==view.root,
         "Dac0 original mixer allocation/root field missing");
    need(!std::memcmp(data,expected.data(),expected.size()) &&
         sdk==std::array<uint32_t,2>{PPC_LOAD_U32(0x82E2D9F0),PPC_LOAD_U32(0x82E2D9F4)},
         "Dac0 CPU prefix footprint differs or native acquisition wrote output SDK globals");
    throw OutputObserved{};
}
struct OutputObservation {
    uint8_t* base;
    explicit OutputObservation(uint8_t* value):base(value){outputBody=PPC_LOOKUP_FUNC(base,0x823456D0);need(outputBody!=nullptr,"Dac0 mapping missing");PPC_LOOKUP_FUNC(base,0x823456D0)=observeOutput;}
    ~OutputObservation(){PPC_LOOKUP_FUNC(base,0x823456D0)=outputBody;outputBody=nullptr;}
};
struct Allocation {uint32_t bytes;uint64_t generation=0;};
struct AllocationObservation;
AllocationObservation* allocationObservation{};
struct AllocationObservation {
    uint8_t* base;uint32_t adapter;
    PPCFunc *allocateBody,*freeBody;
    std::map<uint32_t,Allocation> live;
    size_t allocated=0,freed=0;
    bool failNext=false;
    unsigned injectedNulls=0;
    uint32_t lastAddress=0;
    static void allocate(PPCContext& ctx,uint8_t* base) {
        auto& self=*allocationObservation;const uint32_t adapter=ctx.r3.u32,bytes=ctx.r4.u32,alignment=ctx.r7.u32;
        // Explicit test-only allocation failure, never fabricated success.
        // The real generic wrapper must handle null and retire its transaction.
        if(adapter==self.adapter && self.failNext) {self.failNext=false;++self.injectedNulls;ctx.r3.u64=0;return;}
        self.allocateBody(ctx,base); // Actual original backing allocation, never a replacement pointer.
        if(adapter!=self.adapter || !ctx.r3.u32) return;
        const uint32_t address=ctx.r3.u32;
        need(alignment==16 && !(address&15),"EXm0 original allocation alignment changed");
        need(self.live.emplace(address,Allocation{bytes}).second,"Original allocator reused a live test allocation");
        std::memset(active->pointer(address,bytes,true),0xa5,bytes); // Only the newly owned payload, before its original base ctor.
        ++self.allocated;self.lastAddress=address;
    }
    static void release(PPCContext& ctx,uint8_t* base) {
        auto& self=*allocationObservation;const uint32_t adapter=ctx.r3.u32,address=ctx.r4.u32;
        const auto found=self.live.find(address);
        if(adapter==self.adapter && found!=self.live.end()) {
            rejects([&]{active->engineAudio->view(address);}); // Native retirement must precede the original free.
            self.freeBody(ctx,base);
            self.live.erase(found);++self.freed;
        }else self.freeBody(ctx,base);
    }
    AllocationObservation(uint8_t* memory,uint32_t owner):base(memory),adapter(owner),
        allocateBody(PPC_LOOKUP_FUNC(base,0x8274B140)),freeBody(PPC_LOOKUP_FUNC(base,0x8274B1A0)) {
        need(allocateBody && freeBody && !allocationObservation,"Original allocator observation setup failed");
        allocationObservation=this;PPC_LOOKUP_FUNC(base,0x8274B140)=allocate;PPC_LOOKUP_FUNC(base,0x8274B1A0)=release;
    }
    ~AllocationObservation(){PPC_LOOKUP_FUNC(base,0x8274B140)=allocateBody;PPC_LOOKUP_FUNC(base,0x8274B1A0)=freeBody;allocationObservation=nullptr;}
};
std::vector<uint8_t> bytes(Runtime& rt,uint32_t address,uint32_t size){auto* p=rt.pointer(address,size,false);return {p,p+size};}
void expectedWord(std::vector<uint8_t>& expected,uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)expected.at(at+i)=uint8_t(value>>(24-8*i));}
void checkLayout(Runtime& rt,uint32_t v,uint32_t channels,uint32_t owner) {
    const uint32_t layers=(channels+1)/2,queue=0x58+0x18*layers,extent=queue+0x190;
    std::vector<uint8_t> expected(extent,0xa5);
    for(auto [offset,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
        {0,0x821DCAE0},{4,owner},{8,v},{0xC,0x8233EC58},{0x10,0},{0x14,0x8233FAF8},
        {0x18,0x45586D30},{0x1C,0},{0x20,extent},{0x24,queue},{0x34,v+0x58},
        {0x3C,0},{0x44,layers},{0x48,0},{0x4C,0}}) expectedWord(expected,offset,value);
    expected[0x2C]=expected[0x2D]=0;expected[0x2E]=uint8_t(channels);
    expected[0x2F]=expected[0x30]=expected[0x31]=expected[0x33]=0;expected[0x32]=20;
    expected[0x54]=0;expected[0x55]=1;
    std::fill(expected.begin()+0x58,expected.begin()+queue,0);
    for(uint32_t i=0;i<layers;++i) expected[0x58+0x18*i+0xC]=uint8_t(std::min(2u,channels-2*i));
    for(uint32_t i=0;i<20;++i){expectedWord(expected,queue+0x14*i,0);expectedWord(expected,queue+0x14*i+0xC,0);}
    const auto actual=bytes(rt,v,extent);
    for(size_t i=0;i<extent;++i) {
        if(actual[i]!=expected[i]) {std::fprintf(stderr,"EXm0 layout mismatch v=%08X offset=%zX got=%02X expected=%02X\n",v,i,actual[i],expected[i]);}
        need(actual[i]==expected[i],"Original/native EXm0 CPU byte footprint differs");
    }
}
// Forwarding observer of the genuine original lower-heap free. It never
// fabricates a heap owner, callback, return value or replacement allocation.
struct AllocationSpanFreeObservation {
    inline static AllocationSpanFreeObservation* current{};
    Runtime& rt;uint32_t address,extent;uint64_t generation;
    uint32_t beforeCalls=0,afterCalls=0;
    AllocationSpanFreeObservation(Runtime& value,uint32_t at,uint32_t size,uint64_t epoch):rt(value),address(at),extent(size),generation(epoch){
        need(!current,"Original borrowed span free observer overlapped");current=this;
    }
    ~AllocationSpanFreeObservation(){current=nullptr;}
    bool matches(const PPCContext& ctx,uint8_t* base)const {
        return base==rt.base&&ctx.r4.u32==address&&(ctx.lr==0x8268DFD8||ctx.lr==0x8268E018);
    }
    void inspect(PPCContext& ctx,bool after) {
        // Native lookup failures/diagnostics must not change the callback's
        // CPU registers or host state before the real original body runs.
        struct HostRestore {uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
            ~HostRestore(){PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}} host;
        PPCContext before;std::memcpy(&before,&ctx,sizeof(before));
        const auto payload=bytes(rt,address,extent);auto& owners=*rt.engineAudio;
        for(uint32_t at:{address,address+1,address+extent-1}) {
            bool closing=false;try{(void)owners.allocationSpan(at);}catch(const Failure& error){
                closing=std::string_view(error.what()).find("closing")!=std::string_view::npos;
            }
            need(closing,"Original free callback did not reject its closing borrowed owner");
        }
        rejects([&]{owners.allocationGeneration(address,extent);});
        need(bytes(rt,address,extent)==payload&&!std::memcmp(&before,&ctx,sizeof(before)),
             "Closing borrowed-span query changed actual free CPU/payload state");
        if(after)++afterCalls;else ++beforeCalls;
    }
};
struct ResourceAllocationAbi {
    uint64_t sp,lr,r13;
    std::array<uint64_t,18> gpr,fpr;
    bool operator==(const ResourceAllocationAbi&)const=default;
};
ResourceAllocationAbi resourceAllocationAbi(const PPCContext& c){
    return {c.r1.u64,c.lr,c.r13.u64,
        {c.r14.u64,c.r15.u64,c.r16.u64,c.r17.u64,c.r18.u64,c.r19.u64,c.r20.u64,c.r21.u64,c.r22.u64,
         c.r23.u64,c.r24.u64,c.r25.u64,c.r26.u64,c.r27.u64,c.r28.u64,c.r29.u64,c.r30.u64,c.r31.u64},
        {c.f14.u64,c.f15.u64,c.f16.u64,c.f17.u64,c.f18.u64,c.f19.u64,c.f20.u64,c.f21.u64,c.f22.u64,
         c.f23.u64,c.f24.u64,c.f25.u64,c.f26.u64,c.f27.u64,c.f28.u64,c.f29.u64,c.f30.u64,c.f31.u64}};
}
void runContracts(Runtime& rt,const PPCContext& entry,uint8_t* base) {
    EngineCpuCalls cpu(entry,base);auto& ctx=cpu.registers();auto& owners=*rt.engineAudio;
    const uint32_t q=PPC_LOAD_U32(0x82E31BCC),adapter=PPC_LOAD_U32(q+0x14),registry=PPC_LOAD_U32(q+0x2C);
    need(q && adapter && registry && PPC_LOAD_U32(adapter)==0x8215CA2C,"Original startup did not establish the real audio allocator/registry");
    need(PPC_LOAD_U32(q+0x18)==0x82339788 && PPC_LOAD_U32(q+0x1C)==0x82339798,
         "Original shared physical allocator callbacks were not installed");
    // The actual application already called82340448 after its direct provider
    // call. Follow its original linked registry and require exactly one EXm0.
    size_t registryCount=0,found=0;
    for(uint32_t node=PPC_LOAD_U32(registry);node;node=PPC_LOAD_U32(node)) {
        need(++registryCount<=32,"Invalid original codec registry chain");found+=node==D+0x10;
    }
    need(found==1 && registryCount==PPC_LOAD_U32(registry+8),"Actual startup did not retain the original EXm0 registration");
    const auto registered=bytes(rt,registry,12);
    need(cpu.invoke(0x82340448,registry,D)==D && bytes(rt,registry,12)==registered,"Original duplicate registration mutated registry");
    // Original82342B98 passes retained S+4=Q, proved by8233DC24/28.
    // This fixture invokes the codec wrapper; it does not construct a voice.
    const uint32_t fixtureOwner=q;
    // These resource allocation entry points bypass8238E880. Observe the real
    // shared manager's returned extent and its paired release, including reuse.
    uint64_t previousAllocation=0;std::map<uint32_t,uint64_t> retiredResources;
    uint32_t allocationSpanCases=0,closingCallbacks=0,naturalReuse=0;
    // Original8268DF90 calls this same immutable free body on both heap paths.
    need(PPC_LOAD_U32(0x8268DFD4)==0x481CA81D&&PPC_LOAD_U32(0x8268E014)==0x481CA7DD,
         "Original borrowed span free callback instruction changed");
    for(uint32_t entry:{0x8269BD70u,0x8269BDE0u})for(uint32_t bankBytes:{515216u,130u}) {
        ContextRestore restore(ctx);ctx.lr=0x11223344;const auto caller=resourceAllocationAbi(ctx);
        const uint32_t allocation=cpu.invoke(entry,bankBytes,16);
        need(allocation&&resourceAllocationAbi(ctx)==caller,"Original resource allocation damaged caller ABI");
        const auto observed=owners.allocationGeneration(allocation,bankBytes);
        need(observed&&observed!=previousAllocation,"Original resource allocation reused its generation");
        const EngineAudioOwners::AllocationSpan expected{allocation,bankBytes,observed};
        for(uint32_t at:{allocation,allocation+1,allocation+bankBytes/2,allocation+bankBytes-1})
            need(owners.allocationSpan(at)==expected,"Original borrowed span lost base/interior/last-byte owner extent/generation");
        const auto end=owners.allocationSpan(allocation+bankBytes);
        need(!end||end->address!=allocation,"Original borrowed span included the logical allocation end");
        const auto preceding=owners.allocationSpan(allocation-1);
        need(!preceding||preceding->address!=allocation,"Original borrowed span included allocator prefix bytes");
        need(rt.pointer(allocation+bankBytes,1,false)!=nullptr,"Original span fixture did not retain readable allocation padding");
        rejects([&]{owners.allocationGeneration(allocation,bankBytes+1);});
        rejects([&]{owners.allocationGeneration(allocation+1,1);});
        // Use only the original returned payload. Query bounds are the exact
        // requested bytes despite larger heap blocks and mapped pages.
        std::memset(rt.pointer(allocation,bankBytes,true),0x6D,bankBytes);
        const auto payload=bytes(rt,allocation,bankBytes);
        need(owners.allocationSpan(allocation+bankBytes-1)==expected&&bytes(rt,allocation,bankBytes)==payload,
             "Borrowed span observation mutated its live original payload");
        if(const auto retired=retiredResources.find(allocation);retired!=retiredResources.end()){
            ++naturalReuse;need(observed!=retired->second,"Actually reused original allocation retained its retired epoch");
        }
        {
            AllocationSpanFreeObservation free(rt,allocation,bankBytes,observed);
            cpu.invoke(0x8269BEB0,allocation);
            need(free.beforeCalls==1&&free.afterCalls==1,"Borrowed owner was not checked inside exactly one real original free callback");
            closingCallbacks+=free.beforeCalls;
        }
        need(resourceAllocationAbi(ctx)==caller,"Original resource release damaged caller ABI");
        rejects([&]{owners.allocationGeneration(allocation,bankBytes);});
        need(!owners.allocationSpan(allocation)&&!owners.allocationSpan(allocation+1)&&!owners.allocationSpan(allocation+bankBytes-1),
             "Original resource retirement retained a borrowed base/interior owner");
        need(rt.pointer(allocation,1,false)!=nullptr,"Original span retirement case unexpectedly depended on page unmapping");
        retiredResources[allocation]=observed;previousAllocation=observed;++allocationSpanCases;
    }
    need(!AllocationSpanFreeObservation::current&&allocationSpanCases==4&&closingCallbacks==4,
         "Borrowed allocation span lifecycle observer survived original release");
    std::printf("AUDIT_ALLOCATION_SPAN cases=%u original_create=8269BD70_8269BDE0 extent=requested_base_and_interior end=excluded padding=readable closing=original828587F0 retirement=absent fresh_generation=passed natural_address_reuse=%u observer_release=passed caller_ABI=passed concurrency=unproven\n",
        allocationSpanCases,naturalReuse);
    AllocationObservation allocations(base,adapter);
    auto create=[&](uint32_t channels) {
        ContextRestore restore(ctx);ctx.lr=0x11223344;
        const auto sp=ctx.r1.u64;
        const std::array regs={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                              &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
        for(size_t i=0;i<regs.size();++i) regs[i]->u64=0xfedcba9800000000ull+i;
        const uint32_t v=cpu.invoke(0x823404A8,registry,D,channels,0x14,fixtureOwner);
        need(ctx.r1.u64==sp && ctx.lr==0x11223344,"Original EXm0 construction damaged caller SP/LR");
        for(size_t i=0;i<regs.size();++i) need(regs[i]->u64==0xfedcba9800000000ull+i,"Original EXm0 construction damaged a nonvolatile register");
        if(v) {
            need(allocations.live.contains(v),"Native instance has no actual original allocation receipt");
            const auto view=owners.view(v);need(view.channels==channels && view.layers==(channels+1)/2,"Native instance channel ownership changed");
            allocations.live.at(v).generation=view.generation;checkLayout(rt,v,channels,fixtureOwner);
        }
        return v;
    };
    auto destroy=[&](uint32_t v) {
        ContextRestore restore(ctx);ctx.lr=0x55667788;const auto sp=ctx.r1.u64;
        const std::array regs={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                              &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
        for(size_t i=0;i<regs.size();++i) regs[i]->u64=0x1234567800000000ull+i;
        cpu.invoke(0x823402E8,v);
        need(ctx.r1.u64==sp && ctx.lr==0x55667788,"Original EXm0 destruction damaged caller SP/LR");
        for(size_t i=0;i<regs.size();++i) need(regs[i]->u64==0x1234567800000000ull+i,"Original EXm0 destruction damaged a nonvolatile register");
        need(!allocations.live.contains(v),"Original EXm0 allocation did not pass through real free");
    };
    for(uint32_t channels:{0u,7u,0x100u,0xFFFFFFFFu}) {
        const auto old=allocations.allocated;rejects([&]{create(channels);});
        need(allocations.allocated==old && owners.count()==0,"Invalid channel count reached original allocation");
    }
    allocations.failNext=true;
    need(create(2)==0 && allocations.injectedNulls==1 && allocations.allocated==0 && allocations.freed==0 &&
         owners.count()==0 && owners.reservedLayers()==0,"Null original allocation result leaked a creation transaction");
    auto retry=create(2);need(retry!=0,"Retry after a null allocation result failed");destroy(retry);
    // P6B0 is registered by actual startup after EXm0. Its leaf CPU
    // constructor and null release callback must pass through the shared
    // helpers without acquiring or retiring an EXm0 native owner.
    {
        constexpr uint32_t p=0x82D073CC;
        size_t otherFound=0,nodes=0;
        for(uint32_t node=PPC_LOAD_U32(registry);node;node=PPC_LOAD_U32(node)) {
            need(++nodes<=32,"Invalid P6B0 registry chain");otherFound+=node==p+0x10;
        }
        need(otherFound==1 && cpu.invoke(0x8233E1D8)==p && bytes(rt,registry,12)==registered,
             "Original startup did not retain the P6B0 descriptor unchanged");
        for(auto [offset,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
            {0,0x8233E1C8},{4,0x8233E1E8},{8,0},{0xC,0x8233E210},{0x14,0x50364230},{0x18,0}})
            need(PPC_LOAD_U32(p+offset)==value,"Original P6B0 descriptor changed");
        const auto count=owners.count(),allocated=allocations.allocated,freed=allocations.freed;
        const auto layers=owners.reservedLayers();const bool ready=owners.ready();
        uint32_t v;
        {
            ContextRestore restore(ctx);ctx.lr=0x11223344;const auto sp=ctx.r1.u64;
            const std::array regs={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                                  &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
            for(size_t i=0;i<regs.size();++i) regs[i]->u64=0xfedcba9800000000ull+i;
            v=cpu.invoke(0x823404A8,registry,p,2,0x14,q);
            need(ctx.r1.u64==sp && ctx.lr==0x11223344,"Original P6B0 construction damaged caller SP/LR");
            for(size_t i=0;i<regs.size();++i) need(regs[i]->u64==0xfedcba9800000000ull+i,"Original P6B0 construction damaged a nonvolatile register");
        }
        need(v && allocations.live.contains(v) && allocations.live.at(v).bytes==0x1D0 &&
             allocations.allocated==allocated+1 && allocations.freed==freed,"P6B0 did not acquire exactly one real allocation");
        std::vector<uint8_t> expected(0x1D0,0xa5);
        for(auto [offset,value]:std::initializer_list<std::pair<uint32_t,uint32_t>>{
            {0,0x821DCAD8},{4,q},{8,v},{0xC,0},{0x10,0},{0x14,0x8233E210},{0x18,0x50364230},
            {0x1C,0},{0x20,0x1D0},{0x24,0x40},{0x34,0},{0x38,0}}) expectedWord(expected,offset,value);
        expected[0x2C]=expected[0x2D]=0;expected[0x2E]=2;
        expected[0x2F]=expected[0x30]=expected[0x31]=expected[0x33]=0;expected[0x32]=0x14;
        for(uint32_t i=0;i<20;++i){expectedWord(expected,0x40+0x14*i,0);expectedWord(expected,0x4C+0x14*i,0);}
        const auto actual=bytes(rt,v,0x1D0);
        for(size_t i=0;i<expected.size();++i) need(actual[i]==expected[i],"Original P6B0 CPU footprint changed");
        rejects([&]{owners.view(v);});
        need(owners.count()==count && owners.reservedLayers()==layers && owners.ready()==ready,"P6B0 construction changed EXm0 ownership");
        destroy(v);
        need(allocations.allocated==allocated+1 && allocations.freed==freed+1 &&
             owners.count()==count && owners.reservedLayers()==layers && owners.ready()==ready,"P6B0 destruction changed EXm0 ownership");
    }
    std::map<uint32_t,uint64_t> retired;
    bool reused=false;
    for(unsigned cycle=0;cycle<3;++cycle) for(uint32_t channels=1;channels<=6;++channels) {
        const auto v=create(channels);need(v!=0,"Native unconfigured EXm0 instance creation failed");
        const auto view=owners.view(v);auto weak=owners.lease(v,view.generation);auto held=weak.lock();need(bool(held),"Native instance lease unavailable");
        if(retired.contains(v)) {reused=true;rejects([&]{owners.lease(v,retired.at(v));});}
        const auto before=bytes(rt,v,allocations.live.at(v).bytes);
        {
            ContextRestore restore(ctx);cpu.invoke(0x8233E8A0,0,0);need(!owners.view(v).enabled,"Native active disable failed");
            cpu.invoke(0x8233E920);need(owners.view(v).enabled,"Native active enable failed");
            rejects([&]{cpu.invoke(0x8233E8A0,0,v);});need(owners.view(v).enabled,"Rejected hardware exclusion changed admission");
            rejects([&]{cpu.invoke(0x8233E7E8);});need(owners.ready(),"Rejected teardown released a live factory");
            rejects([&]{cpu.invoke(0x8233EC58,v);});
            rejects([&]{cpu.invoke(0x8233E9C8,v);});
            ctx.r8.u32=0;ctx.r9.u32=0;
            rejects([&]{cpu.invoke(0x8234E768,v,fixtureOwner,512,0,0);});
            for(uint32_t guard:{0x8233E5C0u,0x8233E568u,0x8233ED78u,0x8233FF80u,0x8233FAF0u,
                                0x8233F000u,0x8233FAF8u,0x8233EC00u,0x8233EC48u,0x8233EBF0u})
                rejects([&]{cpu.invoke(guard,v,fixtureOwner,256);});
        }
        need(bytes(rt,v,allocations.live.at(v).bytes)==before && owners.count()==1,"Guarded input/backend operation mutated ownership or CPU queue");
        destroy(v);retired[v]=view.generation;
        need(owners.count()==0 && owners.reservedLayers()==0 && !weak.expired(),"Retirement lost a separately retained native lease");
        rejects([&]{owners.lease(v,view.generation);});held.reset();need(weak.expired(),"Native instance leaked after last lease release");
    }
    need(reused,"Repeated real allocations did not exercise same-address generation reuse");
    // Exhaust the same 256-layer admission capacity as the original pool. A
    // real false ctor result must trigger original release + free exactly once.
    std::vector<uint32_t> full;
    for(unsigned i=0;i<85;++i) {auto v=create(6);need(v!=0,"Native layer admission failed before capacity");full.push_back(v);}
    need(owners.reservedLayers()==255,"Native layer reservation count changed");
    auto oldAllocated=allocations.allocated,oldFreed=allocations.freed;
    need(create(6)==0,"Partial-capacity native construction should fail");
    need(allocations.allocated==oldAllocated+1 && allocations.freed==oldFreed+1 && owners.reservedLayers()==255 && owners.count()==85,
         "Failed native constructor did not unwind original allocation exactly once");
    full.push_back(create(1));need(full.back()!=0 && owners.reservedLayers()==256,"Final native layer was not acquired");
    oldAllocated=allocations.allocated;oldFreed=allocations.freed;
    need(create(1)==0,"Full native layer capacity admitted another instance");
    need(allocations.allocated==oldAllocated+1 && allocations.freed==oldFreed+1 && owners.reservedLayers()==256,
         "Capacity failure leaked original allocation or native reservation");
    for(auto v:full) destroy(v);
    // Exercise the owner's region scan while another real host thread grows
    // and erases Runtime::regions using the normal VM synchronization domain.
    std::atomic_uint32_t mappingCycles{0};std::promise<void> started;auto start=started.get_future();
    std::exception_ptr mappingError;
    {
        std::jthread mappings([&](std::stop_token stop) {
            bool announced=false;
            try {
                do {
                    for(uint32_t i=0;i<32;++i) {
                        std::lock_guard vmLock(rt.vmMutex);
                        rt.map(0x7D000000+i*0x1000,0x1000,true,"EXm0 concurrent VM fixture",MemoryUse::Host);
                    }
                    if(!announced) {started.set_value();announced=true;}
                    for(uint32_t i=0;i<32;++i) rt.unmap(0x7D000000+i*0x1000);
                    ++mappingCycles;
                }while(!stop.stop_requested());
            }catch(...) {mappingError=std::current_exception();if(!announced) started.set_exception(mappingError);}
        });
        start.get();
        for(unsigned i=0;i<24;++i) {const auto v=create(i%6+1);need(v!=0,"EXm0 construction failed during concurrent region changes");destroy(v);}
        mappings.request_stop();mappings.join();
    }
    if(mappingError) std::rethrow_exception(mappingError);
    need(mappingCycles.load()!=0,"Concurrent mapping fixture made no progress");
    for(uint32_t i=0;i<32;++i) need(!rt.pageAccess[(0x7D000000+i*0x1000)>>12].load(),"Concurrent mapping fixture retained a page");
    // Retired unconfigured metadata can safely outlive factory admission.
    // This is not a drain contract for future configured decoder/source work.
    const auto retained=create(2);const auto identity=owners.view(retained).generation;
    auto weak=owners.lease(retained,identity);auto held=weak.lock();destroy(retained);
    cpu.invoke(0x8233E7E8);need(!owners.ready() && !weak.expired(),"Factory stop invalidated retired native metadata");
    need(cpu.invoke(0x8233E598)==D && owners.ready(),"Factory restart with a retired metadata lease failed");
    rejects([&]{owners.lease(retained,identity);});held.reset();need(weak.expired(),"Retired native metadata leaked after factory restart");
    need(allocations.live.empty() && allocations.allocated==allocations.freed && !owners.count() && !owners.reservedLayers(),
         "Original/native EXm0 lifetimes did not balance");
    cpu.invoke(0x8233E7E8);need(!owners.ready(),"Empty native factory teardown failed");
    need(cpu.invoke(0x8233E598)==D && owners.ready(),"Native EXm0 factory restart failed");
    cpu.invoke(0x8233E7E8);need(!owners.ready(),"Restarted factory teardown failed");
    for(uint32_t address:{0x82E36CA8u,0x82E36CACu,0x82E36CB0u,0x82E36CB4u,0x82E37314u,0x82E37318u,0x82E3731Cu,0x82E37320u})
        need(PPC_LOAD_U32(address)==0,"Native lifecycle populated an original hardware pool field");
    need(PPC_LOAD_U8(0x82E36B82)==0,"Native lifecycle impersonated the hardware initialized flag");
    std::printf("Original EXm0 lifecycle PASS: %zu checks, %zu actual allocations/frees; decode remains guarded\n",checks,allocations.allocated);
}
}
PPC_FUNC(sub_828587F0){
    auto* observation=AllocationSpanFreeObservation::current;
    const bool watched=observation&&observation->matches(ctx,base);
    if(watched)observation->inspect(ctx,false);
    // Always execute the complete immutable lower-heap body exactly once.
    __imp__sub_828587F0(ctx,base);
    if(watched)observation->inspect(ctx,true);
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==2,"Original image path required");Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        const auto entry=original;bool reached=false;
        // Dac0 is reached through the actual generic indirect constructor.
        // The diagnostic observer stops only this fixture after real native
        // source creation. Production has no observer. It never substitutes
        // constructor success or resumes an exception-unwound guest frame.
        rt.audioBoundaryObserver=[](uint32_t pc,PPCContext&,uint8_t*) {
            need(pc==0x82345920,"Unexpected audio diagnostic boundary in EXm0 fixture");throw SourceObserved{};
        };
        {OutputObservation observation(rt.base);try{runOriginal(original,rt.base);}catch(const OutputObserved&){reached=true;}}
        rt.audioBoundaryObserver={};
        need(reached && rt.engineAudio && rt.engineAudio->ready() && !rt.engineAudio->count(),
             "Actual startup did not complete native EXm0 factory acquisition");
        runContracts(rt,entry,rt.base);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Original EXm0 lifecycle FAIL: %s\n",error.what());return 1;}
}
