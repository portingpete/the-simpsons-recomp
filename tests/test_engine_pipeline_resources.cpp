// Default: parent's real Runtime + regenerated original AOT init/cleanup.
// Optional standalone mode: checked guest-memory fixture, production index and
// declaration hooks, and a small callsite/record-store fixture. No SDK objects.
#include "runtime/engine_pipeline_resources.h"
#include "runtime/engine_resources.h"
#include "runtime/engine_cpu_calls.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>

#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
#include "runtime/native_window.h"
#include "runtime/threads.h"
#endif

void SimpsonsNativeDeclarationCreate(PPCContext&,uint8_t*);
void SimpsonsNativeDeclarationRelease(PPCContext&,uint8_t*);
using namespace Simpsons;
namespace {
size_t checks{};
constexpr uint32_t field=0x82D507F0,cursor=field-4;
void require(bool value,const char* message) {++checks;if(!value) throw Failure(message);}
template<class F> void rejects(F&& fn) {
    ++checks;try {fn();} catch(const std::exception&) {return;}
    throw Failure("Expected pipeline ownership operation to fail");
}
// All original meaningful bytes; +B is overwritten with a nonzero test value
// while constructing guest input. Production original AOT keeps its own bytes.
const std::array<std::vector<uint32_t>,3> declarations={{
    {0x00000000,0x002A23B9,0x00000000, 0x0000000C,0x002A23B9,0x00030000,
     0x00000018,0x00182886,0x000A0000, 0x0000001C,0x002C23A5,0x00050000,
     0x00FF0000,0xFFFFFFFF,0x00000000},
    {0x00000000,0x002A23B9,0x00000000, 0x0000000C,0x00182886,0x000A0000,
     0x00000010,0x002C23A5,0x00050000, 0x00FF0000,0xFFFFFFFF,0x00000000},
    {0x00000000,0x002A23B9,0x00000000, 0x0000000C,0x00182886,0x000A0000,
     0x00FF0000,0xFFFFFFFF,0x00000000}}};
void arguments(PPCContext& ctx) {
    ctx.r3.u32=0x1FFFE;ctx.r4.u32=8;ctx.r5.u32=1;ctx.r6.u32=0;ctx.r27.u32=0x82D507FC;
    ctx.lr=0x12345678;
}
void createDirect(EngineCpuCalls& cpu,uint8_t* base) {
    auto& ctx=cpu.registers();arguments(ctx);
    ctx.r6.u32=(PPC_LOAD_U32(0x82E3DFBC)&0x10000u)?0u:2u;
    SimpsonsNativePipelineIndexCreate(ctx,base);
    require(ctx.lr==0x82416CA4,"Create hook did not reproduce BL link value");
    require(PPC_LOAD_U32(field)==0,"Create hook performed original following guest store");
    PPC_STORE_U32(field,ctx.r3.u32); // Only the fixture reproduces following STW.
}
void makeDeclaration(EngineCpuCalls& cpu,uint8_t* base,unsigned which) {
    constexpr uint32_t input=0x11000;
    for(size_t i=0;i<declarations[which].size();++i) {
        auto word=declarations[which][i];
        if(i%3==2) word|=0xA0+uint32_t(i/3); // Opaque +B retained by real registry.
        PPC_STORE_U32(input+uint32_t(i)*4,word);
    }
#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
    auto& ctx=cpu.registers();ctx.r3.u32=input;ctx.r4.u32=field+4+4*which;
    SimpsonsNativeDeclarationCreate(ctx,base);
#else
    require(cpu.invoke(0x823EF838,input,field+4+4*which)==1,"Original declaration bridge creation failed");
#endif
}
void initializeOriginal(EngineCpuCalls& cpu,uint8_t* base) {
#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
    PPC_STORE_U32(cursor,0);
    createDirect(cpu,base);
    for(unsigned i=0;i<3;++i) makeDeclaration(cpu,base,i);
    cpu.registers().r3.u32=1;
#else
    (void)base;
    require(cpu.invoke(0x82416C58)==1,"Original pipeline initializer did not succeed");
#endif
}
void cleanupOriginal(EngineCpuCalls& cpu,uint8_t* base) {
#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
    auto& ctx=cpu.registers();ctx.r31.u32=0x82D507F4;
    if(const auto id=PPC_LOAD_U32(field)) {
        ctx.r3.u32=id;SimpsonsNativePipelineIndexRelease(ctx,base);
        require(ctx.r3.u32==0 && ctx.lr==0x82416BF0,"Release hook did not reproduce return/link values");
        require(PPC_LOAD_U32(field)==id,"Release hook performed original following clear");
        PPC_STORE_U32(field,0);
    }
    for(unsigned n=3;n>0;--n) {
        auto output=field+4*n;
        if(const auto id=PPC_LOAD_U32(output)) {
            ctx.r3.u32=id;SimpsonsNativeDeclarationRelease(ctx,base);PPC_STORE_U32(output,0);
        }
    }
#else
    (void)base;cpu.invoke(0x82416BC8);
#endif
}
void requireFieldsEmpty(uint8_t* base) {
    for(uint32_t at=field;at<=field+12;at+=4) require(PPC_LOAD_U32(at)==0,"Original pipeline cleanup left a guest owner field");
}
}

#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
namespace Simpsons {
Runtime* active{};thread_local PPCContext* currentContext{};
NativeWindow::~NativeWindow()=default;GuestThread::~GuestThread()=default;
Runtime::Runtime() {
    require(!active,"Duplicate fixture runtime");
    base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));
    require(base!=nullptr,"Guest address space reservation failed");active=this;
}
Runtime::~Runtime() {VirtualFree(base,0,MEM_RELEASE);active=nullptr;currentContext=nullptr;}
void Runtime::checkRunning() {if(stopping) throw Failure("pipeline fixture cancellation");}
void Runtime::map(uint32_t address,uint64_t size,bool write,const char* name,MemoryUse use) {
    require(size && !(size&0xFFF) && !(address&0xFFF) && uint64_t(address)+size<=0x100000000ull,"Invalid fixture mapping");
    for(const auto& r:regions) require(!(address<uint64_t(r.address)+r.size && r.address<uint64_t(address)+size),"Fixture mapping overlaps");
    require(VirtualAlloc(base+address,size,MEM_COMMIT,write?PAGE_READWRITE:PAGE_READONLY)==base+address,"Fixture mapping failed");
    regions.push_back({address,size,write,name,use});
    for(uint64_t at=address;at<uint64_t(address)+size;at+=4096) pageAccess[at>>12]=write?3:1;
}
uint8_t* Runtime::pointer(uint32_t address,unsigned width,bool write) {
    require(width && address>=0x10000 && uint64_t(address)+width<=0x100000000ull,"Invalid guest range");
    for(uint64_t p=address>>12;p<=(uint64_t(address)+width-1)>>12;++p) {
        auto access=pageAccess[p].load();require((access&1) && (!write || (access&2)),"Unmapped or protected guest range");
    }
    return base+address;
}
}
uint8_t* PPCGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    require(active && base==active->base,"Wrong checked-memory base");active->checkRunning();return active->pointer(address,width,write);
}
[[noreturn]] void PPCRecompFailure(const PPCContext&,uint32_t,const char* text) {throw Failure(text);}
#endif

namespace {
void tests(Runtime& runtime,EngineCpuCalls& cpu,Graphics::NativeBackend& backend) {
    auto* base=runtime.base;auto& ctx=cpu.registers();
    PPC_STORE_U32(0x82E3DFBC,PPC_LOAD_U32(0x82E3DFBC)|0x10000);
    auto scratchIndex=backend.createBuffer(0x4E20,Graphics::BufferKind::Index16);
    EngineScratchResources declarationsOwner(scratchIndex);
    arguments(ctx);rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});
    rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});
    uint32_t stale{};
    {
        EnginePipelineResources owner(backend);
        rejects([&]{EnginePipelineResources nested(backend);});
        rejects([&]{owner.requireReleased();});rejects([&]{owner.index(0);});
        owner.requireUnowned(); // Genuine preallocation before original creation.
        PPC_STORE_U32(field,0xBAD);rejects([&]{owner.requireUnowned();});PPC_STORE_U32(field,0);
        // Reject each scalar ABI difference, non-original destination register,
        // wrong context/base, capability/argument mismatch, and foreign owner field.
        for(unsigned which=0;which<5;++which) {
            arguments(ctx);
            switch(which) {case 0:ctx.r3.u32+=2;break;case 1:ctx.r4.u32=0x28;break;
            case 2:ctx.r5.u32=0;break;case 3:ctx.r6.u32=2;break;case 4:ctx.r27.u32+=4;break;}
            rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});requireFieldsEmpty(base);
        }
        arguments(ctx);PPCContext other=ctx;rejects([&]{SimpsonsNativePipelineIndexCreate(other,base);});
        rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base+1);});
        const auto caps=PPC_LOAD_U32(0x82E3DFBC);PPC_STORE_U32(0x82E3DFBC,caps&~0x10000u);
        rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});PPC_STORE_U32(0x82E3DFBC,caps);
        for(uint32_t at=cursor;at<cursor+20;at+=4) {
            PPC_STORE_U32(at,0xBAD);rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});PPC_STORE_U32(at,0);
        }
        const auto access=runtime.pageAccess[field>>12].load();runtime.pageAccess[field>>12]=1;
        rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});runtime.pageAccess[field>>12]=access;
        runtime.stopping=true;rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});runtime.stopping=false;
        // Mark the native-ID range unavailable without mapping fake resources.
        std::array<uint8_t,256> previous{};
        for(unsigned i=0;i<256;++i) {previous[i]=runtime.pageAccess[0xA00+i].load();runtime.pageAccess[0xA00+i]=1;}
        rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});
        for(unsigned i=0;i<256;++i) runtime.pageAccess[0xA00+i]=previous[i];
        std::atomic<bool> wrongThreadRejected=false,allocationRejected=false;
        std::thread worker([&] {
            try {owner.validateCreated(0);} catch(const Failure&) {wrongThreadRejected=true;}
            try {EnginePipelineResources wrongBackendThread(backend);} catch(const std::exception&) {allocationRejected=true;}
        });worker.join();require(wrongThreadRejected && allocationRejected,"Owner/backend thread requirements were not enforced");

        initializeOriginal(cpu,base);stale=PPC_LOAD_U32(field);owner.validateCreated(stale);
        rejects([&]{owner.requireUnowned();});
        PPC_STORE_U32(field,0);rejects([&]{owner.requireUnowned();});PPC_STORE_U32(field,stale);
        require(stale>=0x00A00001 && stale<0x00B00000 && !runtime.pageAccess[stale>>12].load(),"Index identity overlaps guest memory");
        require(PPC_LOAD_U32(cursor)==0,"Original initializer cursor store missing");
        for(unsigned i=1;i<=3;++i) require(PPC_LOAD_U32(field+4*i)!=0,"Original pipeline declaration output missing");
        auto retained=owner.index(stale);std::weak_ptr<Graphics::Buffer> weak=retained;
        require(retained->type()==Graphics::BufferKind::Index16 && retained->byteSize()==0x1FFFE,"Wrong native backing format/size");
        std::vector<uint8_t> data(0x1FFFE);
        for(size_t i=0;i<data.size();++i) data[i]=uint8_t(i*29+i/17);
        backend.writeBuffer(retained,0,data);require(backend.readbackBuffer(retained)==data,"Real 1FFFE-byte index upload/readback failed");
        arguments(ctx);rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});
        ctx.r31.u32=0x82D507F4;ctx.r3.u32=stale+1;rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});
        ctx.r3.u32=stale;ctx.r31.u32+=4;rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});ctx.r31.u32-=4;
        runtime.pageAccess[field>>12]=1;rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});runtime.pageAccess[field>>12]=access;
        runtime.stopping=true;rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});runtime.stopping=false;
        owner.validateCreated(stale);
        cleanupOriginal(cpu,base);owner.requireReleased();declarationsOwner.requireReleased();requireFieldsEmpty(base);
        owner.requireUnowned();
        PPC_STORE_U32(field,stale);rejects([&]{owner.requireUnowned();});PPC_STORE_U32(field,0);
        require(!weak.expired() && backend.readbackBuffer(retained)==data,"Logical release destroyed independent native owner");
        rejects([&]{owner.index(stale);});ctx.r3.u32=stale;ctx.r31.u32=0x82D507F4;
        rejects([&]{SimpsonsNativePipelineIndexRelease(ctx,base);});
        arguments(ctx);rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});
        retained.reset();require(weak.expired(),"Final native index reference leaked");
        cleanupOriginal(cpu,base);owner.requireReleased(); // Original zero-field cleanup is idempotent.
    }
    arguments(ctx);rejects([&]{SimpsonsNativePipelineIndexCreate(ctx,base);});
    // Native exception after publication and zero/one/two successful declaration
    // calls: run actual original cleanup in the parent mode while scopes live.
    for(unsigned partial=0;partial<3;++partial) {
        EnginePipelineResources owner(backend);createDirect(cpu,base);
        auto id=PPC_LOAD_U32(field);require(id>stale,"ID reused across owner lifetimes");stale=id;
        owner.validateCreated(id);
        for(unsigned i=0;i<partial;++i) makeDeclaration(cpu,base,i);
        // A verified declaration service must fail explicitly on malformed data.
        PPC_STORE_U32(0x11000,0);PPC_STORE_U32(0x11004,0xDEADBEEF);
        PPC_STORE_U32(0x1100C,0x00FF0000);PPC_STORE_U32(0x11010,0xFFFFFFFF);PPC_STORE_U32(0x11014,0);
        ctx.r3.u32=0x11000;ctx.r4.u32=field+4+4*partial;
        rejects([&]{SimpsonsNativeDeclarationCreate(ctx,base);});
        cleanupOriginal(cpu,base);owner.requireReleased();declarationsOwner.requireReleased();requireFieldsEmpty(base);
    }
    for(unsigned cycle=0;cycle<32;++cycle) {
        EnginePipelineResources owner(backend);initializeOriginal(cpu,base);
        auto id=PPC_LOAD_U32(field);require(id>stale,"Repeated lifetime reused index identity");stale=id;
        owner.validateCreated(id);cleanupOriginal(cpu,base);owner.requireReleased();declarationsOwner.requireReleased();requireFieldsEmpty(base);
    }
    // Keep an unrelated declaration live in the enclosing scope (as real
    // scratch ownership will be). Pipeline cleanup must not drain that owner.
    PPC_STORE_U32(0x11000,0);PPC_STORE_U32(0x11004,0x002C23A5);PPC_STORE_U32(0x11008,0x5A);
    PPC_STORE_U32(0x1100C,0x00FF0000);PPC_STORE_U32(0x11010,0xFFFFFFFF);PPC_STORE_U32(0x11014,0x5A);
    ctx.r3.u32=0x11000;ctx.r4.u32=0x12000;SimpsonsNativeDeclarationCreate(ctx,base);
    const auto independentDeclaration=PPC_LOAD_U32(0x12000);
    {EnginePipelineResources owner(backend);initializeOriginal(cpu,base);owner.validateCreated(PPC_LOAD_U32(field));
        cleanupOriginal(cpu,base);owner.requireReleased();requireFieldsEmpty(base);}
    rejects([&]{declarationsOwner.requireReleased();});
    ctx.r3.u32=independentDeclaration;SimpsonsNativeDeclarationRelease(ctx,base);declarationsOwner.requireReleased();
    require(!backend.presentationCount() && !backend.screenDrawCount(),"Ownership test submitted a frame");
}
constexpr std::array<uint32_t,8> capabilityCases{
    0u,0x10000u,0xFFFFFFFFu,0xFFFEFFFFu,1u,0x80000000u,0x10001u,0x80010000u};
std::vector<std::string> auditRows(const std::filesystem::path& path) {
    std::ifstream input(path);require(bool(input),"Cannot read release grouping audit");
    std::vector<std::string> rows;std::string line;
    while(std::getline(input,line))rows.push_back(line);
    require(input.eof(),"Release grouping audit read was incomplete");return rows;
}
// Return the exact escaped JSON string value. Equality must compare the logger's
// real group, not a test-side reconstruction or a normalized surrogate.
std::string auditString(std::string_view row,std::string_view key) {
    const auto prefix="\""+std::string(key)+"\":\"";
    const auto at=row.find(prefix);require(at!=std::string_view::npos,"Release grouping receipt omitted a string field");
    const auto start=at+prefix.size();bool escaped=false;
    for(size_t end=start;end<row.size();++end) {
        const auto ch=row[end];
        if(escaped){escaped=false;continue;}
        if(ch=='\\'){escaped=true;continue;}
        if(ch=='\"')return std::string(row.substr(start,end-start));
    }
    throw Failure("Release grouping receipt has an unterminated string field");
}
size_t releaseEncounters(const std::vector<std::string>& rows) {
    return size_t(std::count_if(rows.begin(),rows.end(),[](const auto& row){
        return row.find("\"kind\":\"pipeline_index\"")!=std::string::npos&&
               row.find("\"event\":\"encounter\"")!=std::string::npos&&
               row.find("operation=release")!=std::string::npos;
    }));
}
struct ReleaseGrouping {
    std::string first,second,foreignOwner;
    std::vector<std::pair<std::string,std::string>> controls;
    size_t before{},afterFirst{},afterSecond{},afterForeign{};
    uint32_t caps{};
    PPCContext request{};
};
std::string rejectedReleaseSnapshot(Runtime& runtime,PPCContext& request,Graphics::NativeBackend& backend,
        const std::shared_ptr<Graphics::Buffer>& retained,const std::shared_ptr<Graphics::Buffer>& scratch,
        const std::filesystem::path& auditPath,const char* expected) {
    auto* base=runtime.base;
    const auto bytes=[&](uint32_t address,unsigned count){const auto* begin=runtime.pointer(address,count,false);
        return std::vector<uint8_t>(begin,begin+count);};
    const auto globals=bytes(cursor,20),caps=bytes(0x82E3DFBC,4),stack=bytes(0x10000,0x10000);
    const auto indexBytes=backend.readbackBuffer(retained),scratchBytes=backend.readbackBuffer(scratch);
    const auto nativeCounts=std::array{backend.bufferUploadCount(),backend.presentationCount(),backend.screenDrawCount(),
        backend.immediateDrawCount(),backend.recordingDrawCount(),backend.recordingExecutedDrawCount()};
    const auto indexRefs=retained.use_count(),scratchRefs=scratch.use_count();
    std::array<uint8_t,sizeof(PPCContext)> before{};std::memcpy(before.data(),&request,sizeof(request));
    const auto* tls=currentContext;const auto oldCsr=PPCFPSCRRegister::getcsr();const auto oldError=GetLastError();
    const auto probeCsr=(oldCsr&~0x6000u)|0x4000u;
    PPCFPSCRRegister::restoreHostCSR(probeCsr);SetLastError(0x65A0C0DE);
    bool failed=false;std::string reason;
    try {SimpsonsNativePipelineIndexRelease(request,base);}catch(const Failure& error){failed=true;reason=error.what();}
    const auto returnedCsr=PPCFPSCRRegister::getcsr();const auto returnedError=GetLastError();
    PPCFPSCRRegister::restoreHostCSR(oldCsr);SetLastError(oldError);
    require(failed&&reason==expected,"Release grouping probe rejected at the wrong frontier");
    require(!std::memcmp(before.data(),&request,sizeof(request))&&currentContext==tls,
            "Release grouping rejection changed full PPC state or current context");
    require(returnedCsr==probeCsr&&returnedError==0x65A0C0DE,"Release grouping prevalidation changed CSR or LastError");
    require(bytes(cursor,20)==globals&&bytes(0x82E3DFBC,4)==caps&&bytes(0x10000,0x10000)==stack,
            "Release grouping rejection changed guest globals, capability or caller stack/temporary bytes");
    require(backend.readbackBuffer(retained)==indexBytes&&backend.readbackBuffer(scratch)==scratchBytes&&
            retained.use_count()==indexRefs&&scratch.use_count()==scratchRefs,
            "Release grouping rejection changed retained native backing or references");
    require(nativeCounts==std::array{backend.bufferUploadCount(),backend.presentationCount(),backend.screenDrawCount(),
        backend.immediateDrawCount(),backend.recordingDrawCount(),backend.recordingExecutedDrawCount()},
            "Release grouping rejection submitted native work");
    // observe() may suppress the duplicate. failure() must expose the latest
    // raw request even when no second encounter row was written.
    runtime.resourceAudit.failure(reason);const auto rows=auditRows(auditPath);
    require(!rows.empty()&&auditString(rows.back(),"event")=="failure"&&
            auditString(rows.back(),"kind")=="pipeline_index"&&auditString(rows.back(),"reason")==expected,
            "Rejected release lost its latest prevalidation failure snapshot");
    return rows.back();
}
ReleaseGrouping releaseGroupingProbes(Runtime& runtime,EngineCpuCalls& cpu,Graphics::NativeBackend& backend,
        EnginePipelineResources& owner,const std::shared_ptr<Graphics::Buffer>& retained,
        const std::shared_ptr<Graphics::Buffer>& scratch,const std::filesystem::path& auditPath,unsigned which,uint32_t id) {
    auto* base=runtime.base;auto& ctx=cpu.registers();const auto saved=ctx;
    const auto caps=PPC_LOAD_U32(0x82E3DFBC);
    const auto action="release-grouping-case-"+std::to_string(which);
    runtime.resourceAudit.mission("fixture:pipeline-capability");runtime.resourceAudit.action(action);
    ReleaseGrouping result;result.request=ctx;result.caps=caps;
    result.request.r3.u32=id+1;result.request.r31.u32=0x82D507F4;
    result.request.r4.u32=0x11112222;result.request.r5.u32=0x33334444;
    result.request.r6.u32=0x55556666;result.request.r27.u32=0x77778888;
    constexpr auto mismatch="Unknown, stale or mismatched pipeline index release";
    const auto probe=[&]{
        const auto row=rejectedReleaseSnapshot(runtime,ctx,backend,retained,scratch,auditPath,mismatch);
        require(retained->type()==Graphics::BufferKind::Index16&&retained->byteSize()==0x1FFFE,
                "Rejected release changed native buffer type or extent");return row;
    };
    ctx=result.request;result.before=releaseEncounters(auditRows(auditPath));result.first=probe();
    result.afterFirst=releaseEncounters(auditRows(auditPath));owner.validateCreated(id);
    ctx=result.request;ctx.r4.u32=0x9999AAAA;ctx.r5.u32=0xBBBBCCCC;ctx.r6.u32=0xDDDDEEEE;ctx.r27.u32=0xFFFF0000;
    result.second=probe();result.afterSecond=releaseEncounters(auditRows(auditPath));owner.validateCreated(id);
    ctx=result.request;ctx.r3.u32=id+2;result.foreignOwner=probe();
    result.afterForeign=releaseEncounters(auditRows(auditPath));owner.validateCreated(id);
    const auto control=[&](const char* name){result.controls.emplace_back(name,probe());};
    ctx=result.request;ctx.r31.u32+=4;control("global-r31");owner.validateCreated(id);
    ctx=result.request;ctx.lr^=4;control("caller");owner.validateCreated(id);
    ctx=result.request;runtime.resourceAudit.mission("fixture:pipeline-other-mission");control("mission");
    runtime.resourceAudit.mission("fixture:pipeline-capability");owner.validateCreated(id);
    ctx=result.request;runtime.resourceAudit.action(action+"-different");control("action");
    runtime.resourceAudit.action(action);owner.validateCreated(id);
    ctx=result.request;PPC_STORE_U32(0x82E3DFBC,caps^0x10000u);control("caps");
    PPC_STORE_U32(0x82E3DFBC,caps);owner.validateCreated(id);
    // Correct r3 changes the semantic owner-match class, but the deliberately
    // foreign published field still forces the exact guarded rejection.
    ctx=result.request;ctx.r3.u32=id;PPC_STORE_U32(field,id+1);control("owner-match");
    PPC_STORE_U32(field,id);owner.validateCreated(id);
    PPCContext foreign=result.request;
    result.controls.emplace_back("context",rejectedReleaseSnapshot(runtime,foreign,backend,retained,scratch,auditPath,
        "Pipeline index hook has an invalid base or original callback context"));owner.validateCreated(id);
    require(owner.index(id)==retained,"Rejected release controls changed the registered native owner");
    ctx=saved;runtime.resourceAudit.action("capability-case-"+std::to_string(which));return result;
}
void verifyReleaseGrouping(const ReleaseGrouping& result,const std::filesystem::path& path,unsigned which) {
    const auto group=auditString(result.first,"group");
    const bool same=group==auditString(result.second,"group");
    std::printf("[PIPELINE RELEASE GROUP PROBE] case=%u equal=%u first=%s second=%s receipts=%s\n",
                which,unsigned(same),group.c_str(),auditString(result.second,"group").c_str(),path.string().c_str());
    require(same,"Release grouping incorrectly keys unqualified r4/r5/r6/r27 lanes");
    require(group==auditString(result.foreignOwner,"group"),"Foreign owner instance changed its stable mismatch class");
    require(result.afterFirst==result.before+1&&result.afterSecond==result.afterFirst&&result.afterForeign==result.afterSecond,
            "Repeated release instances emitted a new encounter instead of refreshing latest context");
    const auto firstInstance=auditString(result.first,"instance"),lastInstance=auditString(result.second,"instance");
    for(const auto text:{"r4_raw=11112222","r5_raw=33334444","r6_raw=55556666","r27_raw=77778888"})
        require(firstInstance.find(text)!=std::string::npos,"First release instance lost an excluded raw lane");
    for(const auto text:{"r4_raw=9999AAAA","r5_raw=BBBBCCCC","r6_raw=DDDDEEEE","r27_raw=FFFF0000"})
        require(lastInstance.find(text)!=std::string::npos,"Duplicate failure did not retain the latest excluded raw lane");
    char rawOwner[32];std::snprintf(rawOwner,sizeof(rawOwner),"r3_raw=%08X",result.request.r3.u32+1);
    require(auditString(result.foreignOwner,"instance").find(rawOwner)!=std::string::npos,
            "Foreign owner failure did not retain its latest raw identity");
    for(size_t index=0;index<result.controls.size();++index) {
        const auto& [name,row]=result.controls[index];const auto controlGroup=auditString(row,"group");
        require(controlGroup!=group,"Semantic/global/caller/owner/mission/action/caps variant was collapsed");
        for(size_t previous=0;previous<index;++previous)
            require(controlGroup!=auditString(result.controls[previous].second,"group"),"Different semantic release controls share a group");
        const auto controlParameters=auditString(row,"parameters"),ownership=auditString(row,"ownership");
        if(name=="global-r31")require(controlParameters.find("r31=82D507F8")!=std::string::npos,"Global release variant was not observed");
        else if(name=="caller")require(row.find("\"caller\":"+std::to_string(uint32_t(result.request.lr)^4u)+",")!=std::string::npos,
                                      "Caller release variant was not observed");
        else if(name=="mission")require(auditString(row,"mission")=="fixture:pipeline-other-mission","Mission release variant was not observed");
        else if(name=="action")require(auditString(row,"last_action")=="release-grouping-case-"+std::to_string(which)+"-different",
                                      "Action release variant was not observed");
        else if(name=="caps") {char word[32];std::snprintf(word,sizeof(word),"caps=%08X",result.caps^0x10000u);
            require(controlParameters.find(word)!=std::string::npos,"Capability release variant was not observed");}
        else if(name=="owner-match")require(controlParameters.find("r3=owner-match")!=std::string::npos,"Owner-match release variant was not observed");
        else if(name=="context")require(ownership.find("context=mismatch")!=std::string::npos,"Context release variant was not observed");
        else if(name=="released-phase")require(ownership.find("phase=2 backing=0")!=std::string::npos,"Released-phase variant was not observed");
        else require(false,"Unrecognized semantic release control");
    }
    const auto parameters=auditString(result.second,"parameters");
    require(parameters.find("operation=release producer=82416BC8 callsite=82416BEC")!=std::string::npos&&
            parameters.find("r3=owner-mismatch")!=std::string::npos&&parameters.find("r31=82D507F4")!=std::string::npos&&
            parameters.find("caps=")!=std::string::npos,"Release normalization removed a qualified boundary/global/owner/caps field");
    for(const auto lane:{" r4="," r5="," r6="," r27="})
        require(parameters.find(lane)==std::string::npos,"Release stable parameters retain an unqualified register lane");
    std::printf("AUDIT_PIPELINE_RELEASE_GROUPING case=%u original_create_use_release=passed transient_lanes=instance latest_duplicate=passed owner_instance=excluded semantic_controls=distinct ppc=preserved csr=preserved last_error=preserved guest_bytes=preserved native_backing=preserved declarations=original_release\n",which);
}
void capabilityLifetime(Runtime& runtime,EngineCpuCalls& cpu,Graphics::NativeBackend& backend,unsigned which,
                        const std::filesystem::path& auditPath) {
    auto* base=runtime.base;auto& ctx=cpu.registers();const auto caps=capabilityCases.at(which);
    PPC_STORE_U32(0x82E3DFBC,caps);
    runtime.resourceAudit.action("capability-case-"+std::to_string(which));
    auto scratchIndex=backend.createBuffer(0x4E20,Graphics::BufferKind::Index16);
    const std::vector<uint8_t> scratchBytes(0x4E20,0x3C);
    backend.writeBuffer(scratchIndex,0,scratchBytes);
    EngineScratchResources declarationsOwner(scratchIndex);EnginePipelineResources owner(backend);
    const auto mode=(caps&0x10000u)?0u:2u;
    for(const auto bad:{0u,1u,2u,3u,0xFFFFFFFFu})if(bad!=mode) {
        arguments(ctx);ctx.r6.u32=bad;const auto before=ctx;
        const auto fp=PPCFPSCRRegister::getcsr();SetLastError(0x65A04321);bool rejected=false;
        try{SimpsonsNativePipelineIndexCreate(ctx,base);}catch(const Failure& error){
            rejected=true;
            require(!std::memcmp(&ctx,&before,sizeof(ctx)),"Rejected capability input changed original context");
            require(PPCFPSCRRegister::getcsr()==fp&&GetLastError()==0x65A04321,"Prevalidation audit changed host state");
            runtime.resourceAudit.failure(error.what());
        }
        require(rejected,"Malformed capability/input correlation was admitted");owner.requireUnowned();requireFieldsEmpty(base);
    }
    initializeOriginal(cpu,base);const auto id=PPC_LOAD_U32(field);owner.validateCreated(id);
    for(unsigned i=1;i<=3;++i)require(PPC_LOAD_U32(field+4*i)!=0,"Capability branch lost original declarations");
    auto retained=owner.index(id);std::weak_ptr<Graphics::Buffer> weak=retained;
    require(retained->type()==Graphics::BufferKind::Index16&&retained->byteSize()==0x1FFFE,"Capability branch changed original backing");
    std::vector<uint8_t> data(0x1FFFE);
    for(size_t i=0;i<data.size();++i)data[i]=uint8_t(i*13+i/29+which);
    backend.writeBuffer(retained,0,data);require(backend.readbackBuffer(retained)==data,"Capability branch upload/use differed");
    // Finish the original lifetime even on the expected test-first grouping
    // failure: defer signature assertions until after cleanup/final native free.
    auto grouping=releaseGroupingProbes(runtime,cpu,backend,owner,retained,scratchIndex,auditPath,which,id);
    cleanupOriginal(cpu,base);owner.requireReleased();declarationsOwner.requireReleased();requireFieldsEmpty(base);
    require(!weak.expired()&&backend.readbackBuffer(retained)==data,"Capability branch retired an independent native owner");
    const auto saved=ctx;ctx=grouping.request;
    runtime.resourceAudit.action("release-grouping-case-"+std::to_string(which));
    grouping.controls.emplace_back("released-phase",rejectedReleaseSnapshot(runtime,ctx,backend,retained,scratchIndex,auditPath,
        "Unknown, stale or mismatched pipeline index release"));
    ctx=saved;runtime.resourceAudit.action("capability-case-"+std::to_string(which));
    owner.requireReleased();declarationsOwner.requireReleased();requireFieldsEmpty(base);
    rejects([&]{owner.index(id);});retained.reset();require(weak.expired(),"Capability branch leaked final native backing");
    cleanupOriginal(cpu,base);owner.requireReleased();
    std::printf("[ORIGINAL PIPELINE CAPABILITY] case=%u caps=%08X original_r6=%u create/use/readback/CPU-declarations/release=passed final-backing-owner=expired malformed=passed prevalidation=passed\n",which,caps,mode);
    verifyReleaseGrouping(grouping,auditPath,which);
}
void evidence(const char* path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Cannot open original byte evidence");
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(image.size()==15466496,"Unexpected original flat image size");
    for(auto [pc,w]:std::array<std::pair<uint32_t,uint32_t>,26>{{
        {0x82416C70,0x3C600001},{0x82416C74,0x38A00001},{0x82416C78,0x38800008},
        {0x82416C80,0x816B001C},{0x82416C88,0x7D6B58F8},
        {0x82416C7C,0x6063FFFE},{0x82416C8C,0x55668FBC},{0x82416C94,0x3B6B07FC},
        {0x82416C9C,0x917BFFF0},{0x82416CA0,0x4802AD69},{0x82416CA4,0x907BFFF4},
        {0x82416D6C,0x4BFD8ACD},{0x82416DDC,0x4BFD8A5D},{0x82416E30,0x4BFD8A09},
        {0x82416BDC,0x3BEB07F4},{0x82416BEC,0x4802AB1D},{0x82416BF0,0x39600000},
        {0x82416BF4,0x917FFFFC},{0x82416C04,0x4BFD8E15},{0x82416C1C,0x4BFD8DFD},
        {0x82416C34,0x4BFD8DE5},{0x82416E34,0x7EA3AB78},{0x82416E40,0x4BFFFD89},
        {0x82441A14,0x7C7C1B78},{0x82441A18,0x7C9D2378},{0x82441A24,0x7CBE2B78}}}) {
        const auto* p=image.data()+(pc-0x82000000);
        require(((uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3])==w,"Original pipeline hook/ABI evidence changed");
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>4) throw Failure("Usage: EnginePipelineResourcesTests original-flat-image [capability-case0..7 [--hardware]]");
        unsigned selected=unsigned(capabilityCases.size());
        if(argc>=3){const auto end=argv[2]+std::strlen(argv[2]);const auto result=std::from_chars(argv[2],end,selected);
            require(result.ec==std::errc{}&&result.ptr==end&&selected<capabilityCases.size(),"Invalid independent capability case");}
        const bool hardware=argc==4;
        require(!hardware||!std::strcmp(argv[3],"--hardware"),"Invalid pipeline backend option");
        evidence(argv[1]);Runtime runtime;
#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
        runtime.map(0x82D50000,0x1000,true,"pipeline owner globals");
        runtime.map(0x82E3D000,0x1000,true,"capability fixture");
#else
        runtime.load(argv[1]);
#endif
        runtime.map(0x10000,0x10000,true,"pipeline test stack/temporary declaration");
        PPCContext incoming{};incoming.r1.u32=0x20000;incoming.lr=0x12345678;
        currentContext=&incoming;
        const auto auditPath=std::filesystem::current_path()/("pipeline-capability-"+std::to_string(GetCurrentProcessId())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        runtime.resourceAudit.configure(auditPath);runtime.resourceAudit.mission("fixture:pipeline-capability");
        {EngineCpuCalls cpu(incoming,runtime.base);Graphics::NativeBackend backend(!hardware);
            if(selected<capabilityCases.size())capabilityLifetime(runtime,cpu,backend,selected,auditPath);
            else {tests(runtime,cpu,backend);for(unsigned i=0;i<capabilityCases.size();++i)capabilityLifetime(runtime,cpu,backend,i,auditPath);}}
        std::ifstream auditInput(auditPath);const std::string receipts{std::istreambuf_iterator<char>(auditInput),std::istreambuf_iterator<char>()};
        require(receipts.find("\"kind\":\"pipeline_index\"")!=std::string::npos&&receipts.find("\"event\":\"failure\"")!=std::string::npos,
            "Malformed pipeline rejection lacks its preceding producer snapshot");
        require(receipts.find("producer=82416C58 callsite=82416CA0")!=std::string::npos&&receipts.find("fixture:pipeline-capability")!=std::string::npos,
            "Pipeline prevalidation snapshot lacks original caller or fixture ownership context");
        std::printf("[PIPELINE AUDIT] preserved=%s\n",auditPath.string().c_str());
        require(currentContext==&incoming && incoming.r1.u32==0x20000 && incoming.lr==0x12345678,"Caller/TLS context was not preserved");
#ifdef SIMPSONS_PIPELINE_RESOURCES_STANDALONE
        std::printf("PASS: pipeline owner standalone / %zu checks; real native index + production declaration hooks; original AOT integration pending\n",checks);
#else
        std::printf("PASS: original pipeline init/cleanup with real native index/declarations / %zu checks\n",checks);
#endif
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
