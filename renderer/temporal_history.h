#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Simpsons::Graphics {
using TemporalRegister=std::array<float,4>;
static_assert(sizeof(TemporalRegister)==16 && std::is_trivially_copyable_v<TemporalRegister>);
struct TemporalGeometryIdentity {
    // Original object/submesh plus original heap allocation generation. Multiple instances
    // sharing an immutable upload must never share transform or bone history.
    uint64_t object{},generation{},geometry{},submesh{};
    uint32_t shader{};
    bool operator==(const TemporalGeometryIdentity&) const =default;
};
struct TemporalGeometryIdentityHash {
    size_t operator()(const TemporalGeometryIdentity& value) const noexcept {
        uint64_t h=value.object;
        for(uint64_t n:{value.generation,value.geometry,value.submesh,uint64_t(value.shader)})
            h^=n+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
        return size_t(h);
    }
};
struct TemporalHistoryPair {
    std::vector<TemporalRegister> previous;
    bool valid{};
};
class TemporalGeometryHistory {
    struct Entry {
        uint64_t previousFrame{},currentFrame{};
        std::vector<TemporalRegister> previous,current;
    };
    std::unordered_map<TemporalGeometryIdentity,Entry,TemporalGeometryIdentityHash> entries;
    uint64_t frame=1;
public:
    TemporalHistoryPair capture(const TemporalGeometryIdentity& identity,std::span<const TemporalRegister> values) {
        if(!identity.object||!identity.generation||!identity.geometry||!identity.submesh||!identity.shader||values.size()<4||values.size()>256)
            throw std::runtime_error("Temporal geometry needs an actual stable instance and complete transform bank");
        auto& e=entries[identity];
        if(e.currentFrame!=frame) {
            e.previous=std::move(e.current);e.previousFrame=e.currentFrame;e.currentFrame=frame;
            e.current.assign(values.begin(),values.end());
        } else if(e.current.size()!=values.size() ||
            std::memcmp(e.current.data(),values.data(),values.size_bytes())!=0) {
            // The same submesh must not silently change its transform within one
            // scene frame: a distinct original draw owner is required.
            throw std::runtime_error("Temporal instance/submesh identity aliases different transforms in one frame");
        }
        const bool valid=e.previousFrame+1==frame&&e.previous.size()==values.size();
        return {valid?e.previous:std::vector<TemporalRegister>(values.begin(),values.end()),valid};
    }
    void finishFrame() {
        ++frame;
        // Keep visibility history bounded; unseen objects use current-frame bias.
        // Original allocation generations independently reject recycled addresses.
        std::erase_if(entries,[this](const auto& pair){return pair.second.currentFrame+2<frame;});
    }
    void reset() {entries.clear();}
    uint64_t frameNumber() const noexcept {return frame;}
    size_t retainedInstances() const noexcept {return entries.size();}
};
inline float temporalHalton(uint32_t index,uint32_t base) noexcept {
    float result=0,place=1;
    while(index){place/=float(base);result+=place*float(index%base);index/=base;}
    return result;
}
inline std::array<float,2> temporalJitter(uint64_t frame,uint32_t renderWidth,uint32_t renderHeight,
    uint32_t outputWidth,uint32_t outputHeight) {
    if(!renderWidth||!renderHeight||!outputWidth||!outputHeight)
        throw std::runtime_error("Temporal jitter requires nonzero render and output extents");
    const double area=double(outputWidth)*outputHeight/(double(renderWidth)*renderHeight);
    const uint32_t phases=uint32_t(std::ceil(8.0*area));
    const uint32_t index=uint32_t(frame%phases)+1;
    return {temporalHalton(index,2)-0.5f,temporalHalton(index,3)-0.5f};
}
inline void jitterTemporalConstants(std::span<TemporalRegister> values,const std::array<float,2>& jitter,
    uint32_t width,uint32_t height) {
    if(values.size()<4||!width||!height)throw std::runtime_error("Temporal jitter requires the actual clip-transform rows");
    for(size_t lane=0;lane<4;++lane) {
        values[0][lane]+=values[3][lane]*(2.0f*jitter[0]/float(width));
        values[1][lane]-=values[3][lane]*(2.0f*jitter[1]/float(height));
    }
}
}
