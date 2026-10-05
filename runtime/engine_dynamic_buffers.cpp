#include "engine_dynamic_buffers.h"
#include "engine_cpu_calls.h"
#include "renderer/driver_resources.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace Simpsons {
namespace {
constexpr uint32_t globals=0x82D0D0C4,head=0x82D0D0D4,current=0x82D0D0DC;
constexpr uint32_t cursors=0x82D0D0E0,sizes=0x82D0D0F0,outputs=0x82D0D100;
constexpr uint32_t createPool=0x823FBEA8,destroyPool=0x823FB708,hint=0x00040411;
constexpr std::array<uint32_t,4> poolFields={0x82D0D0C8,0x82D0D0CC,0x82D0D0D0,0x82D0D0D8};
constexpr std::array<uint32_t,4> elementSizes={16,20,8,20},elementCounts={16,100,100,42};
constexpr std::array<uint32_t,4> poolReturnPC={0x823FCF94,0x823FCFB4,0x823FCFCC,0x823FCFE4};
constexpr std::array<uint32_t,4> destroyReturnPC={0x823FCF4C,0x823FCF34,0x823FCF1C,0x823FCE4C};
// Disjoint from scratch index Cxxxxx and declaration Bxxxxx native IDs. Never
// reuse IDs, including after partial creation. They must remain unmapped.
std::atomic<uint32_t> nextId{0x00D00001};
uint32_t reserveId(Runtime& rt) {
    uint32_t id=nextId.load();
    while(id<0x00E00000) {
        if(nextId.compare_exchange_weak(id,id+1)) {
            if(rt.pageAccess[id>>12].load()) throw Failure("Dynamic native buffer ID overlaps mapped guest memory");
            return id;
        }
    }
    throw Failure("Dynamic native buffer identity space exhausted");
}
uint32_t read(Runtime& rt,uint32_t address) {
    const auto* p=rt.pointer(address,4,false);
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
uint32_t checkedWord(const uint8_t* bytes) {
    uint32_t v{};std::memcpy(&v,bytes,sizeof(v));
    return __builtin_bswap32(v);
}
void writeBytes(uint8_t* p,uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i) p[i]=uint8_t(value>>(24-8*i));
}
void write(Runtime& rt,uint32_t address,uint32_t value) {writeBytes(rt.pointer(address,4,true),value);}
void clearArrays(Runtime& rt) {
    auto* p=rt.pointer(current,0x34,true);
    for(unsigned offset=0;offset<0x34;offset+=4) writeBytes(p+offset,0);
}
std::string reason(std::exception_ptr error) {
    try {std::rethrow_exception(error);} catch(const std::exception& e) {return e.what();}
    catch(...) {return "non-standard exception";}
}
struct Busy {
    bool& flag;
    explicit Busy(bool& value):flag(value) {if(flag) throw Failure("Reentrant native dynamic buffer operation");flag=true;}
    ~Busy() {flag=false;}
};
}

EngineDynamicBuffers::EngineDynamicBuffers():thread_(GetCurrentThreadId()) {}
EngineDynamicBuffers::~EngineDynamicBuffers() {
    // Guest callbacks require a live, explicit EngineCpuCalls frame. Never hide
    // callback failure inside a noexcept destructor or call a stale frame here.
    if(hasOwnership()) std::fprintf(stderr,"[NATIVE ENGINE] dynamic buffer owner destroyed before successful stop; guest ownership remains incomplete\n");
}
void EngineDynamicBuffers::requireCaller(Runtime& rt,EngineCpuCalls& cpu,uint8_t* base) const {
    if(GetCurrentThreadId()!=thread_ || active!=&rt || base!=rt.base || currentContext!=&cpu.registers() ||
       (runtime_ && runtime_!=&rt)) throw Failure("Invalid native dynamic buffer caller/runtime/owner thread");
    rt.checkRunning();
}

void EngineDynamicBuffers::start(Runtime& rt,EngineCpuCalls& cpu,uint8_t* base,const Graphics::StartupResources& startup) {
    requireCaller(rt,cpu,base);
    Busy busy(busy_);
    if(hasOwnership()) throw Failure("Native dynamic buffers already own startup state; stop before restart");
    // Preflight all immutable inputs before touching original ownership fields.
    rt.pointer(globals,0x4C,true);
    if(read(rt,globals) || read(rt,head)) throw Failure("Dynamic startup requires empty original CPU ownership lists");
    for(auto field:poolFields) if(read(rt,field)) throw Failure("Dynamic startup found a foreign original pool");
    for(unsigned i=0;i<4;++i) if(read(rt,outputs+4*i)) throw Failure("Dynamic startup found a foreign vertex buffer slot");
    if(!(read(rt,0x82E3DFBC)&0x10000)) throw Failure("Unimplemented original single-buffer capability path");
    const auto& resources=startup.resources();
    for(unsigned i=0;i<4;++i) {
        const auto& buffer=resources.vertices[i];
        if(!buffer || buffer->type()!=Graphics::BufferKind::Vertex || buffer->byteSize()!=0x40000)
            throw Failure("Dynamic startup lacks four original-sized native vertex buffers");
        for(unsigned j=0;j<i;++j) if(buffer==resources.vertices[j]) throw Failure("Dynamic vertex roles alias one resource");
    }
    const uint32_t engine=read(rt,0x82D0CA68);
    if(uint64_t(engine)+0x140>0x100000000ull) throw Failure("Engine callback table wraps guest memory");
    rt.pointer(engine,0x140,false);
    if(!read(rt,engine+0x138) || !read(rt,engine+0x13C)) throw Failure("Missing original engine pool callbacks");
    std::array<uint32_t,4> ids;
    for(auto& id:ids) id=reserveId(rt);

    runtime_=&rt;engine_=engine;
    for(unsigned i=0;i<4;++i) {slots_[i].buffer=resources.vertices[i];slots_[i].id=ids[i];}
    try {
        // Original 823FCF74 calls stop first. Empty-list preflight makes these
        // exact array clears its only mutable effects in this supported scope.
        clearArrays(rt);
        for(unsigned i=0;i<4;++i) {
            rt.checkRunning();
            auto* destination=rt.pointer(poolFields[i],4,true);
            cpu.registers().lr=poolReturnPC[i];
            pools_[i]=cpu.invoke(createPool,elementSizes[i],elementCounts[i],4,hint);
            if(!pools_[i]) throw Failure("Original dynamic CPU pool allocation returned zero");
            writeBytes(destination,pools_[i]);
        }
        for(unsigned i=0;i<4;++i) {
            rt.checkRunning();
            auto* output=rt.pointer(outputs+4*i,4,true);
            auto* listHead=rt.pointer(head,4,true);
            write(rt,cursors+4*i,0);write(rt,sizes+4*i,0x40000);
            cpu.registers().lr=0x823FC7A0;
            auto& slot=slots_[i];
            slot.record=cpu.invoke(read(rt,engine_+0x138),pools_[3],hint);
            if(!slot.record) throw Failure("Original dynamic CPU record allocation returned zero");
            auto* record=rt.pointer(slot.record,20,true);
            // Equivalent final fields of 823FC7A8..7F8; all native resources
            // already exist, so there is no fabricated SDK object or creation.
            writeBytes(record,0x40000);writeBytes(record+4,1);writeBytes(record+8,slot.id);
            writeBytes(record+12,outputs+4*i);writeBytes(record+16,read(rt,head));
            writeBytes(listHead,slot.record);slot.linked=true;
            writeBytes(output,slot.id);
        }
        validateOwned(rt,base);
        rt.checkRunning();
        initialized_=true;
    } catch(...) {
        auto original=std::current_exception();
        try {cleanup(rt,cpu,base);} catch(...) {
            throw Failure("Dynamic startup failed: "+reason(original)+"; original CPU rollback remains incomplete: "+reason(std::current_exception()));
        }
        std::rethrow_exception(original);
    }
}

void EngineDynamicBuffers::validateOwned(Runtime& rt,uint8_t*) const {
    if(read(rt,0x82D0CA68)!=engine_) throw Failure("Dynamic engine owner changed during its lifetime");
    const auto* fields=rt.pointer(globals,0x4C,false);
    const auto field=[&](uint32_t address){return checkedWord(fields+(address-globals));};
    if(field(globals)) throw Failure("Dynamic cleanup cannot drain unimplemented mesh ownership list D0C4");
    for(unsigned i=0;i<4;++i) if(field(poolFields[i])!=pools_[i])
        throw Failure("Original dynamic pool field no longer matches its native owner");
    uint32_t expected=0;
    for(unsigned i=0;i<4;++i) {
        const auto& slot=slots_[i];
        const uint32_t output=field(outputs+4*i);
        if(!slot.linked) {
            if(output) throw Failure("Unexpected dynamic output without a linked CPU record");
            continue;
        }
        const auto* record=rt.pointer(slot.record,20,true);
        if(checkedWord(record)!=0x40000 || checkedWord(record+8)!=slot.id ||
           checkedWord(record+16)!=expected || output!=(cleaning_?0:slot.id) ||
           checkedWord(record+4)!=(cleaning_?0u:1u) || checkedWord(record+12)!=(cleaning_?0:outputs+4*i))
            throw Failure("Dynamic CPU record/list/output changed outside verified startup scope");
        expected=slot.record;
    }
    if(field(head)!=expected) throw Failure("Original dynamic list contains foreign or reordered ownership");
}

void EngineDynamicBuffers::cleanup(Runtime& rt,EngineCpuCalls& cpu,uint8_t* base) {
    rt.checkRunning();
    validateOwned(rt,base); // Check the complete list before any clear or release.
    initialized_=false;
    // Prevalidate the entire detachment block before its nonthrowing writes.
    // A subsequent callback failure retains a resumable cleanup phase.
    if(!cleaning_) {
        auto* arrays=rt.pointer(current,0x34,true);
        std::array<uint8_t*,4> records{};
        for(unsigned i=0;i<4;++i) if(slots_[i].linked) records[i]=rt.pointer(slots_[i].record,20,true);
        for(unsigned offset=0;offset<0x34;offset+=4) writeBytes(arrays+offset,0);
        for(auto* record:records) if(record) {writeBytes(record+4,0);writeBytes(record+12,0);}
        cleaning_=true;
    }
    for(unsigned n=4;n>0;--n) {
        auto& slot=slots_[n-1];
        if(slot.record) {
            rt.checkRunning();
            const uint32_t next=slot.linked?read(rt,slot.record+16):read(rt,head);
            auto* listHead=rt.pointer(head,4,true);
            const uint32_t releaseEntry=read(rt,engine_+0x13C);
            if(slot.linked) write(rt,slot.record+8,0);
            // Native equivalent of SDK release: drop this owner's shared ref.
            // StartupResources/other real owners may still retain the VB.
            slot.buffer.reset();slot.id=0;
            cpu.registers().lr=0x823FCE2C;
            cpu.invoke(releaseEntry,pools_[3],slot.record);
            slot.record=0;slot.linked=false;writeBytes(listHead,next);
        } else {slot.buffer.reset();slot.id=0;}
    }
    // Exact pool destroy order D8,D0,CC,C8. D0C4 is empty in this scope.
    for(unsigned n=4;n>0;--n) {
        const unsigned i=n-1;
        if(pools_[i]) {
            rt.checkRunning();
            auto* output=rt.pointer(poolFields[i],4,true);
            cpu.registers().lr=destroyReturnPC[i];
            cpu.invoke(destroyPool,pools_[i]);
            pools_[i]=0;writeBytes(output,0);
        }
    }
    runtime_=nullptr;engine_=0;cleaning_=false;
}

void EngineDynamicBuffers::stop(Runtime& rt,EngineCpuCalls& cpu,uint8_t* base) {
    requireCaller(rt,cpu,base);
    Busy busy(busy_);
    if(hasOwnership()) cleanup(rt,cpu,base);
}
std::shared_ptr<Graphics::Buffer> EngineDynamicBuffers::buffer(uint32_t id) const {
    if(GetCurrentThreadId()!=thread_ || busy_ || !initialized_ || !runtime_ || !id)
        throw Failure("Dynamic native buffer lookup outside its initialized owner scope");
    for(const auto& slot:slots_) if(slot.id==id && slot.record && slot.buffer) return slot.buffer;
    char reason[180];int n=std::snprintf(reason,sizeof(reason),"Unknown or stale dynamic native buffer ID=%08X function=%08X caller=%08X",
        id,currentContext?currentContext->lastFunction:0,currentContext?uint32_t(currentContext->lr):0);
    if(n<0||size_t(n)>=sizeof(reason)) throw Failure("Unknown dynamic buffer: diagnostic truncated");
    throw Failure(reason);
}
void EngineDynamicBuffers::validateOwnership(Runtime& rt,uint8_t* base) const {
    if(GetCurrentThreadId()!=thread_ || busy_ || !initialized_ || runtime_!=&rt || active!=&rt || base!=rt.base)
        throw Failure("Dynamic vertex ownership validation is outside its live runtime/thread");
    rt.checkRunning();validateOwned(rt,base);
}
}
