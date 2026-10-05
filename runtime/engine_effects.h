#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct PPCContext;
namespace Simpsons {
class Runtime;
namespace Graphics {class NativeBackend;class NativeRecordingPayload;class NativeScreenReplacementReceipt;class NativeScreenInputReceipt;class NativeScreenBatchReceipt;class NativeBindingResetReceipt;struct SkinMeshDraw;}
// Native effect metadata ownership at the original engine wrapper boundary.
// No SDK object or console context is created. The exact49 FX profiles support
// metadata/cache ownership; the independently qualified edge pass also draws.
class EngineEffects {
    struct State;
    std::unique_ptr<State> state;
    void prepareSkinDrawTextures(Graphics::SkinMeshDraw&);
    void observeSkinDraw(const Graphics::SkinMeshDraw&,uint32_t geometry,uint32_t submesh) const noexcept;
public:
    enum class Phase {Created,Reflected,Failed};
    struct Scalar {uint32_t sdkId,value;};
    struct Sampler {uint32_t stage,sdkId,value;};
    struct View {
        uint32_t identity,wrapper,manager,source,cache,pool,cacheBytes;
        Phase phase;
        std::vector<Scalar> scalars;
        std::vector<Sampler> samplers;
        std::vector<uint32_t> defaultVectorWords;
    };
    EngineEffects(Runtime&,Graphics::NativeBackend&);
    ~EngineEffects();
    EngineEffects(const EngineEffects&)=delete;
    EngineEffects& operator=(const EngineEffects&)=delete;
    void create(PPCContext&,uint8_t*);
    void reflect(PPCContext&,uint8_t*);
    void reflectTyped(PPCContext&,uint8_t*);
    void initializeShadowSamplers(PPCContext&,uint8_t*);
    void setShadowWorld(PPCContext&,uint8_t*);
    void setShadowBones(PPCContext&,uint8_t*);
    void release(PPCContext&,uint8_t*);
    void query(PPCContext&,uint8_t*,bool technique);
    uint32_t technique(uint32_t identity,std::string_view name) const;
    uint32_t parameter(uint32_t identity,std::string_view name) const;
    View view(uint32_t identity) const;
    // The scalar fields of view() under the same validation (including the private parameter
    // block's extent and mapping), without copying its scalar/sampler/parameter vectors: the
    // per-draw dispatch paths read only these fields.
    struct ViewHeader {
        uint32_t identity,wrapper,manager,source,cache,pool,cacheBytes;
        Phase phase;
    };
    ViewHeader viewHeader(uint32_t identity) const;
    // view(identity).defaultVectorWords, without copying the rest of the view.
    std::vector<uint32_t> parameterValues(uint32_t identity) const;
    std::array<uint32_t,4> auditIdentity(uint32_t identity) const;
    // Observation only. Candidate ownership/selection is never admitted here.
    void observeProducerEntry(const PPCContext&,uint8_t*,const char* boundary) const noexcept;
    // Borrow valid only on the owner thread until this effect is released.
    std::span<const uint8_t> originalBytes(uint32_t identity) const;
    std::array<uint8_t,128> privateParameterMask(uint32_t identity) const;
    std::array<uint8_t,128> privateModifiedMask(uint32_t identity) const;
    uint32_t typedReflectionCount(uint32_t identity) const;
    size_t count() const;
    size_t compiledShaderCount(uint32_t identity) const;
    size_t shaderCount(uint32_t identity) const;
    // Borrowed genuine CPU pool storage for a checked shared leaf handle.
    // This is an address, not a shader constant upload or SDK FX object.
    uint32_t sharedParameterStorage(uint32_t identity,uint32_t handle) const;
    // Exact edge setter closure: replace SDK descriptor resolution with owned
    // parameter storage, retaining the original scalar conversions and stores.
    void edgeParameter(PPCContext&,uint8_t*,uint32_t site);
    void aaParameter(PPCContext&,uint8_t*,uint32_t site);
    void edgeAAParameter(PPCContext&,uint8_t*,uint32_t site);
    void edgeAAHelper(PPCContext&,uint8_t*,bool palette);
    void edgeBoolean(PPCContext&,uint8_t*);
    void beginEdge(PPCContext&,uint8_t*);
    void beginShadowDepth(PPCContext&,uint8_t*);
    void commitShadowDepth(PPCContext&,uint8_t*);
    void beginMono(PPCContext&,uint8_t*);
    void monoOperation(PPCContext&,uint8_t*,uint32_t site);
    void inspectMonoMesh(PPCContext&,uint8_t*);
    void monoMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void monoImmediateOperation(PPCContext&,uint8_t*,uint32_t site);
    void monoImmediateMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void monoImmediateTextureOperation(PPCContext&,uint8_t*,uint32_t site);
    void monoRecordingOperation(PPCContext&,uint8_t*,uint32_t site);
    void monoSkinOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t monoMeshDrawCount() const;
    void beginZPrepass(PPCContext&,uint8_t*);
    void beginRigid(PPCContext&,uint8_t*);
    void requireRigidSelection(uint32_t typed) const;
    std::array<uint8_t,40> rigidRecordingMask(uint32_t typed) const;
    void beginSkin(PPCContext&,uint8_t*);
    void beginVfxRigid(PPCContext&,uint8_t*);
    void requireSkinSelection(uint32_t typed) const;
    void setSkinBones(PPCContext&,uint8_t*);
    void skinMaterialOperation(PPCContext&,uint8_t*,uint32_t site);
    void skinParameterOperation(PPCContext&,uint8_t*,uint32_t site);
    void skinMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void skinReplayOperation(PPCContext&,uint8_t*,uint32_t site);
    void skinImmediateOperation(PPCContext&,uint8_t*,uint32_t site);
    void requireSkinRecordingComplete(uint32_t typed) const;
    void prepareSkinReplay(uint32_t typed);
    uint64_t skinMeshDrawCount() const;
    void skinStreamSetup(PPCContext&,uint8_t*);
    void skinSamplerSkip(PPCContext&,uint8_t*);
    void skinMorphSkip(PPCContext&,uint8_t*);
    void skinSamplerCommitSkip(PPCContext&,uint8_t*);
    void skinMorphStreamBind(PPCContext&,uint8_t*);
    void skinDraw(PPCContext&,uint8_t*);
    void skinLoopEnd(PPCContext&,uint8_t*);
    void skyTextureBind(PPCContext&,uint8_t*);
    void skyImmediateOperation(PPCContext&,uint8_t*,uint32_t site);
    void skyMaterialOperation(PPCContext&,uint8_t*,uint32_t site);
    void skyParameterOperation(PPCContext&,uint8_t*,uint32_t site);
    void skyMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void associateRecordingContext(PPCContext&,uint8_t*);
    void rigidTextureOperation(PPCContext&,uint8_t*,uint32_t site);
    void rigidMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void rigidMaterialOperation(PPCContext&,uint8_t*,uint32_t site);
    void rigidParameterOperation(PPCContext&,uint8_t*,uint32_t site);
    void rigidReplayOperation(PPCContext&,uint8_t*,uint32_t site);
    void rigidImmediateOperation(PPCContext&,uint8_t*,uint32_t site);
    // Checks the fresh active recording before the original finish body seals
    // it. Successful FX restoration retains its owners by payload identity.
    void requireRigidRecordingComplete(uint32_t typed) const;
    // Selects the associated retained payload, including an older cache node;
    // copies both completed original staging banks into that payload's live
    // owner. Recorded per-draw material snapshots remain immutable.
    void prepareRigidReplay(uint32_t typed,uint32_t packet,uint32_t payloadIdentity);
    void requireCachedRecordRetirement(uint32_t id,uint32_t typed,uint32_t object,uint32_t metadata,
        const std::shared_ptr<Graphics::NativeRecordingPayload>&) const;
    void retireCachedRecord(uint32_t id,uint32_t typed,uint32_t object,uint32_t metadata,
        const std::shared_ptr<Graphics::NativeRecordingPayload>&);
    void commitZPrepass(PPCContext&,uint8_t*);
    void inspectZPrepassMesh(PPCContext&,uint8_t*);
    void zprepassMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    uint64_t zprepassDrawCount() const;
    std::array<uint32_t,976> readbackZPrepassConstants();
    std::array<uint32_t,4> readbackZPrepassBooleans();
    std::array<uint32_t,976> readbackShadowConstants();
    void inspectShadowMesh(PPCContext&,uint8_t*);
    void shadowMeshOperation(PPCContext&,uint8_t*,uint32_t site);
    void completeShadowMesh(PPCContext&,uint8_t*,uint32_t site);
    uint64_t shadowMeshDrawCount() const;
    void commitEdge(PPCContext&,uint8_t*);
    void edgeRectangle(PPCContext&,uint8_t*,uint32_t site);
    void endEdge(PPCContext&,uint8_t*,bool wrapper);
    // A completed original screen draw can legally replace the physical
    // shaders while the original manager's logical FX/cache remains selected.
    void preflightScreenReplacement(uint8_t*) const;
    void preflightBindingReset(uint8_t*) const;
    void completeScreenReplacement(uint8_t*,const Graphics::NativeScreenReplacementReceipt&,
        uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache);
    void completeRestoredScreenReplacement(uint8_t*,uint64_t postFilterBefore,
        uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache);
    void completeDistortionScreenReplacement(uint8_t*,uint64_t postFilterBefore,
        uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache);
    void completeScreenInputReplacement(uint8_t*,const Graphics::NativeScreenInputReceipt&,
        uint32_t declaration,uint32_t vertexCache,uint32_t pixelCache,bool pendingDraw);
    void requirePendingScreenInputs(uint8_t*,const Graphics::NativeScreenInputReceipt&) const;
    void completeScreenBatchRetirement(uint8_t*,const Graphics::NativeScreenBatchReceipt&);
    void completeBindingReset(PPCContext&,uint8_t*,const Graphics::NativeBindingResetReceipt&);
    void completeFixedFunctionDeclaration(uint8_t*,uint32_t declaration);
    void deviceEpilogueOperation(PPCContext&,uint8_t*);
    uint32_t activeEdge() const;
    uint64_t edgeDrawCount() const;
    uint64_t aaDrawCount() const;
    uint64_t edgeAADrawCount() const;
    void requireReleased() const;
    void requirePoolReleased(uint32_t pool) const;
};
}

void SimpsonsNativeEffectCreate(PPCContext&,uint8_t*);
void SimpsonsNativeEffectReflect(PPCContext&,uint8_t*);
void SimpsonsNativeEffectTypedReflection(PPCContext&,uint8_t*);
void SimpsonsNativeEffectShadowSamplers(PPCContext&,uint8_t*);
void SimpsonsNativeShadowWorld(PPCContext&,uint8_t*);
void SimpsonsNativeEffectRelease(PPCContext&,uint8_t*);
void SimpsonsNativeEffectTechnique(PPCContext&,uint8_t*);
void SimpsonsNativeEffectParameter(PPCContext&,uint8_t*);
void SimpsonsNativeEffectPoolRetire(PPCContext&,uint8_t*);
void SimpsonsNativeEffectPoolCreated(PPCContext&,uint8_t*);
