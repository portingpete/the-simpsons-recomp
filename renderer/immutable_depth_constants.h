#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace Simpsons::Graphics {
// Exact 16-byte immutable depth constants: {reverse, depthBiasBits,
// slopeBiasBits, 0}. Every constant bit is key (signed-zero/float patterns
// exact via memcmp); callers validate finite/range before lookup.
struct DepthConstants {uint32_t reverse,constantBits,slopeBits,reserved;};
static_assert(sizeof(DepthConstants)==16);
// Bounded per-mesh-State reuse: 8 exact 16-byte keys + owned immutable
// buffers, round-robin replacement. State lifetime gives device isolation;
// eviction drops only the cache ref, live/GPU refs survive via caller ComPtr.
// Never updated in place: misses create a NEW immutable buffer first.
struct DepthConstantsCache {
    static constexpr size_t kEntries=8;
    std::array<DepthConstants,kEntries> keys{};
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,kEntries> buffers{};
    size_t count=0,next=0;
    ID3D11Buffer* find(const DepthConstants& key) const noexcept {
        for(size_t i=0;i<count;++i)
            if(std::memcmp(&keys[i],&key,sizeof(key))==0)return buffers[i].Get();
        return nullptr;
    }
    void publish(const DepthConstants& key,ID3D11Buffer* buffer) noexcept {
        if(!buffer)return;
        if(count<kEntries){keys[count]=key;buffers[count]=buffer;++count;return;}
        keys[next]=key;buffers[next]=buffer;next=(next+1)%kEntries;
    }
};
}  // namespace Simpsons::Graphics
