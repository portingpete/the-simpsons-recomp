#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class CubeTexture;class RenderTarget;}
// Original8273C2B8/8273C000 own a context lease, cube and two2D textures.
// CPU constructor, six-face memset loop and paired destruction remain AOT.
class EngineReflectionTextures {
public:
    enum class Phase {Prepared,Constructing,Ready,Destroying};
    struct View {
        uint32_t owner,size,context,camera,quad,container;
        std::array<uint32_t,3> identities;
        uint32_t completedFaces,staging;
        bool contextRetained;
        Phase phase;
    };
    EngineReflectionTextures(Runtime&,Graphics::NativeBackend&,uint32_t context);
    ~EngineReflectionTextures();
    void begin(PPCContext&,uint8_t*);
    void retainContext(PPCContext&,uint8_t*);
    void create(PPCContext&,uint8_t*);
    void lock(PPCContext&,uint8_t*);
    void unlock(PPCContext&,uint8_t*);
    void commit(PPCContext&,uint8_t*);
    void destroyBegin(PPCContext&,uint8_t*);
    void releaseContext(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    void destroyCommit(PPCContext&,uint8_t*);
    bool owns(uint32_t identity) const;
    size_t count() const;
    size_t leaseCount() const;
    View view(uint32_t owner) const;
    std::shared_ptr<Graphics::CubeTexture> cube(uint32_t owner) const;
    std::shared_ptr<Graphics::RenderTarget> companion(uint32_t owner,uint32_t index) const;
    std::vector<uint8_t> readbackFace(uint32_t owner,uint32_t face);
    void requireReleased() const;
private:
    struct State;std::unique_ptr<State> state;
};
}
void SimpsonsNativeReflectionBegin(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionRetain(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionLock(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionUnlock(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionCommit(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionDestroyBegin(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionReleaseContext(PPCContext&,uint8_t*);
void SimpsonsNativeReflectionDestroyCommit(PPCContext&,uint8_t*);
void SimpsonsNativeEngineTextureCreate(PPCContext&,uint8_t*);
