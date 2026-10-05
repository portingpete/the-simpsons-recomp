#pragma once
#include "native_backend.h"
#include <array>
#include <memory>

namespace Simpsons::Graphics {

// Narrow allocation seam for transactional allocation tests. Production callers
// use initialize(NativeBackend&, ...); implementations must return owned native
// resources, never guest SDK objects. The factory is borrowed only during a call.
class StartupResourceFactory {
public:
    virtual ~StartupResourceFactory()=default;
    virtual std::shared_ptr<RenderTarget> createTarget(uint32_t width,uint32_t height,TargetFormat format)=0;
    virtual std::shared_ptr<DepthTarget> createDepthTarget(uint32_t width,uint32_t height)=0;
    virtual std::shared_ptr<Buffer> createBuffer(uint32_t size,BufferKind kind)=0;
};

// Only the host backing portion of original 823EDF20 and its buffer owners.
// Allocation success does not mean engine/driver readiness. No guest writes,
// CPU pools, shader/declaration creation, binding, clears or presentation occur.
class StartupResources {
public:
    static constexpr uint32_t ScratchIndexBytes=0x4E20;
    static constexpr uint32_t VertexBytes=0x40000;
    static constexpr size_t VertexCount=4;

    struct Resources {
        uint32_t width{},height{};
        std::shared_ptr<RenderTarget> defaultColor; // CB00: original 182801B6
        std::shared_ptr<DepthTarget> defaultDepth;  // CAFC: original 1A220197
        std::shared_ptr<DepthTarget> depthCopy;     // CF84: separate 1A220197 role
        std::shared_ptr<RenderTarget> frontColor0;  // CF90: original 28280136
        std::shared_ptr<RenderTarget> frontColor1;  // CF8C: separate 28280136 role
        std::shared_ptr<RenderTarget> colorCopy;    // CF88: original 182801B6
        std::shared_ptr<Buffer> scratchIndex;
        std::array<std::shared_ptr<Buffer>,VertexCount> vertices;
    };

    StartupResources()=default;
    StartupResources(NativeBackend& backend,uint32_t width,uint32_t height) {initialize(backend,width,height);}
    StartupResources(const StartupResources&)=delete;
    StartupResources& operator=(const StartupResources&)=delete;
    StartupResources(StartupResources&&) noexcept=default;
    StartupResources& operator=(StartupResources&&) noexcept=default;

    // Call on the backend owner thread. Replacing an existing bundle is atomic
    // with respect to exceptions, not concurrent access: callers serialize use.
    // On failure the prior bundle is unchanged; partial new allocations unwind.
    void initialize(NativeBackend& backend,uint32_t width,uint32_t height);
    void initialize(StartupResourceFactory& factory,uint32_t width,uint32_t height);
    bool hasResources() const noexcept {return bool(resources_);}
    const Resources& resources() const;
    // Drops this owner's references. Independently retained shared_ptrs survive.
    void reset() noexcept {resources_.reset();}

private:
    std::unique_ptr<Resources> resources_;
};

}
