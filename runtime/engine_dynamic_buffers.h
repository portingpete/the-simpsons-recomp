#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace Simpsons {
class Runtime;
class EngineCpuCalls;
namespace Graphics {class Buffer;class StartupResources;}

// Native equivalent of the verified first-start portion of 823FCF60 and its
// stop at 823FCD58 (823FCEE0 is NOT an entry). Owns real original guest pools
// and records plus four native VB references. Caller serializes on its owner
// thread and explicitly calls stop before destruction. No driver-ready state.
class EngineDynamicBuffers {
public:
    EngineDynamicBuffers();
    ~EngineDynamicBuffers();
    EngineDynamicBuffers(const EngineDynamicBuffers&)=delete;
    EngineDynamicBuffers& operator=(const EngineDynamicBuffers&)=delete;
    void start(Runtime&,EngineCpuCalls&,uint8_t* base,const Graphics::StartupResources&);
    void stop(Runtime&,EngineCpuCalls&,uint8_t* base);
    bool hasOwnership() const noexcept {return runtime_!=nullptr;}
    bool initialized() const noexcept {return initialized_;} // This component only.
    // Returns real backing only while this owner is active. IDs are not guest
    // pointers and must never enter original SDK release/lock/bind functions.
    std::shared_ptr<Graphics::Buffer> buffer(uint32_t nativeId) const;
    void validateOwnership(Runtime&,uint8_t* base) const;
private:
    struct Slot {
        std::shared_ptr<Graphics::Buffer> buffer;
        uint32_t record{},id{};
        bool linked{};
    };
    void requireCaller(Runtime&,EngineCpuCalls&,uint8_t*) const;
    void validateOwned(Runtime&,uint8_t*) const;
    void cleanup(Runtime&,EngineCpuCalls&,uint8_t*);
    std::array<uint32_t,4> pools_{};
    std::array<Slot,4> slots_{};
    Runtime* runtime_{};
    uint32_t engine_{},thread_{};
    bool initialized_{},cleaning_{},busy_{};
};
}
