#pragma once
#include <cstdint>
#include <memory>

struct PPCContext;
namespace Simpsons {
namespace Graphics {class NativeBackend;class Buffer;}

// Owns a genuine 1FFFE-byte Index16 allocation. One original initialization /
// cleanup lifetime per scope. Separate EngineScratchResources must enclose all
// original declaration calls. No SDK layout, guest writes or driver readiness.
class EnginePipelineResources {
public:
    explicit EnginePipelineResources(Graphics::NativeBackend&);
    ~EnginePipelineResources();
    EnginePipelineResources(const EnginePipelineResources&)=delete;
    EnginePipelineResources& operator=(const EnginePipelineResources&)=delete;
    void validateCreated(uint32_t originalIndexField) const;
    void requireReleased() const;
    // Partial startup may stop before the original create call. Real unused
    // preallocation is allowed, but no published guest/native ownership is.
    void requireUnowned() const;
    std::shared_ptr<Graphics::Buffer> index(uint32_t nativeId) const;
private:
    struct Owner;
    std::unique_ptr<Owner> owner_;
};
}

// Config hooks skip exactly one verified BL; they preserve original following
// stores and declaration control flow. Neither hook directly writes guest memory.
void SimpsonsNativePipelineIndexCreate(PPCContext&,uint8_t* base);
void SimpsonsNativePipelineIndexRelease(PPCContext&,uint8_t* base);
