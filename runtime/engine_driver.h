#pragma once
#include <memory>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
struct PPCContext;
namespace Simpsons {
class Runtime;
class EngineRecordingOwners;
class EngineEffects;
class EngineShadowTextures;
class EngineReflectionTextures;
class EngineBuiltinTextures;
class EngineQuadDeclarations;
class EngineITXDTextures;
class EngineViewportSurfaces;
class EngineSceneCopies;
class EngineParticles;
namespace Graphics {class RenderTarget;class DepthTarget;class Texture;class EngineState;struct ImmediateDraw;}
struct NativeCameraBinding {
    uint32_t camera{},colorRaster{},depthRaster{},colorIdentity{},depthIdentity{};
    // Raster fields describe the original camera association; identities name
    // the actual attachments, which a mode-zero reset can restore to defaults.
    // Camera selection uses logical depth1->0; changed-target reset uses0->1.
    std::array<uint32_t,6> viewport{};
};
// Native request-2/request-3 lifetime. Resources live through the original
// engine plugin constructors/destructors; unsupported engine draws still fail.
class EngineDriver {
public:
    EngineDriver(Runtime&,uint32_t width,uint32_t height);
    ~EngineDriver();
    EngineDriver(const EngineDriver&)=delete;
    EngineDriver& operator=(const EngineDriver&)=delete;
    void start(const PPCContext&,uint8_t* base);
    void stop(const PPCContext&,uint8_t* base);
    bool started() const;
    // Original 823EE8F8 cache reset plus a real native-backend identity.
    // The identity is never a guest pointer or a console SDK device object.
    uint32_t refreshContext(const PPCContext&,uint8_t* base);
    void requireContext(uint32_t identity) const;
    void beginRecordingState(uint32_t identity);
    void requireRecordingStateSeed(uint32_t identity) const;
    void endRecordingState(uint32_t identity);
    void configureSubmission(uint8_t* base,uint32_t identity,uint32_t descriptor);
    bool submissionConfigured() const;
    EngineRecordingOwners& recordingOwners();
    EngineEffects& effects();
    EngineShadowTextures& shadowTextures();
    EngineReflectionTextures& reflectionTextures();
    EngineBuiltinTextures& builtinTextures();
    EngineQuadDeclarations& quadDeclarations();
    EngineITXDTextures& itxdTextures();
    EngineViewportSurfaces& viewportSurfaces();
    EngineSceneCopies& sceneCopies();
    EngineParticles& particles();
    void preflightEffectPoolRetire(uint8_t* base,uint32_t pool) const;
    void createRaster(const PPCContext&,uint8_t* base,uint32_t raster,uint32_t flags);
    void beginSnapshotRaster(PPCContext&,uint8_t*);
    void createSnapshotTexture(PPCContext&,uint8_t*);
    void finishSnapshotRaster(PPCContext&,uint8_t*);
    void commitCrossfade(PPCContext&,uint8_t*);
    void commitMovieFrame(PPCContext&,uint8_t*);
    void preflightMovieLock(PPCContext&,uint8_t*);
    void lockMovieRaster(PPCContext&,uint8_t*);
    void preflightMovieUnlock(PPCContext&,uint8_t*);
    void unlockMovieRaster(PPCContext&,uint8_t*);
    void destroyRaster(const PPCContext&,uint8_t* base,uint32_t raster);
    void preflightRasterDestroy(uint8_t* base,uint32_t raster) const;
    size_t rasterCount() const;
    void attachTextureRaster(uint32_t raster,std::shared_ptr<Graphics::Texture>);
    std::shared_ptr<Graphics::Texture> textureRaster(uint32_t raster) const;
    // Material texture values are either published copied ITXD headers or
    // identities published by the original builtin-image creation callbacks.
    std::shared_ptr<Graphics::Texture> materialTexture(uint8_t* base,uint32_t value) const;
    std::vector<uint8_t> readbackTextureRaster(uint32_t raster);
    bool readNativeTexture(const PPCContext&,uint8_t* base,uint32_t stream,uint32_t output,uint32_t chunkLength);
    uint32_t applicationScalar(uint8_t* base,uint32_t application,uint32_t selector,uint32_t value,bool apply);
    uint32_t applicationSampler(uint8_t* base,uint32_t application,uint32_t stage,uint32_t selector,uint32_t value,bool apply);
    const Graphics::EngineState& effectiveState() const;
    uint32_t pipelineResetField(uint8_t* base,uint32_t index,bool apply);
    void directScalar(uint8_t* base,uint32_t id,uint32_t value);
    void directSampler(uint8_t* base,uint32_t stage,uint32_t id,uint32_t value);
    void renderWareSampler(PPCContext&,uint8_t* base,uint32_t selector,uint32_t value,bool execute);
    void resetBindings(PPCContext&,uint8_t* base);
    void preflightResetNullTexture(uint8_t* base,uint32_t raster,uint32_t stage) const;
    void preflightNullRaster(uint8_t* base,uint32_t raster,uint32_t stage) const;
    void setTextureRaster(uint8_t* base,uint32_t stage,uint32_t identity,uint64_t mask,uint32_t caller);
    void resetNullTexture(uint8_t* base,uint32_t stage,uint64_t mask,uint32_t caller);
    uint64_t bindingResetCount() const;
    void present(PPCContext&,uint8_t* base,uint32_t raster);
    uint64_t presentationCount() const;
    uint64_t presentationAttemptCount() const;
    uint64_t frontCopyCount() const;
    // Value-owned RGBA snapshot for a modal native menu. A platform worker
    // requests the next frame; only the submission thread reads GPU resources.
    std::vector<uint8_t> readbackMenuFrame(uint32_t& width,uint32_t& height);
    // Actual completion state; never waits. The latest presentation may still be
    // queued on the GPU when present() returns.
    bool submissionCompleted(uint32_t receipt) const;
    // Bounded (five-second) GPU event wait for a known receipt's queued work,
    // then its actual completion state. Unknown/retired receipts reject.
    bool waitSubmission(uint32_t receipt);
    void selectCamera(uint8_t* base,uint32_t camera);
    void preflightCameraPass(uint8_t* base,uint32_t camera,bool begin) const;
    void clearCamera(uint8_t* base,uint32_t camera,uint32_t rgba,uint32_t selector);
    NativeCameraBinding cameraBinding() const;
    double renderAspect() const;
    void setUiDrawing(bool);
    uint64_t cameraClearCount() const;
    void copyCameraTargets(const PPCContext&,uint8_t* base,uint32_t colorDestination,uint32_t depthDestination,uint32_t camera);
    uint64_t cameraCopyCount() const;
    std::shared_ptr<Graphics::RenderTarget> sampledColorCopy(uint32_t id,uint32_t camera) const;
    std::shared_ptr<Graphics::DepthTarget> sampledDepthCopy(uint32_t id,uint32_t camera) const;
    void screenDeclaration(PPCContext&,uint8_t*,uint32_t index,bool release);
    void preflightScreen(PPCContext&,uint8_t*);
    void drawScreen(PPCContext&,uint8_t*);
    void ballEffectOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t ballEffectDrawCount() const;
    void distortionOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t distortionPhaseCount() const;
    uint64_t distortionDrawCount() const;
    // Original 82770AF0 add/sub/lerp luma layers over the resolved scene.
    void lumaOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t lumaDrawCount() const;
    // Original Dof/Blur/Bloom/Fog/Sat screen-effect passes (pass 0..4).
    void screenEffectOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t screenEffectDrawCount(uint32_t pass) const;
    // Diagnostic view of PS c0..9 in the console-device shadow the passes write.
    std::array<std::array<float,4>,10> screenEffectConstants() const;
    void drawMovie(PPCContext&,uint8_t*);
    bool beginDirectSprite(PPCContext&,uint8_t*);
    void directSpriteBatch(PPCContext&,uint8_t*,bool begin,bool clearTextures,uint32_t endpoint);
    void lockDirectSprite(PPCContext&,uint8_t*);
    void finishDirectSprite(PPCContext&,uint8_t*);
    void coronaDeclaration(PPCContext&,uint8_t*,bool release);
    void beginCoronaQueries(PPCContext&,uint8_t*);
    void finishCoronaQueries(PPCContext&,uint8_t*);
    uint64_t coronaQueryDrawCount() const;
    void immediateBufferLifecycle(PPCContext&,uint8_t*,bool release);
    void bindImmediateBuffer(uint8_t*);
    void immediateGeometryState(PPCContext&,uint8_t*,bool begin);
    void immediateDeclaration(PPCContext&,uint8_t*,bool release);
    void beginTrail(PPCContext&,uint8_t*);
    void reserveImmediate(PPCContext&,uint8_t*);
    void finishTrailBatch(PPCContext&,uint8_t*);
    void billboardOperation(PPCContext&,uint8_t*,uint32_t site);
    void beamOperation(PPCContext&,uint8_t*,uint32_t site);
    void immediateEffectOperation(PPCContext&,uint8_t*,uint32_t site);
    void decalOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t immediateDrawCount() const;
    void postFilterOperation(PPCContext&,uint8_t*,uint32_t site);
    void radialOperation(PPCContext&,uint8_t*,uint32_t operation);
    uint64_t movieDrawCount() const;
    // Original CPU allocation/copy/setup; native immutable input ownership.
    void preflightIm2DUpload(PPCContext&,uint8_t*,bool triangle=false);
    void lockIm2DUpload(PPCContext&,uint8_t*);
    void unlockIm2DUpload(PPCContext&,uint8_t*);
    void preflightIm2DSetup(PPCContext&,uint8_t*);
    void bindIm2DDeclaration(PPCContext&,uint8_t*);
    void bindIm2DStream(PPCContext&,uint8_t*);
    void drawIm2D(PPCContext&,uint8_t*);
    uint64_t im2DDrawCount() const;
    uint64_t im2DUploadCount() const;
    std::vector<uint8_t> readbackDynamicBuffer(uint32_t id);
    uint64_t screenDrawCount() const;
    void drawGameplayOverlay(PPCContext&,uint8_t* base);
    void preparePostFilter(PPCContext&,uint8_t* base,uint32_t site);
    std::vector<uint8_t> readbackColor(uint32_t id);
    std::vector<uint8_t> readbackDepth(uint32_t id);
    // Front-color roles sample alpha as one; storage still has two alpha bits.
    std::shared_ptr<Graphics::RenderTarget> color(uint32_t id,bool& sampledAlphaOne) const;
    std::shared_ptr<Graphics::DepthTarget> depth(uint32_t id) const;
private:
    friend class EngineRasters;
    friend class EngineShadowTextures;
    friend class EngineReflectionTextures;
    friend class EngineBuiltinTextures;
    friend class EngineQuadDeclarations;
    friend class EngineViewportSurfaces;
    friend class EngineSceneCopies;
    uint32_t allocateTargetIdentity();
    void requireStateContext(uint32_t identity) const;
    void submitImmediateVertices(uint8_t*,Graphics::ImmediateDraw&,uint32_t row,uint32_t source,uint32_t count);
    void prepareImmediateProjection(uint8_t*,Graphics::ImmediateDraw&,Graphics::EngineState&,uint32_t& owner,uint32_t& identity);
    struct State;
    std::unique_ptr<State> state;
};
}
