#include "runtime.h"
#include "native_window.h"
#include "guest_memory.h"
#include "checked_running.h"
#include "threads.h"
#include "engine_driver.h"
#include "engine_audio.h"
#include "engine_audio_output.h"
#include "ppc_image_metadata.h"
#include "stall_profiler.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <bit>

// Clang 22 emits this C23 libcall from SIMDe, but the installed UCRT lacks it.
// Integer IEEE-754 rounding is independent of the current host rounding mode.
extern "C" float roundevenf(float value) {
    uint32_t bits=std::bit_cast<uint32_t>(value), sign=bits&0x80000000u, mag=bits&0x7fffffffu;
    int exponent=int((mag>>23)&255)-127;
    if(exponent>=23) return value;
    if(exponent<0) return std::bit_cast<float>(sign|(mag>0x3f000000u?0x3f800000u:0u));
    unsigned shift=23-exponent;
    uint32_t mask=(1u<<shift)-1, fraction=mag&mask, result=mag&~mask, half=1u<<(shift-1);
    if(fraction>half || (fraction==half && ((result>>shift)&1))) result+=1u<<shift;
    return std::bit_cast<float>(sign|result);
}

namespace Simpsons {
Runtime* active{};
thread_local PPCContext* currentContext{};
// The verified XEX imports xboxkrnl.exe version 0x20168600, i.e. 2.0.5766.0.
// XboxKrnlVersion is an exported data record of four big-endian u16 fields.
static constexpr uint32_t xboxKernelVersionAddress=0x0102F000;
static constexpr uint16_t xboxKernelMajor=2,xboxKernelMinor=0,xboxKernelBuild=5766,xboxKernelQfe=0;
std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    StallProfiler::Scope profiling(StallProfiler::Section::FileIO,"readFile",currentContext);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw Failure("Cannot read file: " + path.string());
    auto size = f.tellg();
    if (size < 0 || uint64_t(size) > 0x100000000ull) throw Failure("File size out of range");
    if (size == 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) throw Failure("Truncated file read");
    return bytes;
}
static std::string sha256(const std::vector<uint8_t>& bytes) {
    uint8_t hash[32];
    if (bytes.size() > ULONG_MAX) throw Failure("SHA256 input exceeds platform limit");
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, bytes.empty() ? nullptr : const_cast<PUCHAR>(bytes.data()),
                   ULONG(bytes.size()), hash, sizeof(hash)) < 0) throw Failure("SHA256 calculation failed");
    std::string out;
    for (auto c : hash) { out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
    return out;
}
// A guest memory fault in recompiled original code is turned into the same thrown
// Failure the checked path raised: the vectored handler redirects the faulting thread to
// PPCGuestFaultThunk as if the faulting instruction had called it, so the exception
// unwinds through the guest frames to the existing catch sites (and to tests). The thunk
// realigns the stack (a frameless faulting function may be misaligned) behind an RBP
// frame that the unwinder understands.
extern "C" void PPCGuestFaultThunk();
extern "C" [[noreturn]] void PPCGuestFaultThrowImpl(uint64_t hostAddress,uint64_t write) noexcept(false) {
    const uint32_t address=Simpsons::active?uint32_t(hostAddress-uint64_t(reinterpret_cast<uintptr_t>(Simpsons::active->base))):0;
    if(Simpsons::currentContext) {
        const auto& ctx=*Simpsons::currentContext;
        fprintf(stderr,"[MEMORY FAILURE] address=0x%08X write=%d function=0x%08X lr=0x%08X sp=0x%08X (hardware fault)\n",
            address,int(write),ctx.lastFunction,uint32_t(ctx.lr),ctx.r1.u32);
        Simpsons::dumpGuestStack(ctx);
    }
    char text[160];
    snprintf(text,sizeof(text),"Unmapped/protected guest %s address=0x%08X",write?"write":"read",address);
    throw Simpsons::Failure(text);
}
__asm__(
    ".att_syntax\n"
    ".text\n"
    ".globl PPCGuestFaultThunk\n"
    ".def PPCGuestFaultThunk; .scl 2; .type 32; .endef\n"
    ".seh_proc PPCGuestFaultThunk\n"
    "PPCGuestFaultThunk:\n"
    "    pushq %rbp\n"
    "    .seh_pushreg %rbp\n"
    "    movq %rsp, %rbp\n"
    "    .seh_setframe %rbp, 0\n"
    "    .seh_endprologue\n"
    "    andq $-16, %rsp\n"
    "    subq $32, %rsp\n"
    "    callq PPCGuestFaultThrowImpl\n"
    "    int3\n"
    ".seh_endproc\n"
    ".intel_syntax noprefix\n");
namespace {
LONG CALLBACK guestFaultHandler(EXCEPTION_POINTERS* info) {
    auto& record=*info->ExceptionRecord;
    if(record.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record.NumberParameters<2 || !active || !active->base) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t fault=uint64_t(record.ExceptionInformation[1]);
    const uint64_t base=uint64_t(reinterpret_cast<uintptr_t>(active->base));
    if(fault<base || fault-base>=0x100000000ull) return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t address=uint32_t(fault-base);
    // Null-device scratch demand map: the first guest touch of a low page maps it
    // zero-filled RW and the instruction is retried.
    if(address<0x10000 && active->mapZeroPage(address,1)) {
        fprintf(stderr,"[NATIVE ZERO PAGE] address=0x%08X width=1 write=%d (hardware fault demand map)\n",address,int(record.ExceptionInformation[0]==1));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    CONTEXT& context=*info->ContextRecord;
    const uint64_t thunk=uint64_t(reinterpret_cast<uintptr_t>(&PPCGuestFaultThunk));
    if(context.Rip>=thunk && context.Rip<thunk+64) return EXCEPTION_CONTINUE_SEARCH; // Never convert a fault inside the conversion.
    context.Rsp-=8;
    *reinterpret_cast<uint64_t*>(context.Rsp)=context.Rip;
    context.Rip=thunk;
    context.Rcx=fault;
    context.Rdx=record.ExceptionInformation[0]==1?1:0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
}

Runtime::Runtime() {
    if (active) throw Failure("Only one runtime instance is supported");
    base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) throw Failure("Unable to reserve the 32-bit guest address space");
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopEvent) {VirtualFree(base,0,MEM_RELEASE);base=nullptr;throw Failure("Native cancellation event creation failed");}
    active = this;
    PPCStopRequested=0;
    guestWatchArmedTable=writeWatchArmed.get();guestWatchVersionTable=writeWatchVersion.get();
    // Never let the OS demote this process to efficiency cores or coarse timers (EcoQoS), which
    // it does for a background or unfocused window and which makes frame times erratic.
    {PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED|PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,0};
     SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttling,sizeof(throttling));}
    // Original instructions no longer validate each access in software: the CPU rejects
    // unmapped and protected guest addresses. Demand-map the null-device scratch pages
    // here exactly as the checked path does, and leave every other fault to SEH.
    if(!vectoredHandler) vectoredHandler=AddVectoredExceptionHandler(1,guestFaultHandler);
}
Runtime::~Runtime() {
    guestWatchArmedTable=nullptr;guestWatchVersionTable=nullptr;
    if(vectoredHandler) {RemoveVectoredExceptionHandler(vectoredHandler);vectoredHandler=nullptr;}
    stopThreads(); engineAudioOutput.reset(); engineAudio.reset();
    {std::lock_guard lifetime(engineDriverMutex);engineDriver.reset();}
    window.reset(); objectReferences.clear(); handles.clear(); threads.clear();mainThreadHandle.reset();
    currentContext=nullptr; active=nullptr;
    if(stopEvent) CloseHandle(stopEvent);
    if(base) VirtualFree(base,0,MEM_RELEASE);
}
void Runtime::map(uint32_t address, uint64_t size, bool write, const char* name,MemoryUse use) {
    // Bootstrap calls this before threads exist. Physical/thread allocators
    // already hold vmMutex across selection and publication of the mapping;
    // taking that nonrecursive mutex again deadlocks the first allocation.
    if (address < 0x10000 || !size || (address & 0xFFF) || (size & 0xFFF) || uint64_t(address)+size > 0x100000000ull)
        throw Failure("Invalid page mapping request");
    for (const auto& r : regions)
        if (address < uint64_t(r.address)+r.size && r.address < uint64_t(address)+size)
            throw Failure("Overlapping guest mapping: " + r.name);
    if(use!=MemoryUse::Host && committedPages()+size/4096>memoryBudgetPages)
        throw Failure("Guest memory compatibility budget exhausted");
    if (VirtualAlloc(base+address, size, MEM_COMMIT, write ? PAGE_READWRITE : PAGE_READONLY) != base+address)
        throw Failure("Unable to commit guest mapping");
    regions.push_back({address, size, write, name,use});
    for(uint64_t pos=address;pos<uint64_t(address)+size;pos+=0x1000)
        pageAccess[pos>>12].store(write?3:1,std::memory_order_release);
    invalidateWatchedPages(address,size);
}
void noteGuestWriteExact(uint32_t canonicalAddress,uint32_t width) noexcept {
    if(active)active->noteGuestWrite(canonicalAddress,width);
}
void Runtime::noteGuestWrite(uint32_t canonicalAddress,uint32_t width) noexcept {
    if(!width) return;
    const uint64_t last=(uint64_t(canonicalAddress)+width-1)>>12;
    for(uint64_t page=canonicalAddress>>12;page<=last&&page<0x100000;++page)
        if(writeWatchArmed[page].load(std::memory_order_relaxed)) {
            if(writeWatchArmed[page].exchange(0,std::memory_order_relaxed)==1)
                guestWatchBlockAdjust(page,-1);
            writeWatchVersion[page].fetch_add(1,std::memory_order_release);
        }
}
bool Runtime::mapZeroPage(uint32_t address, unsigned width) {
    if(!width || address>=0x10000 || uint64_t(address)+width>0x10000) return false;
    const uint32_t first=address>>12,last=uint32_t((uint64_t(address)+width-1)>>12);
    if(last-first>=16) return false;
    for(uint32_t page=first;page<=last;++page)
        if(pageAccess[page].load(std::memory_order_acquire)) return false;
    std::unique_lock lock(vmMutex,std::try_to_lock);
    if(!lock.owns_lock()) return false;
    for(uint32_t page=first;page<=last;++page) {
        if(pageAccess[page].load(std::memory_order_acquire)) return false;
        for(const auto& r:regions)
            if(page*0x1000u<uint64_t(r.address)+r.size && r.address<uint64_t(page)*0x1000u+0x1000u) return false;
    }
    for(uint32_t page=first;page<=last;++page) {
        if(VirtualAlloc(base+page*0x1000u,0x1000,MEM_COMMIT,PAGE_READWRITE)!=base+page*0x1000u) {
            for(uint32_t done=first;done<page;++done) {
                if(!VirtualFree(base+done*0x1000u,0x1000,MEM_DECOMMIT))
                    fprintf(stderr,"[VM] rollback decommit failed page=0x%05X error=%lu\n",done,GetLastError());
                pageAccess[done].store(0,std::memory_order_release);
                std::erase_if(regions,[&](const auto& r){return r.address==done*0x1000u;});
            }
            return false;
        }
        regions.push_back({page*0x1000u,0x1000,true,"null-device scratch",MemoryUse::Host});
        pageAccess[page].store(3,std::memory_order_release);
        invalidateWatchedPages(page*0x1000u,0x1000);
        fprintf(stderr,"[VM] null-device scratch page=0x%05X\n",page);
    }
    return true;
}
uint64_t Runtime::committedPages() const {    uint64_t pages=0;
    for(const auto& r:regions) if(r.use!=MemoryUse::Host) pages+=r.size/4096;
    for(const auto& a:allocations) pages+=std::count(a.committed.begin(),a.committed.end(),uint8_t(1));
    return pages;
}
std::array<uint32_t,26> Runtime::memoryStatistics() {
    std::lock_guard lock(vmMutex);
    std::array<uint32_t,26> stats{};
    stats[0]=104; stats[1]=memoryBudgetPages; stats[25]=memoryBudgetPages-1;
    // The implemented virtual allocation interval, including its fixed reservations.
    stats[4]=0x70000000-0x10000;
    uint64_t used=committedPages();
    if(used>memoryBudgetPages) throw Failure("Memory accounting exceeds compatibility budget");
    stats[3]=memoryBudgetPages-uint32_t(used);
    for(const auto& r:regions) {
        uint32_t pages=uint32_t(r.size/4096);
        switch(r.use) {
        case MemoryUse::Host: continue;
        case MemoryUse::Kernel: stats[2]+=pages; break;
        case MemoryUse::Image: stats[9]+=pages; break;
        case MemoryUse::Stack: stats[8]+=pages; break;
        case MemoryUse::Virtual: stats[11]+=pages; break;
        case MemoryUse::Physical: stats[6]+=pages; break;
        case MemoryUse::Pool: stats[7]+=pages; break;
        }
        if(r.address>=0x10000 && uint64_t(r.address)+r.size<=0x70000000)
            stats[5]+=uint32_t(r.size);
    }
    for(const auto& a:allocations) {
        stats[5]+=uint32_t(a.size);
        stats[11]+=uint32_t(std::count(a.committed.begin(),a.committed.end(),uint8_t(1)));
    }
    return stats;
}
uint8_t* Runtime::pointer(uint32_t address, unsigned width, bool write) {
    // Native owner validation often requests a short record on one page. A hit in the small
    // software TLB proves that complete range's permission exactly as the permission table
    // would (an entry is a copy of the cell, cleared by every store to it); a miss validates
    // against the table and fills the entry. The original validator stays verbatim for
    // exceptional pages/ranges/failures.
    if(width && width<=4096 && (address&4095u)+width<=4096 &&
       address>=0x10000 && address<0xFFD00000) [[likely]] {
        const uint32_t page=address>>12;
        const uint32_t required=write?3u:1u;
        static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000,0xC0001000};
        const uint32_t entry=guestAccessTlb[page&255].load(std::memory_order_relaxed);
        if((entry>>2)==page && (entry&required)==required) [[likely]] {
            const uint32_t canonical=address+adjustment[address>>29];
            if(write) PPCGuestStoreBarrier(canonical,width);
            return base+canonical;
        }
        if(page!=(threadObjectType>>12) && page!=0x82000) [[likely]] {
            const uint8_t access=pageAccess.load(page,std::memory_order_acquire);
            if((access&required)==required) [[likely]] {
                guestAccessTlb[page&255].store((page<<2)|(access&3u),std::memory_order_relaxed);
                const uint32_t canonical=address+adjustment[address>>29];
                if(write) PPCGuestStoreBarrier(canonical,width);
                return base+canonical;
            }
        }
    }
    return pointerSlow(address,width,write);
}
uint8_t* Runtime::probe(uint32_t address, unsigned width, bool write) {
    if(width && width<=4096 && (address&4095u)+width<=4096 &&
       address>=0x10000 && address<0xFFD00000) [[likely]] {
        const uint32_t page=address>>12;
        const uint32_t required=write?3u:1u;
        static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000,0xC0001000};
        const uint32_t entry=guestAccessTlb[page&255].load(std::memory_order_relaxed);
        if((entry>>2)==page && (entry&required)==required) [[likely]]
            return base+(address+adjustment[address>>29]);
        if(page!=(threadObjectType>>12) && page!=0x82000) [[likely]] {
            const uint8_t access=pageAccess.load(page,std::memory_order_acquire);
            if((access&required)==required) [[likely]] {
                guestAccessTlb[page&255].store((page<<2)|(access&3u),std::memory_order_relaxed);
                return base+(address+adjustment[address>>29]);
            }
        }
    }
    return pointerSlow(address,width,write,false);
}
uint8_t* Runtime::pointerSlow(uint32_t address,unsigned width,bool write,bool note) {
    uint32_t checkedAddress=address;
    if(address<threadObjectType+0x40 && uint64_t(address)+width>threadObjectType)
        throw Failure("Native thread object type is opaque; descriptor field access is not implemented");
    // CPU-visible physical aliases share one native backing allocation. The 4 KiB
    // aperture has the verified +0x1000 physical offset. No device MMIO is mapped.
    if(address>=0xa0000000) {
        uint64_t end=uint64_t(address)+width;
        uint32_t physical;
        if(address<0xc0000000 && end<=0xc0000000) physical=address-0xa0000000;
        else if(address>=0xc0000000 && address<0xe0000000 && end<=0xe0000000) physical=address-0xc0000000;
        else if(address>=0xe0000000 && address<0xffd00000 && end<=0xffd00000) physical=address-0xe0000000+0x1000;
        else throw Failure("Unmapped physical aperture or device MMIO");
        address=0xa0000000+physical;
    }
    if (checkingImports && address<0x82000900 && uint64_t(address)+width>0x82000600) {
        for (const auto& d : kDataImports) {
            if (uint64_t(address) < uint64_t(d.address)+4 && uint64_t(d.address) < uint64_t(address)+width &&
                std::find(boundImports.begin(),boundImports.end(),d.address)==boundImports.end())
                throw Failure(std::string("Unimplemented data import: ")+d.name);
        }
    }
    // Low pages have no categorical gate: pageAccess decides. Demand-mapped
    // null-device scratch passes here; never-mapped low pages still fail.
    bool valid=width && uint64_t(address)+width<=0x100000000ull;
    if(valid) for(uint64_t page=checkedAddress>>12;page<=(uint64_t(checkedAddress)+width-1)>>12;++page) {
        uint8_t access=pageAccess[page].load(std::memory_order_acquire);
        if(!(access&1) || (write && !(access&2))) { valid=false; break; }
    }
    if(valid) {
        if(write&&note) noteGuestWrite(address,width);
        return base+address;
    }
    char text[160];
    snprintf(text,sizeof(text),"Unmapped/protected guest %s address=0x%08X width=%u",write?"write":"read",address,width);
    throw Failure(text);
}
uint32_t Runtime::allocateVirtual(uint32_t& address,uint32_t& size,uint32_t flags,uint32_t protect) {
    std::lock_guard lock(vmMutex);
    constexpr uint32_t invalid=0xc000000du,conflict=0xc0000018u,noMemory=0xc0000017u;
    if(!size || !(flags&0x3000)) return invalid;
    if(flags&~0x60003000u) throw Failure("Unsupported NtAllocateVirtualMemory allocation flags");
    if(protect!=PAGE_READWRITE && protect!=PAGE_READONLY && protect!=PAGE_NOACCESS)
        throw Failure("Unsupported NtAllocateVirtualMemory protection");
    uint32_t pageSize=(flags&0x20000000u)?0x10000:0x1000;
    Allocation* allocation=nullptr;
    bool created=false;
    uint32_t previousNext=nextAllocation;
    if(address) for(auto& a:allocations) if(address>=a.address && address<uint64_t(a.address)+a.size) {
        allocation=&a; pageSize=a.pageSize; break;
    }
    uint32_t aligned=address&~(pageSize-1);
    uint64_t rounded=(uint64_t(size)+(address-aligned)+pageSize-1)&~uint64_t(pageSize-1);
    if(!rounded || rounded>0xffffffffu || uint64_t(aligned)+rounded>0x100000000ull) return invalid;
    if((flags&0x2000) || !address) {
        if(allocation) return conflict;
        if(!address) {
            aligned=(nextAllocation+0xffffu)&~0xffffu;
            rounded=(rounded+0xffffu)&~0xffffull;
        }
        if(aligned<0x10000 || uint64_t(aligned)+rounded>0x70000000ull) return noMemory;
        for(const auto& a:allocations) if(aligned<uint64_t(a.address)+a.size && a.address<uint64_t(aligned)+rounded) return conflict;
        for(const auto& r:regions) if(aligned<uint64_t(r.address)+r.size && r.address<uint64_t(aligned)+rounded) return conflict;
        allocations.push_back({aligned,rounded,pageSize,std::vector<uint8_t>(size_t(rounded/4096))});
        allocation=&allocations.back();
        created=true;
        if(!address) nextAllocation=uint32_t(uint64_t(aligned)+rounded);
    }
    if(!allocation || aligned<allocation->address || uint64_t(aligned)+rounded>uint64_t(allocation->address)+allocation->size) return conflict;
    if(flags&0x1000) {
        size_t first=(aligned-allocation->address)/4096,count=size_t(rounded/4096);
        uint64_t additional=std::count(allocation->committed.begin()+first,allocation->committed.begin()+first+count,uint8_t(0));
        auto rollback=[&] { if(created) {allocations.pop_back();nextAllocation=previousNext;} };
        if(committedPages()+additional>memoryBudgetPages) {rollback();return noMemory;}
        // Already committed pages retain their contents. New host pages are zeroed;
        // NOZERO permits this and never warrants clearing an existing allocation.
        if(VirtualAlloc(base+aligned,rounded,MEM_COMMIT,protect)!=base+aligned) {rollback();return noMemory;}
        uint8_t access=protect==PAGE_NOACCESS?0:(protect==PAGE_READWRITE?3:1);
        for(size_t i=0;i<count;++i) if(!allocation->committed[first+i])
            pageAccess[(aligned>>12)+i].store(access,std::memory_order_release);
        invalidateWatchedPages(aligned,rounded);
        std::fill_n(allocation->committed.begin()+first,count,uint8_t(1));
    }
    address=aligned; size=uint32_t(rounded);
    return 0;
}
void Runtime::bindData(uint32_t slot,uint32_t value) {
    boundImports.push_back(slot);
    PPCStoreU32(base,slot,value);
}
void Runtime::load(const std::filesystem::path& path) {
    gameRoot=std::filesystem::canonical(path.parent_path().parent_path()/"Simpsons Game, The (USA)");
    if(!std::filesystem::is_directory(gameRoot)) throw Failure("Original game data directory is missing");
    auto bytes=readFile(path);
    if (bytes.size()!=PPC_IMAGE_SIZE || sha256(bytes)!=kImageSha256) throw Failure("Mapped game image SHA256 mismatch");
    map(uint32_t(PPC_IMAGE_BASE),PPC_IMAGE_SIZE,true,"original image",MemoryUse::Image);
    memcpy(base+PPC_IMAGE_BASE,bytes.data(),bytes.size());
    const uint32_t dispatch=uint32_t(PPC_IMAGE_BASE+PPC_IMAGE_SIZE);
    map(dispatch,(PPC_CODE_SIZE*2+0xFFF)&~0xFFFull,true,"AOT dispatch mapping",MemoryUse::Host);
    size_t functions=0;
    for (const auto* m=PPCFuncMappings; m->host; ++m) {
        if (m->guest<PPC_CODE_BASE || m->guest>=PPC_CODE_BASE+PPC_CODE_SIZE || (m->guest&3))
            throw Failure("AOT mapping outside executable code");
        auto& slot=PPC_LOOKUP_FUNC(base,m->guest);
        if (slot && slot!=m->host) throw Failure("Conflicting AOT mappings");
        slot=m->host;
        ++functions;
    }
    fprintf(stderr,"[IMAGE] SHA256 verified; %zu AOT mappings; native D3D11 engine backend; no CPU decoder or console command processor\n",functions);
    for(const auto& imported:kCodeImports) {
        if(imported.thunk<PPC_CODE_BASE || imported.thunk>=PPC_CODE_BASE+PPC_CODE_SIZE ||
           !PPC_LOOKUP_FUNC(base,imported.thunk)) throw Failure("Callable import has no AOT thunk");
        PPCStoreU32(base,imported.slot,imported.thunk);
    }
    auto xex=readFile(path.parent_path()/"simpsons.unencrypted.xex");
    if(sha256(xex)!=kDerivedXexSha256) throw Failure("Derived XEX SHA256 mismatch");
    if(xex.size()<12) throw Failure("Truncated XEX header");
    uint32_t headerSizeRaw{};std::memcpy(&headerSizeRaw,xex.data()+8,sizeof(headerSizeRaw));
    uint32_t headerSize=__builtin_bswap32(headerSizeRaw);
    if(headerSize>0x10000 || headerSize>xex.size()) throw Failure("XEX header size exceeds bootstrap mapping");
    map(0x01000000,0x40000,true,"platform bootstrap",MemoryUse::Kernel);
    memcpy(base+headerAddress,xex.data(),headerSize);
}
void Runtime::initialize(PPCContext& ctx) {
    map(0x02000000,0x40000,true,"main guest stack",MemoryUse::Stack);
    ctx.r1.u64=0x0203FF00;
    ctx.lr=0;
    // An unbound data slot fails on access rather than exposing its encoded ordinal.
    currentContext=&ctx;
    // Original startup 0x824340C0 reads slot -> cell -> module -> +0x58 XEX header.
    PPCStoreU32(base,0x01000000,moduleAddress);
    PPCStoreU32(base,moduleAddress+0x58,headerAddress);
    PPCStoreU32(base,moduleAddress+0x1c,uint32_t(PPC_IMAGE_BASE));
    PPCStoreU32(base,moduleAddress+0x20,uint32_t(PPC_IMAGE_SIZE));
    PPCStoreU32(base,moduleAddress+0x38,uint32_t(PPC_IMAGE_SIZE));
    PPCStoreU32(base,moduleAddress+0x3c,0x82432280);
    PPCStoreU32(base,xboxKernelVersionAddress,(uint32_t(xboxKernelMajor)<<16)|xboxKernelMinor);
    PPCStoreU32(base,xboxKernelVersionAddress+4,(uint32_t(xboxKernelBuild)<<16)|xboxKernelQfe);
    bindData(0x82000664,xboxKernelVersionAddress);
    bindData(0x82000770,0x01000000);
    bindData(0x820007bc,threadObjectType);
    HANDLE mainNative{};
    if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&mainNative,0,FALSE,DUPLICATE_SAME_ACCESS))
        throw Failure("Main native thread handle duplication failed");
    mainThreadHandle=std::make_shared<KernelHandle>(mainNative,KernelHandle::Type::Thread);
    mainThreadHandle->guestObject=threadAddress;
    // KPCR/KTHREAD ABI fields cross-checked against the existing platform model.
    // Static TLS size/source comes directly from this executable's XEX header.
    if(uint64_t(kTls_data_size)+uint64_t(kTls_slot_count)*4>0x1000) throw Failure("TLS exceeds bootstrap page");
    memcpy(pointer(staticTlsAddress,kTls_raw_data_size,true),pointer(kTls_raw_data_address,kTls_raw_data_size,false),kTls_raw_data_size);
    dynamicTlsAddress=staticTlsAddress+kTls_data_size;
    tlsBases.push_back(dynamicTlsAddress);
    tlsSlots.resize(kTls_slot_count,false);
    ctx.r13.u64=pcrAddress;
    PPCStoreU32(base,pcrAddress,staticTlsAddress);
    PPCStoreU64(base,pcrAddress+0x30,pcrAddress);
    PPCStoreU32(base,pcrAddress+0x70,0x02040000);
    PPCStoreU32(base,pcrAddress+0x74,0x02000000);
    PPCStoreU32(base,pcrAddress+0x100,threadAddress);
    PPCStoreU32(base,pcrAddress+0x110,1); // host runs initial logical guest CPU 0
    PPCStoreU32(base,pcrAddress+0x2a8,pcrAddress+0x100);
    PPCStoreU32(base,threadAddress+0x5c,0x02040000);
    PPCStoreU32(base,threadAddress+0x60,0x02000000);
    PPCStoreU32(base,threadAddress+0x68,dynamicTlsAddress);
    PPCStoreU32(base,threadAddress+0xc0,pcrAddress+0x100);
    PPCStoreU32(base,threadAddress+0xc4,pcrAddress+0x100);
    *pointer(threadAddress,1,true)=6;
    *pointer(threadAddress+0x70,1,true)=8;
    *pointer(threadAddress+0x72,1,true)=1; // user/title process type
    *pointer(threadAddress+0x73,1,true)=1;
    PPCStoreU32(base,threadAddress+0x14c,GetCurrentThreadId());
    assignThreadAffinity(*this,mainNative,pcrAddress,threadAddress,1);
    checkingImports=true;
}
void dumpGuestStack(const PPCContext& ctx) noexcept {
    if(!active) return;
    try {
        auto read32=[](uint32_t address) {
            uint32_t value; memcpy(&value,active->pointer(address,4,false),4);return _byteswap_ulong(value);
        };
        uint32_t lower=read32(ctx.r13.u32+0x74),upper=read32(ctx.r13.u32+0x70),frame=ctx.r1.u32;
        fprintf(stderr,"[GUEST STACK] sp=0x%08X range=0x%08X..0x%08X r27=0x%08X r28=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X\n",
            frame,lower,upper,ctx.r27.u32,ctx.r28.u32,ctx.r29.u32,ctx.r30.u32,ctx.r31.u32);
        // Original EABI backchains and saved LR slots are diagnostic evidence,
        // not a mechanism for guest control flow or exception unwinding.
        for(unsigned depth=0;depth<48;++depth) {
            if(frame<lower || frame>=upper || (frame&15)) break;
            uint32_t parent=read32(frame);
            if(parent<=frame || parent>upper || (parent&15) || parent-frame<16) break;
            uint32_t saved=read32(parent-8);
            fprintf(stderr,"[GUEST FRAME] depth=%u sp=0x%08X caller_sp=0x%08X saved_lr=0x%08X\n",depth,frame,parent,saved);
            frame=parent;
        }
    } catch(...) {fprintf(stderr,"[GUEST STACK] stopped at unavailable frame metadata\n");}
}
LONG exceptionFilter(EXCEPTION_POINTERS* info) {
    auto& r=*info->ExceptionRecord;
    if(r.ExceptionCode==0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH; // C++ diagnostics keep normal unwinding
    fprintf(stderr,"[HOST EXCEPTION] code=0x%08lX host_pc=%p",r.ExceptionCode,r.ExceptionAddress);
    if(r.ExceptionCode==EXCEPTION_ACCESS_VIOLATION && r.NumberParameters>=2)
        fprintf(stderr," access=%llu address=%p",uint64_t(r.ExceptionInformation[0]),reinterpret_cast<void*>(r.ExceptionInformation[1]));
    fputc('\n',stderr);
    if(currentContext) fprintf(stderr,"[GUEST] last_function=0x%08X lr=0x%08X sp=0x%08X\n",
        currentContext->lastFunction,uint32_t(currentContext->lr),currentContext->r1.u32);
    if(currentContext) dumpGuestStack(*currentContext);
    return EXCEPTION_EXECUTE_HANDLER;
}
}
__declspec(noinline) uint8_t* PPCGuestPointerSlow(uint8_t* base,uint32_t address,unsigned width,bool write) {
    if(!Simpsons::active || Simpsons::active->base!=base) throw Simpsons::Failure("Invalid guest base");
    // Preserve every mapping, import, alias, permission and cancellation check,
    // but avoid two extra native calls for each original memory instruction.
    [[clang::always_inline]] Simpsons::checkRuntimeRunning(*Simpsons::active);
    try { [[clang::always_inline]] return Simpsons::active->pointer(address,width,write); }
    catch(const Simpsons::Failure&) {
        // Null-device scratch demand-map: first guest touch of a low page
        // maps it zero-filled RW (logged below and in mapZeroPage) and the
        // access is retried. Anything still invalid falls through to the
        // standard loud failure. Native pointer() callers never reach here.
        if(width && Simpsons::active->mapZeroPage(address,width)) {
            fprintf(stderr,"[NATIVE ZERO PAGE] address=0x%08X width=%u write=%d\n",address,width,int(write));
            try { return Simpsons::active->pointer(address,width,write); }
            catch(const Simpsons::Failure&) {}
        }
        if(Simpsons::currentContext) {
            const auto& ctx=*Simpsons::currentContext;
            fprintf(stderr,"[MEMORY FAILURE] address=0x%08X width=%u write=%d function=0x%08X lr=0x%08X sp=0x%08X\n",
                address,width,int(write),ctx.lastFunction,uint32_t(ctx.lr),ctx.r1.u32);
            Simpsons::dumpGuestStack(ctx);
        }
        throw;
    }
}
[[clang::always_inline]] uint8_t* PPCGuestPointer(uint8_t* base,uint32_t address,unsigned width,bool write) {
    return PPCCheckedGuestPointer(base,address,width,write);
}
// Memory access of the recompiled original code. Mapping and permission violations
// are raised by the CPU (the guest window is reserved PAGE_NOACCESS and each mapping
// is committed with exactly its guest protection) and take the existing SEH route;
// the unmapped opaque thread-object page and the device aperture fault the same way.
// Kept as explicit checks: the unbound data-import page, which the checked slow path
// validates slot by slot, and the exact guest-write barrier for cached-source
// validation (a store into a 64 KiB block holding an armed page takes the exact
// per-page check). Cancellation is polled at original function entry (PPC_TRACE_ENTRY)
// and in every wait, not per access.
// The barrier body is a separate function reached by tail call, so the hot access
// functions below stay frameless leaves (a call inside them costs a push/pop pair
// and a stack adjustment on every access, including the common non-barrier path).
__declspec(noinline) uint8_t* PPCGuestPointerBarrier(uint8_t* base,uint32_t canonical,unsigned width) {
    Simpsons::noteGuestWriteExact(canonical,width);
    return base+canonical;
}
uint8_t* PPCGuestPointerRead(uint8_t* base,uint32_t address,unsigned width) {
    if((address>>12)==0x82000u) [[unlikely]] return PPCGuestPointerSlow(base,address,width,false);
    static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000u,0xC0001000u};
    return base+(address+adjustment[address>>29]);
}
uint8_t* PPCGuestPointerWrite(uint8_t* base,uint32_t address,unsigned width) {
    if((address>>12)==0x82000u) [[unlikely]] return PPCGuestPointerSlow(base,address,width,true);
    static constexpr uint32_t adjustment[8]={0,0,0,0,0,0,0xE0000000u,0xC0001000u};
    const uint32_t canonical=address+adjustment[address>>29];
    if(Simpsons::guestWatchBlocks[canonical>>16].load(std::memory_order_relaxed)) [[unlikely]]
        return PPCGuestPointerBarrier(base,canonical,width);
    return base+canonical;
}
// Direct 16-byte vector and 128-byte dcbzl accesses still pass their direction.
uint8_t* PPCGuestPointerFast(uint8_t* base,uint32_t address,unsigned width,bool write) {
    return write?PPCGuestPointerWrite(base,address,width):PPCGuestPointerRead(base,address,width);
}
void PPCStopNow(PPCContext&) {
    if(Simpsons::active) Simpsons::checkRuntimeRunning(*Simpsons::active);
}

[[noreturn]] void PPCRecompFailure(const PPCContext& ctx,uint32_t address,const char* reason) {
    // A close accepted on the UI thread can race an indirect-call guard before
    // the next original function entry polls cancellation. Keep that explicit
    // close reason; live faults and earlier worker/runtime failures stay loud.
    if(auto* runtime=Simpsons::active;runtime&&runtime->stopping.load(std::memory_order_acquire)) {
        std::lock_guard lock(runtime->stopMutex);
        if(runtime->stopReason=="Native window closed")throw Simpsons::Failure(runtime->stopReason);
    }
    fprintf(stderr,"[AOT FAILURE] pc=0x%08X function=0x%08X lr=0x%08X r3=0x%08X r4=0x%08X: %s\n",
        address,ctx.lastFunction,uint32_t(ctx.lr),ctx.r3.u32,ctx.r4.u32,reason);
    fprintf(stderr,"[ARGUMENTS] r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X r10=0x%08X\n",
        ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32,ctx.r9.u32,ctx.r10.u32);
    Simpsons::dumpGuestStack(ctx);
    throw Simpsons::Failure(reason);
}
uint64_t PPCQueryTimebase() {
    if(Simpsons::active && Simpsons::currentContext) Simpsons::active->checkRunning();
    LARGE_INTEGER now{},frequency{};
    if(!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)
        throw Simpsons::Failure("Host performance counter unavailable");
    // 50 MHz guest timebase; quotient/remainder avoid uptime multiplication overflow.
    return (uint64_t(now.QuadPart)/frequency.QuadPart)*Simpsons::Runtime::timebaseFrequency+
        (uint64_t(now.QuadPart)%frequency.QuadPart)*Simpsons::Runtime::timebaseFrequency/frequency.QuadPart;
}
