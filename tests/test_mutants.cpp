// Real native mutants and PPC imports; no original image, mock ownership or
// forced thread termination. NativeMutantTests links SimpsonsRuntime.
#include "runtime/runtime.h"
#include "runtime/stall_profiler.h"
#include "ppc_recomp_shared.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

namespace {
using Simpsons::Runtime;
using Simpsons::KernelHandle;
constexpr uint32_t success=0,timeout=0x102,abandoned=0x80;
constexpr uint32_t invalidParameter=0xc000000d,invalidHandle=0xc0000008,notOwned=0xc0000046;
constexpr uint32_t output=0x10040,pollTimeout=0x10080,boundedTimeout=0x10088;

void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
template<class F> void rejects(F&& action,const char* message,const char* reason="") {
    try {action();}
    catch(const Simpsons::Failure& error) {
        require(std::string(error.what()).find(reason)!=std::string::npos,"Unexpected mutant rejection reason");
        return;
    }
    throw std::runtime_error(message);
}
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle):value(handle) {require(value && value!=INVALID_HANDLE_VALUE,"Fixture handle creation failed");}
    ~Handle() {CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
HANDLE duplicate(HANDLE original) {
    HANDLE result{};
    require(DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&result,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,
            "Fixture handle duplication failed");
    return result;
}
// Every worker body uses either polling or one native wait bounded to two
// seconds. join observes actual OS thread exit, including mutant abandonment.
struct Worker {
    std::exception_ptr error;
    std::jthread thread;
    template<class F> explicit Worker(F body):thread([this,body] {
        try {body();} catch(...) {error=std::current_exception();}
    }) {}
    void finish() {thread.join();if(error) std::rethrow_exception(error);}
};

uint32_t create(Runtime& rt,uint32_t address=output,uint32_t attributes=0,uint32_t initiallyOwned=0) {
    PPCContext ctx{};ctx.r3.u64=address;ctx.r4.u64=attributes;ctx.r5.u64=initiallyOwned;ctx.lr=0x82b76618;
    __imp__NtCreateMutant(ctx,rt.base);
    require(ctx.r3.u64==ctx.r3.u32 && ctx.lr==0x82b76618,"Create returned a malformed NT status or changed LR");
    return ctx.r3.u32;
}
uint32_t makeMutant(Runtime& rt,uint32_t owned=0) {
    require(create(rt,output,0,owned)==success,"Native mutant creation failed");
    auto* base=rt.base;const uint32_t id=PPC_LOAD_U32(output);
    const auto object=rt.getHandle(id);
    require(id && object && object->native && object->type==KernelHandle::Type::Mutant,"Create did not publish an owned Mutant handle");
    const std::array<uint8_t,4> expected={uint8_t(id>>24),uint8_t(id>>16),uint8_t(id>>8),uint8_t(id)};
    require(std::memcmp(rt.pointer(output,4,false),expected.data(),4)==0,"Mutant handle output was not big endian");
    return id;
}
uint32_t release(Runtime& rt,uint32_t handle,uint32_t reserved=0) {
    PPCContext ctx{};ctx.r3.u64=handle;ctx.r4.u64=reserved;ctx.lr=0x82b76674;
    __imp__NtReleaseMutant(ctx,rt.base);
    require(ctx.r3.u64==ctx.r3.u32 && ctx.lr==0x82b76674,"Release returned a malformed NT status or changed LR");
    return ctx.r3.u32;
}
uint32_t wait(Runtime& rt,uint32_t handle,bool block=false) {
    PPCContext ctx{};ctx.r3.u64=handle;ctx.r4.u64=1;ctx.r5.u64=0;ctx.r6.u64=block?boundedTimeout:pollTimeout;
    __imp__NtWaitForSingleObjectEx(ctx,rt.base);
    require(ctx.r3.u64==ctx.r3.u32,"Wait returned a malformed NT status");
    return ctx.r3.u32;
}
uint32_t close(Runtime& rt,uint32_t handle) {
    PPCContext ctx{};ctx.r3.u64=handle;__imp__NtClose(ctx,rt.base);return ctx.r3.u32;
}
void awaitWaitLease(const std::shared_ptr<KernelHandle>& object) {
    // Baseline references are the registry and this fixture's shared_ptr. The
    // blocked real wait import supplies the third. The mutex remains owned by
    // this thread, so a successful acquisition cannot race this observation.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(object.use_count()<3 && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(object.use_count()>=3,"Real wait import failed to retain its handle lease within the bound");
}

void invalidCreationContracts(Runtime& rt) {
    auto* base=rt.base;
    PPC_STORE_U32(output,0xdeadbeef);
    const auto count=rt.handles.size();const auto next=rt.nextHandle;
    require(create(rt,0)==invalidParameter,"Null mutant output did not return INVALID_PARAMETER");
    for(uint32_t address:std::array<uint32_t,3>{0x20000,0x30000,0x10ffe})
        rejects([&]{create(rt,address);},"Readonly/unmapped/straddling output accepted");
    // The attributes value is deliberately unmapped: rejection must precede
    // allocation and must not attempt to decode an unverified guest structure.
    rejects([&]{create(rt,output,0x30000,1);},"Named mutant creation succeeded","Named mutant attributes");
    require(rt.handles.size()==count && rt.nextHandle==next && PPC_LOAD_U32(output)==0xdeadbeef,
            "Rejected create allocated a guest handle or changed its output");
    PPCContext bad{};bad.r3.u64=output;
    rejects([&]{__imp__NtCreateMutant(bad,base+1);},"Create accepted a foreign guest base","Invalid native mutant runtime");
    require(rt.handles.size()==count && PPC_LOAD_U32(output)==0xdeadbeef,"Invalid runtime create mutated ownership/output");
}

void recursiveOwnershipContracts(Runtime& rt) {
    auto* base=rt.base;
    const uint32_t id=makeMutant(rt,0x100); // Nonzero u32 with a zero low byte.
    auto object=rt.getHandle(id);
    const uint64_t initialOwner=object->stallMutantOwner.load(std::memory_order_relaxed);
    require(!Simpsons::StallProfiler::enabled || uint32_t(initialOwner>>32)==GetCurrentThreadId(),
            "Initially owned mutant diagnostic did not identify its native owner");
    require(wait(rt,id)==success,"Initially owned mutant was not recursively acquirable"); // Depth two.
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==initialOwner,
            "Recursive acquisition replaced the initial owner observation");
    PPC_STORE_U32(output+4,0x11223344);
    rejects([&]{release(rt,id,output+4);},"Release interpreted reserved r4 as a previous-count pointer","Unsupported native mutant release control");
    require(PPC_LOAD_U32(output+4)==0x11223344,"Rejected release wrote the reserved argument as output");
    const auto count=rt.handles.size();
    rejects([&]{create(rt,output,1,1);},"Named create accepted while another mutant was owned","Named mutant attributes");
    require(rt.handles.size()==count,"Named create changed existing handle ownership");
    Worker nonowner([&] {
        require(release(rt,id)==notOwned,"Non-owner release did not preserve real MUTANT_NOT_OWNED status");
        require(wait(rt,id)==timeout,"Rejected controls/release made another thread's mutant available");
    });
    nonowner.finish();
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==initialOwner,
            "Non-owner release or timed-out wait changed the owner observation");
    require(release(rt,id)==success,"First recursive release failed");
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==initialOwner,
            "Recursive release prematurely cleared the owner observation");
    Worker stillOwned([&]{require(wait(rt,id)==timeout,"One release incorrectly exhausted recursive ownership");});
    stillOwned.finish();
    Worker blocked([&] {
        require(wait(rt,id,true)==success,"Cross-thread wait did not acquire after final owner release");
        require(!Simpsons::StallProfiler::enabled ||
                uint32_t(object->stallMutantOwner.load(std::memory_order_relaxed)>>32)==GetCurrentThreadId(),
                "Handoff did not record the new native owner");
        require(release(rt,id)==success,"Acquiring native thread could not release its mutant");
    });
    awaitWaitLease(object);
    require(release(rt,id)==success,"Final recursive owner release failed");
    blocked.finish();
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==0,
            "Final release retained a stale owner observation");
    require(release(rt,id)==notOwned,"Unowned mutant release reported fabricated success");
    require(wait(rt,id)==success && release(rt,id)==success,"Mutant could not be acquired again after handoff");
    require(close(rt,id)==success,"Recursive fixture close failed");
}

void abandonmentContracts(Runtime& rt) {
    const uint32_t id=makeMutant(rt);
    auto object=rt.getHandle(id);
    require(release(rt,id)==notOwned,"Unowned creation granted implicit ownership");
    Worker owner([&] {
        require(wait(rt,id)==success && wait(rt,id)==success,"Native owner did not acquire recursive mutant");
        // Deliberately return with recursion depth two. No Release/TerminateThread.
    });
    owner.finish(); // Actual native owner exit is the abandonment trigger.
    const uint32_t departedOwner=uint32_t(object->stallMutantOwner.load(std::memory_order_relaxed)>>32);
    require(!Simpsons::StallProfiler::enabled || (departedOwner && departedOwner!=GetCurrentThreadId()),
            "Departed owner diagnostic did not retain its observed native thread");
    require(wait(rt,id)==abandoned,"Owner exit did not return ABANDONED_WAIT_0 through the real wait import");
    require(!Simpsons::StallProfiler::enabled ||
            uint32_t(object->stallMutantOwner.load(std::memory_order_relaxed)>>32)==GetCurrentThreadId(),
            "Abandonment did not replace the departed owner observation");
    require(wait(rt,id)==success,"Abandonment did not transfer ownership to the waiting thread");
    require(release(rt,id)==success && release(rt,id)==success && release(rt,id)==notOwned,
            "Abandoned recursion was not reset or new recursive ownership was lost");
    require(wait(rt,id)==success && release(rt,id)==success,"Abandonment incorrectly persisted into later ownership");
    require(close(rt,id)==success,"Abandonment fixture close failed");
}

void diagnosticCallerContracts(Runtime& rt) {
    auto* base=rt.base;
    constexpr uint32_t stack=0x10100,previous=0x10200,caller=0x82010004;
    rt.map(0x82010000,0x1000,false,"mutant diagnostic original code fixture");
    PPC_STORE_U32(stack,previous);PPC_STORE_U32(previous-8,caller);
    const uint32_t id=makeMutant(rt);auto object=rt.getHandle(id);
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==0,"Unowned create published diagnostic ownership");
    PPCContext ctx{};ctx.r3.u64=id;ctx.r4.u64=1;ctx.r6.u64=pollTimeout;ctx.r1.u64=stack;ctx.lr=0x82434fc8;
    const uint32_t fp=PPCFPSCRRegister::getcsr();
    constexpr DWORD error=0x12347890;
    SetLastError(error);
    __imp__NtWaitForSingleObjectEx(ctx,base);
    const DWORD resultingError=GetLastError();
    require(resultingError==error && PPCFPSCRRegister::getcsr()==fp,"Mutant diagnostics changed host state");
    require(ctx.r3.u64==success && ctx.r1.u64==stack && ctx.lr==0x82434fc8,
            "Mutant diagnostic stack read changed guest wait state");
    const uint64_t observed=object->stallMutantOwner.load(std::memory_order_relaxed);
    require(Simpsons::StallProfiler::enabled?
            uint32_t(observed>>32)==GetCurrentThreadId() && uint32_t(observed)==caller:observed==0,
            "Mutant diagnostics did not recover the bounded saved outer caller");
    require(release(rt,id)==success,"Diagnostic caller fixture release failed");
    require(object->stallMutantOwner.load(std::memory_order_relaxed)==0,"Diagnostic caller final release did not clear observation");
    ctx.r3.u64=id;ctx.r1.u64=0x30000; // An unmapped frame must stay diagnostic-only.
    __imp__NtWaitForSingleObjectEx(ctx,base);
    require(ctx.r3.u64==success && ctx.r1.u64==0x30000 &&
            uint32_t(object->stallMutantOwner.load(std::memory_order_relaxed))==0,
            "Unmapped diagnostic frame threw or fabricated an outer caller");
    require(release(rt,id)==success && close(rt,id)==success,"Diagnostic caller fixture cleanup failed");
}

void closeDuringActualWaitContracts(Runtime& rt) {
    const uint32_t id=makeMutant(rt,1);
    auto object=rt.getHandle(id);std::weak_ptr<KernelHandle> weak=object;
    // This native duplicate controls the same OS mutant after its guest handle
    // is closed. It does not retain the KernelHandle shared_ptr under test.
    Handle native(duplicate(object->native));
    Worker blocked([&] {
        require(wait(rt,id,true)==success,"Closing a guest handle invalidated its in-flight native wait");
        require(ReleaseMutex(native.value)!=FALSE,"Wait did not transfer native ownership after guest close");
    });
    awaitWaitLease(object);
    require(close(rt,id)==success && !rt.getHandle(id),"Guest close did not remove its registry entry");
    object.reset();
    require(!weak.expired(),"Wait import did not retain sole in-flight KernelHandle ownership");
    require(release(rt,id)==invalidHandle && wait(rt,id)==invalidHandle && close(rt,id)==invalidHandle,
            "Stale mutant handle remained usable");
    require(ReleaseMutex(native.value)!=FALSE,"Fixture could not release initial native ownership after guest close");
    blocked.finish();
    require(weak.expired(),"Completed wait leaked its closed KernelHandle lease");
    require(WaitForSingleObject(native.value,0)==WAIT_OBJECT_0,"Surviving native duplicate was invalid or abandoned");
    require(ReleaseMutex(native.value)!=FALSE,"Surviving duplicate ownership cleanup failed");
}

void invalidHandleAndCancellationContracts(Runtime& rt) {
    auto* base=rt.base;
    const uint32_t wrong=rt.addHandle(std::make_shared<KernelHandle>(CreateEventW(nullptr,TRUE,FALSE,nullptr),KernelHandle::Type::Event));
    auto event=rt.getHandle(wrong);require(event->native!=nullptr,"Wrong-type event fixture failed");
    require(release(rt,wrong)==invalidHandle && release(rt,0)==invalidHandle && release(rt,0xfffffff0)==invalidHandle,
            "Wrong-type or invalid mutant release did not return INVALID_HANDLE");
    require(WaitForSingleObject(event->native,0)==WAIT_TIMEOUT,"Wrong-type release changed the event state");
    require(close(rt,wrong)==success,"Wrong-type fixture close failed");event.reset();

    const uint32_t id=makeMutant(rt,1);auto object=rt.getHandle(id);
    PPCContext bad{};bad.r3.u64=id;
    rejects([&]{__imp__NtReleaseMutant(bad,base+1);},"Release accepted a foreign guest base","Invalid native mutant runtime");
    PPC_STORE_U32(output,0xdeadbeef);
    const auto count=rt.handles.size();const auto next=rt.nextHandle;
    rt.requestStop("mutant fixture cancellation");
    rejects([&]{create(rt);},"Cancelled runtime created a mutant","mutant fixture cancellation");
    rejects([&]{release(rt,id);},"Cancelled runtime released ownership","mutant fixture cancellation");
    const std::array<uint8_t,4> sentinel={0xde,0xad,0xbe,0xef};
    require(rt.handles.size()==count && rt.nextHandle==next && std::memcmp(rt.pointer(output,4,false),sentinel.data(),4)==0,
            "Cancelled create changed handles or guest output");
    require(ReleaseMutex(object->native)!=FALSE,"Rejected foreign-base/cancelled release changed native ownership");
    require(close(rt,id)==success && rt.handles.empty(),"Mutant fixture leaked guest handles");
}
}

int main() {
    try {
        Runtime rt;rt.map(0x10000,0x1000,true,"mutant fixture RAM");
        rt.map(0x20000,0x1000,false,"mutant fixture readonly output");
        auto* base=rt.base;
        PPC_STORE_U64(pollTimeout,0);
        PPC_STORE_U64(boundedTimeout,uint64_t(-20000000ll)); // Relative NT 100 ns units: two seconds.
        invalidCreationContracts(rt);recursiveOwnershipContracts(rt);abandonmentContracts(rt);diagnosticCallerContracts(rt);
        closeDuringActualWaitContracts(rt);invalidHandleAndCancellationContracts(rt);
        std::puts("NativeMutantOwnership PASS: real recursion, thread handoff/abandonment, checked imports and in-flight close");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"NativeMutantOwnership FAIL: %s\n",error.what());return 1;
    }
}
