#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <array>
struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class RenderTarget;class DepthTarget;class Texture;class NativeBackend;}
struct NativeMovieFrame {
    uint32_t descriptor{},provider{},width{},height{};
    std::array<uint32_t,3> rasters{},pitches{};
    std::array<std::shared_ptr<Graphics::Texture>,3> textures;
};
struct NativeCameraSurface {
    uint32_t identity{};
    std::shared_ptr<Graphics::RenderTarget> color;
    std::shared_ptr<Graphics::DepthTarget> depth;
};
// The same validated result without owning references (camera validation only tests whether
// the private views exist), so per-draw validation takes no shared_ptr reference-count traffic.
struct NativeCameraSurfaceRole {
    uint32_t identity{};
    bool color{},depth{};
};
// Bounded camera/shared-depth associations and original loading BC3 textures.
// Original wrappers retain allocation, plugin construction/destruction and free.
class EngineRasters {
public:
    EngineRasters(Runtime&,Graphics::NativeBackend&,uint32_t colorId,uint32_t depthId,
        std::shared_ptr<Graphics::RenderTarget>,std::shared_ptr<Graphics::DepthTarget>);
    ~EngineRasters();
    void create(const PPCContext&,uint8_t* base,uint32_t raster,uint32_t flags);
    void beginSnapshot(PPCContext&,uint8_t*);
    void createSnapshotTexture(PPCContext&,uint8_t*);
    void finishSnapshot(PPCContext&,uint8_t*);
    void commitCrossfade(PPCContext&,uint8_t*);
    void commitMovieFrame(PPCContext&,uint8_t*);
    void preflightMovieLock(PPCContext&,uint8_t*);
    void lockMovieRaster(PPCContext&,uint8_t*);
    void preflightMovieUnlock(PPCContext&,uint8_t*);
    void unlockMovieRaster(PPCContext&,uint8_t*);
    NativeMovieFrame movieFrame(uint8_t* base,uint32_t presenter) const;
    void preflightDestroy(uint8_t* base,uint32_t raster) const;
    void requireCameraRoot(uint8_t* base,uint32_t raster,uint32_t type) const;
    uint32_t cameraSurfaceIdentity(uint8_t* base,uint32_t raster,uint32_t type) const;
    NativeCameraSurface cameraSurface(uint8_t* base,uint32_t raster,uint32_t type) const;
    // One complete live list walk for both attachment roots. No original CPU
    // execution or retained validation result crosses this call.
    std::array<NativeCameraSurfaceRole,2> cameraSurfaces(uint8_t* base,uint32_t colorRaster,uint32_t colorType,uint32_t depthRaster) const;
    void destroy(const PPCContext&,uint8_t* base,uint32_t raster);
    void attachTexture(uint8_t* base,uint32_t raster,std::shared_ptr<Graphics::Texture>);
    std::shared_ptr<Graphics::Texture> texture(uint8_t* base,uint32_t raster) const;
    // Raster key owning an attached textureId, or 0. Effect-code binds carry
    // a texture identity with a null stage raster cache; resolving by identity
    // binds the same attached object the raster path would.
    uint32_t rasterByIdentity(uint8_t* base,uint32_t identity) const;
    // Raster key whose +0x34 plugin pointer equals identity, or 0. Identities
    // are namespaces by readability: mapped heap pointers are plugin objects,
    // while textureIds are unmapped by construction. At most one can hit.
    uint32_t rasterByPlugin(uint8_t* base,uint32_t identity) const;
    std::shared_ptr<Graphics::RenderTarget> ownedColor(uint32_t identity) const;
    std::shared_ptr<Graphics::DepthTarget> ownedDepth(uint32_t identity) const;
    void requireReleased() const;
    size_t liveCount() const;
private:
    struct State;
    std::unique_ptr<State> state;
};
}
