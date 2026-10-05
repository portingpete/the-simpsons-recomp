// Production dynamic owner + shared CPU caller + genuine D3D11 VBs. The small
// checked-memory/AOT callback fixture verifies the allocator ABI and effects;
// it does not substitute for the parent's real original-pool boot integration.
#include "runtime/engine_dynamic_buffers.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/native_window.h"
#include "runtime/threads.h"
#include "renderer/driver_resources.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>

#ifndef SIMPSONS_DYNAMIC_BUFFERS_STANDALONE
#error "Standalone test supplies Runtime/memory fixtures; do not link SimpsonsRuntime"
#endif
using namespace Simpsons;
namespace {
constexpr uint32_t engine=0x02000000,stack=0x0300F000,heap=0x04000000;
constexpr uint32_t createPool=0x823FBEA8,destroyPool=0x823FB708;
constexpr uint32_t allocateEntry=uint32_t(PPC_CODE_BASE)+0x100,freeEntry=allocateEntry+4;
constexpr uint32_t global=0x82D0D0C4,head=0x82D0D0D4,outputs=0x82D0D100;
constexpr std::array<uint32_t,4> fields={0x82D0D0C8,0x82D0D0CC,0x82D0D0D0,0x82D0D0D8};
constexpr std::array<uint32_t,4> sizes={16,20,8,20},counts={16,100,100,42};
constexpr std::array<uint32_t,4> createLR={0x823FCF94,0x823FCFB4,0x823FCFCC,0x823FCFE4};
constexpr std::array<uint32_t,4> destroyLR={0x823FCF4C,0x823FCF34,0x823FCF1C,0x823FCE4C};
void require(bool value,const char* why) {if(!value) throw Failure(why);}
template<class F> std::string rejects(F&& call) {
    try {call();} catch(const std::exception& error) {return error.what();}
    throw Failure("Expected explicit native dynamic buffer failure");
}
uint32_t failPointer{};
struct Pool {unsigned role;std::vector<uint32_t> records;};
struct Model {
    std::map<uint32_t,Pool> pools;
    std::map<uint32_t,uint32_t> records;
    std::vector<uint32_t> madeRecords,freedRecords;
    std::vector<unsigned> destroyedPools;
    uint32_t next=heap;
    unsigned created{},startCalls{},stopCalls{},failStart{},failStop{},cancelStart{};
    bool throwStart{},failRecordPointer{};
    void reset() {
        require(pools.empty() && records.empty(),"Fixture reset would hide outstanding CPU allocations");
        *this=Model{};
    }
    bool startFailure() {
        ++startCalls;
        if(startCalls!=failStart) return false;
        if(throwStart) throw Failure("injected allocator callback failure");
        return true;
    }
    void stopFailure() {
        if(++stopCalls==failStop) throw Failure("injected cleanup callback failure before effects");
    }
    uint32_t allocate(Runtime& rt,uint32_t bytes) {
        const auto result=next;next+=(bytes+15)&~15u;
        std::fill_n(rt.pointer(result,bytes,true),bytes,uint8_t(0xA7));return result;
    }
} model;
void checkFrame(PPCContext& ctx,uint8_t* base) {
    require(currentContext==&ctx && ctx.r1.u32==stack-0x100,"CPU callback has wrong context/frame");
    require(PPC_LOAD_U32(ctx.r1.u32)==stack,"CPU callback backchain not preserved");
    active->checkRunning();
}
PPC_FUNC(poolCreate) {
    checkFrame(ctx,base);
    const unsigned role=model.created;
    require(role<4 && ctx.r3.u32==sizes[role] && ctx.r4.u32==counts[role] &&
            ctx.r5.u32==4 && ctx.r6.u32==0x40411 && uint32_t(ctx.lr)==createLR[role],"Original pool creation ABI changed");
    if(model.startFailure()) {ctx.r3.u32=0;return;}
    uint32_t pool=model.allocate(*active,64);
    model.pools.emplace(pool,Pool{role,{}});++model.created;
    ctx.r3.u32=pool;
    if(model.startCalls==model.cancelStart) active->stopping=true;
}
PPC_FUNC(recordCreate) {
    checkFrame(ctx,base);
    auto pool=model.pools.find(ctx.r3.u32);
    require(pool!=model.pools.end() && pool->second.role==3 && ctx.r4.u32==0x40411 &&
            uint32_t(ctx.lr)==0x823FC7A0,"Original E+138 allocation ABI changed");
    if(model.startFailure()) {ctx.r3.u32=0;return;}
    const uint32_t record=model.allocate(*active,20);
    pool->second.records.push_back(record);model.records.emplace(record,pool->first);model.madeRecords.push_back(record);
    ctx.r3.u32=record;
    if(model.failRecordPointer) {failPointer=record;model.failRecordPointer=false;}
    if(model.startCalls==model.cancelStart) active->stopping=true;
}
PPC_FUNC(recordFree) {
    checkFrame(ctx,base);
    auto record=model.records.find(ctx.r4.u32);
    require(record!=model.records.end() && record->second==ctx.r3.u32 && uint32_t(ctx.lr)==0x823FCE2C,
            "Original E+13C free ABI or exactly-once ownership changed");
    for(unsigned offset=0;offset<0x34;offset+=4)
        require(PPC_LOAD_U32(0x82D0D0DC+offset)==0,"CPU record free preceded original array detachment");
    if(PPC_LOAD_U32(head)==record->first)
        require(PPC_LOAD_U32(record->first+4)==0 && PPC_LOAD_U32(record->first+8)==0 &&
                PPC_LOAD_U32(record->first+12)==0,"CPU record free preceded native resource/owner detachment");
    model.stopFailure();
    auto& owned=model.pools.at(record->second).records;
    auto found=std::find(owned.begin(),owned.end(),record->first);
    require(found!=owned.end(),"Pool did not own record");owned.erase(found);
    model.freedRecords.push_back(record->first);
    std::fill_n(active->pointer(record->first,20,true),20,uint8_t(0xDD)); // Catch reads after free.
    model.records.erase(record);ctx.r3.u32=1;
}
PPC_FUNC(poolDestroy) {
    checkFrame(ctx,base);
    auto found=model.pools.find(ctx.r3.u32);
    require(found!=model.pools.end() && found->second.records.empty(),"Destroyed foreign pool or pool with live records");
    const unsigned role=found->second.role;
    require(uint32_t(ctx.lr)==destroyLR[role] && PPC_LOAD_U32(fields[role])==found->first,
            "Pool destroy order/ABI lost guest ownership before callback");
    model.stopFailure();
    model.destroyedPools.push_back(role);model.pools.erase(found);ctx.r3.u32=1;
}
}

namespace Simpsons {
Runtime* active{};
thread_local PPCContext* currentContext{};
// This standalone graphics fixture supplies the Runtime implementation and
// never creates audio readers. Production links the real reader unwinder.
void unwindAudioReaderCall(PPCContext&) noexcept {
    if(active && active->engineAudioReader) std::terminate();
}
NativeWindow::~NativeWindow()=default;
GuestThread::~GuestThread()=default;
Runtime::Runtime() {
    require(!active,"Duplicate fixture runtime");
    base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));
    require(base!=nullptr,"Guest reservation failed");active=this;
}
Runtime::~Runtime() {VirtualFree(base,0,MEM_RELEASE);active=nullptr;currentContext=nullptr;}
void Runtime::checkRunning() {if(stopping) throw Failure("fixture runtime cancellation");}
void Runtime::map(uint32_t address,uint64_t size,bool write,const char* name,MemoryUse use) {
    require(address>=0x10000 && !(address&0xFFF) && size && !(size&0xFFF) && uint64_t(address)+size<=0x100000000ull,"Invalid fixture map");
    for(const auto& r:regions) require(!(address<uint64_t(r.address)+r.size && r.address<uint64_t(address)+size),"Fixture map overlap");
    require(VirtualAlloc(base+address,size,MEM_COMMIT,write?PAGE_READWRITE:PAGE_READONLY)==base+address,"Fixture map failed");
    regions.push_back({address,size,write,name,use});
    for(uint64_t p=address;p<uint64_t(address)+size;p+=4096) pageAccess[p>>12]=write?3:1;
}
uint8_t* Runtime::pointer(uint32_t address,unsigned width,bool write) {
    if(address==failPointer && failPointer) {failPointer=0;throw Failure("injected record mapping validation failure");}
    require(width && address>=0x10000 && uint64_t(address)+width<=0x100000000ull,"Bad fixture guest range");
    for(uint64_t p=address>>12;p<=(uint64_t(address)+width-1)>>12;++p) {
        auto access=pageAccess[p].load();require((access&1) && (!write || (access&2)),"Unmapped/protected guest fixture access");
    }
    return base+address;
}
}
uint8_t* PPCGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    require(active && active->base==base,"Wrong guest fixture base");active->checkRunning();return active->pointer(address,width,write);
}
[[noreturn]] void PPCRecompFailure(const PPCContext&,uint32_t,const char* message) {throw Failure(message);}

namespace {
void prepare(Runtime& rt,PPCContext& ctx) {
    rt.map(0x82D00000,0x20000,true,"engine globals");
    rt.map(0x82E3D000,0x1000,true,"caps");
    rt.map(engine,0x1000,true,"engine callbacks");
    rt.map(stack&~0xFFFFu,0x10000,true,"caller stack");
    rt.map(heap,0x100000,true,"real guest fixture allocator");
    auto* base=rt.base;
    for(auto [address,function]:std::array<std::pair<uint32_t,PPCFunc*>,4>{{
        {createPool,poolCreate},{destroyPool,poolDestroy},{allocateEntry,recordCreate},{freeEntry,recordFree}}}) {
        const uint32_t page=uint32_t(PPC_IMAGE_BASE+PPC_IMAGE_SIZE+(uint64_t(address-PPC_CODE_BASE)*2))&~0xFFFu;
        if(!rt.pageAccess[page>>12].load()) rt.map(page,0x1000,true,"fixture AOT dispatch",MemoryUse::Host);
        PPC_LOOKUP_FUNC(base,address)=function;
    }
    PPC_STORE_U32(0x82D0CA68,engine);PPC_STORE_U32(engine+0x138,allocateEntry);PPC_STORE_U32(engine+0x13C,freeEntry);
    PPC_STORE_U32(0x82E3DFBC,0x10000);
    ctx.r1.u32=stack;ctx.lr=0x823ECA18;ctx.r3.u64=0xDEADC0DE12345678ull;
    currentContext=&ctx;
}
void empty(Runtime& rt,const EngineDynamicBuffers& owner) {
    auto* base=rt.base;
    require(!owner.hasOwnership() && !owner.initialized() && model.records.empty() && model.pools.empty(),"CPU/native ownership leaked");
    for(uint32_t at=global;at<outputs+16;at+=4) require(PPC_LOAD_U32(at)==0,"Guest dynamic state not cleared");
}
void complete(Runtime& rt,const EngineDynamicBuffers& owner,const Graphics::StartupResources& startup) {
    auto* base=rt.base;
    require(owner.initialized() && owner.hasOwnership() && model.pools.size()==4 && model.records.size()==4,"Incomplete startup was published");
    uint32_t previous=0;
    for(unsigned i=0;i<4;++i) {
        auto record=model.madeRecords[i];auto id=PPC_LOAD_U32(outputs+4*i);
        require(PPC_LOAD_U32(record)==0x40000 && PPC_LOAD_U32(record+4)==1 && PPC_LOAD_U32(record+8)==id &&
                PPC_LOAD_U32(record+12)==outputs+4*i && PPC_LOAD_U32(record+16)==previous,"Wrong guest record/list fields");
        require(PPC_LOAD_U32(0x82D0D0E0+4*i)==0 && PPC_LOAD_U32(0x82D0D0F0+4*i)==0x40000,"Wrong cursors/sizes");
        require(id>=0x00D00001 && id<0x00E00000 && !rt.pageAccess[id>>12].load(),"ID is reused as mapped guest memory");
        require(owner.buffer(id)==startup.resources().vertices[i],"Native ID is not backed by the corresponding real VB");
        previous=record;
    }
    require(PPC_LOAD_U32(head)==previous && PPC_LOAD_U32(0x82D0D0DC)==0,"Wrong list head/current index");
}
void normalAndValidation(Runtime& rt,EngineCpuCalls& cpu,Graphics::NativeBackend& backend,Graphics::StartupResources& startup) {
    auto* base=rt.base;
    EngineDynamicBuffers owner;
    rejects([&]{owner.buffer(1);});
    owner.stop(rt,cpu,base); // Idempotent inactive stop.
    auto checkReject=[&] {
        const auto before=model.startCalls;
        rejects([&]{owner.start(rt,cpu,base,startup);});
        require(!owner.hasOwnership() && model.startCalls==before,"Preflight failure mutated CPU ownership");
    };
    PPC_STORE_U32(global,0xDEAD);checkReject();PPC_STORE_U32(global,0);
    PPC_STORE_U32(head,0xDEAD);checkReject();PPC_STORE_U32(head,0);
    PPC_STORE_U32(fields[0],0xDEAD);checkReject();PPC_STORE_U32(fields[0],0);
    PPC_STORE_U32(outputs,0xDEAD);checkReject();PPC_STORE_U32(outputs,0);
    PPC_STORE_U32(0x82E3DFBC,0);checkReject();PPC_STORE_U32(0x82E3DFBC,0x10000);
    PPC_STORE_U32(engine+0x138,0);checkReject();PPC_STORE_U32(engine+0x138,allocateEntry);
    rt.stopping=true;checkReject();rt.stopping=false;
    rejects([&]{owner.start(rt,cpu,base+1,startup);});
    Graphics::StartupResources noBacking;rejects([&]{owner.start(rt,cpu,base,noBacking);});
    std::atomic<bool> threadRejected=false;
    std::thread other([&]{try {owner.start(rt,cpu,base,startup);} catch(const Failure&) {threadRejected=true;}});
    other.join();require(threadRejected,"Wrong owner thread was accepted");

    // The initial original stop clears stale counters before creating pools.
    PPC_STORE_U32(0x82D0D0DC,3);PPC_STORE_U32(0x82D0D0E0,64);PPC_STORE_U32(0x82D0D0F0,0x1234);
    owner.start(rt,cpu,base,startup);complete(rt,owner,startup);
    owner.validateOwnership(rt,base);
    std::vector<uint32_t> guarded={global,head};
    guarded.insert(guarded.end(),fields.begin(),fields.end());
    for(unsigned i=0;i<4;++i)guarded.push_back(outputs+4*i);
    for(const auto record:model.madeRecords)for(uint32_t offset=0;offset<20;offset+=4)guarded.push_back(record+offset);
    for(const auto address:guarded) {
        const auto old=PPC_LOAD_U32(address);PPC_STORE_U32(address,old^4);
        rejects([&]{owner.validateOwnership(rt,base);});PPC_STORE_U32(address,old);
        owner.validateOwnership(rt,base);
        require(owner.initialized()&&model.stopCalls==0,"Validation changed dynamic ownership before rejection");
    }
    for(const auto record:model.madeRecords) {
        const auto page=record>>12;const auto access=rt.pageAccess[page].load();rt.pageAccess[page]=1;
        rejects([&]{owner.validateOwnership(rt,base);});rt.pageAccess[page]=access;
        owner.validateOwnership(rt,base);
    }
    rejects([&]{owner.start(rt,cpu,base,startup);});
    const auto stale=PPC_LOAD_U32(outputs);
    auto retained=owner.buffer(stale);
    std::vector<uint8_t> bytes(0x40000);
    for(unsigned i=0;i<4;++i) {
        std::fill(bytes.begin(),bytes.end(),uint8_t(0x20+i));
        backend.writeBuffer(owner.buffer(PPC_LOAD_U32(outputs+4*i)),0,bytes);
    }
    for(unsigned i=0;i<4;++i) {
        std::fill(bytes.begin(),bytes.end(),uint8_t(0x20+i));
        require(backend.readbackBuffer(owner.buffer(PPC_LOAD_U32(outputs+4*i)))==bytes,"Native VBs aliased or lost data");
    }
    const auto before=model.stopCalls;
    PPC_STORE_U32(global,0xDEAD);rejects([&]{owner.stop(rt,cpu,base);});PPC_STORE_U32(global,0);
    const auto originalNext=PPC_LOAD_U32(model.madeRecords.back()+16);
    PPC_STORE_U32(model.madeRecords.back()+16,model.madeRecords.back());
    rejects([&]{owner.stop(rt,cpu,base);});PPC_STORE_U32(model.madeRecords.back()+16,originalNext);
    require(model.stopCalls==before && owner.initialized(),"Foreign/corrupt cleanup mutated ownership");
    owner.stop(rt,cpu,base);empty(rt,owner);owner.stop(rt,cpu,base);
    rejects([&]{owner.buffer(stale);});
    std::fill(bytes.begin(),bytes.end(),0x20);require(backend.readbackBuffer(retained)==bytes,"Retained native VB was destroyed at logical release");
    auto reversed=model.madeRecords;std::reverse(reversed.begin(),reversed.end());
    require(model.freedRecords==reversed && model.destroyedPools==std::vector<unsigned>({3,2,1,0}),"Original reverse cleanup order changed");
    model.reset();owner.start(rt,cpu,base,startup);
    require(PPC_LOAD_U32(outputs)!=stale,"Restart reused a stale native ID");owner.stop(rt,cpu,base);empty(rt,owner);

    // Bundle disposal leaves this owner alive until explicit stop, then only an
    // independently retained native reference survives.
    model.reset();owner.start(rt,cpu,base,startup);
    std::array<std::weak_ptr<Graphics::Buffer>,4> weak;
    for(unsigned i=0;i<4;++i) weak[i]=startup.resources().vertices[i];
    auto independent=owner.buffer(PPC_LOAD_U32(outputs+4));
    retained.reset();startup.reset();
    for(const auto& p:weak) require(!p.expired(),"Bundle reset destroyed active dynamic ownership");
    owner.stop(rt,cpu,base);empty(rt,owner);
    require(weak[0].expired() && !weak[1].expired() && weak[2].expired() && weak[3].expired(),"Dynamic stop leaked/destroyed native references");
    independent.reset();require(weak[1].expired(),"Independent final owner leaked");
    startup.initialize(backend,4,4);
}
void failures(Runtime& rt,EngineCpuCalls& cpu,Graphics::StartupResources& startup) {
    auto* base=rt.base;
    for(bool throwing:{false,true}) for(unsigned point=1;point<=8;++point) {
        model.reset();model.failStart=point;model.throwStart=throwing;
        EngineDynamicBuffers owner;
        rejects([&]{owner.start(rt,cpu,base,startup);});empty(rt,owner);
        for(const auto& vb:startup.resources().vertices) require(vb.use_count()==1,"Startup rollback retained native owner reference");
    }
    model.reset();model.failRecordPointer=true;
    {EngineDynamicBuffers owner;rejects([&]{owner.start(rt,cpu,base,startup);});empty(rt,owner);}
    for(unsigned point=1;point<=8;++point) {
        model.reset();EngineDynamicBuffers owner;owner.start(rt,cpu,base,startup);
        model.failStop=point;
        rejects([&]{owner.stop(rt,cpu,base);});
        require(owner.hasOwnership() && !owner.initialized(),"Interrupted stop lost resumable ownership");
        owner.stop(rt,cpu,base);empty(rt,owner);
        require(model.freedRecords.size()==4 && model.destroyedPools.size()==4,"Cleanup retry repeated or omitted frees");
    }
    for(unsigned point:{1u,5u,8u}) {
        model.reset();model.cancelStart=point;
        EngineDynamicBuffers owner;
        const auto message=rejects([&]{owner.start(rt,cpu,base,startup);});
        // Final callback may finish all fields before cancellation is observed;
        // start's final validation must still check running before success.
        require(message.find("cancellation")!=std::string::npos && owner.hasOwnership(),"Cancellation was swallowed as successful startup");
        rt.stopping=false; // Test-only recovery to inspect/unwind retained ownership.
        owner.stop(rt,cpu,base);empty(rt,owner);
    }
    model.reset();
    EngineDynamicBuffers owner;
    for(unsigned i=0;i<64;++i) {model.reset();owner.start(rt,cpu,base,startup);owner.stop(rt,cpu,base);empty(rt,owner);}
}
void evidence(const char* path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Cannot read original byte evidence");
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(image.size()==15466496,"Unexpected original image size");
    for(const auto [address,word]:std::array<std::pair<uint32_t,uint32_t>,18>{{
        {0x823FCF74,0x4BFFFDE5},{0x823FCD58,0x7D8802A6},{0x823FCEE0,0x280B0000},
        {0x823FCF90,0x4BFFEF19},{0x823FCFB0,0x4BFFEEF9},{0x823FCFC8,0x4BFFEEE1},
        {0x823FCFE0,0x4BFFEEC9},{0x823FCFE8,0x4BFFFAF1},{0x823FC79C,0x4E800421},
        {0x823FC7A8,0x917F0010},{0x823FC7CC,0x4804500D},{0x823FC7E8,0x939F0000},
        {0x823FC7EC,0x917F0004},{0x823FC7F4,0x93BF000C},{0x823FC7F8,0x917F0008},
        {0x823FCE28,0x4E800421},{0x823FCE48,0x4BFFE8C1},{0x823FCFEC,0x38600001}}}) {
        const auto* p=image.data()+(address-0x82000000);
        require(((uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3])==word,"Original dynamic contract evidence changed");
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc>2) throw Failure("Usage: EngineDynamicBuffersTests [original-flat-image]");
        Runtime rt;PPCContext incoming{};prepare(rt,incoming);
        Graphics::NativeBackend backend(true);Graphics::StartupResources startup(backend,4,4);
        {EngineCpuCalls cpu(incoming,rt.base);
            normalAndValidation(rt,cpu,backend,startup);failures(rt,cpu,startup);
        }
        require(currentContext==&incoming && incoming.r1.u32==stack && incoming.r3.u64==0xDEADC0DE12345678ull &&
                incoming.lr==0x823ECA18,"Shared CPU call frame did not restore/isolate incoming context");
        if(argc==2) evidence(argv[1]);
        require(!backend.presentationCount() && !backend.screenDrawCount(),"Dynamic ownership test rendered a frame");
        puts("Dynamic buffer startup/stop passed: real four VBs, original ABI fixtures, guest fields, allocation rollback, cleanup retry, cancellation, stale IDs.");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Dynamic buffer contract failure: %s\n",error.what());return 1;}
}
