#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class DepthTarget;class Texture;}
// The three resources allocated by original shadows constructor 827064C0.
// Identities never point to an SDK header; the original CPU constructor remains.
class EngineShadowTextures {
public:
    enum class Phase {Allocated,Locked,Uploaded};
    struct View {
        uint32_t identity,owner,field,width,height,format,staging;
        Phase phase;
    };
    EngineShadowTextures(Runtime&,Graphics::NativeBackend&,uint32_t context);
    ~EngineShadowTextures();
    void create(PPCContext&,uint8_t*);
    void lock(PPCContext&,uint8_t*);
    void unlock(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    size_t count() const;
    View view(uint32_t identity) const;
    std::shared_ptr<Graphics::DepthTarget> depth(uint32_t identity) const;
    std::shared_ptr<Graphics::Texture> texture(uint32_t identity) const;
    std::vector<uint8_t> readback(uint32_t identity);
    // Only the pair observed in the real constructor, while all three owned
    // textures and the same original camera/raster generations remain live.
    bool ownsCamera(uint32_t camera) const;
    void setScissor(PPCContext&,uint8_t*);
    uint64_t scissorCount() const;
    void copyDepth(PPCContext&,uint8_t*);
    uint64_t copyCount() const;
    void requireReleased() const;
private:
    struct State;
    std::unique_ptr<State> state;
};
}
void SimpsonsNativeShadowTextureCreate(PPCContext&,uint8_t*);
void SimpsonsNativeShadowTextureLock(PPCContext&,uint8_t*);
void SimpsonsNativeShadowTextureUnlock(PPCContext&,uint8_t*);
void SimpsonsNativeShadowTextureRelease(PPCContext&,uint8_t*);
void SimpsonsNativeShadowScissor(PPCContext&,uint8_t*);
void SimpsonsNativeShadowDepthCopy(PPCContext&,uint8_t*);
