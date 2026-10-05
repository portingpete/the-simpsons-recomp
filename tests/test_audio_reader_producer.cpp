#include "runtime/engine_audio_output.h"
#include "runtime/engine_audio.h"
#include "runtime/engine_audio_reader.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/threads.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* why) {++checks;if(!value) throw Failure(why);}
struct StartupObserved {};
struct Case {float duration;uint32_t bitrate,entries,ring;};
constexpr Case cases[]={
    {float(0x47E00)/48000.0f,0,4,0x47E00},
    {float(0x55280)/48000.0f,48,4,0x55280},
    {float(0x4BC80)/48000.0f,48,4,0x4BC80},
    {1.25f,96,7,120000},
    {2.0f,17,0,34000},
    {0.125f,80,253,10000},
    {1.25f,0,4,60000}, // Only a zero bitrate defaults to 48 in the original branch.
    {0.128f,1,4,128},
    {0.001f,1,4,16},
    {0.0005f,1,4,0},
    {std::numeric_limits<float>::denorm_min(),1,4,0},
    {0.0005f,1,4,0}, // Independently runs 65 simultaneously live original groups.
    {2.064f,1,4,2064}, // Smallest fresh ring meeting the 2 KiB watermark plus 16-byte slack.
    {2.048f,1,4,2048}, // One alignment step below the real initial request watermark.
};

void originalEvidence(Runtime& rt,const std::filesystem::path& image) {
    constexpr std::pair<uint32_t,uint32_t> pins[]={
        {0x8233054C,0x9421FF70},{0x82330554,0xFFE00890},
        {0x823305B0,0xC00BD0D8},{0x823305B8,0x419A00B0},
        {0x823305C0,0x409A0008},{0x823305C4,0x3BA00030},{0x823305C8,0x7FAA07B4},
        {0x823305D8,0x7FC6F378},{0x823305E0,0xF9410050},
        {0x82330600,0xFC00069C},{0x82330604,0xFC000018},
        {0x82330608,0xEDA007F2},{0x8233060C,0xC00BD434},
        {0x82330610,0xEC0D0032},{0x82330614,0xFC00065E},
        {0x82330618,0x7C002FAE},{0x82330620,0x216B000F},
        {0x82330624,0x55650036},{0x82330628,0x4800CFD1},
        {0x82330630,0x817D00F8},{0x82330654,0x396B0001},
        {0x82330658,0x917E8ACC},{0x8233065C,0x816A00F0},
        {0x82333E8C,0x394BC850},{0x82333E9C,0x915F0000},
        {0x82330330,0x90830148},{0x821DC940,0x82334140},
        {0x821DC948,0x82330330},{0x821DD0D8,0},
        {0x821DD434,0xC47A0000},{0x82816804,0x4BB26DF5},
        {0x8233D624,0x3B060002},{0x8233D68C,0x3B380001},
        {0x8238C8A0,0x39600006},{0x8238C8A4,0x7D685BD6},
        {0x8238C8AC,0x556B0028},{0x8238BC90,0x554A063E},
        {0x8238C570,0x394AFFF0},{0x8238C604,0x7F0A5800},
        {0x8238C608,0x40980010},{0x8238C610,0x917F0070},
        {0x8280FD78,0x9421FFA0},{0x8280FD7C,0x81630330},
        {0x8280FD8C,0x38800010},{0x8280FD90,0x386B0008},
        {0x8280FD94,0x4BB24E3D},{0x8280FD9C,0x4BB24D75},
        {0x8280FDA0,0x4BB206F1},{0x8232E0E0,0x914B6C90},
        {0x8232E10C,0x914B6C94},
    };
    for(const auto& [pc,value]:pins) need(PPCLoadU32(rt.base,pc)==value,"Original reader producer instruction/data changed");
    // Independently census the immutable image's direct BL instructions. The
    // stock four-manager startup and this duration-derived producer are the
    // two call sites; this does not claim arbitrary computed callers.
    const auto bytes=readFile(image);std::array<uint32_t,2> calls{};size_t count=0;
    for(size_t i=0;i+4<=bytes.size();i+=4) {
        const uint32_t w=(uint32_t(bytes[i])<<24)|(uint32_t(bytes[i+1])<<16)|
                         (uint32_t(bytes[i+2])<<8)|bytes[i+3];
        if((w>>26)!=18 || !(w&1)) continue;
        const int32_t displacement=int32_t(w&0x03FFFFFCu)-(w&0x02000000u?0x04000000:0);
        const uint32_t pc=0x82000000+uint32_t(i);
        const uint32_t target=w&2?uint32_t(displacement):pc+uint32_t(displacement);
        if(target!=0x8233D5F8) continue;
        need(count<calls.size(),"Unreviewed stock reader constructor direct caller");calls[count++]=pc;
    }
    need(count==2 && calls==std::array<uint32_t,2>{0x82330628,0x82816804},"Stock reader constructor caller census changed");
}

struct PreservedABI {
    uint64_t sp,lr;std::array<uint64_t,18> gprs{},fprs{};
    explicit PreservedABI(const PPCContext& c):sp(c.r1.u64),lr(c.lr) {
        const auto g=std::array{c.r14,c.r15,c.r16,c.r17,c.r18,c.r19,c.r20,c.r21,c.r22,
                               c.r23,c.r24,c.r25,c.r26,c.r27,c.r28,c.r29,c.r30,c.r31};
        const auto f=std::array{c.f14,c.f15,c.f16,c.f17,c.f18,c.f19,c.f20,c.f21,c.f22,
                               c.f23,c.f24,c.f25,c.f26,c.f27,c.f28,c.f29,c.f30,c.f31};
        for(size_t i=0;i<18;++i){gprs[i]=g[i].u64;fprs[i]=f[i].u64;}
    }
    void verify(const PPCContext& c) const {
        const PreservedABI after(c);need(after.sp==sp && after.lr==lr && after.gprs==gprs && after.fprs==fprs,
                                        "Original reader producer changed nonvolatile ABI");
    }
};
class RootLock {
    EngineCpuCalls& cpu;uint32_t q;bool held=true;
public:
    RootLock(EngineCpuCalls& calls,uint32_t root):cpu(calls),q(root) {need(cpu.invoke(0x823392C8,q)==0,"Q lock failed");}
    void release(){if(held){need(cpu.invoke(0x823392F0,q)==1,"Q unlock failed");held=false;}}
    ~RootLock(){if(held){try{release();}catch(...){active->requestStop("Reader producer Q lock unwind failed");}}}
};
struct Inputs {
    Runtime& rt;uint32_t address;
    explicit Inputs(Runtime& value):rt(value),address(rt.allocatePhysical(0,4096,PAGE_READWRITE,0,
                                                               std::numeric_limits<uint32_t>::max(),4096)) {
        need(address!=0,"Reader producer fixture inputs unavailable");
    }
    ~Inputs(){rt.stopThreads();try{rt.freePhysical(address);}catch(...){std::fputs("Reader input release failed\n",stderr);}}
};

// This observer returns to the actual original constructor, never supplies an
// allocation/result, and never enables permitFixtureGroup. Its negative probes
// use the real parent frame/voice before any group allocation and restore every
// touched field before forwarding that SAME original call.
class ProducerObserver {
    Runtime& rt;std::shared_ptr<EngineAudioReader> readers;
    static ProducerObserver* current;
    bool negatives=true;
    template<class F> void rejected(PPCContext& c,F&& mutation) {
        const PPCContext original=c;bool failed=false;
        try {mutation();readers->observe(0x8233D5F8,c,rt.base);}
        catch(const Failure&){failed=true;}
        c=original;need(failed,"Changed original reader producer contract admitted");
        const auto v=readers->snapshot();need(!v.groups && !v.managers && !v.claims && !v.operations,
                                             "Rejected producer stranded reader provenance");
    }
    void rejectedWord(PPCContext& c,uint32_t at,uint32_t value) {
        const uint32_t old=PPCLoadU32(rt.base,at);
        try {rejected(c,[&]{PPCStoreU32(rt.base,at,value);});}
        catch(...){PPCStoreU32(rt.base,at,old);throw;}
        PPCStoreU32(rt.base,at,old);
    }
    void rejectedCalculatedExtent(PPCContext& c,double duration) {
        const uint32_t at=c.r1.u32+0x50,old=PPCLoadU32(rt.base,at);
        try {rejected(c,[&] {
            c.f31.f64=duration;
            c.f13.f64=double(float(double(float(c.r10.s64))*duration));
            const auto converted=int64_t(double(float(c.f13.f64*-1000.0)));
            c.f0.s64=converted;PPCStoreU32(rt.base,at,uint32_t(converted));
            c.r11.u32=15u-uint32_t(converted);c.r5.u32=c.r11.u32&~15u;
        });} catch(...) {PPCStoreU32(rt.base,at,old);throw;}
        PPCStoreU32(rt.base,at,old);
    }
    static void dispatch(PPCContext& c,uint8_t* base) {
        auto& self=*current;need(base==self.rt.base && c.lr==0x8233062C,"Producer bypassed the original whole parent");
        const uint32_t sp=c.r1.u32,voice=c.r31.u32;
        std::fprintf(stderr,"[PRODUCER FRAME] sp=%08X backchain=%08X sp4=%08X voice=%08X vtable=%08X r29=%08X id=%08X/global=%08X rate=%lld duration=%.17g entries=%u allocator=%08X factory=%08X\n",
            sp,PPCLoadU32(base,sp),PPCLoadU32(base,sp+4),voice,voice?PPCLoadU32(base,voice):0,
            c.r29.u32,c.r3.u32,PPCLoadU32(base,0x82D08ACC),static_cast<long long>(c.r10.s64),c.f31.f64,c.r6.u32,
            PPCLoadU32(base,0x82E36BEC),PPCLoadU32(base,0x82E36C94));
        need(PPCLoadU32(base,sp)==sp+0x90 && voice && PPCLoadU32(base,voice)==0x821DC850 &&
             c.r3.u32==PPCLoadU32(base,0x82D08ACC),"Real producer frame/voice missing");
        if(self.negatives) {
            self.rejected(c,[&]{c.lr=0x82330630;});
            self.rejected(c,[&]{c.r1.u32+=16;});
            self.rejected(c,[&]{++c.r3.u32;});
            self.rejected(c,[&]{c.r4.u32=2;});
            self.rejected(c,[&]{c.r5.u32+=16;});
            self.rejected(c,[&]{c.r6.u32=254;});
            self.rejected(c,[&]{c.r7.u32+=4;});
            self.rejected(c,[&]{c.r8.u32=4;});
            self.rejected(c,[&]{c.r9.u32=0x1000;});
            self.rejected(c,[&]{c.r10.s64=-1;});
            self.rejected(c,[&]{c.r10.s64=int64_t(std::numeric_limits<int32_t>::max())+1;});
            self.rejected(c,[&]{c.r29.u32+=4;});
            self.rejected(c,[&]{c.r30.u32+=0x10000;});
            self.rejected(c,[&]{++c.r11.u32;});
            self.rejected(c,[&]{++c.f0.s64;});
            self.rejected(c,[&]{c.f13.f64+=1;});
            self.rejected(c,[&]{c.f31.f64=0;});
            self.rejected(c,[&]{c.f31.f64=std::numeric_limits<double>::quiet_NaN();});
            self.rejected(c,[&]{c.f31.f64=std::numeric_limits<double>::infinity();});
            self.rejected(c,[&]{c.f31.f64=1000000000.;c.f13.f64=double(float(double(float(c.r10.s64))*c.f31.f64));});
            // All arithmetic witnesses match this oversized request. It must
            // still reject: original8238C8A4 divw interprets ring as signed.
            self.rejectedCalculatedExtent(c,(double(std::numeric_limits<int32_t>::max())+4096.)/
                                             (double(float(c.r10.s64))*1000.));
            self.rejectedCalculatedExtent(c,-0.0005);
            self.rejectedWord(c,sp,sp+0xA0);
            self.rejectedWord(c,sp+0x50,PPCLoadU32(base,sp+0x50)^16u);
            self.rejectedWord(c,sp+0x58,c.r7.u32+4);
            self.rejectedWord(c,voice,0x821DC760);
            self.rejectedWord(c,voice+4,0);
            self.rejectedWord(c,voice+0x1C,5);
            self.rejectedWord(c,voice+0x148,voice);
            self.rejectedWord(c,voice+0x154,0);
            self.rejectedWord(c,0x821DC948,0x82330334);
            self.negatives=false;
        }
        ++self.calls;self.ring=c.r5.u32;self.entryCount=c.r6.u32+3;self.identifier=c.r3.u32;
    }
public:
    size_t calls=0;uint32_t ring=0,entryCount=0,identifier=0;
    ProducerObserver(Runtime& value,std::shared_ptr<EngineAudioReader> r):rt(value),readers(std::move(r)) {
        need(!current && !rt.audioBoundaryObserver,"Original reader constructor observer unavailable");current=this;
        rt.audioBoundaryObserver=[](uint32_t pc,PPCContext& c,uint8_t* base){
            need(pc==0x8233D5F8,"Unexpected original reader observation");dispatch(c,base);
        };
    }
    ~ProducerObserver(){rt.stopThreads();rt.audioBoundaryObserver={};current=nullptr;}
};
ProducerObserver* ProducerObserver::current=nullptr;

struct ReaderProfile {
    uint32_t count,ringSize,entries;
    uint32_t ringOffset() const {return (0x30+0x20*count+15)&~15u;}
    uint32_t extent() const {return count*ringSize+ringOffset();}
};
constexpr uint32_t cycles=3;
constexpr size_t totalAllocations=15; // Reader roles only; outer voices/services/startup allocations are excluded by original allocator LR.
constexpr ReaderProfile profileForCycle(uint32_t cycle){return cycle<3?ReaderProfile{1,cases[2].ring,cases[2].entries+3}:ReaderProfile{1,cases[3].ring,cases[3].entries+3};}
enum class Role {Group,Manager,Entries,Handle,Filter};
struct Allocation {
    uint32_t address{},bytes{},allocator{},allocationLR{},freeLR{},freeThread{},cycle{};
    uint64_t generation{},parent{},freeOrder{};
    Role role{};
    bool live{},freeing{};
};

// Fixture-only metadata. It observes real indirect allocator dispatches and
// never allocates, clears, poisons, or returns replacement guest storage.
// Its generation predicate is an observation oracle, NOT a production lease.
class AllocationAudit {
    enum class Kind {Allocate,Release,Retirement};
    struct Hook {uint32_t address{};PPCFunc* body{};Kind kind{};};
    Runtime& rt;
    std::array<Hook,5> hooks{};
    size_t hookCount=0;
    std::mutex mutex;
    std::array<Allocation,totalAllocations> records{};
    size_t used=0;
    uint64_t freeOrder=0;
    uint32_t cycle=0;bool tracking=true;
    std::array<bool,cycles> retirementSeen{};
    ReaderProfile profile=profileForCycle(1);
    const char* error=nullptr;
    uint32_t groupAllocator,readerAllocator,worker;
    static AllocationAudit* current;
    void check(bool condition,const char* message) {
        if(!condition && !error) error=message;
    }
    static uint32_t expectedFreeLR(Role role) {
        switch(role) {
        case Role::Group:return 0x8233D5EC;
        case Role::Manager:return 0x8238CF58;
        case Role::Entries:return 0x8238CB6C;
        case Role::Handle:return 0x8238CCEC;
        case Role::Filter:return 0x8238CC90;
        }
        return 0;
    }
    void allocation(uint32_t allocator,uint32_t bytes,uint32_t lr,uint32_t address) {
        Role role;uint32_t expected;
        switch(lr) {
        case 0x8233DB40:role=Role::Group;expected=profile.extent();break;
        case 0x8238CD4C:role=Role::Manager;expected=0x218;break;
        case 0x8238C810:role=Role::Entries;expected=0x138*profile.entries;break;
        case 0x8238C93C:role=Role::Handle;expected=0x14;break;
        case 0x8238C9B8:role=Role::Filter;expected=0x10;break;
        default:return;
        }
        std::lock_guard lock(mutex);
        if(!tracking)return;
        check(cycle && bytes==expected && address &&
              allocator==(role==Role::Group?groupAllocator:readerAllocator),
              "Original reader allocation ABI/extent/result changed");
        check(GetCurrentThreadId()!=worker,"Reader construction unexpectedly ran on Dac worker");
        for(size_t i=0;i<used;++i) check(!records[i].live || records[i].address!=address,
                                      "Original allocation reused live reader storage");
        check(used<records.size(),"Reader allocation exceeded the bounded fixture");
        if(used==records.size()) return;
        auto& record=records[used++];
        record.address=address;record.bytes=bytes;record.allocator=allocator;
        record.allocationLR=lr;record.cycle=cycle;record.generation=used;
        record.role=role;record.live=true;
    }
    template<size_t I> static void dispatch(PPCContext& ctx,uint8_t* base) {
        auto& self=*current;
        const uint32_t allocator=ctx.r3.u32,argument=ctx.r4.u32,lr=uint32_t(ctx.lr);
        if(self.hooks[I].kind==Kind::Retirement) {
            bool tracking;
            {std::lock_guard lock(self.mutex);tracking=self.tracking;}
            if(!tracking){self.hooks[I].body(ctx,base);return;}
            // The actual Q command executor calls this indirectly while it
            // owns the installed Q40/Q44 lock. Observe the result before that SAME worker can free G.
            const uint32_t g=PPCLoadU32(base,allocator+4),q=PPCLoadU32(base,g);
            const uint32_t pending=PPCLoadU32(base,q+0xF0);
            PPCContext incoming;std::memcpy(&incoming,&ctx,sizeof(incoming));
            uint32_t closingCycle=0;
            {std::lock_guard lock(self.mutex);for(size_t i=0;i<self.used;++i)
                if(self.records[i].role==Role::Group&&self.records[i].live&&self.records[i].address==g)closingCycle=self.records[i].cycle;}
            self.hooks[I].body(ctx,base);
            const bool effects=ctx.r3.u32==8 && PPCLoadU32(base,g+12)==0x8233D520 &&
                PPCLoadU32(base,g+16)==g && PPCLoadU32(base,q+0xF0)==pending+1;
            {std::lock_guard lock(self.mutex);
            self.check(lr==0x823395BC && GetCurrentThreadId()==self.worker && effects,
                       "Original Q executor did not perform the real reader retirement command");
            self.check(closingCycle && closingCycle<=cycles && !self.retirementSeen[closingCycle-1],
                       "Duplicate/unscoped original reader retirement command");
            if(closingCycle && closingCycle<=cycles) self.retirementSeen[closingCycle-1]=true;}
            if(self.closeProbe)self.closeProbe(closingCycle,incoming,ctx,base,g);
            return;
        }
        if(self.hooks[I].kind==Kind::Allocate) {
            self.hooks[I].body(ctx,base);
            self.allocation(allocator,argument,lr,ctx.r3.u32);
            return;
        }
        size_t found=self.records.size();
        {
            std::lock_guard lock(self.mutex);
            for(size_t i=0;i<self.used;++i) {
                auto& r=self.records[i];
                if(!r.live || r.address!=argument || r.allocator!=allocator) continue;
                found=i;
                self.check(!r.freeing && lr==expectedFreeLR(r.role) && ctx.r5.u32==0,
                           "Reader free used an unexpected caller/ABI or duplicated a live free");
                self.check(GetCurrentThreadId()==self.worker,"Reader cleanup did not run on the real Dac worker");
                self.check(self.retirementSeen[r.cycle-1],"Reader cleanup preceded the original retirement command return");
                for(size_t j=0;j<self.used;++j) if(self.records[j].parent==r.generation)
                    self.check(!self.records[j].live,"Original parent free preceded a member free");
                r.freeing=true;break;
            }
        }
        self.hooks[I].body(ctx,base); // No audit mutex across the actual AOT allocator.
        if(found==self.records.size()) return;
        std::lock_guard lock(self.mutex);
        auto& r=self.records[found];
        r.live=false;r.freeing=false;r.freeOrder=++self.freeOrder;
        r.freeLR=lr;r.freeThread=GetCurrentThreadId();
        if(r.role==Role::Group) SetEvent(self.completed);
        // Do not inspect the guest pointer after this original free returns.
    }
    void add(uint32_t address,Kind kind) {
        for(size_t i=0;i<hookCount;++i) if(hooks[i].address==address) {
            need(hooks[i].kind==kind,"Allocator callback has conflicting fixture roles");return;
        }
        need(hookCount<hooks.size() && address && PPC_LOOKUP_FUNC(rt.base,address),
             "Original allocator dispatch entry unavailable");
        hooks[hookCount++]={address,PPC_LOOKUP_FUNC(rt.base,address),kind};
    }
public:
    std::function<void(uint32_t,const PPCContext&,PPCContext&,uint8_t*,uint32_t)> closeProbe;
    HANDLE completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    AllocationAudit(Runtime& runtime,uint32_t ga,uint32_t ra,uint32_t workerId):
        rt(runtime),groupAllocator(ga),readerAllocator(ra),worker(workerId) {
        need(completed && !current,"Reader observer setup failed");
        const auto gv=PPCLoadU32(rt.base,ga),rv=PPCLoadU32(rt.base,ra);
        add(PPCLoadU32(rt.base,gv+4),Kind::Allocate);add(PPCLoadU32(rt.base,rv+8),Kind::Allocate);
        add(PPCLoadU32(rt.base,gv+12),Kind::Release);add(PPCLoadU32(rt.base,rv+12),Kind::Release);
        add(0x8233D980,Kind::Retirement);
        const std::array<PPCFunc*,5> thunks={dispatch<0>,dispatch<1>,dispatch<2>,dispatch<3>,dispatch<4>};
        current=this;
        for(size_t i=0;i<hookCount;++i) PPC_LOOKUP_FUNC(rt.base,hooks[i].address)=thunks[i];
        std::printf("Original allocators G=%08X/vtable=%08X reader=%08X/vtable=%08X; %zu forwarded dispatch entries\n",ga,gv,ra,rv,hookCount);
    }
    ~AllocationAudit() {
        // On a failed assertion, cancel/join guest workers before removing
        // their observer. A successful run has already joined via the root.
        rt.stopThreads();
        for(size_t i=0;i<hookCount;++i)PPC_LOOKUP_FUNC(rt.base,hooks[i].address)=hooks[i].body;
        current=nullptr;if(completed)CloseHandle(completed);
    }
    void begin(uint32_t value,ReaderProfile nextProfile) {
        std::lock_guard lock(mutex);
        for(size_t i=0;i<used;++i) need(!records[i].live,"Previous reader cycle still live");
        need(value==cycle+1 && value<=cycles && ResetEvent(completed),"Invalid reader cycle/event reset");
        cycle=value;profile=nextProfile;
    }
    void beginPair(uint32_t value,ReaderProfile nextProfile) {
        std::lock_guard lock(mutex);need(value==cycle+1&&value<=cycles,"Invalid original profile triple cycle");
        for(size_t i=0;i<used;++i)if(records[i].live)
            need(records[i].cycle<value&&!records[i].freeing,"Unexpected live allocation during profile triple construction");
        need(ResetEvent(completed),"Original pair event reset failed");cycle=value;profile=nextProfile;
    }
    void resetCompletion(){std::lock_guard lock(mutex);need(ResetEvent(completed),"Original pair completion reset failed");}
    // Disarm tracking under original Q40 after all15 actual reader-role frees.
    // Shared dispatch callbacks keep forwarding until root/OS join; removal is
    // never justified solely by the Q lock or this reader-role allocation set.
    void disarm(){
        std::lock_guard lock(mutex);need(tracking&&used==records.size(),"Profile observer disarm scope differs");
        for(size_t i=0;i<used;++i)need(!records[i].live&&!records[i].freeing,"Profile observer disarmed with live reader storage");
        tracking=false;
    }
    Allocation associate(uint32_t address,Role role,uint64_t parent=0) {
        std::lock_guard lock(mutex);
        for(size_t i=0;i<used;++i) if(records[i].address==address && records[i].live) {
            auto& r=records[i];need(r.role==role && r.cycle==cycle,"Allocation role/generation mismatch");
            r.parent=parent;return r;
        }
        throw Failure("Original constructor returned storage without a forwarded allocation");
    }
    // Only addresses in still-live, matching allocation generations may be
    // inspected by this fixture. This performs NO read or production claim.
    bool canInspect(uint64_t group,uint64_t manager,uint32_t ring,uint32_t bytes) {
        std::lock_guard lock(mutex);
        if(!group || !manager || group>used || manager>used || !bytes) return false;
        const auto& g=records[size_t(group-1)];const auto& m=records[size_t(manager-1)];
        return g.live && !g.freeing && m.live && !m.freeing && m.parent==group &&
            g.role==Role::Group && m.role==Role::Manager &&
            uint64_t(ring)>=uint64_t(g.address)+profileForCycle(g.cycle).ringOffset() &&
            uint64_t(ring)+bytes<=uint64_t(g.address)+g.bytes;
    }
    std::array<Allocation,totalAllocations> snapshot() {
        std::lock_guard lock(mutex);need(!error,error?error:"Reader observer error");return records;
    }
};
AllocationAudit* AllocationAudit::current=nullptr;

// Stack-only guards. No destructor allocates, validates a pointer, calls guest
// code, or changes TLS. The command pointer belongs to the held original Q lock.
struct ReaderHostRestore {
    uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    ~ReaderHostRestore() noexcept {PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
};
struct ReaderPPCContextRestore {
    PPCContext& target;PPCContext saved;
    explicit ReaderPPCContextRestore(PPCContext& value) noexcept:target(value){std::memcpy(&saved,&value,sizeof(saved));}
    ~ReaderPPCContextRestore() noexcept {std::memcpy(&target,&saved,sizeof(saved));}
};
struct ReaderProbeRestore {
    ReaderPPCContextRestore context;
    uint8_t* const command;
    std::array<uint8_t,8> bytes{};
    ReaderProbeRestore(PPCContext& value,uint8_t* memory) noexcept:context(value),command(memory){std::memcpy(bytes.data(),memory,bytes.size());}
    ~ReaderProbeRestore() noexcept {std::memcpy(command,bytes.data(),bytes.size());}
};
struct ReaderEvidenceFile {
    std::filesystem::path path;
    ReaderEvidenceFile() {
        for(uint32_t i=0;i<256;++i) {
            const auto candidate=std::filesystem::current_path()/
                ("Simpsons-reader-profiles-"+std::to_string(GetCurrentProcessId())+"-"+
                 std::to_string(GetTickCount64())+"-"+std::to_string(i)+".jsonl");
            const auto file=CreateFileW(candidate.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);path=candidate;return;}
            need(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,
                 "Reader close receipt reservation failed");
        }
        throw Failure("Reader close receipt collision budget exhausted");
    }
    ~ReaderEvidenceFile(){std::printf("AUDIT_READER_PROFILE_RECEIPTS preserved=%s\n",path.string().c_str());}
};

// Source-only candidate: sampled only after the same genuine D980 body returns.
// Comparison mismatches never throw on the worker; finish() runs after root join.
class ProfileCloseDiagnostics {
    Runtime& rt;EngineAudioReader& readers;const std::filesystem::path& path;
    uint32_t root,worker;
    struct Owner {uint32_t group{},identifier{};bool closed{};size_t rows{};uint64_t sequence{},generation{};};
    std::array<Owner,3> owners{};
    std::array<std::string,3> comparisons{};
    size_t assertions{};std::vector<std::string> errors;bool exceptional=false;
    void check(bool value,const std::string& why){++assertions;if(!value)errors.push_back(why);}
    static std::string field(const std::string& row,const char* key) {
        const auto label="\""+std::string(key)+"\":\"";const auto at=row.find(label);
        if(at==std::string::npos)return {};const auto start=at+label.size();auto end=start;
        for(;end<row.size();++end){if(row[end]=='\\'){++end;continue;}if(row[end]=='"')break;}
        return end<row.size()?row.substr(start,end-start):std::string{};
    }
    static std::string token(const std::string& value,const char* key) {
        const auto label=std::string(key)+"=";size_t at=0;
        while((at=value.find(label,at))!=std::string::npos){
            if(!at||value[at-1]==' '){const auto start=at+label.size(),end=value.find(' ',start);return value.substr(start,end-start);}
            at+=label.size();
        }return {};
    }
    static std::optional<uint64_t> hex(const std::string& value) {
        if(value.empty())return {};uint64_t result=0;
        for(const auto ch:value){uint32_t digit;
            if(ch>='0'&&ch<='9')digit=uint32_t(ch-'0');else if(ch>='a'&&ch<='f')digit=uint32_t(ch-'a'+10);
            else if(ch>='A'&&ch<='F')digit=uint32_t(ch-'A'+10);else return {};
            if(result>(UINT64_MAX-digit)/16)return {};result=result*16+digit;
        }return result;
    }
    static uint64_t decimal(const std::string& row,const char* key) {
        const auto label="\""+std::string(key)+"\":";const auto at=row.find(label);if(at==std::string::npos)return 0;
        uint64_t result=0;bool seen=false;
        for(size_t i=at+label.size();i<row.size()&&row[i]>='0'&&row[i]<='9';++i){const auto digit=uint32_t(row[i]-'0');
            if(result>(UINT64_MAX-digit)/10)return 0;result=result*10+digit;seen=true;
        }return seen?result:0;
    }
    std::vector<std::string> rows() const {
        std::ifstream input(path);std::vector<std::string> result;std::string row;
        while(std::getline(input,row))result.push_back(row);return result;
    }
    struct Cursor {size_t rows{};uint64_t sequence{};};
    Cursor cursor() const {
        const auto values=rows();Cursor result{values.size(),0};
        for(const auto& row:values)if(!row.empty()&&row.back()=='}')result.sequence=std::max(result.sequence,decimal(row,"sequence"));
        return result;
    }
    std::string receipt(const Cursor& before,const char* action,const char* event) const {
        const auto values=rows();
        for(size_t i=values.size();i>before.rows;--i){const auto& row=values[i-1];
            if(decimal(row,"sequence")>before.sequence&&decimal(row,"thread")==worker&&
               field(row,"last_action")==action&&field(row,"event")==event&&field(row,"kind")=="audio-reader-release"&&
               field(row,"parameters").find("boundary=8233d980 ")==0)return row;
        }return {};
    }
    std::string parameters(size_t index) const {
        const auto profile=profileForCycle(uint32_t(index+1));
        return "boundary=8233d980 group_argument=command+4 profile_source=cached_candidate origin=stock_stream member_count=1 ring_bytes="+
            std::to_string(profile.ringSize)+" entry_capacity="+std::to_string(profile.entries)+
            " requested_entries="+std::to_string(profile.entries-3)+" allocator_override=0";
    }
    struct OwnedBytes {
        std::array<uint8_t,8> command{};std::vector<std::pair<uint32_t,std::array<uint8_t,0x30>>> groups;
        std::array<uint32_t,8> rootWords{};EngineAudioReader::Snapshot native{};
    };
    OwnedBytes owned(uint32_t command) const {
        OwnedBytes result;std::memcpy(result.command.data(),rt.pointer(command,8,false),8);
        for(const auto& owner:owners)if(!owner.closed){std::array<uint8_t,0x30> bytes{};
            std::memcpy(bytes.data(),rt.pointer(owner.group,uint32_t(bytes.size()),false),bytes.size());result.groups.emplace_back(owner.group,bytes);
        }
        constexpr uint32_t offsets[]={0x20,0x60,0xCC,0xD0,0xEC,0xF0};
        for(size_t i=0;i<std::size(offsets);++i)result.rootWords[i]=PPCLoadU32(rt.base,root+offsets[i]);
        result.rootWords[6]=PPCLoadU32(rt.base,0x82E37310);result.rootWords[7]=PPCLoadU32(rt.base,0x82E36B94);
        result.native=readers.snapshot();return result;
    }
    void unchanged(const OwnedBytes& before,uint32_t command) {
        const auto after=owned(command);
        check(before.command==after.command&&before.groups==after.groups&&before.rootWords==after.rootWords,
              "Profile diagnostic changed actual command/live group/root bytes");
        check(before.native.groups==after.native.groups&&before.native.managers==after.native.managers&&
              before.native.claims==after.native.claims&&before.native.copies==after.native.copies&&
              before.native.operations==after.native.operations,"Profile diagnostic changed native ownership counters");
    }
    void inspect(const std::string& row,size_t index,const PPCContext& context,const Cursor& before,
                 const char* action,const char* event,uint32_t phase) {
        const auto instance=field(row,"instance"),ownership=field(row,"ownership");
        check(!row.empty()&&decimal(row,"sequence")>before.sequence,"Fresh profile receipt missing");
        check(field(row,"parameters")==parameters(index),"Cached whole-producer profile differs");
        check(field(row,"last_action")==action&&field(row,"event")==event&&
              field(row,"mission")=="audio-reader-close-profile-producer"&&field(row,"asset")=="original-reader"&&
              decimal(row,"caller")==0x823395BC&&decimal(row,"thread")==worker&&worker==GetCurrentThreadId(),
              "Profile receipt caller/action/mission/worker differs");
        check(ownership=="contextQualified=1 requestReadable=1 requestFunctionMatches=1 requestContextQualified=1 candidateRegisteredGroup=1 registeredManager=0 registeredGroup=1 candidateGroupPhase="+
              std::to_string(phase)+" groupPhase="+std::to_string(phase),"Profile receipt ownership differs");
        for(const auto& value:{std::pair{"r3",context.r3.u32},std::pair{"r4",context.r4.u32},
            std::pair{"command",context.r3.u32},std::pair{"r31",context.r31.u32},std::pair{"r30",context.r30.u32},
            std::pair{"r29",context.r29.u32},std::pair{"function",0x8233D980u},std::pair{"candidate_group",owners[index].group},
            std::pair{"group",owners[index].group},std::pair{"root",root},std::pair{"reader_identifier",owners[index].identifier},
            std::pair{"create_caller",0x8233062Cu}})
            check(hex(token(instance,value.first))==value.second,"Profile instance lost "+std::string(value.first));
        for(const auto& value:{std::pair{"rawR3",context.r3.u64},std::pair{"rawR4",context.r4.u64},
            std::pair{"rawR31",context.r31.u64},std::pair{"rawR30",context.r30.u64},std::pair{"rawR29",context.r29.u64}})
            check(hex(token(instance,value.first))==value.second,"Profile instance lost raw lane "+std::string(value.first));
        const auto generation=hex(token(instance,"candidateGroupGeneration"));
        check(generation&&*generation&&hex(token(instance,"groupGeneration"))==generation,"Profile native generation differs");
        if(generation){if(owners[index].generation)check(owners[index].generation==*generation,"Profile cached generation changed");else owners[index].generation=*generation;}
        check(hex(token(instance,"queueBase"))==PPCLoadU32(rt.base,root+0x20)&&
              hex(token(instance,"queueUsed"))==PPCLoadU32(rt.base,root+0xD0)&&
              hex(token(instance,"queueCapacity"))==PPCLoadU32(rt.base,root+0xCC)&&
              hex(token(instance,"queueEnd"))==uint64_t(PPCLoadU32(rt.base,root+0x20))+PPCLoadU32(rt.base,root+0xD0),
              "Profile original executor queue snapshot differs");
    }
    std::string duplicate(size_t index,PPCContext& returned,uint8_t* base,uint32_t command,uint64_t inherited) {
        ReaderHostRestore host;auto* const memory=rt.pointer(command,8,true);ReaderProbeRestore restore(returned,memory);
        const auto at=cursor();returned.r3.u64=command;returned.r4.u64=inherited;
        rt.resourceAudit.action("reader-close-profile-comparison");
        const auto bytes=owned(command);PPCContext before;std::memcpy(&before,&returned,sizeof(before));
        const auto csr=(host.csr&~uint32_t(PPCFPSCRRegister::RoundMask))|uint32_t(PPCFPSCRRegister::GuestToHost[PPC_ROUND_UP]);
        PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(0x8233D980);
        bool threw=false;std::string reason;
        try{readers.observe(0x8233D980,returned,base);}catch(const Failure& error){threw=true;reason=error.what();}
        const auto afterCSR=PPCFPSCRRegister::getcsr();const auto afterError=GetLastError();
        check(threw&&reason=="Duplicate/stale original reader group retirement","Profile duplicate changed its genuine failure frontier");
        check(!std::memcmp(&before,&returned,sizeof(before))&&afterCSR==csr&&afterError==0x8233D980,
              "Profile duplicate changed complete PPC/host state");unchanged(bytes,command);
        if(threw)rt.resourceAudit.failure(reason); // Only the actual caught failure gets a failure row.
        const auto row=receipt(at,"reader-close-profile-comparison","failure");
        inspect(row,index,before,at,"reader-close-profile-comparison","failure",3);
        check(field(row,"reason")=="Duplicate/stale original reader group retirement","Profile failure snapshot lost reason");
        return row;
    }
public:
    ProfileCloseDiagnostics(Runtime& runtime,EngineAudioReader& source,const std::filesystem::path& output,uint32_t q,uint32_t id):
        rt(runtime),readers(source),path(output),root(q),worker(id){}
    void owner(size_t index,uint32_t group,uint32_t identifier){need(index<owners.size(),"Profile owner index invalid");owners[index].group=group;owners[index].identifier=identifier;}
    void queued(size_t index){const auto at=cursor();owners[index].rows=at.rows;owners[index].sequence=at.sequence;rt.resourceAudit.action("reader-close-profile-original");}
    void close(uint32_t cycle,const PPCContext& incoming,PPCContext& returned,uint8_t* base,uint32_t group) noexcept {
        ReaderHostRestore host;ReaderPPCContextRestore restore(returned);
        try{
            const auto index=size_t(cycle-1);if(index>=owners.size()){exceptional=true;return;}
            const bool wasClosed=owners[index].closed;
            struct MarkClosed {bool& value;~MarkClosed() noexcept {value=true;}} markClosed{owners[index].closed};
            // Even an unexpected diagnostic exception excludes this owner from
            // later live-byte snapshots; the same real body already returned.
            check(!wasClosed&&owners[index].group==group,"Profile whole close selected another owner");
            check(incoming.r3.u32==incoming.r30.u32&&incoming.r31.u32==root&&incoming.lr==0x823395BC,
                  "Profile whole close bypassed genuine executor");
            const Cursor at{owners[index].rows,owners[index].sequence};
            const auto original=receipt(at,"reader-close-profile-original","encounter");
            if(index!=1)inspect(original,index,incoming,at,"reader-close-profile-original","encounter",1);
            else check(original.empty(),"Equivalent genuine Live close failed encounter deduplication");
            const auto control=duplicate(index,returned,base,incoming.r3.u32,0);
            // A closes while C's genuinely different numeric profile is still live.
            const uint64_t misleading=0x7654321000000000ull|(index<2?owners[2].group:owners[0].group);
            const auto collision=duplicate(index,returned,base,incoming.r3.u32,misleading);
            check(field(control,"group")==field(collision,"group")&&field(control,"parameters")==field(collision,"parameters")&&
                  field(control,"ownership")==field(collision,"ownership")&&field(control,"instance")!=field(collision,"instance"),
                  "Inherited r4 selected another numeric profile/key");
            comparisons[index]=control;
        }catch(...){exceptional=true;} // Never abort real guest cleanup for a diagnostic discrepancy.
        constexpr const char* freeActions[]={"reader-close-profile-first-free","reader-close-profile-second-free","reader-close-profile-third-free"};
        // const-char conversion can allocate before entering action() noexcept;
        // contain that diagnostic exception without terminating the real worker.
        try{if(cycle&&cycle<=owners.size())rt.resourceAudit.action(freeActions[cycle-1]);}
        catch(...){exceptional=true;} // Fresh actual free receipt, or a deferred diagnostic mismatch.
    }
    void finish(){
        check(!exceptional,"Unexpected profile diagnostic exception (cleanup remained forwarded)");
        const auto values=rows();
        for(size_t i=0;i<owners.size();++i){
            check(owners[i].closed&&!comparisons[i].empty(),"Whole producer profile was not closed/compared");
            std::optional<uint64_t> freed;
            constexpr const char* freeActions[]={"reader-close-profile-first-free","reader-close-profile-second-free","reader-close-profile-third-free"};
            for(const auto& row:values)if(decimal(row,"sequence")>owners[i].sequence&&decimal(row,"thread")==worker&&
                decimal(row,"caller")==0x8233D5B0&&field(row,"mission")=="audio-reader-close-profile-producer"&&
                field(row,"last_action")==freeActions[i]&&
                field(row,"event")=="encounter"&&field(row,"kind")=="audio-reader-release"&&
                field(row,"parameters")=="boundary=8233d5e8"&&hex(token(field(row,"instance"),"group"))==owners[i].group){
                freed=hex(token(field(row,"instance"),"groupGeneration"));break;
            }
            check(freed&&*freed==owners[i].generation,"Cached close profile generation differs from genuine free");
        }
        check(field(comparisons[0],"group")==field(comparisons[1],"group")&&field(comparisons[0],"instance")!=field(comparisons[1],"instance"),
              "Equal whole-producer profiles/natural serials did not share key with distinct instances");
        check(field(comparisons[0],"group")!=field(comparisons[2],"group"),"Different whole-producer numeric profile was merged");
        check(owners[1].identifier==owners[0].identifier+1&&owners[2].identifier==owners[1].identifier+1&&
              owners[0].generation&&owners[1].generation&&owners[2].generation&&
              owners[0].generation!=owners[1].generation&&owners[0].generation!=owners[2].generation&&owners[1].generation!=owners[2].generation,
              "Natural whole-producer serials/native generations were not distinct");
        checks+=assertions;for(const auto& error:errors)std::fprintf(stderr,"[READER PROFILE DIAGNOSTIC DIFFERENCE] %s\n",error.c_str());
        need(errors.empty(),"Cached close profile comparisons differ after actual15 frees/service shutdown/root join");
        std::printf("AUDIT_READER_CLOSE_PROFILE_PRODUCER origin=stock_stream equivalent_profiles=same_key natural_serials=distinct_instances different_profile=different_key actual_duplicate_failures=fresh r4_collision=instance_only source_claim_releases=3 actual_group_frees=3 actual_manager_frees=3 actual_allocations=15 actual_frees=15 services_shutdown=passed root_join=passed catalog_link=unproven receipts=%s\n",path.string().c_str());
    }
};

void waitRetired(Runtime& rt,const std::shared_ptr<EngineAudioReader>& readers) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    for(;;){rt.checkRunning();const auto s=readers->snapshot();
        if(!s.groups && !s.managers && !s.claims && !s.operations) break;
        need(std::chrono::steady_clock::now()<deadline,"Original worker failed to retire producer group");Sleep(10);}
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    try {
        need(argc==2 || argc==3,"Original image path and optional independent case index required");
        size_t firstCase=0,endCase=std::size(cases);
        if(argc==3) {
            const auto last=argv[2]+std::char_traits<char>::length(argv[2]);
            const auto result=std::from_chars(argv[2],last,firstCase);
            need(result.ec==std::errc{} && result.ptr==last && firstCase<std::size(cases),"Invalid independent producer case index");
            endCase=firstCase+1;
        }
        std::optional<ReaderEvidenceFile> evidenceFile;
        if(firstCase<=2&&endCase>2)evidenceFile.emplace();
        Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        if(evidenceFile){rt.resourceAudit.configure(evidenceFile->path);rt.resourceAudit.mission("audio-reader-close-profile-producer");}
        originalEvidence(rt,argv[1]);const auto entry=original;bool source=false,started=false;uint32_t manager=0;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext& c,uint8_t*) {
            const auto v=rt.engineAudioOutput->view();need(v.nativeEngine && v.configured && v.identity,"Real audio source missing");
            if(pc==0x82345920){need(!source && !v.active && !v.workerId,"Audio source order changed");source=true;return;}
            need(pc==0x828166FC && source && v.active && v.workerId && v.event,"Original audio startup missing");
            manager=c.r31.u32;need(manager && PPCLoadU32(rt.base,manager+0x348)==v.root,
                                  "Original startup lost the audio manager/root association");throw StartupObserved{};
        };
        try{runOriginal(original,rt.base);}catch(const StartupObserved&){started=true;}
        rt.audioBoundaryObserver={};need(started,"Real original startup did not release its Q lock");
        const auto v=rt.engineAudioOutput->view();const uint32_t q=v.root;
        std::shared_ptr<KernelHandle> worker;
        {std::lock_guard lock(rt.threadMutex);for(const auto& t:rt.threads) if(t->id==v.workerId) worker=t->object;}
        need(worker && GetThreadId(worker->native)==v.workerId,"Original Dac worker missing");
        EngineCpuCalls cpu(entry,rt.base);auto readers=audioReaders(rt,true);Inputs inputs(rt);
        // The startup observation precedes the stock audio manager's virtual
        // initialization. Run that whole method on the manager captured from
        // startup: its real allocator+8 installs the factory, asset manager and
        // Q-linked clock needed by the ordinary stream constructor.
        const uint32_t allocator=PPCLoadU32(rt.base,manager+0x330);
        need(allocator && !PPCLoadU32(rt.base,0x82E36C90) && !PPCLoadU32(rt.base,0x82E36C94),
             "Original stream service initialization state changed");
        const PreservedABI initializedABI(cpu.registers());
        need(cpu.invoke(0x8280FD70,manager)==1,"Original audio manager failed to initialize stream services");
        initializedABI.verify(cpu.registers());
        const uint32_t assets=PPCLoadU32(rt.base,0x82E36C90),clock=PPCLoadU32(rt.base,0x82E36C94);
        need(PPCLoadU32(rt.base,0x82E36C9C)==allocator+8 && PPCLoadU32(rt.base,0x82E36CA0)==16 &&
             PPCLoadU32(rt.base,0x82E36BEC)==0x82334C18 && PPCLoadU32(rt.base,0x82E36BF0)==0x82334CA0 &&
             assets && clock && PPCLoadU32(rt.base,assets)==0x821DCA7C && PPCLoadU32(rt.base,clock)==0x821DC738,
             "Original stream services lost their factory or genuine constructed owners");
        ProducerObserver observe(rt,readers);
        // Reverse destruction joins/restores the real observer before its
        // captured diagnostic object can be destroyed on any exceptional path.
        std::unique_ptr<ProfileCloseDiagnostics> profileDiagnostics;
        std::unique_ptr<AllocationAudit> profileAudit;
        constexpr uint32_t bytes=64;std::array<uint8_t,bytes> expected{};expected[3]=bytes;
        for(size_t i=4;i<bytes;++i) expected[i]=uint8_t(3*i+7);
        std::copy(expected.begin(),expected.end(),rt.pointer(inputs.address,bytes,true));
        const uint32_t out=inputs.address+512;
        for(size_t caseIndex=firstCase;caseIndex<endCase;++caseIndex) {
            const auto& c=cases[caseIndex];
            if(caseIndex==11) {
                std::vector<uint32_t> voices;voices.reserve(65);
                {
                    RootLock lock(cpu,q);const PreservedABI abi(cpu.registers());
                    for(size_t liveCount=0;liveCount<65;++liveCount) {
                        const auto before=observe.calls;const uint32_t identifier=PPCLoadU32(rt.base,0x82D08ACC);
                        PPCStoreU32(rt.base,out,0);cpu.registers().f1.f64=double(c.duration);
                        need(cpu.invoke(0x82330540,0,out,c.entries,0,c.bitrate)==0,"Original concurrent stream factory failed");
                        abi.verify(cpu.registers());
                        const uint32_t voice=PPCLoadU32(rt.base,out),g=voice?PPCLoadU32(rt.base,voice+0x148):0;
                        need(voice && g && observe.calls==before+1 && !observe.ring && observe.identifier==identifier &&
                             PPCLoadU32(rt.base,voice+4)==identifier && PPCLoadU32(rt.base,0x82D08ACC)==identifier+1,
                             "Original concurrent reader lost factory/identifier/group association");
                        const uint32_t h=PPCLoadU32(rt.base,g+0x44),m=PPCLoadU32(rt.base,h+4);
                        need(PPCLoadU32(rt.base,m+0x64)==g+0x50 && PPCLoadU32(rt.base,m+0x6C)==g+0x50 &&
                             !PPCLoadU32(rt.base,m+0x208),"Original concurrent empty reader has nonempty storage");
                        need(!cpu.invoke(0x8238D568,h),"Original concurrent empty reader returned an unrequested node");
                        abi.verify(cpu.registers());cpu.invoke(0x8238D480,h);abi.verify(cpu.registers());
                        voices.push_back(voice);
                        const auto snapshot=readers->snapshot();
                        need(snapshot.groups==voices.size() && snapshot.managers==voices.size() &&
                             !snapshot.claims && !snapshot.operations && !snapshot.copies,
                             "Original concurrent reader ownership changed");
                    }
                    need(voices.size()==65 && PPCLoadU32(rt.base,0x82E37310),"Original factory did not retain 65 actual reader groups");
                    for(const uint32_t voice:voices) {
                        need(uint64_t(PPCLoadU32(rt.base,q+0xD0))+8<=PPCLoadU32(rt.base,q+0xCC),
                             "Original concurrent stream retirement has no command slot");
                        cpu.invoke(0x82333EC8,voice,1);abi.verify(cpu.registers());
                    }
                }
                waitRetired(rt,readers);
                {RootLock lock(cpu,q);need(!PPCLoadU32(rt.base,0x82E37310),"Original concurrent group links survived destruction");}
                std::puts("Original producer: 65 simultaneously live empty readers; whole factory/empty-claim/reset/destructor and worker retirement");
                continue;
            }
            EngineAudioReader::OwnedClaim copy;
            {
                RootLock lock(cpu,q);const auto before=observe.calls;
                const uint32_t identifier=PPCLoadU32(rt.base,0x82D08ACC);
                PPCStoreU32(rt.base,out,0);cpu.registers().f1.f64=double(c.duration);
                const PreservedABI abi(cpu.registers());
                need(cpu.invoke(0x82330540,0,out,c.entries,0,c.bitrate)==0,"Original stream factory failed");abi.verify(cpu.registers());
                need(observe.calls==before+1 && observe.ring==c.ring && observe.entryCount==c.entries+3 && observe.identifier==identifier,
                     "Original whole producer did not calculate the qualified case");
                const uint32_t voice=PPCLoadU32(rt.base,out);need(voice!=0,"Original stream factory returned no owner");
                rt.pointer(voice,0x15C,false);const uint32_t g=PPCLoadU32(rt.base,voice+0x148);
                need(g && PPCLoadU32(rt.base,voice)==0x821DC850 && PPCLoadU32(rt.base,voice+4)==identifier &&
                     PPCLoadU32(rt.base,0x82D08ACC)==identifier+1 && PPCLoadU32(rt.base,g+0x1C)==identifier,
                     "Original identifier/group attachment continuation changed");
                const uint32_t h=PPCLoadU32(rt.base,g+0x44),m=PPCLoadU32(rt.base,h+4),ring=g+0x50;
                need(PPCLoadU32(rt.base,g)==q && !PPCLoadU32(rt.base,g+0x18) && PPCLoadU32(rt.base,m+0x64)==ring &&
                     PPCLoadU32(rt.base,m+0x6C)==ring+c.ring && PPCLoadU32(rt.base,m+0x3C)==c.entries+3 &&
                     PPCLoadU32(rt.base,m+0x208)==((c.ring/6+0x7FF)&~0x7FFu),"Original dynamic containing-ring layout changed");
                const uint32_t threshold=(c.ring/6+0x7FF)&~0x7FFu;
                const bool canRequest=int64_t(c.ring)-16>=threshold;
                if(canRequest) {
                    const uint32_t token=cpu.invoke(0x8238D228,h,inputs.address,bytes,0x823412C0,voice);abi.verify(cpu.registers());
                    const uint32_t node=cpu.invoke(0x8238D568,h);abi.verify(cpu.registers());
                    need(token && node && PPCLoadU32(rt.base,node)==token,"Original producer's member failed to request/claim");
                    copy=readers->copy(h,node,voice,token);readers->validate(copy,true);
                    need(copy.group==g && copy.manager==m && copy.owner==voice && copy.bytes.size()==bytes &&
                         std::equal(copy.bytes.begin(),copy.bytes.end(),expected.begin()),"Real producer claim copy/provenance changed");
                    cpu.invoke(0x8238D640,h,node);abi.verify(cpu.registers());readers->validateReleased(copy);
                } else {
                    // The original scheduler compares free bytes after 16-byte
                    // slack with the rounded watermark, independently of the
                    // smaller requested payload. A nonempty undersized ring
                    // retains a genuine request until reset cancels it.
                    uint32_t pendingEntry=0;
                    if(c.ring) {
                        const uint32_t token=cpu.invoke(0x8238D228,h,inputs.address,bytes,0x823412C0,voice);
                        abi.verify(cpu.registers());
                        pendingEntry=PPCLoadU32(rt.base,m+0x98);
                        need(token && pendingEntry && PPCLoadU32(rt.base,pendingEntry)==token &&
                             PPCLoadU32(rt.base,pendingEntry+4)==2 && PPCLoadU32(rt.base,m+0x70)==2 &&
                             !PPCLoadU32(rt.base,h+0xC),"Original below-watermark request did not defer with its real token");
                    }
                    need(!cpu.invoke(0x8238D568,h),"Original small/empty reader returned an unrequested node");
                    abi.verify(cpu.registers());cpu.invoke(0x8238D480,h);abi.verify(cpu.registers());
                    if(pendingEntry) need(PPCLoadU32(rt.base,pendingEntry+4)==4 && !PPCLoadU32(rt.base,m+0x70) &&
                                          !PPCLoadU32(rt.base,h+0xC),"Original reset did not cancel the deferred real request");
                    const auto snapshot=readers->snapshot();
                    need(snapshot.groups==1 && snapshot.managers==1 && !snapshot.claims && !snapshot.operations && !snapshot.copies,
                         "Original small/empty reader use/reset changed ownership");
                    bool unregisteredRejected=false;
                    try {readers->copy(h,ring,voice,1);}catch(const Failure&){unregisteredRejected=true;}
                    need(unregisteredRejected,"Small/empty reader accepted an unregistered compressed payload");
                }
                const uint32_t used=PPCLoadU32(rt.base,q+0xD0),capacity=PPCLoadU32(rt.base,q+0xCC);
                need(uint64_t(used)+8<=capacity,"Original stream destructor has no retirement command slot");
                cpu.invoke(0x82333EC8,voice,1);abi.verify(cpu.registers()); // Real voice frees + original G deferred retirement.
                std::printf("Original producer duration=%.9g bitrate=%u entries=%u -> ring=%X; real stream/group/%s/destructor\n",
                            double(c.duration),c.bitrate,c.entries,c.ring,canRequest?"claim/release":c.ring?"deferred-request/reset":"empty-claim/reset");
            }
            waitRetired(rt,readers);
            {RootLock lock(cpu,q);need(!PPCLoadU32(rt.base,0x82E37310),"Original group link survived stream destruction");}
            need(std::equal(copy.bytes.begin(),copy.bytes.end(),expected.begin()),"Owned claim bytes changed after real stream/group free");
            if(caseIndex==2) {
                // This extra exercise preserves the original Case2 above. A/B
                // use exactly Case2; C uses the genuine different Case3 profile.
                std::array<uint32_t,3> voices{},groups{},identifiers{};
                uint32_t previousHead=0,pending=0;
                {
                    RootLock lock(cpu,q);previousHead=PPCLoadU32(rt.base,0x82E37310);pending=PPCLoadU32(rt.base,q+0xF0);
                    need(!previousHead,"Profile triple began with another live reader group");
                    profileAudit=std::make_unique<AllocationAudit>(rt,PPCLoadU32(rt.base,q+0x14),PPCLoadU32(rt.base,0x82E36B94),v.workerId);
                    profileDiagnostics=std::make_unique<ProfileCloseDiagnostics>(rt,*readers,evidenceFile->path,q,v.workerId);
                    profileAudit->closeProbe=[&](uint32_t cycle,const PPCContext& incoming,PPCContext& returned,uint8_t* base,uint32_t group){
                        profileDiagnostics->close(cycle,incoming,returned,base,group);
                    };
                    for(size_t index=0;index<voices.size();++index){
                        const auto& selected=cases[index<2?2:3];const auto profile=profileForCycle(uint32_t(index+1));
                        profileAudit->beginPair(uint32_t(index+1),profile);const PreservedABI abi(cpu.registers());
                        const auto before=observe.calls;identifiers[index]=PPCLoadU32(rt.base,0x82D08ACC);
                        PPCStoreU32(rt.base,out,0);cpu.registers().f1.f64=double(selected.duration);
                        need(cpu.invoke(0x82330540,0,out,selected.entries,0,selected.bitrate)==0,"Original profile triple factory failed");abi.verify(cpu.registers());
                        voices[index]=PPCLoadU32(rt.base,out);need(voices[index]!=0,"Original profile triple returned no voice");
                        groups[index]=PPCLoadU32(rt.base,voices[index]+0x148);need(groups[index]!=0,"Original profile triple returned no group");
                        need(observe.calls==before+1&&observe.ring==selected.ring&&observe.entryCount==selected.entries+3&&
                             observe.identifier==identifiers[index]&&PPCLoadU32(rt.base,voices[index]+4)==identifiers[index]&&
                             PPCLoadU32(rt.base,groups[index]+0x1C)==identifiers[index]&&
                             PPCLoadU32(rt.base,0x82D08ACC)==identifiers[index]+1,"Original profile triple lost calculated/natural identity");
                        const auto ga=profileAudit->associate(groups[index],Role::Group);
                        const auto h=PPCLoadU32(rt.base,groups[index]+0x44),m=PPCLoadU32(rt.base,h+4);
                        const auto ma=profileAudit->associate(m,Role::Manager,ga.generation);
                        profileAudit->associate(h,Role::Handle,ma.generation);
                        profileAudit->associate(PPCLoadU32(rt.base,m+0x38),Role::Entries,ma.generation);
                        profileAudit->associate(PPCLoadU32(rt.base,m+0x40),Role::Filter,ma.generation);
                        need(ga.bytes==profile.extent()&&PPCLoadU32(rt.base,groups[index])==q&&!PPCLoadU32(rt.base,groups[index]+0x18)&&
                             PPCLoadU32(rt.base,m+0x3C)==profile.entries&&PPCLoadU32(rt.base,m+0x64)==groups[index]+0x50&&
                             PPCLoadU32(rt.base,m+0x6C)==groups[index]+0x50+profile.ringSize,"Actual triple group allocation/layout differs");
                        profileDiagnostics->owner(index,groups[index],identifiers[index]);
                        const auto token=cpu.invoke(0x8238D228,h,inputs.address,bytes,0x823412C0,voices[index]);abi.verify(cpu.registers());
                        const auto node=cpu.invoke(0x8238D568,h);abi.verify(cpu.registers());
                        need(token&&node&&PPCLoadU32(rt.base,node)==token,"Original profile triple could not claim actual request");
                        const auto claim=readers->copy(h,node,voices[index],token);readers->validate(claim,true);
                        need(claim.group==groups[index]&&claim.manager==m&&claim.owner==voices[index]&&
                             claim.bytes.size()==bytes&&std::equal(claim.bytes.begin(),claim.bytes.end(),expected.begin()),"Actual profile triple claim/provenance differs");
                        cpu.invoke(0x8238D640,h,node);abi.verify(cpu.registers());readers->validateReleased(claim);
                        const auto snapshot=readers->snapshot();
                        need(snapshot.groups==index+1&&snapshot.managers==index+1&&!snapshot.claims&&!snapshot.copies&&!snapshot.operations,
                             "Profile triple ownership or real claim release differs");
                    }
                    need(voices[0]!=voices[1]&&voices[0]!=voices[2]&&voices[1]!=voices[2]&&
                         groups[0]!=groups[1]&&groups[0]!=groups[2]&&groups[1]!=groups[2],"Triple original live owners alias");
                }
                for(size_t index=0;index<voices.size();++index){
                    {
                        RootLock lock(cpu,q);profileAudit->resetCompletion();const PreservedABI abi(cpu.registers());
                        const auto used=PPCLoadU32(rt.base,q+0xD0),capacity=PPCLoadU32(rt.base,q+0xCC),commands=PPCLoadU32(rt.base,q+0x20);
                        need(uint64_t(used)+8<=capacity&&uint64_t(commands)+used+8<=0x100000000ull,"Profile triple has no original close command slot");
                        const auto command=commands+used;rt.pointer(command,8,true);profileDiagnostics->queued(index);
                        cpu.invoke(0x82333EC8,voices[index],1);abi.verify(cpu.registers());
                        need(PPCLoadU32(rt.base,command)==0x8233D980&&PPCLoadU32(rt.base,command+4)==groups[index]&&
                             PPCLoadU32(rt.base,q+0xD0)==used+8&&PPCLoadU32(rt.base,q+0xF0)==pending,
                             "Actual voice destructor did not publish original G close command");
                        need(WaitForSingleObject(profileAudit->completed,0)==WAIT_TIMEOUT,"Actual profile close ran under held Q lock");
                    }
                    const HANDLE waits[]={profileAudit->completed,rt.stopEvent};const auto wait=WaitForMultipleObjects(2,waits,FALSE,8000);rt.checkRunning();
                    need(wait==WAIT_OBJECT_0,"Original profile triple group free did not complete");
                    {
                        RootLock lock(cpu,q);const auto records=profileAudit->snapshot();size_t freed=0;
                        for(const auto& allocation:records)if(allocation.cycle==index+1){
                            need(!allocation.live&&allocation.freeOrder&&allocation.freeThread==v.workerId,"Profile allocation was not actually freed by original worker");++freed;
                            if(allocation.parent)need(allocation.freeOrder<records[size_t(allocation.parent-1)].freeOrder,"Actual profile child free followed parent free");
                        }
                        need(freed==5&&PPCLoadU32(rt.base,q+0xF0)==pending&&
                             PPCLoadU32(rt.base,0x82E37310)==(index<2?groups[2]+0x24:previousHead),"Actual triple group free/list/pending state differs");
                        const auto snapshot=readers->snapshot();need(snapshot.groups==2-index&&snapshot.managers==2-index&&
                             !snapshot.claims&&!snapshot.copies&&!snapshot.operations,"Profile triple retired another live owner");
                        std::printf("AUDIT_READER_CLOSE_PROFILE_REAL_FREE case=2 cycle=%zu reader_role_allocations=5 reader_role_frees=5 worker=%u root_live=1 stale_guest_reads=0\n",index+1,v.workerId);
                    }
                }
                {
                    RootLock lock(cpu,q);const auto records=profileAudit->snapshot();size_t freed=0;
                    for(const auto& allocation:records){need(!allocation.live&&allocation.freeOrder,"Actual profile15 reader-role frees incomplete");++freed;}
                    need(freed==15,"Original profile reader-role allocation count differs");profileAudit->disarm();
                }
                // Tracking is disarmed; saved original dispatch bodies keep
                // forwarding through all later cases until real root/OS join.
                std::puts("AUDIT_READER_CLOSE_PROFILE_FUNCTIONAL_LIFETIME reader_role_groups=3 reader_role_managers=3 reader_role_allocations=15 reader_role_frees=15 actual_source_claim_releases=3 diagnostic_assertion=pending service_root_join=pending");
            }

        }
        // The real producer explicitly bypasses group construction for zero
        // duration. Preserve this distinct source path: it creates no group,
        // whereas a positive sub-byte duration above creates an empty group.
        {
            RootLock lock(cpu,q);const auto before=observe.calls;const uint32_t identifier=PPCLoadU32(rt.base,0x82D08ACC);
            cpu.registers().f1.f64=0;const PreservedABI abi(cpu.registers());
            need(cpu.invoke(0x82330540,0,out,4,0,0)==0,"Original zero-duration stream failed");abi.verify(cpu.registers());
            const uint32_t voice=PPCLoadU32(rt.base,out);
            need(voice && observe.calls==before && !PPCLoadU32(rt.base,voice+0x148) &&
                 PPCLoadU32(rt.base,voice+4)==0xFFFFFFFF && PPCLoadU32(rt.base,0x82D08ACC)==identifier,
                 "Original zero-duration branch created a reader or consumed an identifier");
            cpu.invoke(0x82333EC8,voice,1);abi.verify(cpu.registers());
        }
        readers->requireStopped();
        // Original shutdown clears and deletes both services. The clock still
        // belongs to Q here; retire it before the original root/worker teardown.
        const PreservedABI shutdownABI(cpu.registers());
        need(cpu.invoke(0x8232E068)==1,"Original stream service shutdown failed");shutdownABI.verify(cpu.registers());
        need(!PPCLoadU32(rt.base,0x82E36C90) && !PPCLoadU32(rt.base,0x82E36C94),
             "Original stream services survived their shutdown");
        cpu.invoke(0x82338FA0,q);DWORD result=~0u;
        need(WaitForSingleObject(worker->native,0)==WAIT_OBJECT_0 && GetExitCodeThread(worker->native,&result) && !result,
             "Original audio root did not join the Dac worker");
        need(!PPCLoadU32(rt.base,0x82E31BCC) && rt.engineAudio && !rt.engineAudio->ready(),"Original root retirement incomplete");
        if(profileDiagnostics){
            // Recheck the disarmed real-reader oracle after normal worker join.
            // Shared original dispatch hooks are still installed/forwarded here.
            const auto records=profileAudit->snapshot();size_t freed=0;
            for(const auto& allocation:records){need(!allocation.live&&allocation.freeOrder&&allocation.freeThread==v.workerId,
                "Profile reader-role free evidence changed before root join");++freed;}
            need(freed==15,"Joined profile reader-role15-free evidence incomplete");
            // This marker precedes all deferred profile comparison assertions.
            std::printf("AUDIT_READER_CLOSE_PROFILE_POST_JOIN case=2 reader_role_groups=3 reader_role_managers=3 reader_role_allocations=15 reader_role_frees=15 actual_source_claim_releases=3 original_service_shutdown=passed original_root_join=passed actual_worker_exit=0 diagnostic_assertion=pending receipts=%s\n",evidenceFile->path.string().c_str());
            profileDiagnostics->finish();
        }
        std::printf("PASS original audio reader producer: %zu checks; %zu of fourteen independent whole duration/bitrate/entry cases; initial watermark boundary, 128/16/0-byte rings, zero default bitrate and 65 simultaneous groups available; zero-duration bypass; strict real-frame negative probes; real claims/deferred requests/resets/destructors/worker frees; no gameplay/codec or authored-duration census claim\n",checks,endCase-firstCase);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL original audio reader producer: %s\n",e.what());return 1;}
}
