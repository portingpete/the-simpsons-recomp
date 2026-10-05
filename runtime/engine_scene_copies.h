#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class RenderTarget;}
// Two original global RGB10A2 camera-copy destinations. Shader sampling and
// attachment binding require separate qualification; allocation does not draw.
class EngineSceneCopies {
public:
    EngineSceneCopies(Runtime&,Graphics::NativeBackend&,uint32_t);
    ~EngineSceneCopies();
    void create(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    bool owns(uint32_t) const;
    size_t count() const;
    std::shared_ptr<Graphics::RenderTarget> destination(const PPCContext&,uint32_t,uint32_t) const;
    // Sampling requires a completed CPU submission of the original color copy.
    // The immediate context orders that copy before subsequent shader reads.
    std::shared_ptr<Graphics::RenderTarget> edgeSource(uint32_t id,uint32_t camera) const;
    std::shared_ptr<Graphics::RenderTarget> aaSource(uint32_t id,uint32_t camera) const;
    std::shared_ptr<Graphics::RenderTarget> backing(uint32_t) const;
    std::vector<uint8_t> readback(uint32_t);
    void requireReleased() const;
private:
    friend class EngineDriver;
    void copied(uint32_t id,uint32_t camera);
    struct State;std::unique_ptr<State> state;
};
}
