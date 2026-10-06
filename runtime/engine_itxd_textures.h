#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <array>
#include <optional>
#include <string>
struct PPCContext;
namespace Simpsons {
struct NativeControlSettings;
class Runtime;
namespace Graphics {class NativeBackend;class Texture;}
// Copied ITXD records have original CPU metadata and allocator ownership.
// Native textures are resolved separately, on the backend's owner thread.
class EngineITXDTextures {
public:
    EngineITXDTextures(Runtime&,Graphics::NativeBackend&,uint32_t context);
    ~EngineITXDTextures();
    void beginLoad(PPCContext&,uint8_t*);
    void endLoad(PPCContext&,uint8_t*);
    void beginCopy(PPCContext&,uint8_t*);
    void observeAllocation(PPCContext&,uint8_t*,bool payload);
    void finishCopy(PPCContext&,uint8_t*);
    void finishRelocate(PPCContext&,uint8_t*);
    void preflightRelease(PPCContext&,uint8_t*);
    void recoverReleasePayload(PPCContext&,uint8_t*);
    void finishRelease(PPCContext&,uint8_t*);
    bool ownsRaster(uint32_t) const;
    // Resolve a published copied texture's embedded CPU header to its raster.
    // Unknown or retiring headers have no native raster owner.
    uint32_t rasterByHeader(uint8_t* base,uint32_t header) const;
    std::shared_ptr<Graphics::Texture> texture(uint8_t* base,uint32_t raster);
    // Optional draw-only keyboard/mouse artwork. Original texture bindings and
    // resource bytes always retain texture(); unrecognized rasters return null.
    std::shared_ptr<Graphics::Texture> inputPromptTexture(uint8_t* base,uint32_t raster,const NativeControlSettings* controls=nullptr);
    // Original effect stores embedded H. Resolve only the published named
    // palette's H->T->R association; H is never treated as an SDK object.
    std::shared_ptr<Graphics::Texture> paletteFromHeader(uint8_t* base,uint32_t header);
    std::shared_ptr<Graphics::Texture> textureFromHeader(uint8_t* base,uint32_t header);
    size_t count() const;
    struct AuditView {std::string name;std::array<uint32_t,6> descriptor;uint32_t bytes,phase,metadata,payload;uint64_t generation;};
    // Native cached provenance only; this diagnostic lookup performs no guest
    // reads and never admits a binding or changes ownership.
    std::optional<AuditView> auditIdentity(uint32_t header) const;
    // Shutdown check works before context publication and during failed start.
    void requireReleased() const;
    // Optional synchronous fixture observation, installed before worker loads.
    // Throwing stops execution; returning preserves all original operations.
    // Invoked outside the registry mutex at verified load entry/completion.
    std::function<void(uint32_t,PPCContext&,uint8_t*)> boundaryObserver;
private:
    struct State;std::unique_ptr<State> state;
};
}
void SimpsonsNativeITXDBeginLoad(PPCContext&,uint8_t*);
void SimpsonsNativeITXDEndLoad(PPCContext&,uint8_t*);
void SimpsonsNativeITXDBeginCopy(PPCContext&,uint8_t*);
void SimpsonsNativeITXDMetadataAllocation(PPCContext&,uint8_t*);
void SimpsonsNativeITXDPayloadAllocation(PPCContext&,uint8_t*);
void SimpsonsNativeITXDFinishCopy(PPCContext&,uint8_t*);
void SimpsonsNativeITXDFinishRelocate(PPCContext&,uint8_t*);
// Install at BOTH 826F80C0 (before index removal) and 82736D50.
void SimpsonsNativeITXDPreflightRelease(PPCContext&,uint8_t*);
void SimpsonsNativeITXDRecoverReleasePayload(PPCContext&,uint8_t*);
void SimpsonsNativeITXDFinishRelease(PPCContext&,uint8_t*);
