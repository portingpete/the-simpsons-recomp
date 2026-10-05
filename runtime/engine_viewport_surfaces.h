#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class RenderTarget;class DepthTarget;}
// Native ownership for original auxiliary viewport attachments and explicit
// resolve snapshots. Original aliases are separated by their checked lifetimes.
class EngineViewportSurfaces {
public:
    EngineViewportSurfaces(Runtime&,Graphics::NativeBackend&,uint32_t);
    ~EngineViewportSurfaces();
    void create(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    void publishDepth(PPCContext&,uint8_t*);
    void retireDepth(PPCContext&,uint8_t*);
    void copyDepth(PPCContext&,uint8_t*);
    // Row-zero slots0/2/3/4/6/7 retain the original texture headers/allocation, with
    // separate native snapshots from the auxiliary render attachments.
    std::shared_ptr<Graphics::RenderTarget> colorTexture(uint32_t header) const;
    void copyColor(PPCContext&,uint8_t*,uint32_t site);
    // Used by the checked original distortion phase; source must be selected.
    void resolveColor(uint32_t header,const std::shared_ptr<Graphics::RenderTarget>& source,bool clear=false);
    void resolveAndClearColor(PPCContext&,uint8_t*);
    std::vector<uint8_t> readbackColorTexture(uint32_t header) const;
    uint64_t colorCopyCount() const;
    std::shared_ptr<Graphics::DepthTarget> depthTexture(uint32_t header) const;
    std::shared_ptr<Graphics::RenderTarget> queryTexture(uint32_t header) const;
    std::shared_ptr<Graphics::RenderTarget> queryBackupTexture(uint32_t header) const;
    std::vector<uint8_t> readbackDepthTexture(uint32_t header) const;
    uint64_t depthCopyCount() const;
    uint32_t depthCopyCamera() const;
    bool owns(uint32_t) const;
    size_t count() const;
    std::shared_ptr<Graphics::RenderTarget> backing(uint32_t) const;
    void requireReleased() const;
private:
    struct State;std::unique_ptr<State> state;
};
}
void SimpsonsNativeViewportSurfaceCreate(PPCContext&,uint8_t*);
void SimpsonsNativeViewportDepthPublish(PPCContext&,uint8_t*);
void SimpsonsNativeViewportDepthRetire(PPCContext&,uint8_t*);
void SimpsonsNativeViewportDepthCopy(PPCContext&,uint8_t*);
void SimpsonsNativeViewportResolveAndClearColor(PPCContext&,uint8_t*);
