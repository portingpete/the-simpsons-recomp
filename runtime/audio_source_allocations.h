#pragma once
// Which allocation generations the live audio sources decode from.
//
// An original free through the observed allocator must not retire an allocation that a
// live resident source still decodes from (its copied bank audio or its AMX payload). The
// audio owner used to check that by visiting every live source on every free; a
// level-section swap frees tens of thousands of allocations in one frame. This keeps a
// count per generation, updated wherever the owner publishes or retires a source, so the
// check is one lookup that answers exactly what the scan answered.
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Simpsons {
class AudioSourceAllocations {
public:
    template<class Source> using Sources=std::unordered_map<uint64_t,std::shared_ptr<Source>>;
    // Same publication as sources.emplace(key,source): an existing receipt stays. The
    // source's bank/amx pointers must not change while it is published.
    template<class Source> void publish(Sources<Source>& sources,uint64_t key,const std::shared_ptr<Source>& source) {
        if(sources.contains(key))return;
        bool bank=false,amx=false;
        try {
            if(source->bank){add(source->bank->generation);bank=true;}
            if(source->amx){add(source->amx->generation);amx=true;}
            sources.emplace(key,source);
        } catch(...) {
            if(bank)remove(source->bank->generation);
            if(amx)remove(source->amx->generation);
            throw;
        }
    }
    // Same as sources.erase(key).
    template<class Source> void retire(Sources<Source>& sources,uint64_t key) {
        const auto found=sources.find(key);
        if(found==sources.end())return;
        const auto source=found->second;
        sources.erase(found);
        if(source->bank)remove(source->bank->generation);
        if(source->amx)remove(source->amx->generation);
    }
    // True when a published source's bank or AMX payload has this allocation generation.
    bool decodesFrom(uint64_t generation) const {return counts.contains(generation);}
    size_t size() const noexcept {return counts.size();}
private:
    void add(uint64_t generation) {++counts[generation];}
    void remove(uint64_t generation) {
        const auto found=counts.find(generation);
        if(found!=counts.end()&&!--found->second)counts.erase(found);
    }
    std::unordered_map<uint64_t,uint32_t> counts;
};
}
