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
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace Simpsons;
size_t checks=0;
void need(bool value,const char* message) {++checks;if(!value) throw Failure(message);}
struct StartupObserved {};
struct ReaderProfile {
    uint32_t count,ringSize,entries;
    uint32_t ringOffset() const {return (0x30+0x20*count+15)&~15u;}
    uint32_t extent() const {return count*ringSize+ringOffset();}
    uint32_t chunk() const {return (ringSize/6+0x7FF)&~0x7FFu;}
};
constexpr ReaderProfile startupProfile{4,0x64000,13},streamProfile{1,0x55280,7};
constexpr ReaderProfile introProfile{1,0x47E00,7};
constexpr ReaderProfile profileForCycle(uint32_t cycle){return cycle<=2?startupProfile:(cycle<=4?streamProfile:introProfile);}
constexpr uint32_t sequentialCycles=6,cycles=8;
constexpr size_t totalAllocations=64; // Six original sequential cases plus two simultaneous intro groups.
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
    uint32_t cycle=0;
    std::array<bool,cycles> retirementSeen{};
    ReaderProfile profile=startupProfile;
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
        for(size_t i=0;i<hookCount;++i) PPC_LOOKUP_FUNC(rt.base,hooks[i].address)=hooks[i].body;
        current=nullptr;if(completed) CloseHandle(completed);
    }
    void begin(uint32_t value,ReaderProfile nextProfile) {
        std::lock_guard lock(mutex);
        for(size_t i=0;i<used;++i) need(!records[i].live,"Previous reader cycle still live");
        need(value==cycle+1 && value<=cycles && ResetEvent(completed),"Invalid reader cycle/event reset");
        cycle=value;profile=nextProfile;
    }
    void beginPair(uint32_t value,ReaderProfile nextProfile) {
        std::lock_guard lock(mutex);need(value==cycle+1&&(value==7||value==8),"Invalid original pair cycle");
        for(size_t i=0;i<used;++i)if(records[i].live)
            need(value==8&&records[i].cycle==7&&!records[i].freeing,"Unexpected live allocation during pair construction");
        need(ResetEvent(completed),"Original pair event reset failed");cycle=value;profile=nextProfile;
    }
    void resetCompletion(){std::lock_guard lock(mutex);need(ResetEvent(completed),"Original pair completion reset failed");}
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

class RootLock {
    EngineCpuCalls& cpu;
    uint32_t address;
    bool held=true;
public:
    RootLock(EngineCpuCalls& calls,uint32_t root):cpu(calls),address(root) {
        need(cpu.invoke(0x823392C8,address)==0,"Original root mutant callback acquisition failed");
    }
    void release() {if(held){need(cpu.invoke(0x823392F0,address)==1,"Original root mutant callback release failed");held=false;}}
    ~RootLock() {if(held) {try{release();}catch(...) {active->requestStop("Reader fixture lock unwind failed");}}}
};

struct PreservedABI {
    uint64_t sp,lr;
    std::array<uint64_t,18> values{};
    static auto registers(PPCContext& ctx) {
        return std::array{&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                          &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
    }
    explicit PreservedABI(PPCContext& ctx):sp(ctx.r1.u64),lr(ctx.lr) {
        auto regs=registers(ctx);for(size_t i=0;i<regs.size();++i) values[i]=regs[i]->u64;
    }
    void verify(PPCContext& ctx) const {
        need(ctx.r1.u64==sp && ctx.lr==lr,"Reader original call changed SP/LR");
        auto regs=registers(ctx);for(size_t i=0;i<regs.size();++i)
            need(regs[i]->u64==values[i],"Reader original call changed a nonvolatile GPR");
    }
};

struct GroupView {Allocation group;std::array<Allocation,4> managers;std::array<uint32_t,4> rings;};
GroupView verifyGroup(Runtime& rt,AllocationAudit& audit,uint32_t g,uint32_t q,uint32_t adapter,uint32_t oldHead,
                      ReaderProfile profile,uint32_t identifier) {
    auto* base=rt.base;
    GroupView v{};v.group=audit.associate(g,Role::Group);
    need(!(g&15) && v.group.bytes==profile.extent(),"Group allocation alignment/extent differs");
    rt.pointer(g,profile.extent(),false); // One real containing allocation; never write the rings.
    need(PPC_LOAD_U32(g)==q && PPC_LOAD_U32(g+4)==g+0x30 && PPC_LOAD_U32(g+0x18)==adapter &&
         PPC_LOAD_U32(g+0x1C)==identifier && PPC_LOAD_U8(g+0x20)==profile.count,"Original G ownership/record fields differ");
    need(PPC_LOAD_U32(0x82E37310)==g+0x24 && PPC_LOAD_U32(g+0x24)==oldHead && !PPC_LOAD_U32(g+0x28),
         "Original group insertion did not publish the correct global link");
    if(oldHead) need(PPC_LOAD_U32(oldHead+4)==g+0x24,"Previous group did not receive its original back-link");
    for(uint32_t i=0;i<profile.count;++i) {
        const uint32_t record=g+0x30+0x20*i,h=PPC_LOAD_U32(record+0x14),m=PPC_LOAD_U32(h+4);
        const uint32_t ring=g+profile.ringOffset()+i*profile.ringSize,e=PPC_LOAD_U32(m+0x38),f=PPC_LOAD_U32(m+0x40);
        v.managers[i]=audit.associate(m,Role::Manager,v.group.generation);v.rings[i]=ring;
        audit.associate(h,Role::Handle,v.managers[i].generation);
        audit.associate(e,Role::Entries,v.managers[i].generation);
        audit.associate(f,Role::Filter,v.managers[i].generation);
        need(audit.canInspect(v.group.generation,v.managers[i].generation,ring,profile.ringSize),"Live original ring lost its containing generation");
        need(!PPC_LOAD_U16(record+0x18) && !PPC_LOAD_U8(record+0x1A),"New reader record is already referenced/active");
        need(PPC_LOAD_U32(m+0x3C)==profile.entries && PPC_LOAD_U32(m+0x208)==profile.chunk() && PPC_LOAD_U32(m+0xA0)==e,
             "Original entry count/default chunk/free list differs");
        for(uint32_t offset:{0x64u,0x68u,0x88u,0x8Cu,0x90u}) need(PPC_LOAD_U32(m+offset)==ring,"Original initial ring cursor differs");
        need(PPC_LOAD_U32(m+0x6C)==ring+profile.ringSize,"Original manager ring end differs");
        for(uint32_t offset:{0x58u,0x5Cu,0x60u,0x70u,0x7Cu,0x84u,0x94u,0x98u,0x9Cu,0xA8u,0x1ACu})
            need(!PPC_LOAD_U32(m+offset),"New original reader manager has queued input/job/file state");
        need(!PPC_LOAD_U8(m+0x80) && PPC_LOAD_U32(m+0x44)==f && PPC_LOAD_U32(m+0x48)==1 &&
             PPC_LOAD_U32(m+0x4C)==h && PPC_LOAD_U32(m+0x50)==h && PPC_LOAD_U32(m+0x54)==1,
             "Original handle/filter lists differ");
        need(!PPC_LOAD_U32(h) && PPC_LOAD_U32(h+4)==m && PPC_LOAD_U32(h+8)==1 &&
             !PPC_LOAD_U32(h+12) && !PPC_LOAD_U32(h+16),"Original H constructor fields differ");
        need(!PPC_LOAD_U32(f) && !PPC_LOAD_U32(f+4) && !PPC_LOAD_U32(f+8) && PPC_LOAD_U32(f+12)==1,
             "Original filter constructor fields differ");
        for(uint32_t j=0;j<profile.entries;++j) {
            const uint32_t at=e+j*0x138;
            need(PPC_LOAD_U32(at)==j && !PPC_LOAD_U32(at+4) && !PPC_LOAD_U32(at+0x130) &&
                 PPC_LOAD_U32(at+12)==(j+1==profile.entries?0:at+0x138),"Original empty entry chain differs");
        }
        std::printf("  M%u=%08X H=%08X ring=%08X..%08X gen=%llu chunk=%X\n",i,m,h,ring,ring+profile.ringSize,
                    static_cast<unsigned long long>(v.managers[i].generation),profile.chunk());
    }
    need(uint64_t(v.rings[profile.count-1])+profile.ringSize==uint64_t(g)+v.group.bytes,"Final ring does not end at allocation boundary");
    return v;
}

// Candidate-only logging regression. The file is exclusively reserved and is
// always retained; an expected diagnostic baseline failure must preserve it.

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
                ("Simpsons-reader-close-"+std::to_string(GetCurrentProcessId())+"-"+
                 std::to_string(GetTickCount64())+"-"+std::to_string(i)+".jsonl");
            const auto file=CreateFileW(candidate.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);path=candidate;return;}
            need(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,
                 "Reader close receipt reservation failed");
        }
        throw Failure("Reader close receipt collision budget exhausted");
    }
    ~ReaderEvidenceFile(){std::printf("AUDIT_READER_CLOSE_RECEIPTS preserved=%s\n",path.string().c_str());}
};

// Native observations only. No additional original close/allocator body runs
// in these probes. Diagnostic discrepancies are deferred until real root join.
class CloseDiagnostics {
    Runtime& rt;
    EngineAudioReader& readers;
    const std::filesystem::path& path;
    uint32_t first{},second{},root{},worker{},pairIdentifier{},pairCreateCaller{};
    uint64_t firstGeneration{},secondGeneration{};
    size_t assertions{};
    std::vector<std::string> errors;
    struct Receipt {
        std::string label,row;
        uint32_t group{};
        uint64_t generation{};
    };
    std::vector<Receipt> receipts;
    std::array<bool,2> closeSeen{};
    size_t pairRowStart{};uint64_t pairSequence{};
    void check(bool value,const std::string& message){++assertions;if(!value)errors.push_back(message);}
    static std::string field(const std::string& row,const char* key) {
        const auto label="\""+std::string(key)+"\":\"";
        const auto at=row.find(label);if(at==std::string::npos)return {};
        const auto firstChar=at+label.size();auto end=firstChar;
        for(;end<row.size();++end){if(row[end]=='\\'){++end;continue;}if(row[end]=='"')break;}
        return end<row.size()?row.substr(firstChar,end-firstChar):std::string{};
    }
    static std::string token(const std::string& value,const char* key) {
        const auto label=std::string(key)+"=";size_t at=0;
        while((at=value.find(label,at))!=std::string::npos) {
            if(!at||value[at-1]==' '){const auto start=at+label.size(),end=value.find(' ',start);return value.substr(start,end-start);}
            at+=label.size();
        }
        return {};
    }
    static bool hex(const std::string& text,uint64_t expected) {
        if(text.empty())return false;uint64_t value=0;
        for(char c:text){uint32_t n;
            if(c>='0'&&c<='9')n=uint32_t(c-'0');else if(c>='a'&&c<='f')n=uint32_t(c-'a'+10);
            else if(c>='A'&&c<='F')n=uint32_t(c-'A'+10);else return false;
            if(value>(UINT64_MAX-n)/16)return false;value=16*value+n;
        }
        return value==expected;
    }
    static std::optional<uint64_t> hexValue(const std::string& text) {
        if(text.empty())return {};uint64_t value=0;
        for(char c:text){uint32_t n;
            if(c>='0'&&c<='9')n=uint32_t(c-'0');else if(c>='a'&&c<='f')n=uint32_t(c-'a'+10);
            else if(c>='A'&&c<='F')n=uint32_t(c-'A'+10);else return {};
            if(value>(UINT64_MAX-n)/16)return {};value=16*value+n;
        }
        return value;
    }
    std::vector<std::string> rows() const {
        std::ifstream stream(path);std::vector<std::string> result;std::string row;
        while(std::getline(stream,row))result.push_back(row);return result;
    }

    static uint64_t decimal(const std::string& row,const char* key) {
        const auto label="\""+std::string(key)+"\":";const auto at=row.find(label);
        if(at==std::string::npos)return 0;uint64_t value=0;size_t i=at+label.size();bool present=false;
        for(;i<row.size()&&row[i]>='0'&&row[i]<='9';++i) {
            const auto n=uint32_t(row[i]-'0');if(value>(UINT64_MAX-n)/10)return 0;value=10*value+n;present=true;
        }
        return present?value:0;
    }
    struct ReceiptCursor {size_t rows{};uint64_t sequence{};};
    ReceiptCursor cursor() const {
        const auto values=rows();ReceiptCursor result{values.size(),0};
        for(const auto& row:values)if(!row.empty()&&row.back()=='}')result.sequence=std::max(result.sequence,decimal(row,"sequence"));
        return result;
    }
    std::string lastClose(const ReceiptCursor& before,const char* action) const {
        const auto values=rows();
        for(size_t i=values.size();i>before.rows;--i) {const auto& row=values[i-1];
            if(decimal(row,"sequence")>before.sequence&&decimal(row,"thread")==worker&&
               field(row,"last_action")==action&&field(row,"kind")=="audio-reader-release"&&
               field(row,"parameters").find("boundary=8233d980")!=std::string::npos)return row;
        }
        return {};
    }
    struct OwnedBytes {
        std::array<uint8_t,8> command{};
        std::vector<std::pair<uint32_t,std::array<uint8_t,0x30>>> groups;
        std::array<uint32_t,8> rootWords{};
        EngineAudioReader::Snapshot native{};
    };
    OwnedBytes owned(uint32_t command,bool firstLive) const {
        OwnedBytes value;std::memcpy(value.command.data(),rt.pointer(command,8,false),8);
        for(const auto address:{firstLive?first:0u,second})if(address) {
            std::array<uint8_t,0x30> bytes{};std::memcpy(bytes.data(),rt.pointer(address,uint32_t(bytes.size()),false),bytes.size());
            value.groups.emplace_back(address,bytes);
        }
        constexpr uint32_t offsets[]={0x20,0x60,0xCC,0xD0,0xEC,0xF0};
        for(size_t i=0;i<std::size(offsets);++i)value.rootWords[i]=PPCLoadU32(rt.base,root+offsets[i]);
        value.rootWords[6]=PPCLoadU32(rt.base,0x82E37310);value.rootWords[7]=PPCLoadU32(rt.base,0x82E36B94);
        value.native=readers.snapshot();return value;
    }
    void unchanged(const OwnedBytes& before,uint32_t command,bool firstLive,const std::string& label) {
        const auto after=owned(command,firstLive);
        check(after.command==before.command&&after.groups==before.groups&&after.rootWords==before.rootWords,
              label+": observation mutated owned command/group/root bytes");
        check(after.native.groups==before.native.groups&&after.native.managers==before.native.managers&&
              after.native.claims==before.native.claims&&after.native.copies==before.native.copies&&
              after.native.operations==before.native.operations,label+": observation changed native reader counters");
    }
    struct Expected {bool context,readable,function,envelope,candidate,registered;uint32_t candidateGroup,phase;};
    std::string inspect(const std::string& label,const PPCContext& c,const Expected& e,
                        const char* action,const ReceiptCursor& before,const char* event) {
        const auto row=lastClose(before,action),parameters=field(row,"parameters"),ownership=field(row,"ownership"),instance=field(row,"instance");
        check(!row.empty(),label+": fresh prevalidation reader receipt missing");
        check(decimal(row,"sequence")>before.sequence,label+": borrowed an earlier receipt sequence");
        check(field(row,"last_action")==action&&field(row,"mission")=="audio-reader-close-fixture"&&
              field(row,"event")==event&&field(row,"asset")=="original-reader",label+": wrong action/mission/event/asset");
        check(decimal(row,"thread")==worker&&worker==GetCurrentThreadId(),label+": receipt belongs to another worker");
        const auto expectedParameters=e.candidate?
            "boundary=8233d980 group_argument=command+4 profile_source=cached_candidate origin=fixture_stream member_count=1 ring_bytes="+
                std::to_string(introProfile.ringSize)+" entry_capacity=7 requested_entries=4 allocator_override=0":
            std::string("boundary=8233d980 group_argument=command+4 profile_source=unknown");
        check(parameters==expectedParameters,label+": wrong cached profile/source-derived group argument");
        for(const auto& flag:{std::pair{"contextQualified",e.context},std::pair{"requestReadable",e.readable},
            std::pair{"requestFunctionMatches",e.function},std::pair{"requestContextQualified",e.envelope},
            std::pair{"candidateRegisteredGroup",e.candidate},std::pair{"registeredManager",false},std::pair{"registeredGroup",e.registered}})
            check(token(ownership,flag.first)==(flag.second?"1":"0"),label+": wrong ownership field "+flag.first);
        check(hex(token(instance,"r3"),c.r3.u32)&&hex(token(instance,"r4"),c.r4.u32)&&
              hex(token(instance,"rawR3"),c.r3.u64)&&hex(token(instance,"rawR4"),c.r4.u64),label+": raw command/inherited lanes lost");
        check(hex(token(instance,"command"),c.r3.u32)&&hex(token(instance,"r31"),c.r31.u32)&&
              hex(token(instance,"r30"),c.r30.u32)&&hex(token(instance,"r29"),c.r29.u32)&&
              hex(token(instance,"rawR31"),c.r31.u64)&&hex(token(instance,"rawR30"),c.r30.u64)&&
              hex(token(instance,"rawR29"),c.r29.u64),label+": original executor raw frame lost");
        if(e.readable) {
            check(hex(token(instance,"candidate_group"),e.candidateGroup),label+": command+4 candidate differs");
            check(hex(token(instance,"function"),e.function?0x8233D980u:0u),label+": original command function differs");
        } else check(token(instance,"function").empty()&&token(instance,"candidate_group").empty(),
                     label+": unreadable command acquired decoded fields");
        if(e.candidate) {
            check(token(ownership,"candidateGroupPhase")==std::to_string(e.phase),label+": candidate phase differs");
            const auto generation=hexValue(token(instance,"candidateGroupGeneration"));
            check(generation&&*generation,label+": cached candidate generation missing");
            if(generation)receipts.push_back({label,row,e.candidateGroup,*generation});
            check(hex(token(instance,"root"),root),label+": cached candidate root differs");
            check(hex(token(instance,"reader_identifier"),pairIdentifier)&&
                  hex(token(instance,"create_caller"),pairCreateCaller),label+": cached constructor instance differs");
        } else check(token(ownership,"candidateGroupPhase").empty()&&token(instance,"candidateGroupGeneration").empty()&&
                     token(instance,"root").empty()&&token(instance,"reader_identifier").empty()&&
                     token(instance,"create_caller").empty(),label+": unknown command acquired cached owner fields");
        // v2 logs Q fields only after the original LR/r31/r30 envelope correlates.
        // A function mismatch keeps that bounded Q snapshot but is not an owner.
        const bool queueReadable=e.context&&e.readable&&e.candidate&&c.lr==0x823395BC&&
            c.r31.u32==root&&c.r30.u32==c.r3.u32;
        if(queueReadable) {
            check(hex(token(instance,"queueBase"),PPCLoadU32(rt.base,root+0x20))&&
                  hex(token(instance,"queueUsed"),PPCLoadU32(rt.base,root+0xD0))&&
                  hex(token(instance,"queueCapacity"),PPCLoadU32(rt.base,root+0xCC))&&
                  hex(token(instance,"queueEnd"),uint64_t(PPCLoadU32(rt.base,root+0x20))+PPCLoadU32(rt.base,root+0xD0)),
                  label+": original executor envelope values differ");
        } else check(token(instance,"queueBase").empty()&&token(instance,"queueUsed").empty()&&
                     token(instance,"queueCapacity").empty()&&token(instance,"queueEnd").empty(),
                     label+": uncorrelated command read/claimed Q fields");
        if(e.registered) {
            check(token(ownership,"groupPhase")==std::to_string(e.phase)&&hex(token(instance,"group"),e.candidateGroup)&&
                  token(instance,"groupGeneration")==token(instance,"candidateGroupGeneration"),label+": qualified owner differs");
        } else check(token(ownership,"groupPhase").empty()&&token(instance,"group").empty()&&token(instance,"groupGeneration").empty(),
                     label+": unqualified command claimed a group owner");
        check(row.find("\"caller\":"+std::to_string(uint32_t(c.lr))+",")!=std::string::npos,label+": original caller lost");
        return row;
    }
    std::string probe(PPCContext& c,uint8_t* base,uint32_t command,bool firstLive,const std::string& label,
                      const char* action,const std::function<void(PPCContext&)>& mutate,const Expected& expected,
                      const char* failure,bool emitFailure=true) {
        ReaderHostRestore host; // Capture before pointer checks, file reads or formatting.
        auto* const ownedCommand=rt.pointer(command,8,true);
        ReaderProbeRestore restore(c,ownedCommand);
        const auto receiptStart=cursor();
        c.r3.u64=command;mutate(c);rt.resourceAudit.action(action);
        const auto bytes=owned(command,firstLive);PPCContext before;std::memcpy(&before,&c,sizeof(before));
        const auto csr=(host.csr&~uint32_t(PPCFPSCRRegister::RoundMask))|
            uint32_t(PPCFPSCRRegister::GuestToHost[PPC_ROUND_UP]);
        PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(0x8233D980);
        bool threw=false;std::string reason;
        try{readers.observe(0x8233D980,c,base);}catch(const Failure& error){threw=true;reason=error.what();}
        const auto afterError=GetLastError();const auto afterCSR=PPCFPSCRRegister::getcsr();
        check(!std::memcmp(&before,&c,sizeof(c)),label+": C++ observation modified complete PPC context");
        check(afterError==0x8233D980&&afterCSR==csr,label+": C++ observation modified host CSR/LastError");
        check(threw==bool(failure)&&(!failure||reason==failure),label+": functional result/diagnostic changed ("+reason+")");
        unchanged(bytes,command,firstLive,label);
        // Only actual thrown probes get failure receipts. Unknown/stale no-op
        // probes use distinct finite actions, never fabricated failures.
        if(threw&&emitFailure)rt.resourceAudit.failure(reason);
        return inspect(label,before,expected,action,receiptStart,threw&&emitFailure?"failure":"encounter");
        // restore's noexcept destructor writes only the already-checked command
        // pointer and this supplied PPC object, then host restores CSR/LastError.
    }
public:
    CloseDiagnostics(Runtime& runtime,EngineAudioReader& source,const std::filesystem::path& output):rt(runtime),readers(source),path(output){}
    void pair(uint32_t a,uint32_t b,uint32_t q,uint32_t id,uint32_t identifier,uint32_t caller){
        first=a;second=b;root=q;worker=id;pairIdentifier=identifier;pairCreateCaller=caller;
        const auto at=cursor();pairRowStart=at.rows;pairSequence=at.sequence;
    }
    void close(uint32_t cycle,const PPCContext& entry,PPCContext& returned,uint8_t* base,uint32_t group) {
        ReaderHostRestore host; // First: covers every helper/probe and exceptional path.
        ReaderPPCContextRestore actualReturn(returned);
        const bool firstClose=cycle==7;check(firstClose||cycle==8,"Pair close has a nonpair cycle");
        const auto index=firstClose?0u:1u;check(!closeSeen[index],"Pair original close body observed twice");closeSeen[index]=true;
        check(group==(firstClose?first:second)&&entry.r3.u32==entry.r30.u32&&entry.r31.u32==root&&
              entry.lr==0x823395BC&&uint64_t(entry.r29.u32)==uint64_t(PPCLoadU32(base,root+0x20))+PPCLoadU32(base,root+0xD0),
              "Pair original executor entry/caller/owned command differs");
        inspect(firstClose?"first real queued close":"second real queued close",entry,{true,true,true,true,true,true,group,1},
                firstClose?"reader-close-pair-first-original":"reader-close-pair-second-original",
                {pairRowStart,pairSequence},"encounter");
        const auto command=entry.r3.u32;
        if(firstClose) {
            const auto control=probe(returned,base,command,true,"duplicate first command","reader-close-pair-duplicate",
                [](PPCContext& c){c.r4.u64=0;},{true,true,true,true,true,true,first,3},
                "Duplicate/stale original reader group retirement");
            const auto collision=probe(returned,base,command,true,"other-live-r4 collision","reader-close-pair-duplicate",
                [&](PPCContext& c){c.r4.u64=0x7654321000000000ull|second;},{true,true,true,true,true,true,first,3},
                "Duplicate/stale original reader group retirement");
            check(field(control,"group")==field(collision,"group"),"Inherited r4 changed a stable close group");
            probe(returned,base,command,true,"unreadable command","reader-close-pair-unreadable",
                [&](PPCContext& c){c.r3.u64=0xFFFD0000;c.r4.u64=second;},{true,false,false,false,false,false,0,0},
                "Unmapped physical aperture or device MMIO");
            probe(returned,base,command,true,"wrapping command","reader-close-pair-wrapping",
                [&](PPCContext& c){c.r3.u64=0xFFFFFFFC;c.r4.u64=second;},{true,false,false,false,false,false,0,0},
                "Invalid audio reader extent");
            probe(returned,base,command,true,"changed command function","reader-close-pair-function-mismatch",
                [&](PPCContext& c){PPCStoreU32(base,command,0);c.r4.u64=second;},{true,true,false,false,true,false,first,3},
                "Duplicate/stale original reader group retirement");
            probe(returned,base,command,true,"uncorrelated original queue root","reader-close-pair-register-mismatch",
                [&](PPCContext& c){c.r31.u64=0;c.r4.u64=second;},{true,true,true,false,true,false,first,3},
                "Duplicate/stale original reader group retirement");
            probe(returned,base,command,true,"unknown group no-op","reader-close-pair-unknown",
                [&](PPCContext& c){PPCStoreU32(base,command+4,0);c.r4.u64=second;},{true,true,true,false,false,false,0,0},nullptr);
            // Diagnostic qualification must precede any command/root reads.
            probe(returned,base+1,command,true,"wrong callback base","reader-close-pair-base-mismatch",
                [&](PPCContext& c){c.r4.u64=second;},{false,false,false,false,false,false,0,0},
                "Invalid audio reader callback runtime/context");
            PPCContext copy;std::memcpy(&copy,&returned,sizeof(copy));copy.r3.u64=command;copy.r4.u64=second;
            probe(copy,base,command,true,"wrong callback context","reader-close-pair-context-mismatch",
                [](PPCContext&){},{false,false,false,false,false,false,0,0},"Invalid audio reader callback runtime/context");
        } else {
            probe(returned,base,command,false,"retired first group no-op","reader-close-pair-stale",
                [&](PPCContext& c){PPCStoreU32(base,command+4,first);c.r4.u64=second;},
                {true,true,true,false,false,false,first,0},nullptr);
        }
        check(!std::memcmp(&actualReturn.saved,&returned,sizeof(returned)),"Diagnostics modified the actual returned worker PPC context");
        // Normal mismatch is deferred; guards restore the authentic return and
        // host state before the original executor resumes. Exceptions unwind,
        // restore temporary edits, and remain genuine fixture frontiers.
    }
    void finish() {
        check(closeSeen[0]&&closeSeen[1],"Pair original close entries were not both observed");
        // Existing free-boundary logging resolves actual r4=G. Use its native
        // generation as an independent oracle, never fixture allocation IDs.
        const auto values=rows();
        for(const auto group:{first,second}) {
            std::optional<uint64_t> generation;
            for(size_t i=pairRowStart;i<values.size();++i) {const auto& row=values[i];if(decimal(row,"sequence")>pairSequence&&
                decimal(row,"thread")==worker&&field(row,"mission")=="audio-reader-close-fixture"&&
                field(row,"event")=="encounter"&&field(row,"kind")=="audio-reader-release"&&
                field(row,"parameters").find("boundary=8233d5e8")!=std::string::npos&&
                hex(token(field(row,"instance"),"group"),group)) {
                generation=hexValue(token(field(row,"instance"),"groupGeneration"));break;
            }}
            check(generation&&*generation,"Original pair free lacks independent native generation receipt");
            if(group==first)firstGeneration=generation.value_or(0);else secondGeneration=generation.value_or(0);
            for(const auto& receipt:receipts)if(receipt.group==group)
                check(generation&&receipt.generation==*generation,receipt.label+": prevalidation cached epoch differs from actual free");
        }
        check(firstGeneration&&secondGeneration&&firstGeneration!=secondGeneration,"Distinct pair owners lost distinct native epochs");
        checks+=assertions;
        for(const auto& error:errors)std::fprintf(stderr,"[READER CLOSE DIAGNOSTIC DIFFERENCE] %s\n",error.c_str());
        need(errors.empty(),"Queued-close prevalidation ownership diagnostics differ after complete original root join");
        std::printf("AUDIT_READER_CLOSE_ATTRIBUTION command_group=plus4 r4_collision=instance_only malformed=preserved unknown_stale=noop ppc_fp_error=preserved owned_bytes=unchanged native_counters=unchanged both_original_frees=passed root_join=passed receipts=%s source_decode=unproven\n",path.string().c_str());
    }
};
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
    try {
        need(argc==2,"Original image path required");ReaderEvidenceFile evidenceFile;Runtime rt;rt.load(argv[1]);PPCContext original{};rt.initialize(original);
        rt.resourceAudit.configure(evidenceFile.path);rt.resourceAudit.mission("audio-reader-close-fixture");
        const auto entry=original;bool source=false,startup=false;
        rt.audioBoundaryObserver=[&](uint32_t pc,PPCContext&,uint8_t*) {
            const auto view=rt.engineAudioOutput->view();
            need(view.nativeEngine && view.muted && view.configured && view.identity,"Missing actual muted native source");
            if(pc==0x82345920) {need(!source && !view.active && !view.workerId,"Unexpected source construction order");source=true;return;}
            need(pc==0x828166FC && source && view.active && view.workerId && view.event,"Unexpected startup observation");
            throw StartupObserved{};
        };
        std::puts("Reader fixture: entering actual original startup");
        try{runOriginal(original,rt.base);}catch(const StartupObserved&){startup=true;}
        rt.audioBoundaryObserver={};need(startup,"Real startup did not reach the post-Q4C-release observation");
        const auto view=rt.engineAudioOutput->view();const uint32_t q=view.root;
        std::shared_ptr<KernelHandle> worker;
        {std::lock_guard lock(rt.threadMutex);for(const auto& t:rt.threads) if(t->id==view.workerId) worker=t->object;}
        need(worker && GetThreadId(worker->native)==view.workerId && view.workerId!=GetCurrentThreadId(),"Real Dac worker missing");
        const uint32_t adapter=PPCLoadU32(rt.base,q+0x14),reader=PPCLoadU32(rt.base,0x82E36B94),mutant=PPCLoadU32(rt.base,0x82E31BC8);
        need(adapter && reader && PPCLoadU32(rt.base,0x82E31BCC)==q &&
             PPCLoadU32(rt.base,q+0x40)==0x823469B0 && PPCLoadU32(rt.base,q+0x44)==0x823469C0,
             "Real reader allocator/root callback prerequisite missing");
        const auto lockObject=rt.getHandle(mutant);
        need(lockObject && lockObject->type==KernelHandle::Type::Mutant,"Installed root callback has no real mutant");
        EngineCpuCalls cpu(entry,rt.base); // Fresh saved entry, never the exception-unwound startup frame.
        auto nativeReaders=audioReaders(rt,true);
        nativeReaders->permitFixtureGroup(cpu.registers(),true);
        std::printf("Reader fixture: real startup observed, Q=%08X callback mutant=%08X worker=%u\n",q,mutant,view.workerId);
        RootLock installation(cpu,q);
        CloseDiagnostics diagnostics(rt,*nativeReaders,evidenceFile.path);
        AllocationAudit audit(rt,adapter,reader,view.workerId);
        installation.release();
        GroupView previous{};unsigned reusedManagers=0;bool reusedGroup=false;
        for(uint32_t cycle=1;cycle<=sequentialCycles;++cycle) {
            const auto profile=profileForCycle(cycle);
            const uint32_t identifier=cycle<=2?0x2EA8FB98:PPCLoadU32(rt.base,0x82D08ACC);
            const uint32_t overrideAllocator=cycle<=2?adapter:0;
            GroupView current{};uint32_t oldHead=0,oldPending=0;
            {
                RootLock lock(cpu,q);audit.begin(cycle,profile);
                oldHead=PPCLoadU32(rt.base,0x82E37310);oldPending=PPCLoadU32(rt.base,q+0xF0);
                const PreservedABI abi(cpu.registers());
                cpu.registers().r8.u32=overrideAllocator;cpu.registers().r9.u32=cycle<=2?0x1000:0;
                std::printf("Cycle %u: entering original group constructor with actual root callback lock held\n",cycle);
                const uint32_t g=cpu.invoke(0x8233D5F8,identifier,profile.count,profile.ringSize,profile.entries-3,q);
                abi.verify(cpu.registers());need(g!=0,"Actual group allocation failed");
                std::printf("Cycle %u real G=%08X extent=%X\n",cycle,g,profile.extent());
                current=verifyGroup(rt,audit,g,q,overrideAllocator,oldHead,profile,identifier);
                const auto nativeState=nativeReaders->snapshot();
                need(nativeState.groups==1 && nativeState.managers==profile.count && !nativeState.claims && !nativeState.operations,
                     "Production reader registry did not observe the real empty group");
                if(!(cycle%2)) {
                    need(current.group.generation>previous.group.generation,"Group generation did not advance");
                    reusedGroup=current.group.address==previous.group.address;
                    for(size_t i=0;i<profile.count;++i) {
                        need(current.managers[i].generation>previous.managers[i].generation,"Manager generation did not advance");
                        for(const auto& old:previous.managers) reusedManagers+=current.managers[i].address==old.address;
                        need(!audit.canInspect(previous.group.generation,previous.managers[i].generation,previous.rings[i],1),
                             "Old generation revived after actual original allocator reuse");
                    }
                }
                const uint32_t used=PPCLoadU32(rt.base,q+0xD0),capacity=PPCLoadU32(rt.base,q+0xCC),commands=PPCLoadU32(rt.base,q+0x20);
                need(uint64_t(used)+8<=capacity && uint64_t(commands)+used+8<=0x100000000ull,
                     "Original command queue has no bounded eight-byte retirement slot");
                const uint32_t command=commands+used;rt.pointer(command,8,true);
                cpu.invoke(0x8233D950,g); // Original enqueue writes {8233D980,G} into the REAL Q command buffer.
                abi.verify(cpu.registers());
                need(PPCLoadU32(rt.base,command)==0x8233D980 && PPCLoadU32(rt.base,command+4)==g &&
                     PPCLoadU32(rt.base,q+0xD0)==used+8 && PPCLoadU32(rt.base,q+0xF0)==oldPending &&
                     PPCLoadU32(rt.base,0x82E37310)==g+0x24,"Original enqueue changed the command/retirement publication order");
                need(WaitForSingleObject(audit.completed,0)==WAIT_TIMEOUT,"Dac cleanup ran while root callback lock was held");
            }
            // Event is signalled AFTER the real G free. Do not read G/M/H here.
            const HANDLE waits[]={audit.completed,rt.stopEvent};
            const auto wait=WaitForMultipleObjects(2,waits,FALSE,8000);rt.checkRunning();
            need(wait==WAIT_OBJECT_0,"Real Dac worker did not finish deferred reader cleanup within 8 seconds");
            {
                RootLock lock(cpu,q); // Also wait for the callback's AOT epilogue to return.
                const auto records=audit.snapshot();size_t freed=0,managers=0;
                for(const auto& r:records) if(r.cycle==cycle) {
                    need(!r.live && r.freeOrder && r.freeThread==view.workerId,"Original reader allocation was not freed on Dac worker");
                    ++freed;managers+=r.role==Role::Manager;
                    if(r.parent) need(r.freeOrder<records[size_t(r.parent-1)].freeOrder,"Original member free ordering changed");
                }
                need(freed==1+4*profile.count && managers==profile.count && PPCLoadU32(rt.base,q+0xF0)==oldPending &&
                     PPCLoadU32(rt.base,0x82E37310)==oldHead && PPCLoadU32(rt.base,0x82E36B94)==reader,
                     "Deferred cleanup lost a real free, global link, pending count, or reader allocator");
                for(size_t i=0;i<profile.count;++i) need(!audit.canInspect(current.group.generation,current.managers[i].generation,current.rings[i],profile.ringSize),
                                            "Fixture would inspect an already freed ring generation");
                std::printf("Cycle %u: %zu real frees on worker %u; %zu M frees precede G free; no guest reads after free\n",cycle,freed,view.workerId,managers);
            }
            previous=current;
            const auto nativeState=nativeReaders->snapshot();
            need(!nativeState.groups && !nativeState.managers && !nativeState.claims && !nativeState.operations,
                 "Production reader provenance survived actual group free");
        }

        // Extra pair preserves all six sequential lifecycle/reuse cases above.
        // One genuine startup/root/worker; both original one-manager groups are
        // alive before either real close command is queued.
        GroupView firstPair{},secondPair{};uint32_t pairOldHead=0,pairOldPending=0,pairIdentifier=0,pairCreateCaller=0;
        {
            RootLock lock(cpu,q);pairOldHead=PPCLoadU32(rt.base,0x82E37310);pairOldPending=PPCLoadU32(rt.base,q+0xF0);
            const auto create=[&](uint32_t cycle,uint32_t oldHead) {
                audit.beginPair(cycle,introProfile);const PreservedABI abi(cpu.registers());
                cpu.registers().r8.u32=0;cpu.registers().r9.u32=0;
                const auto identifier=PPCLoadU32(rt.base,0x82D08ACC);
                const auto caller=uint32_t(cpu.registers().lr); // invoke() preserves the actual direct caller lane.
                if(cycle==7){pairIdentifier=identifier;pairCreateCaller=caller;}
                else need(identifier==pairIdentifier&&caller==pairCreateCaller,"Direct fixture pair constructor instance changed");
                const auto group=cpu.invoke(0x8233D5F8,identifier,introProfile.count,introProfile.ringSize,introProfile.entries-3,q);
                abi.verify(cpu.registers());need(group!=0,"Original concurrent pair constructor failed");
                return verifyGroup(rt,audit,group,q,0,oldHead,introProfile,identifier);
            };
            firstPair=create(7,pairOldHead);secondPair=create(8,firstPair.group.address+0x24);
            need(firstPair.group.address!=secondPair.group.address&&firstPair.managers[0].address!=secondPair.managers[0].address,
                 "Original simultaneous pair reused live group/manager storage");
            const auto snapshot=nativeReaders->snapshot();need(snapshot.groups==2&&snapshot.managers==2&&!snapshot.claims&&!snapshot.copies&&!snapshot.operations,
                 "Two original live pair owners are not registered independently");
            diagnostics.pair(firstPair.group.address,secondPair.group.address,q,view.workerId,pairIdentifier,pairCreateCaller);
            audit.closeProbe=[&](uint32_t cycle,const PPCContext& incoming,PPCContext& returned,uint8_t* memory,uint32_t group){
                diagnostics.close(cycle,incoming,returned,memory,group);
            };
        }
        for(const auto& current:{firstPair,secondPair}) {
            const bool first=current.group.address==firstPair.group.address;
            {
                RootLock lock(cpu,q);audit.resetCompletion();const auto used=PPCLoadU32(rt.base,q+0xD0),capacity=PPCLoadU32(rt.base,q+0xCC);
                const auto commands=PPCLoadU32(rt.base,q+0x20);
                need(uint64_t(used)+8<=capacity&&uint64_t(commands)+used+8<=0x100000000ull,"Original pair queue has no complete command slot");
                const auto command=commands+used;rt.pointer(command,8,true);const PreservedABI abi(cpu.registers());
                rt.resourceAudit.action(first?"reader-close-pair-first-original":"reader-close-pair-second-original");
                cpu.invoke(0x8233D950,current.group.address);abi.verify(cpu.registers());
                need(PPCLoadU32(rt.base,command)==0x8233D980&&PPCLoadU32(rt.base,command+4)==current.group.address&&
                     PPCLoadU32(rt.base,q+0xD0)==used+8&&PPCLoadU32(rt.base,q+0xF0)==pairOldPending,
                     "Original pair command/pending publication changed");
                need(WaitForSingleObject(audit.completed,0)==WAIT_TIMEOUT,"Pair cleanup ran under original root lock");
            }
            const HANDLE waits[]={audit.completed,rt.stopEvent};const auto wait=WaitForMultipleObjects(2,waits,FALSE,8000);rt.checkRunning();
            need(wait==WAIT_OBJECT_0,"Original queued pair cleanup failed within8 seconds");
            {
                RootLock lock(cpu,q);const auto records=audit.snapshot();size_t freed=0;
                for(const auto& allocation:records)if(allocation.cycle==current.group.cycle) {
                    need(!allocation.live&&allocation.freeOrder&&allocation.freeThread==view.workerId,"Pair allocation not actually freed on Dac worker");++freed;
                    if(allocation.parent)need(allocation.freeOrder<records[size_t(allocation.parent-1)].freeOrder,"Pair member free preceded wrong parent");
                }
                need(freed==5&&PPCLoadU32(rt.base,q+0xF0)==pairOldPending&&PPCLoadU32(rt.base,q+0xD0)==0&&
                     PPCLoadU32(rt.base,0x82E37310)==(first?secondPair.group.address+0x24:pairOldHead)&&
                     PPCLoadU32(rt.base,0x82E36B94)==reader,"Pair deferred free lost list/pending/reader identity");
                need(!audit.canInspect(current.group.generation,current.managers[0].generation,current.rings[0],1),"Freed pair ring remained inspectable");
                const auto snapshot=nativeReaders->snapshot();need(snapshot.groups==(first?1u:0u)&&snapshot.managers==(first?1u:0u)&&
                    !snapshot.claims&&!snapshot.copies&&!snapshot.operations,"Pair registry did not retire exact original owner");
                if(first)need(audit.canInspect(secondPair.group.generation,secondPair.managers[0].generation,secondPair.rings[0],introProfile.ringSize),
                              "Second original pair owner retired with first");
                std::printf("AUDIT_READER_CLOSE_PAIR_FREE cycle=%u original_allocations=5 original_frees=5 worker=%u root_live=1 stale_guest_reads=0\n",current.group.cycle,view.workerId);
            }
        }
        audit.closeProbe={};
        nativeReaders->permitFixtureGroup(cpu.registers(),false);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        for(;;) {
            rt.checkRunning();const auto progress=rt.engineAudioOutput->view();
            if(progress.submitted>=2 && progress.consumed>=2 && progress.backendRetired && progress.passCallbacks) break;
            need(std::chrono::steady_clock::now()<deadline,"Real muted Dac CPU/DSP/downstream work did not complete");Sleep(10);
        }
        need(WaitForSingleObject(worker->native,0)==WAIT_TIMEOUT,"Dac worker stopped before original root teardown");
        cpu.invoke(0x82338FA0,q); // Actual queued self-worker release and outer OS join before Q free.
        DWORD result=~0u;
        need(WaitForSingleObject(worker->native,0)==WAIT_OBJECT_0 && GetExitCodeThread(worker->native,&result) && result==0,
             "Original root destructor failed to join a normally returned Dac worker");
        need(!PPCLoadU32(rt.base,0x82E31BCC) && rt.engineAudio && !rt.engineAudio->ready(),"Original root/factory teardown incomplete");
        bool retired=false;try{rt.engineAudioOutput->view();}catch(const Failure&){retired=true;}
        need(retired,"Native Dac identity survived original root teardown");audit.snapshot();
        std::printf("AUDIT_READER_CLOSE_FUNCTIONAL_LIFETIME groups=8 managers=14 original_allocations=64 original_frees=64 original_root_join=passed diagnostic_assertion=pending receipts=%s\n",evidenceFile.path.string().c_str());
        diagnostics.finish();
        std::puts("AUDIT_READER_CLOSE_PROFILE_FIXTURE cached=fixture_stream malformed_cached=preserved unknown_stale=unknown identifier_caller=instance_only original_groups=8 original_managers=14 original_allocations=64 original_frees=64 root_join=passed catalog_link=unproven");
        std::printf("PASS original empty audio reader lifecycle: %zu checks, 8 groups/14 managers/64 real allocations and frees; three qualified profiles and concurrent close pair; reused G=%u M=%u; original root teardown/OS join; ALL MUTED; no source admission or production lease claim\n",
                    checks,unsigned(reusedGroup),reusedManagers);
        return 0;
    }catch(const std::exception& error) {std::fprintf(stderr,"FAIL original audio reader lifecycle: %s\n",error.what());return 1;}
}
