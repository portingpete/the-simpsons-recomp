#include "declaration_resources.h"
#include <algorithm>
#include <atomic>
#include <limits>

namespace Simpsons::Graphics {
namespace {
uint16_t be16(const uint8_t* p) {return uint16_t((uint16_t(p[0])<<8)|p[1]);}
uint32_t be32(const uint8_t* p) {return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}

// Process-wide across registries and resets. Exhaustion fails; IDs never wrap.
uint64_t nextGeneration() {
    static std::atomic<uint64_t> next{1};
    uint64_t value=next.load(std::memory_order_relaxed);
    for(;;) {
        if(value==std::numeric_limits<uint64_t>::max())
            throw DeclarationError("Native declaration generation space exhausted");
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) return value;
    }
}

uint32_t storageBytes(uint32_t type) {
    // Exact original descriptors, not a guessed mask accepting other modes.
    // The low six bits match the read-only vertex format declarations; all
    // other bits remain intact for a future verified shader/layout adapter.
    switch(type) {
    case 0x001A23A6: return 16; // 32_32_32_32_FLOAT
    case 0x002A23B9: return 12; // 32_32_32_FLOAT
    case 0x002C23A5: return 8;  // 32_32_FLOAT
    case 0x00182886: return 4;  // 8_8_8_8; no color/swizzle conversion here.
    default: throw UnsupportedDeclaration("Unverified packed declaration type");
    }
}

DeclarationElement decode(const uint8_t* p) {
    return {be16(p),be16(p+2),be32(p+4),p[8],p[9],p[10],p[11],0};
}

void validate(std::span<const uint8_t> bytes) {
    if(bytes.size()<2*DeclarationRegistry::ElementBytes || bytes.size()%DeclarationRegistry::ElementBytes ||
       bytes.size()>(DeclarationRegistry::MaxElements+1)*DeclarationRegistry::ElementBytes)
        throw DeclarationError("Declaration requires 1..64 elements and a complete 12-byte terminator");
    const size_t count=bytes.size()/DeclarationRegistry::ElementBytes;
    uint32_t usages=0;
    for(size_t i=0;i<count;++i) {
        const auto element=decode(bytes.data()+i*DeclarationRegistry::ElementBytes);
        const bool streamEnd=element.stream==0xFF, typeEnd=element.type==UINT32_MAX;
        if(streamEnd || typeEnd) {
            if(!streamEnd || !typeEnd || i!=count-1)
                throw DeclarationError("Declaration terminator sentinels disagree or are not final");
            if(element.offset || element.method || element.usage || element.usageIndex)
                throw UnsupportedDeclaration("Unverified declaration terminator fields");
            continue; // Opaque byte is intentionally unrestricted and preserved.
        }
        if(i==count-1) throw DeclarationError("Declaration has no terminal record");
        const auto width=storageBytes(element.type);
        if(element.stream || element.method || element.usageIndex)
            throw UnsupportedDeclaration("Only verified stream 0, method 0 and usage index 0 declarations are supported");
        if(element.usage!=0 && element.usage!=3 && element.usage!=5 && element.usage!=10)
            throw UnsupportedDeclaration("Unverified declaration usage");
        if((element.offset&3) || uint32_t(element.offset)+width>0x10000)
            throw UnsupportedDeclaration("Declaration source extent exceeds supported aligned 16-bit offset bounds");
        if(usages&(1u<<element.usage))
            throw UnsupportedDeclaration("Repeated usage/index requires a separately verified declaration contract");
        usages|=1u<<element.usage;
    }
}
}

DeclarationRecord::DeclarationRecord(std::span<const uint8_t> bytes,std::pmr::memory_resource* memory)
    :bytes_(bytes.begin(),bytes.end(),memory),elements_(memory) {
    const size_t count=bytes.size()/DeclarationRegistry::ElementBytes-1;
    elements_.reserve(count);
    for(size_t i=0;i<count;++i) {
        auto element=decode(bytes_.data()+i*DeclarationRegistry::ElementBytes);
        element.storageBytes=storageBytes(element.type);
        minimumStreamBytes_=std::max(minimumStreamBytes_,uint32_t(element.offset)+element.storageBytes);
        elements_.push_back(element);
    }
}

DeclarationRegistry::DeclarationRegistry(DeclarationLimits limits,std::pmr::memory_resource* memory)
    :limits_(limits),memory_(memory),entries_(memory?memory:std::pmr::get_default_resource()) {
    if(!memory || !limits.maxCached || !limits.maxReferences)
        throw DeclarationError("Declaration registry requires an allocator and positive limits");
}
DeclarationRegistry::~DeclarationRegistry()=default;

DeclarationId DeclarationRegistry::create(std::span<const uint8_t> bytes) {
    validate(bytes);
    for(size_t i=0;i<entries_.size();++i) {
        auto& entry=entries_[i];
        const auto cached=entry.record->bytes();
        if(cached.size()!=bytes.size() || !std::equal(cached.begin(),cached.end(),bytes.begin())) continue;
        if(entry.references) {
            const DeclarationId id{entry.generation,uint32_t(i)};
            retain(id);
            return id;
        }
        const auto generation=nextGeneration();
        entry.generation=generation;
        entry.references=1;
        ++live_;
        return {generation,uint32_t(i)};
    }
    if(entries_.size()>=limits_.maxCached) throw DeclarationError("Native declaration cache limit exceeded");
    // All throwing allocations precede publication. A failed vector growth
    // releases the pending record and leaves prior cache entries untouched.
    auto pending=std::shared_ptr<const DeclarationRecord>(new DeclarationRecord(bytes,memory_));
    const auto generation=nextGeneration();
    entries_.push_back({std::move(pending),generation,1});
    ++live_;
    return {generation,uint32_t(entries_.size()-1)};
}

bool DeclarationRegistry::contains(DeclarationId id) const noexcept {
    return id.generation && id.slot<entries_.size() && entries_[id.slot].references &&
           entries_[id.slot].generation==id.generation;
}
const DeclarationRegistry::Entry& DeclarationRegistry::get(DeclarationId id) const {
    if(!contains(id)) throw DeclarationError("Unknown, released or foreign native declaration ID");
    return entries_[id.slot];
}
DeclarationRegistry::Entry& DeclarationRegistry::get(DeclarationId id) {
    return const_cast<Entry&>(static_cast<const DeclarationRegistry&>(*this).get(id));
}
void DeclarationRegistry::retain(DeclarationId id) {
    auto& entry=get(id);
    if(entry.references==limits_.maxReferences) throw DeclarationError("Native declaration reference limit exceeded");
    ++entry.references;
}
void DeclarationRegistry::release(DeclarationId id) {
    auto& entry=get(id);
    if(--entry.references==0) --live_; // Keep original byte-key cache until reset.
}
void DeclarationRegistry::reset() noexcept {entries_.clear();live_=0;}
uint32_t DeclarationRegistry::referenceCount(DeclarationId id) const {return get(id).references;}
std::shared_ptr<const DeclarationRecord> DeclarationRegistry::record(DeclarationId id) const {return get(id).record;}
}
