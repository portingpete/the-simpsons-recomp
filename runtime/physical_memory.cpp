#include "runtime.h"
#include <algorithm>

namespace Simpsons {
namespace {
uint8_t accessBits(uint32_t protect) {
    return (protect&0xff)==PAGE_NOACCESS?0:((protect&0xff)==PAGE_READWRITE?3:1);
}
DWORD hostProtect(uint8_t access) {return access&2?PAGE_READWRITE:(access&1?PAGE_READONLY:PAGE_NOACCESS);}
uint32_t aliasAddress(uint32_t physical,unsigned aperture) {
    return (aperture==0?0xa0000000u:(aperture==1?0xc0000000u:0xdffff000u))+physical;
}
bool aliasExists(uint32_t physical,unsigned aperture) {
    return aperture!=2 || (physical>=0x1000 && physical<0x1fd01000);
}
}
uint32_t Runtime::allocatePhysical(uint32_t flags,uint32_t size,uint32_t protect,uint32_t minimum,uint32_t maximum,uint32_t alignment) {
    std::lock_guard lock(vmMutex);
    if(flags!=0) throw Failure("Unsupported physical allocation type");
    if(protect&~0xa0000606u) throw Failure("Unsupported physical memory protection flags");
    if((protect&0x600)==0x600) throw Failure("Conflicting physical cache policies");
    if((protect&6)!=PAGE_READWRITE && (protect&6)!=PAGE_READONLY) return 0;
    if((protect&0xa0000000u)==0xa0000000u) return 0;
    uint32_t page=(protect&0x20000000u)?0x10000:((protect&0x80000000u)?0x1000000:0x1000);
    uint64_t rounded=(uint64_t(size)+page-1)&~uint64_t(page-1);
    uint64_t aligned=(uint64_t(alignment)+page-1)&~uint64_t(page-1);
    if(!aligned) aligned=page;
    if(!size || rounded>0x20000000 || aligned>0x20000000 || (aligned&(aligned-1))) return 0;
    if(committedPages()+rounded/4096>memoryBudgetPages) return 0;
    uint64_t low=std::max<uint64_t>(minimum,page==0x1000?0x1000:0);
    uint64_t high=std::min<uint64_t>(uint64_t(maximum)+1,page==0x1000?0x1fd01000:0x20000000);
    if(low>=high || rounded>high-low) return 0;
    uint64_t physical=(high-rounded)&~(aligned-1);
    for(;;) {
        if(physical<low) return 0;
        const PhysicalAllocation* overlap=nullptr;
        for(const auto& a:physicalAllocations)
            if(physical<uint64_t(a.physical)+a.size && a.physical<physical+rounded) {overlap=&a;break;}
        if(!overlap) break;
        if(overlap->physical<rounded) return 0;
        physical=(uint64_t(overlap->physical)-rounded)&~(aligned-1);
    }
    uint32_t canonical=0xa0000000+uint32_t(physical);
    bool writable=(protect&6)==PAGE_READWRITE;
    // Cache policy bits describe CPU/device sharing on the console. Native CPU
    // memory is coherent; native renderer resource transfers will own GPU copies.
    map(canonical,rounded,writable,"physical allocation",MemoryUse::Physical);
    uint32_t result=(page==0x1000?0xe0000000u-0x1000u:(page==0x10000?0xa0000000u:0xc0000000u))+uint32_t(physical);
    PhysicalAllocation allocation{result,uint32_t(physical),uint32_t(rounded),protect,page,{}};
    for(unsigned aperture=0;aperture<3;++aperture) {
        allocation.pageProtect[aperture].assign(rounded/4096,protect&0x607);
        for(uint64_t pos=physical;pos<physical+rounded;pos+=4096) if(aliasExists(uint32_t(pos),aperture))
            pageAccess[aliasAddress(uint32_t(pos),aperture)>>12].store(accessBits(protect),std::memory_order_release);
    }
    physicalAllocations.push_back(std::move(allocation));
    fprintf(stderr,"[VM] physical address=0x%08X physical=0x%08X size=0x%X protect=0x%X\n",result,uint32_t(physical),uint32_t(rounded),protect);
    return result;
}
uint32_t Runtime::physicalAddress(uint32_t address) {
    std::lock_guard lock(vmMutex);
    // Validate the original range, including read-only allocations, before
    // converting its aperture. Ordinary virtual allocations have no such alias.
    if(address<0xa0000000 || address>=0xffd00000) throw Failure("Physical address requested for nonphysical memory");
    pointer(address,1,false);
    if(address>=0xffd00000) throw Failure("Physical address aperture overflow");
    return address<0xc0000000?address-0xa0000000:
        (address<0xe0000000?address-0xc0000000:address-0xe0000000+0x1000);
}
void Runtime::freePhysical(uint32_t address) {
    std::lock_guard lock(vmMutex);
    auto allocation=std::find_if(physicalAllocations.begin(),physicalAllocations.end(),[&](const auto& a){return a.address==address;});
    if(allocation==physicalAllocations.end()) throw Failure("Physical free requires a live allocation base");
    uint32_t canonical=0xa0000000+allocation->physical;
    auto region=std::find_if(regions.begin(),regions.end(),[&](const auto& r){return r.address==canonical && r.use==MemoryUse::Physical;});
    if(region==regions.end()) throw Failure("Physical allocation accounting lost backing region");
    if(!VirtualFree(base+canonical,allocation->size,MEM_DECOMMIT)) throw Failure("Host physical backing decommit failed");
    for(unsigned aperture=0;aperture<3;++aperture)
        for(uint64_t pos=allocation->physical;pos<uint64_t(allocation->physical)+allocation->size;pos+=4096)
            if(aliasExists(uint32_t(pos),aperture)) pageAccess[aliasAddress(uint32_t(pos),aperture)>>12].store(0,std::memory_order_release);
    invalidateWatchedPages(canonical,allocation->size);
    regions.erase(region);
    physicalAllocations.erase(allocation);
}
namespace {
uint32_t physicalRange(uint32_t address,uint32_t size) {
    uint64_t end=uint64_t(address)+size;
    if(!size) throw Failure("Physical protection requires a nonempty range");
    if(address>=0xa0000000 && end<=0xc0000000) return address-0xa0000000;
    if(address>=0xc0000000 && end<=0xe0000000) return address-0xc0000000;
    if(address>=0xe0000000 && end<=0xffd00000) return address-0xe0000000+0x1000;
    throw Failure("Protection request is outside one physical aperture");
}
}
void Runtime::protectPhysical(uint32_t address,uint32_t size,uint32_t protect) {
    std::lock_guard lock(vmMutex);
    uint32_t access=protect&0xff;
    if((protect&~0x607u) || (access!=PAGE_NOACCESS && access!=PAGE_READONLY && access!=PAGE_READWRITE) || (protect&0x600)==0x600)
        throw Failure("Unsupported physical protection flags");
    uint32_t physical=physicalRange(address,size);
    auto allocation=std::find_if(physicalAllocations.begin(),physicalAllocations.end(),[&](const auto& a){
        return physical>=a.physical && uint64_t(physical)+size<=uint64_t(a.physical)+a.size;
    });
    if(allocation==physicalAllocations.end()) throw Failure("Physical protection spans outside a live allocation");
    unsigned aperture=address<0xc0000000?0:(address<0xe0000000?1:2);
    uint32_t page=aperture==0?0x10000:(aperture==1?0x1000000:0x1000);
    uint32_t begin=physical&~(page-1);
    uint64_t end=(uint64_t(physical)+size+page-1)&~uint64_t(page-1);
    if(begin<allocation->physical || end>uint64_t(allocation->physical)+allocation->size)
        throw Failure("Physical protection page rounding escapes allocation");
    struct Change {uint32_t physical,size;DWORD before,after;};
    std::vector<Change> changes;
    // Shared internal CPU backing must allow the union of live alias accesses.
    // Each translated access separately checks the requested guest aperture.
    for(uint64_t pos=begin;pos<end;pos+=4096) {
        size_t index=(pos-allocation->physical)/4096;
        uint8_t before=0,after=0;
        for(unsigned a=0;a<3;++a) if(aliasExists(uint32_t(pos),a)) {
            before|=accessBits(allocation->pageProtect[a][index]);
            after|=accessBits(a==aperture?protect:allocation->pageProtect[a][index]);
        }
        if(before==after) continue;
        if(!changes.empty() && changes.back().physical+changes.back().size==pos && changes.back().before==hostProtect(before) && changes.back().after==hostProtect(after))
            changes.back().size+=4096;
        else changes.push_back({uint32_t(pos),4096,hostProtect(before),hostProtect(after)});
    }
    size_t applied=0;
    for(const auto& change:changes) {
        DWORD previous=0;
        if(!VirtualProtect(base+0xa0000000+change.physical,change.size,change.after,&previous)) {
            while(applied) {
                const auto& rollback=changes[--applied];
                if(!VirtualProtect(base+0xa0000000+rollback.physical,rollback.size,rollback.before,&previous))
                    throw Failure("Native physical page protection rollback failed");
            }
            throw Failure("Native physical page protection failed");
        }
        ++applied;
    }
    for(uint64_t pos=begin;pos<end;pos+=4096) {
        allocation->pageProtect[aperture][(pos-allocation->physical)/4096]=protect;
        pageAccess[aliasAddress(uint32_t(pos),aperture)>>12].store(accessBits(protect),std::memory_order_release);
    }
    invalidateWatchedPages(0xa0000000+begin,end-begin);
    fprintf(stderr,"[VM] physical protect address=0x%08X size=0x%X protect=0x%X\n",address,size,protect);
}
uint32_t Runtime::queryPhysicalProtect(uint32_t address) {
    std::lock_guard lock(vmMutex);
    uint32_t physical=physicalRange(address,1);
    unsigned aperture=address<0xc0000000?0:(address<0xe0000000?1:2);
    for(const auto& a:physicalAllocations) if(physical>=a.physical && uint64_t(physical)<uint64_t(a.physical)+a.size)
        return a.pageProtect[aperture][(physical-a.physical)/4096];
    return 0;
}
}

PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx) {
    fprintf(stderr,"[VM] physical request flags=0x%X size=0x%X protect=0x%X min=0x%X max=0x%X align=0x%X\n",ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32);
    ctx.r3.u64=Simpsons::active->allocatePhysical(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r8.u32);
}
PPC_FUNC(__imp__MmAllocatePhysicalMemory) {
    ctx.r3.u64=Simpsons::active->allocatePhysical(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,0,0xffffffffu,0);
}
PPC_FUNC(__imp__MmGetPhysicalAddress) { ctx.r3.u64=Simpsons::active->physicalAddress(ctx.r3.u32); }
PPC_FUNC(__imp__MmFreePhysicalMemory) {
    if(ctx.r3.u32!=0) PPC_RECOMP_FAILURE(ctx,uint32_t(ctx.lr),"Unsupported physical free type");
    Simpsons::active->freePhysical(ctx.r4.u32);
}
PPC_FUNC(__imp__MmSetAddressProtect) {
    Simpsons::active->protectPhysical(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32);
}
PPC_FUNC(__imp__MmQueryAddressProtect) {
    ctx.r3.u64=Simpsons::active->queryPhysicalProtect(ctx.r3.u32);
}
